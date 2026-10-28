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
#include "loongarch_uaccess.h"

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

#ifdef CONFIG_LOONGARCH_DEMAND_PAGING
static_assert(CONFIG_LOONGARCH_INITIAL_HEAP_SIZE != 0 &&
              CONFIG_LOONGARCH_INITIAL_HEAP_SIZE <= ARCH_HEAP_SIZE &&
              (CONFIG_LOONGARCH_INITIAL_HEAP_SIZE &
               (LOONGARCH_PAGE_SIZE - 1)) == 0,
              "Initial user heap must fit and be page aligned");
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Serialize page ownership and permission changes, including heap growth
 * by different threads sharing a process. Context switches do not take it.
 */

static mutex_t g_env_lock = NXMUTEX_INITIALIZER;

/* Backing-page references are distinct from page-table ownership. Atomic
 * counters allow inactive environments to be destroyed by the worker while
 * another task creates an environment. The zero page is never released.
 */

static uint32_t g_page_refs[CONFIG_LOONGARCH_PAGE_POOL_SIZE /
                            LOONGARCH_PAGE_SIZE];
static uint8_t g_zero_page[LOONGARCH_PAGE_SIZE]
  aligned_data(LOONGARCH_PAGE_SIZE);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: page_index
 *
 * Description:
 *   Convert an owned backing page to its reference-counter index. Page-table
 *   allocations use the same pool but are never passed to this interface.
 *
 ****************************************************************************/

