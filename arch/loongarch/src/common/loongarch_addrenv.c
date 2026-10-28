/****************************************************************************
 * arch/loongarch/src/common/loongarch_addrenv.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/mman.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <nuttx/addrenv.h>
#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/pgalloc.h>
#include <nuttx/sched.h>
#include <arch/csr.h>

#include "sched/sched.h"
#include "loongarch_internal.h"
#include "loongarch_mmu.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Serialize page ownership and permission changes, including heap growth
 * by different threads sharing a process. Context switches do not take it.
 */

static mutex_t g_env_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int allocate_region(arch_addrenv_t *env, uintptr_t start,
                           size_t size, uint64_t flags)
{
  uintptr_t addr;
  uintptr_t paddr;
  int ret = OK;

  for (addr = start; addr < start + size; addr += LOONGARCH_PAGE_SIZE)
    {
      paddr = mm_pgalloc(1);
      if (paddr == 0)
        {
          ret = -ENOMEM;
          break;
        }

      memset(loongarch_virtaddr(paddr), 0, LOONGARCH_PAGE_SIZE);
      ret = loongarch_mmu_maproot(env->root, addr, paddr,
                                  LOONGARCH_PAGE_SIZE, flags);
      if (ret < 0)
        {
          mm_pgfree(paddr, 1);
          break;
        }
    }

  if (ret < 0)
    {
      while (addr > start)
        {
          addr -= LOONGARCH_PAGE_SIZE;
          paddr = up_addrenv_find_page(env, addr);
          loongarch_mmu_unmaproot(env->root, addr, LOONGARCH_PAGE_SIZE);
          mm_pgfree(paddr, 1);
        }
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int up_addrenv_create(size_t textsize, size_t datasize, size_t heapsize,
                      arch_addrenv_t *env)
{
  int ret;

  memset(env, 0, sizeof(*env));
  if (textsize > ARCH_TEXT_SIZE ||
      datasize > ARCH_DATA_SIZE - ARCH_DATA_RESERVE_SIZE ||
      heapsize > ARCH_HEAP_SIZE)
    {
      return -E2BIG;
    }

  env->textvbase = CONFIG_ARCH_TEXT_VBASE;
  env->datavbase = CONFIG_ARCH_DATA_VBASE + ARCH_DATA_RESERVE_SIZE;
  env->heapvbase = CONFIG_ARCH_HEAP_VBASE;
  env->heapsize = MM_PGALIGNUP(heapsize);
  env->root = loongarch_mmu_newroot();
  if (env->root == 0)
    {
      return -ENOMEM;
    }

  /* The ELF loader needs writable text during loading and relocation. It
   * applies final RX permissions through up_addrenv_mprot before execution.
   */

  ret = allocate_region(env, env->textvbase, MM_PGALIGNUP(textsize),
                        LOONGARCH_MMU_READ | LOONGARCH_PTE_USER |
                        LOONGARCH_PTE_DIRTY | LOONGARCH_PTE_WRITE);
  if (ret >= 0)
    {
      ret = allocate_region(env, CONFIG_ARCH_DATA_VBASE,
                            ARCH_DATA_RESERVE_SIZE + MM_PGALIGNUP(datasize),
                            LOONGARCH_MMU_WRITE | LOONGARCH_PTE_USER);
    }

  if (ret >= 0)
    {
      ret = allocate_region(env, env->heapvbase, env->heapsize,
                            LOONGARCH_MMU_WRITE | LOONGARCH_PTE_USER);
    }

  if (ret < 0)
    {
      up_addrenv_destroy(env);
    }

  return ret;
}

int up_addrenv_destroy(arch_addrenv_t *env)
{
  if (env->root != 0)
    {
      loongarch_mmu_freetable(env->root, 0);
    }

  memset(env, 0, sizeof(*env));
  return OK;
}

int up_addrenv_vtext(arch_addrenv_t *env, void **vtext)
{
  *vtext = (void *)env->textvbase;
  return OK;
}

int up_addrenv_vdata(arch_addrenv_t *env, uintptr_t textsize, void **vdata)
{
  *vdata = (void *)env->datavbase;
  return OK;
}

int up_addrenv_vheap(const arch_addrenv_t *env, void **vheap)
{
  *vheap = (void *)env->heapvbase;
  return OK;
}

ssize_t up_addrenv_heapsize(const arch_addrenv_t *env)
{
  return env->heapsize;
}

int up_addrenv_select(const arch_addrenv_t *env)
{
  /* ASID zero is reused only after a local invalidation. Each CPU selects
   * its own PGDL; process roots never contain the kernel's DMW mappings.
   */

  DEBUGASSERT(env->root != 0);
  csr_writeq(env->root, LOONGARCH_CSR_PGDL);
  loongarch_tlb_invalidate();
  return OK;
}

int up_addrenv_coherent(const arch_addrenv_t *env)
{
  __asm__ __volatile__("dbar 0\n\tibar 0" : : : "memory");
  return OK;
}

int up_addrenv_clone(const arch_addrenv_t *src, arch_addrenv_t *dest)
{
  /* NuttX owns reference counting. Clone transfers the representation; it
   * does not fork the process or duplicate its physical pages.
   */

  *dest = *src;
  return OK;
}

int up_addrenv_attach(struct tcb_s *parent, struct tcb_s *child)
{
  return OK;
}

int up_addrenv_detach(struct tcb_s *tcb)
{
  return OK;
}

uintptr_t up_addrenv_find_page(arch_addrenv_t *env, uintptr_t addr)
{
  uint64_t *pte;

  if (env->root == 0 || !up_addrenv_user_vaddr(addr))
    {
      return 0;
    }

  pte = loongarch_mmu_pte(env->root, addr, false);
  return pte != NULL ? *pte & LOONGARCH_PTE_PADDR_MASK : 0;
}

uintptr_t up_addrenv_page_vaddr(uintptr_t page)
{
  return (uintptr_t)loongarch_virtaddr(page);
}

bool up_addrenv_user_vaddr(uintptr_t addr)
{
  return (addr >= CONFIG_ARCH_TEXT_VBASE && addr < ARCH_TEXT_VEND) ||
         (addr >= CONFIG_ARCH_DATA_VBASE && addr < ARCH_DATA_VEND) ||
         (addr >= CONFIG_ARCH_HEAP_VBASE && addr < ARCH_HEAP_VEND);
}

void up_addrenv_page_wipe(uintptr_t page)
{
  memset(loongarch_virtaddr(page), 0, LOONGARCH_PAGE_SIZE);
}

int up_addrenv_mprot(arch_addrenv_t *env, uintptr_t addr, size_t len, int prot)
{
  uintptr_t end;
  uintptr_t va;
  uint64_t flags = LOONGARCH_MMU_READ | LOONGARCH_PTE_USER;
  uint64_t *pte;
  int ret;

  if ((addr & (LOONGARCH_PAGE_SIZE - 1)) != 0 || len == 0 ||
      addr >= LOONGARCH_MMU_VLIMIT ||
      len > LOONGARCH_MMU_VLIMIT - addr ||
      (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
    {
      return -EINVAL;
    }

  end = addr + MM_PGALIGNUP(len);
  if (!(prot & PROT_READ))
    {
      flags |= LOONGARCH_PTE_NOREAD;
    }

  if (!(prot & PROT_EXEC))
    {
      flags |= LOONGARCH_PTE_NOEXEC;
    }

  if (prot & PROT_WRITE)
    {
      flags |= LOONGARCH_PTE_DIRTY | LOONGARCH_PTE_WRITE;
    }

  ret = nxmutex_lock(&g_env_lock);
  if (ret < 0)
    {
      return ret;
    }

  for (va = addr; va < end; va += LOONGARCH_PAGE_SIZE)
    {
      pte = loongarch_mmu_pte(env->root, va, false);
      if (pte == NULL || *pte == 0)
        {
          ret = -ENOMEM;
          goto out;
        }
    }

  for (va = addr; va < end; va += LOONGARCH_PAGE_SIZE)
    {
      pte = loongarch_mmu_pte(env->root, va, false);
      __atomic_store_n(pte, (*pte & LOONGARCH_PTE_PADDR_MASK) | flags,
                       __ATOMIC_RELEASE);
    }

  loongarch_tlb_sync();

out:
  nxmutex_unlock(&g_env_lock);
  return ret;
}

uintptr_t pgalloc(uintptr_t brkaddr, unsigned int npages)
{
  struct tcb_s *tcb = this_task();
  arch_addrenv_t *env;
  size_t size = (size_t)npages << LOONGARCH_PAGE_SHIFT;
  int ret;

  if (tcb->addrenv_own == NULL || npages == 0)
    {
      return 0;
    }

  env = &tcb->addrenv_own->addrenv;
  if (brkaddr == 0)
    {
      brkaddr = env->heapvbase;
    }

  if ((brkaddr & (LOONGARCH_PAGE_SIZE - 1)) != 0 ||
      brkaddr < env->heapvbase || brkaddr >= ARCH_HEAP_VEND ||
      size > ARCH_HEAP_VEND - brkaddr)
    {
      return 0;
    }

  ret = nxmutex_lock(&g_env_lock);
  if (ret < 0)
    {
      return 0;
    }

  ret = allocate_region(env, brkaddr, size,
                        LOONGARCH_MMU_WRITE | LOONGARCH_PTE_USER);
  nxmutex_unlock(&g_env_lock);
  return ret == OK ? brkaddr : 0;
}

int up_addrenv_kstackalloc(struct tcb_s *tcb)
{
  tcb->xcp.kstack = kmm_memalign(STACKFRAME_ALIGN,
                                CONFIG_ARCH_KERNEL_STACKSIZE);
  if (tcb->xcp.kstack == NULL)
    {
      return -ENOMEM;
    }

#ifdef CONFIG_STACK_COLORATION
  loongarch_stack_color(tcb->xcp.kstack, CONFIG_ARCH_KERNEL_STACKSIZE);
#endif
  return OK;
}

int up_addrenv_kstackfree(struct tcb_s *tcb)
{
  kmm_free(tcb->xcp.kstack);
  tcb->xcp.kstack = NULL;
  return OK;
}
