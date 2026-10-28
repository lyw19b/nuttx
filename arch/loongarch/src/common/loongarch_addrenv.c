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

/* Keep all configured user regions page aligned, ordered and below the
 * canonical low-address limit. The data region includes its reserved page.
 */

static_assert((CONFIG_ARCH_TEXT_VBASE & (LOONGARCH_PAGE_SIZE - 1)) == 0 &&
              (CONFIG_ARCH_DATA_VBASE & (LOONGARCH_PAGE_SIZE - 1)) == 0 &&
              (CONFIG_ARCH_HEAP_VBASE & (LOONGARCH_PAGE_SIZE - 1)) == 0,
              "User regions must be page aligned");
static_assert(CONFIG_ARCH_TEXT_VBASE != 0 &&
              ARCH_TEXT_SIZE != 0 &&
              ARCH_DATA_SIZE >= ARCH_DATA_RESERVE_SIZE &&
              ARCH_HEAP_SIZE != 0,
              "User regions must provide text, reserved data and heap");
static_assert(ARCH_TEXT_VEND <= CONFIG_ARCH_DATA_VBASE &&
              ARCH_DATA_VEND <= CONFIG_ARCH_HEAP_VBASE &&
              ARCH_HEAP_VEND <= LOONGARCH_MMU_VLIMIT,
              "User regions must not overlap or exceed the VA limit");

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

/****************************************************************************
 * Name: allocate_region
 *
 * Description:
 *   Allocate zeroed backing pages and map them into an owned address
 *   environment. The caller serializes changes to the environment. On
 *   failure, unmap and free only the pages allocated by this call;
 *   existing mappings remain intact.
 *
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

      /* Clear through the kernel mapping before exposing the page to PLV3. */

      memset(loongarch_virtaddr(paddr), 0, LOONGARCH_PAGE_SIZE);
      ret = loongarch_mmu_maproot(env->root, addr, paddr,
                                  LOONGARCH_PAGE_SIZE, flags);
      if (ret < 0)
        {
          mm_pgfree(paddr, 1);
          break;
        }
    }

  /* Undo only this allocation, including its partially populated tables. */

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

/****************************************************************************
 * Name: up_addrenv_create
 *
 * Description:
 *   Create a private page-table root and allocate the text, reserved data,
 *   data and initial heap regions for an ELF process. All backing pages
 *   belong to this environment. Return a negative errno and release
 *   partial allocations if any region cannot be created.
 *
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

  /* Share empty lower-level tables until the private root needs them. */

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

/****************************************************************************
 * Name: up_addrenv_destroy
 *
 * Description:
 *   Release an address environment and clear its descriptor. NuttX must
 *   have dropped every task and active-CPU reference before this call, so
 *   no processor can still walk these tables.
 *
 ****************************************************************************/

int up_addrenv_destroy(arch_addrenv_t *env)
{
  if (env->root != 0)
    {
      loongarch_mmu_freetable(env->root, 0);
    }

  memset(env, 0, sizeof(*env));
  return OK;
}

/****************************************************************************
 * Name: up_addrenv_vtext
 *
 * Description:
 *   Return the fixed virtual text address used by the ELF loader.
 *
 ****************************************************************************/

int up_addrenv_vtext(arch_addrenv_t *env, void **vtext)
{
  *vtext = (void *)env->textvbase;
  return OK;
}

/****************************************************************************
 * Name: up_addrenv_vdata
 *
 * Description:
 *   Return the data address after the architecture reserved page. The data
 *   region has a fixed base and does not depend on the text size.
 *
 ****************************************************************************/

int up_addrenv_vdata(arch_addrenv_t *env, uintptr_t textsize, void **vdata)
{
  *vdata = (void *)env->datavbase;
  return OK;
}

/****************************************************************************
 * Name: up_addrenv_vheap
 *
 * Description:
 *   Return the virtual base of the process heap.
 *
 ****************************************************************************/

int up_addrenv_vheap(const arch_addrenv_t *env, void **vheap)
{
  *vheap = (void *)env->heapvbase;
  return OK;
}

/****************************************************************************
 * Name: up_addrenv_heapsize
 *
 * Description:
 *   Return the initial page-aligned heap allocation in bytes.
 *
 ****************************************************************************/

ssize_t up_addrenv_heapsize(const arch_addrenv_t *env)
{
  return env->heapsize;
}

/****************************************************************************
 * Name: up_addrenv_select
 *
 * Description:
 *   Select an address environment on the calling CPU. The scheduler calls
 *   this with interrupts disabled. Invalidate local translations before
 *   returning because all environments currently reuse ASID zero.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: up_addrenv_coherent
 *
 * Description:
 *   Order page contents written by the loader before subsequent
 *   instruction fetches. This port currently relies on coherent caches;
 *   noncoherent platforms require additional cache maintenance.
 *
 ****************************************************************************/