static size_t page_index(uintptr_t page)
{
  uintptr_t base = loongarch_physaddr(CONFIG_RAM_END &
                                     ~(LOONGARCH_PAGE_SIZE - 1ul)) -
                   CONFIG_LOONGARCH_PAGE_POOL_SIZE;

  ASSERT(page >= base && page - base < CONFIG_LOONGARCH_PAGE_POOL_SIZE);
  ASSERT((page & (LOONGARCH_PAGE_SIZE - 1)) == 0);
  return (page - base) >> LOONGARCH_PAGE_SHIFT;
}

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
      paddr = loongarch_page_alloc();
      if (paddr == 0)
        {
          ret = -ENOMEM;
          break;
        }

      ret = loongarch_mmu_maproot(env->root, addr, paddr,
                                  LOONGARCH_PAGE_SIZE, flags);
      if (ret < 0)
        {
          loongarch_page_put(paddr);
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
          loongarch_page_put(paddr);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: reserve_region
 *
 * Description:
 *   Install nonpresent demand-zero leaves without allocating backing pages.
 *   Hardware PTW must see P clear, and software refill must see V clear.
 *   Roll back only leaves installed by this call on allocation failure.
 *
 ****************************************************************************/

#ifdef CONFIG_LOONGARCH_DEMAND_PAGING
static int reserve_region(arch_addrenv_t *env, uintptr_t start, size_t size)
{
  return loongarch_mmu_maproot(env->root, start, 0, size,
                               LOONGARCH_PTE_LAZY | LOONGARCH_PTE_USER |
                               LOONGARCH_PTE_WRITE | LOONGARCH_PTE_CACHE |
                               LOONGARCH_PTE_NOEXEC);
}
#endif

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
#ifdef CONFIG_LOONGARCH_DEMAND_PAGING
  if (env->heapsize > CONFIG_LOONGARCH_INITIAL_HEAP_SIZE)
    {
      env->heapsize = CONFIG_LOONGARCH_INITIAL_HEAP_SIZE;
    }
#endif

  /* Keep mutable ASID contexts separate from the const descriptor passed
   * to up_addrenv_select. Descriptor clones must share this storage, just
   * as they share the page-table root and its backing pages.
   */

  env->asid = kmm_zalloc(CONFIG_SMP_NCPUS * sizeof(*env->asid));
  if (env->asid == NULL)
    {
      return -ENOMEM;
    }

  /* Share empty lower-level tables until the private root needs them. */

  env->root = loongarch_mmu_newroot();
  if (env->root == 0)
    {
      up_addrenv_destroy(env);
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
 *   no processor can still walk these tables. Cached translations may
 *   remain under retired ASIDs, which cannot be reassigned until the local
 *   allocator flushes them on rollover.
 *
 ****************************************************************************/

int up_addrenv_destroy(arch_addrenv_t *env)
{
  if (env->root != 0)
    {
      loongarch_mmu_freetable(env->root, 0);
    }

  kmm_free(env->asid);
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
 *   this with interrupts disabled. Reuse the environment's local ASID
 *   while its generation remains valid; otherwise allocate a fresh one.
 *
 ****************************************************************************/

int up_addrenv_select(const arch_addrenv_t *env)
{
  loongarch_asid_select(env);
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
  uintptr_t page;
  uint64_t value;
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
      value = flags;
      page = *pte & LOONGARCH_PTE_PADDR_MASK;

      if (*pte & LOONGARCH_PTE_LAZY)
        {
          value &= ~(LOONGARCH_PTE_VALID | LOONGARCH_PTE_PRESENT |
                     LOONGARCH_PTE_DIRTY);
          value |= LOONGARCH_PTE_LAZY;
        }
      else if ((prot & PROT_WRITE) &&
               (page == loongarch_physaddr((uintptr_t)g_zero_page) ||
                __atomic_load_n(&g_page_refs[page_index(page)],
                                __ATOMIC_ACQUIRE) > 1))
        {
          value &= ~(LOONGARCH_PTE_DIRTY | LOONGARCH_PTE_WRITE);
          value |= LOONGARCH_PTE_COW;
        }

      __atomic_store_n(pte, page | value, __ATOMIC_RELEASE);
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

#ifdef CONFIG_LOONGARCH_DEMAND_PAGING
  ret = reserve_region(env, brkaddr, size);
#else
  ret = allocate_region(env, brkaddr, size,
                        LOONGARCH_MMU_WRITE | LOONGARCH_PTE_USER);
#endif
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
#ifdef CONFIG_LOONGARCH_USER_ACCESS
  loongarch_user_cleanup(tcb);
#endif

  kmm_free(tcb->xcp.kstack);
  tcb->xcp.kstack = NULL;
  return OK;
}

#endif /* CONFIG_ARCH_KERNEL_STACK */

/****************************************************************************
 * Name: loongarch_page_alloc / loongarch_page_put
 *
 * Description:
 *   Allocate a zeroed user backing page with one reference, or release a
 *   reference after all stale translations have been invalidated. Physical
 *   address zero denotes an unpopulated leaf; the immutable zero page has
 *   permanent storage and does not participate in reference counting.
 *
 ****************************************************************************/

uintptr_t loongarch_page_alloc(void)
{
  uintptr_t page = mm_pgalloc(1);

  if (page != 0)
    {
      ASSERT(__atomic_load_n(&g_page_refs[page_index(page)],
                             __ATOMIC_RELAXED) == 0);
      __atomic_store_n(&g_page_refs[page_index(page)], 1, __ATOMIC_RELEASE);
      memset(loongarch_virtaddr(page), 0, LOONGARCH_PAGE_SIZE);
    }

  return page;
}

void loongarch_page_put(uintptr_t page)
{
  uint32_t refs;

  if (page == 0 || page == loongarch_physaddr((uintptr_t)g_zero_page))
    {
      return;
    }

  refs = __atomic_fetch_sub(&g_page_refs[page_index(page)], 1,
                            __ATOMIC_ACQ_REL);
  ASSERT(refs != 0);
  if (refs == 1)
    {
      mm_pgfree(page, 1);
    }
}

/****************************************************************************
 * Name: resolve_page
 *
 * Description:
 *   Resolve a demand-zero or COW leaf under g_env_lock. Reject accesses
 *   beyond the leaf's logical permissions. COW clears both D and W so a
 *   hardware walker cannot silently make a shared page writable. Publish
 *   the replacement and finish remote invalidation before dropping the old
 *   backing-page reference.
 *
 ****************************************************************************/

static int resolve_page(arch_addrenv_t *env, uintptr_t addr, int prot)
{
  uintptr_t zero = loongarch_physaddr((uintptr_t)g_zero_page);
  uintptr_t oldpage;
  uintptr_t page;
  uint64_t value;
  uint64_t *pte;

  if (addr >= LOONGARCH_MMU_VLIMIT)
    {
      return -EFAULT;
    }

  pte = loongarch_mmu_pte(env->root, addr, false);
  if (pte == NULL)
    {
      return -EFAULT;
    }

  value = __atomic_load_n(pte, __ATOMIC_ACQUIRE);
  if ((value & LOONGARCH_PTE_USER) != LOONGARCH_PTE_USER ||
      !(value & (LOONGARCH_PTE_VALID | LOONGARCH_PTE_LAZY)) ||
      ((prot & PROT_READ) && (value & LOONGARCH_PTE_NOREAD)) ||
      ((prot & PROT_EXEC) && (value & LOONGARCH_PTE_NOEXEC)) ||
      ((prot & PROT_WRITE) &&
       !(value & (LOONGARCH_PTE_WRITE | LOONGARCH_PTE_COW))))
    {
      return -EFAULT;
    }

  oldpage = value & LOONGARCH_PTE_PADDR_MASK;
  if (value & LOONGARCH_PTE_LAZY)
    {
      value &= ~LOONGARCH_PTE_LAZY;
      value |= LOONGARCH_PTE_VALID | LOONGARCH_PTE_PRESENT;
      if (!(prot & PROT_WRITE))
        {
          if (value & LOONGARCH_PTE_WRITE)
            {
              value |= LOONGARCH_PTE_COW;
            }

          value &= ~(LOONGARCH_PTE_WRITE | LOONGARCH_PTE_DIRTY);
          page = zero;
          goto install;
        }
    }
  else if (!(prot & PROT_WRITE) || !(value & LOONGARCH_PTE_COW))
    {
      return OK;
    }

  /* A sole owner can remove write protection without copying the page. */

  page = oldpage;
  if (oldpage == 0 || oldpage == zero ||
      __atomic_load_n(&g_page_refs[page_index(oldpage)],
                      __ATOMIC_ACQUIRE) != 1)
    {
      page = loongarch_page_alloc();
      if (page == 0)
        {
          return -ENOMEM;
        }

      if (oldpage != 0 && oldpage != zero)
        {
          memcpy(loongarch_virtaddr(page), loongarch_virtaddr(oldpage),
                 LOONGARCH_PAGE_SIZE);
        }
    }

  value &= ~LOONGARCH_PTE_COW;
  value |= LOONGARCH_PTE_DIRTY | LOONGARCH_PTE_WRITE;

install:
  value = (value & ~LOONGARCH_PTE_PADDR_MASK) | page;
  __atomic_store_n(pte, value, __ATOMIC_RELEASE);
  loongarch_tlb_sync();
  if (oldpage != page)
    {
      loongarch_page_put(oldpage);
    }

  return OK;
}

/****************************************************************************
 * Name: loongarch_vm_fault
 *
 * Description:
 *   Materialize one permitted user page from thread context. The caller
 *   keeps the environment alive and enables interrupts before entering.
 *   Concurrent faults serialize and recheck the leaf before allocating.
 *
 ****************************************************************************/

int loongarch_vm_fault(arch_addrenv_t *env, uintptr_t addr, int prot)
{
  int ret = nxmutex_lock(&g_env_lock);

  if (ret >= 0)
    {
      ret = resolve_page(env, addr, prot);
      nxmutex_unlock(&g_env_lock);
    }

  return ret;
}

/****************************************************************************
 * Name: user_transfer
 *
 * Description:
 *   Validate and materialize every page in a user range, then optionally
 *   copy through kernel physical aliases. Hold the VM mutex through both
 *   phases to exclude permission changes and COW replacement. Never follow
 *   a user pointer directly in kernel mode. A zero-length range is valid;
 *   nonempty null, wrapped and kernel-window ranges fail with EFAULT.
 *
 ****************************************************************************/

static int user_transfer(uintptr_t user, void *kernel, size_t size, int prot)
{
  struct tcb_s *tcb = this_task();
  arch_addrenv_t *env;
  uintptr_t addr;
  uintptr_t end;
  size_t offset;
  size_t chunk;
  uint64_t *pte;
  void *alias;
  int ret;

  if (size == 0)
    {
      return OK;
    }

  if (user == 0 || user >= LOONGARCH_MMU_VLIMIT ||
      size > LOONGARCH_MMU_VLIMIT - user || tcb->addrenv_curr == NULL)
    {
      return -EFAULT;
    }

  env = &tcb->addrenv_curr->addrenv;
  end = user + size;
  ret = nxmutex_lock(&g_env_lock);
  if (ret < 0)
    {
      return ret;
    }

  for (addr = user & ~(LOONGARCH_PAGE_SIZE - 1ul); addr < end;
       addr += LOONGARCH_PAGE_SIZE)
    {
      ret = resolve_page(env, addr, prot);
      if (ret < 0)
        {
          goto out;
        }
    }

  while (kernel != NULL && size != 0)
    {
      offset = user & (LOONGARCH_PAGE_SIZE - 1);
      chunk = LOONGARCH_PAGE_SIZE - offset;
      if (chunk > size)
        {
          chunk = size;
        }

      pte = loongarch_mmu_pte(env->root, user, false);
      alias = (uint8_t *)loongarch_virtaddr(*pte & LOONGARCH_PTE_PADDR_MASK);
      alias = (uint8_t *)alias + offset;
      if (prot & PROT_WRITE)
        {
          memcpy(alias, kernel, chunk);
        }
      else
        {
          memcpy(kernel, alias, chunk);
        }

      user += chunk;
      kernel = (uint8_t *)kernel + chunk;
      size -= chunk;
    }

out:
  nxmutex_unlock(&g_env_lock);
  return ret;
}

/****************************************************************************
 * Name: loongarch_user_cmpxchg
 *
 * Description:
 *   Update an aligned user counter atomically through its physical alias.
 *   Hold the VM lock across COW resolution and compare/exchange so the
 *   operation cannot target an obsolete backing page. Return EAGAIN when
 *   the counter differs from the expected value.
 *
 ****************************************************************************/

int loongarch_user_cmpxchg(uintptr_t addr, int32_t expected, int32_t desired)
{
  struct tcb_s *tcb = this_task();
  arch_addrenv_t *env;
  int32_t *alias;
  uint64_t *pte;
  int ret;

  if (addr == 0 || addr >= LOONGARCH_MMU_VLIMIT ||
      (addr & (sizeof(int32_t) - 1)) != 0 || tcb->addrenv_curr == NULL)
    {
      return -EFAULT;
    }

  env = &tcb->addrenv_curr->addrenv;
  ret = nxmutex_lock(&g_env_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = resolve_page(env, addr, PROT_READ | PROT_WRITE);
  if (ret == OK)
    {
      pte = loongarch_mmu_pte(env->root, addr, false);
      alias = (int32_t *)((uint8_t *)loongarch_virtaddr(*pte &
                                      LOONGARCH_PTE_PADDR_MASK) +
                         (addr & (LOONGARCH_PAGE_SIZE - 1)));
      if (!__atomic_compare_exchange_n(alias, &expected, desired, false,
                                        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        {
          ret = -EAGAIN;
        }
    }

  nxmutex_unlock(&g_env_lock);
  return ret;
}

/****************************************************************************
 * Name: loongarch_user_access
 *
 * Description:
 *   Check logical permissions and make a user range resident. This is a
 *   permission check, not a pin: callers must use the copy helpers when a
 *   later page-table update could otherwise race the actual transfer.
 *
 ****************************************************************************/

int loongarch_user_access(uintptr_t addr, size_t size, int prot)
{
  return user_transfer(addr, NULL, size, prot);
}

/****************************************************************************
 * Name: loongarch_copyfromuser / loongarch_copytouser
 *
 * Description:
 *   Copy a complete checked user range through its physical aliases. Return
 *   OK or a negative errno; invalid ranges never cause a kernel page fault.
 *
 ****************************************************************************/

int loongarch_copyfromuser(void *dest, const void *src, size_t size)
{
  return user_transfer((uintptr_t)src, dest, size, PROT_READ);
}

int loongarch_copytouser(void *dest, const void *src, size_t size)
{
  return user_transfer((uintptr_t)dest, (void *)src, size, PROT_WRITE);
}

/****************************************************************************
 * Name: cow_table
 *
 * Description:
 *   Duplicate occupied leaves into a private root while holding g_env_lock.
 *   Share backing pages with counted references. Write-protect the parent
 *   before publishing the child; maproot completes synchronous shootdown.
 *   Lazy leaves retain their reservation without acquiring a page reference.
 *
 ****************************************************************************/

static int cow_table(uintptr_t root, arch_addrenv_t *dest,
                     unsigned int level, uintptr_t base)
{
  uint64_t *table = loongarch_virtaddr(root);
  unsigned int shift = 39 - level * 9;
  unsigned int i;
  uintptr_t addr;
  uintptr_t page;
  uint64_t value;
  uint32_t refs;
  int ret;

  for (i = 0; i < LOONGARCH_PT_ENTRIES; i++)
    {
      value = __atomic_load_n(&table[i], __ATOMIC_ACQUIRE);
      addr = base | ((uintptr_t)i << shift);
      if (level != 3)
        {
          /* Empty directories lead to immutable empty tables. Walking
           * them is unnecessary: only configured user regions can own
           * leaves in this port.
           */

          if (addr >= ARCH_HEAP_VEND ||
              addr + (1ul << shift) <= CONFIG_ARCH_TEXT_VBASE)
            {
              continue;
            }

          ret = cow_table(value & LOONGARCH_PTE_PADDR_MASK,
                          dest, level + 1, addr);
          if (ret < 0)
            {
              return ret;
            }

          continue;
        }

      if (value == 0)
        {
          continue;
        }

      page = value & LOONGARCH_PTE_PADDR_MASK;
      if (value & LOONGARCH_PTE_WRITE && !(value & LOONGARCH_PTE_LAZY))
        {
          value &= ~(LOONGARCH_PTE_WRITE | LOONGARCH_PTE_DIRTY);
          value |= LOONGARCH_PTE_COW;
          __atomic_store_n(&table[i], value, __ATOMIC_RELEASE);
        }

      if (page != 0 && page != loongarch_physaddr((uintptr_t)g_zero_page))
        {
          refs = __atomic_fetch_add(&g_page_refs[page_index(page)], 1,
                                    __ATOMIC_ACQ_REL);
          ASSERT(refs != 0 && refs != UINT32_MAX);
        }

      ret = loongarch_mmu_maproot(dest->root, addr, page,
                                  LOONGARCH_PAGE_SIZE,
                                  value & ~LOONGARCH_PTE_PADDR_MASK);
      if (ret < 0)
        {
          loongarch_page_put(page);
          return ret;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: loongarch_addrenv_cow
 *
 * Description:
 *   Build an independently owned address environment that initially shares
 *   the source's backing pages. This differs from up_addrenv_clone, which
 *   transfers a descriptor. The caller keeps src alive and must quiesce its
 *   writers if it requires an atomic snapshot of the entire address space.
 *   On failure destroy the partial child; parent COW leaves remain valid
 *   and recover write access on their next write. This does not create a
 *   task group or implement the POSIX fork process interface.
 *
 ****************************************************************************/

int loongarch_addrenv_cow(arch_addrenv_t *src, arch_addrenv_t *dest)
{
  int ret;

  if (src == dest || src->root == 0)
    {
      return -EINVAL;
    }

  memset(dest, 0, sizeof(*dest));
  ret = nxmutex_lock(&g_env_lock);
  if (ret < 0)
    {
      return ret;
    }

  *dest = *src;
  dest->asid = kmm_zalloc(CONFIG_SMP_NCPUS * sizeof(*dest->asid));
  dest->root = loongarch_mmu_newroot();
  if (dest->asid == NULL || dest->root == 0)
    {
      ret = -ENOMEM;
    }
  else
    {
      ret = cow_table(src->root, dest, 0, 0);
    }

  /* A failed child mapping may have followed a parent PTE downgrade. */

  loongarch_tlb_sync();
  if (ret < 0)
    {
      up_addrenv_destroy(dest);
    }

  nxmutex_unlock(&g_env_lock);
  return ret;
}