int up_addrenv_coherent(const arch_addrenv_t *env)
{
  __asm__ __volatile__("dbar 0\n\tibar 0" : : : "memory");
  return OK;
}

/****************************************************************************
 * Name: up_addrenv_clone
 *
 * Description:
 *   Copy the architecture descriptor when NuttX transfers or shares an
 *   address environment. Generic NuttX code manages its references; this
 *   operation neither duplicates physical pages nor implements fork.
 *
 ****************************************************************************/

int up_addrenv_clone(const arch_addrenv_t *src, arch_addrenv_t *dest)
{
  /* NuttX owns reference counting. Clone transfers the representation; it
   * does not fork the process or duplicate its physical pages.
   */

  *dest = *src;
  return OK;
}

/****************************************************************************
 * Name: up_addrenv_attach
 *
 * Description:
 *   Complete attachment of a thread to its group address environment.
 *   Generic NuttX code owns the references, so no architecture bookkeeping
 *   is needed.
 *
 ****************************************************************************/

int up_addrenv_attach(struct tcb_s *parent, struct tcb_s *child)
{
  return OK;
}

/****************************************************************************
 * Name: up_addrenv_detach
 *
 * Description:
 *   Complete thread detachment. Page ownership is released by the final
 *   address-environment reference, not by individual threads.
 *
 ****************************************************************************/

int up_addrenv_detach(struct tcb_s *tcb)
{
  return OK;
}

/****************************************************************************
 * Name: up_addrenv_find_page
 *
 * Description:
 *   Look up the physical backing page without allocating page tables.
 *   Return zero for an absent mapping or an address outside the user
 *   regions. The caller must keep the environment alive during the lookup.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: up_addrenv_page_vaddr
 *
 * Description:
 *   Translate a physical page into the cached kernel direct-mapping
 *   window.
 *
 ****************************************************************************/

uintptr_t up_addrenv_page_vaddr(uintptr_t page)
{
  return (uintptr_t)loongarch_virtaddr(page);
}

/****************************************************************************
 * Name: up_addrenv_user_vaddr
 *
 * Description:
 *   Check whether an address belongs to a configured user region. This
 *   checks layout only; it does not establish that a page is mapped or
 *   accessible.
 *
 ****************************************************************************/

bool up_addrenv_user_vaddr(uintptr_t addr)
{
  return (addr >= CONFIG_ARCH_TEXT_VBASE && addr < ARCH_TEXT_VEND) ||
         (addr >= CONFIG_ARCH_DATA_VBASE && addr < ARCH_DATA_VEND) ||
         (addr >= CONFIG_ARCH_HEAP_VBASE && addr < ARCH_HEAP_VEND);
}

/****************************************************************************
 * Name: up_addrenv_page_wipe
 *
 * Description:
 *   Clear one physical page through its cached kernel mapping.
 *
 ****************************************************************************/

void up_addrenv_page_wipe(uintptr_t page)
{
  memset(loongarch_virtaddr(page), 0, LOONGARCH_PAGE_SIZE);
}

/****************************************************************************
 * Name: up_addrenv_mprot
 *
 * Description:
 *   Change permissions on an existing page-aligned user range. Validate
 *   every mapping before modifying any entry, then synchronously
 *   invalidate translations on all online CPUs. Return a negative errno on
 *   failure.
 *
 ****************************************************************************/

int up_addrenv_mprot(arch_addrenv_t *env, uintptr_t addr,
                     size_t len, int prot)
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

  /* Check the whole range before changing permissions on any page. */

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

  /* No CPU may retain permissions that were removed by this update. */

  loongarch_tlb_sync();

out:
  nxmutex_unlock(&g_env_lock);
  return ret;
}

/****************************************************************************
 * Name: pgalloc
 *
 * Description:
 *   Extend the current process heap with zeroed pages. Serialize
 *   concurrent extensions and leave existing mappings unchanged on
 *   failure. Return the mapped virtual base, or zero if the request cannot
 *   be satisfied.
 *
 ****************************************************************************/

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

#ifdef CONFIG_ARCH_KERNEL_STACK

/****************************************************************************
 * Name: up_addrenv_kstackalloc
 *
 * Description:
 *   Allocate an aligned, kernel-only exception and syscall stack for a
 *   user thread. Keep this allocation separate from the user-controlled
 *   stack.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: up_addrenv_kstackfree
 *
 * Description:
 *   Free the kernel stack after the thread can no longer execute or
 *   receive exceptions. Clear the pointer to make repeated cleanup
 *   harmless.
 *
 ****************************************************************************/

int up_addrenv_kstackfree(struct tcb_s *tcb)
{
  kmm_free(tcb->xcp.kstack);
  tcb->xcp.kstack = NULL;
  return OK;
}

#endif /* CONFIG_ARCH_KERNEL_STACK */
