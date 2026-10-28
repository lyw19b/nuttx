/****************************************************************************
 * arch/loongarch/src/common/loongarch_mmu.c
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

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <nuttx/pgalloc.h>
#include <arch/barriers.h>
#include <arch/csr.h>

#include "loongarch_mmu.h"

#if CONFIG_MM_PGSIZE != 4096
#  error LoongArch requires 4096-byte pages
#endif

#if (CONFIG_LOONGARCH_PAGE_POOL_SIZE & 4095) != 0
#  error LoongArch page pool must be page aligned
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Empty directories point at real, immutable tables. A hardware walker
 * must never dereference physical address zero for an absent directory.
 */

static uint64_t g_empty_leaf[LOONGARCH_PT_ENTRIES]
  aligned_data(LOONGARCH_PAGE_SIZE);
static uint64_t g_empty_middle[LOONGARCH_PT_ENTRIES]
  aligned_data(LOONGARCH_PAGE_SIZE);
static uint64_t g_empty_upper[LOONGARCH_PT_ENTRIES]
  aligned_data(LOONGARCH_PAGE_SIZE);
static uint64_t g_empty_root[LOONGARCH_PT_ENTRIES]
  aligned_data(LOONGARCH_PAGE_SIZE);
static uint64_t g_kernel_pgt[LOONGARCH_PT_ENTRIES]
  aligned_data(LOONGARCH_PAGE_SIZE);
static mutex_t g_mmu_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: table_paddr
 *
 * Description:
 *   Return the physical address of a table stored in the kernel direct-
 *   mapping window.
 *
 ****************************************************************************/

static uintptr_t table_paddr(const uint64_t *table)
{
  return loongarch_physaddr((uintptr_t)table);
}

/****************************************************************************
 * Name: table_vaddr
 *
 * Description:
 *   Convert a directory entry into a cached kernel table pointer, removing
 *   any entry attribute bits.
 *
 ****************************************************************************/

static uint64_t *table_vaddr(uint64_t entry)
{
  return loongarch_virtaddr(entry & LOONGARCH_PTE_PADDR_MASK);
}

/****************************************************************************
 * Name: table_initialize
 *
 * Description:
 *   Fill a page table with the empty entry appropriate to its level.
 *
 ****************************************************************************/

static void table_initialize(uint64_t *table, uint64_t entry)
{
  unsigned int i;

  for (i = 0; i < LOONGARCH_PT_ENTRIES; i++)
    {
      table[i] = entry;
    }
}

/****************************************************************************
 * Name: table_allocate
 *
 * Description:
 *   Allocate and initialize a table before publishing it to other CPUs or
 *   a hardware walker. Return NULL if the physical page pool is exhausted.
 *
 ****************************************************************************/

static uint64_t *table_allocate(uint64_t entry)
{
  uintptr_t paddr = mm_pgalloc(1);
  uint64_t *table;

  if (paddr == 0)
    {
      return NULL;
    }

  table = loongarch_virtaddr(paddr);
  table_initialize(table, entry);
  UP_DMB();
  return table;
}

/****************************************************************************
 * Name: loongarch_mmu_pte
 *
 * Description:
 *   Walk the supplied physical root and return the leaf entry for a
 *   virtual address. If create is true, replace shared empty directories
 *   with private tables as needed. The caller must serialize mutations and
 *   keep the root alive. Return NULL for an absent path or allocation
 *   failure.
 *
 ****************************************************************************/

uint64_t *loongarch_mmu_pte(uintptr_t root, uintptr_t vaddr, bool create)
{
  uint64_t *table = loongarch_virtaddr(root);
  uint64_t *next;
  uintptr_t empty;
  unsigned int shift;
  unsigned int index;

  for (shift = 39; shift > LOONGARCH_PAGE_SHIFT; shift -= 9)
    {
      index = (vaddr >> shift) & 511;
      empty = table_paddr(shift == 39 ? g_empty_upper :
                          shift == 30 ? g_empty_middle : g_empty_leaf);
      if (table[index] == empty)
        {
          if (!create)
            {
              return NULL;
            }

          next = table_allocate(shift == 39 ? table_paddr(g_empty_middle) :
                                shift == 30 ? table_paddr(g_empty_leaf) : 0);
          if (next == NULL)
            {
              return NULL;
            }

          /* Publish each child only after initializing all its entries. */

          __atomic_store_n(&table[index], table_paddr(next),
                           __ATOMIC_RELEASE);
        }

      table = table_vaddr(table[index]);
    }

  return &table[(vaddr >> LOONGARCH_PAGE_SHIFT) & 511];
}

/****************************************************************************
 * Name: table_empty
 *
 * Description:
 *   Test whether every entry has the empty value for this table level.
 *
 ****************************************************************************/

static bool table_empty(const uint64_t *table, uint64_t empty)
{
  unsigned int i;

  for (i = 0; i < LOONGARCH_PT_ENTRIES; i++)
    {
      if (table[i] != empty)
        {
          return false;
        }
    }

  return true;
}

/* Reclaim only after all CPUs have stopped using a detached table. The
 * MMU mutex is sleepable; remote invalidation never takes this mutex.
 */

/****************************************************************************
 * Name: table_reclaim
 *
 * Description:
 *   Detach empty directories from the bottom of a page-table path upward.
 *   The caller holds the MMU mutex. Wait for remote TLB invalidations
 *   before freeing each detached table so no CPU can walk reused memory.
 *
 ****************************************************************************/

static void table_reclaim(uintptr_t root, uintptr_t vaddr)
{
  uint64_t *tables[4];
  uint64_t empty[4];
  unsigned int indices[3];
  unsigned int level;
  int depth;

  empty[0] = table_paddr(g_empty_upper);
  empty[1] = table_paddr(g_empty_middle);
  empty[2] = table_paddr(g_empty_leaf);
  empty[3] = 0;
  tables[0] = loongarch_virtaddr(root);

  for (level = 0; level < 3; level++)
    {
      indices[level] = (vaddr >> (39 - 9 * level)) & 511;
      if (tables[level][indices[level]] == empty[level])
        {
          break;
        }

      tables[level + 1] = table_vaddr(tables[level][indices[level]]);
    }

  for (depth = level; depth > 0; depth--)
    {
      if (!table_empty(tables[depth], empty[depth]))
        {
          break;
        }

      __atomic_store_n(&tables[depth - 1][indices[depth - 1]],
                       empty[depth - 1], __ATOMIC_RELEASE);
      loongarch_tlb_sync();
      mm_pgfree(table_paddr(tables[depth]), 1);
    }
}

/****************************************************************************
 * Name: valid_range
 *
 * Description:
 *   Check page alignment, nonzero length and overflow against the
 *   supported low canonical virtual-address limit.
 *
 ****************************************************************************/

static bool valid_range(uintptr_t vaddr, size_t size)
{
  return size != 0 && ((vaddr | size) & (LOONGARCH_PAGE_SIZE - 1)) == 0 &&
         vaddr < LOONGARCH_MMU_VLIMIT &&
         size <= LOONGARCH_MMU_VLIMIT - vaddr;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_tlb_invalidate
 *
 * Description:
 *   Order page-table stores, invalidate all local TLB entries and
 *   synchronize instruction fetches. This operation does not affect other
 *   CPUs.
 *
 ****************************************************************************/

void loongarch_tlb_invalidate(void)
{
  __asm__ __volatile__("dbar 0\n\tinvtlb 0, $zero, $zero\n\tibar 0"
                       : : : "memory");
}

/****************************************************************************
 * Name: loongarch_mmu_cpu_initialize
 *
 * Description:
 *   Configure the four-level page-table walker, refill vector and initial
 *   roots on the calling CPU. Enable hardware walking only when both the
 *   configuration and CPU capability permit it. Interrupts remain disabled
 *   until CPU startup completes.
 *
 ****************************************************************************/

void loongarch_mmu_cpu_initialize(void)
{
  extern const char loongarch_tlbrefill[];
  uintptr_t pwch = 39 | (9 << 6);

  /* Four levels cover canonical 48-bit VAs without high-address aliases.
   * Each table has nine index bits and 512 64-bit entries.
   */

  csr_writeq(12 | (9 << 5) | (21 << 10) | (9 << 15) |
             (30 << 20) | (9 << 25), LOONGARCH_CSR_PWCL);
#ifdef CONFIG_LOONGARCH_MMU_PTW
  if ((read_cpucfg(2) & CPUCFG2_HPTW) != 0)
    {
      pwch |= CSR_PWCH_HPTW;
    }
#endif

  csr_writeq(pwch, LOONGARCH_CSR_PWCH);
  csr_writeq(LOONGARCH_PAGE_SHIFT, LOONGARCH_CSR_STLBPS);
  csr_writeq(LOONGARCH_PAGE_SHIFT, LOONGARCH_CSR_TLBREHI);
  csr_writeq(table_paddr(g_kernel_pgt), LOONGARCH_CSR_PGDL);
  csr_writeq(table_paddr(g_empty_root), LOONGARCH_CSR_PGDH);
  csr_writeq(0, LOONGARCH_CSR_ASID);
  csr_writeq(loongarch_physaddr((uintptr_t)loongarch_tlbrefill),
             LOONGARCH_CSR_TLBRENTRY);
  loongarch_tlb_invalidate();
  csr_xchgq(CSR_CRMD_PG, CSR_CRMD_PG | CSR_CRMD_DA,
            LOONGARCH_CSR_CRMD);
}

/****************************************************************************
 * Name: loongarch_mmu_initialize
 *
 * Description:
 *   Initialize immutable empty tables and the boot root once, before
 *   secondary CPUs start, then enable paging on the boot CPU.
 *
 ****************************************************************************/

void loongarch_mmu_initialize(void)
{
  table_initialize(g_empty_middle, table_paddr(g_empty_leaf));
  table_initialize(g_empty_upper, table_paddr(g_empty_middle));
  table_initialize(g_empty_root, table_paddr(g_empty_upper));
  table_initialize(g_kernel_pgt, table_paddr(g_empty_upper));
  UP_DMB();
  loongarch_mmu_cpu_initialize();
}

/****************************************************************************
 * Name: loongarch_mmu_maproot
 *
 * Description:
 *   Map a page-aligned physical range into the supplied root without
 *   replacing existing leaves. Serialize mutations with a sleepable mutex
 *   and roll back partial mappings on allocation failure. Call from task
 *   context; return OK or a negative errno.
 *
 ****************************************************************************/

int loongarch_mmu_maproot(uintptr_t root, uintptr_t vaddr,
                          uintptr_t paddr, size_t size, uint64_t flags)
{
  const uint64_t allowed = LOONGARCH_MMU_WRITE | LOONGARCH_PTE_USER |
                          LOONGARCH_PTE_NOREAD | LOONGARCH_PTE_NOEXEC;
  uintptr_t addr;
  uint64_t *pte;
  int ret;

  if (!valid_range(vaddr, size) ||
      (paddr & (LOONGARCH_PAGE_SIZE - 1)) != 0 ||
      paddr > LOONGARCH_PADDR_MASK ||
      size - 1 > LOONGARCH_PADDR_MASK - paddr ||
      (flags & ~allowed) != 0 || !(flags & LOONGARCH_PTE_VALID))
    {
      return -EINVAL;
    }

  DEBUGASSERT(!up_interrupt_context());
  ret = nxmutex_lock(&g_mmu_lock);
  if (ret < 0)
    {
      return ret;
    }

  /* Validate the entire range before changing any leaf. */

  for (addr = vaddr; addr < vaddr + size; addr += LOONGARCH_PAGE_SIZE)
    {
      pte = loongarch_mmu_pte(root, addr, false);
      if (pte != NULL && *pte != 0)
        {
          ret = -EEXIST;
          goto out;
        }
    }

  for (addr = vaddr; addr < vaddr + size; addr += LOONGARCH_PAGE_SIZE)
    {
      pte = loongarch_mmu_pte(root, addr, true);
      if (pte == NULL)
        {
          ret = -ENOMEM;
          goto rollback;
        }

      __atomic_store_n(pte, (paddr + addr - vaddr) | flags,
                       __ATOMIC_RELEASE);
    }

  loongarch_tlb_sync();
  goto out;

rollback:

  /* Drop new leaves before reclaiming their now-unreachable directories. */

  while (addr > vaddr)
    {
      addr -= LOONGARCH_PAGE_SIZE;
      pte = loongarch_mmu_pte(root, addr, false);
      __atomic_store_n(pte, 0, __ATOMIC_RELEASE);
    }

  loongarch_tlb_sync();

  for (addr = vaddr; addr < vaddr + size; addr += LOONGARCH_PAGE_SIZE)
    {
      table_reclaim(root, addr);
    }

out:
  nxmutex_unlock(&g_mmu_lock);
  return ret;
}

/****************************************************************************
 * Name: loongarch_mmu_unmaproot
 *
 * Description:
 *   Remove mappings and reclaim empty intermediate tables from a root.
 *   Synchronize all online CPUs before returning. The caller retains
 *   ownership of the unmapped backing pages. Call from task context.
 *
 ****************************************************************************/

int loongarch_mmu_unmaproot(uintptr_t root, uintptr_t vaddr, size_t size)
{
  uintptr_t addr;
  uint64_t *pte;
  int ret;

  if (!valid_range(vaddr, size))
    {
      return -EINVAL;
    }

  DEBUGASSERT(!up_interrupt_context());
  ret = nxmutex_lock(&g_mmu_lock);
  if (ret < 0)
    {
      return ret;
    }

  for (addr = vaddr; addr < vaddr + size; addr += LOONGARCH_PAGE_SIZE)
    {
      pte = loongarch_mmu_pte(root, addr, false);
      if (pte != NULL)
        {
          __atomic_store_n(pte, 0, __ATOMIC_RELEASE);
        }
    }

  loongarch_tlb_sync();

  for (addr = vaddr; addr < vaddr + size; addr += LOONGARCH_PAGE_SIZE)
    {
      table_reclaim(root, addr);
    }

  nxmutex_unlock(&g_mmu_lock);
  return OK;
}

/****************************************************************************
 * Name: loongarch_mmu_translate
 *
 * Description:
 *   Translate a virtual address through the boot root, preserving its page
 *   offset. Return -ENOENT for an absent leaf or -EINVAL for an invalid
 *   argument.
 *
 ****************************************************************************/

int loongarch_mmu_translate(uintptr_t vaddr, uintptr_t *paddr)
{
  uint64_t *pte;
  int ret;

  if (paddr == NULL || vaddr >= LOONGARCH_MMU_VLIMIT)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_mmu_lock);
  if (ret < 0)
    {
      return ret;
    }

  pte = loongarch_mmu_pte(table_paddr(g_kernel_pgt), vaddr, false);
  if (pte == NULL || !(*pte & LOONGARCH_PTE_VALID))
    {
      ret = -ENOENT;
    }
  else
    {
      *paddr = (*pte & LOONGARCH_PTE_PADDR_MASK) |
               (vaddr & (LOONGARCH_PAGE_SIZE - 1));
    }

  nxmutex_unlock(&g_mmu_lock);
  return ret;
}

/* The flat-build API uses the boot page directory. Address environments
 * supply their own root to the same mapping and rollback implementation.
 */

/****************************************************************************
 * Name: loongarch_mmu_map
 *
 * Description:
 *   Map pages through the boot root used by flat builds. Address
 *   environments use loongarch_mmu_maproot with their private roots.
 *
 ****************************************************************************/

int loongarch_mmu_map(uintptr_t vaddr, uintptr_t paddr, size_t size,
                      uint64_t flags)
{
  return loongarch_mmu_maproot(table_paddr(g_kernel_pgt), vaddr, paddr,
                               size, flags);
}

/****************************************************************************
 * Name: loongarch_mmu_unmap
 *
 * Description:
 *   Remove pages from the boot root without freeing their backing storage.
 *
 ****************************************************************************/

int loongarch_mmu_unmap(uintptr_t vaddr, size_t size)
{
  return loongarch_mmu_unmaproot(table_paddr(g_kernel_pgt), vaddr, size);
}

/****************************************************************************
 * Name: loongarch_mmu_newroot
 *
 * Description:
 *   Allocate a private root initially pointing to immutable empty tables.
 *   Return its physical address, or zero when allocation fails.
 *
 ****************************************************************************/

uintptr_t loongarch_mmu_newroot(void)
{
  uint64_t *table = table_allocate(table_paddr(g_empty_upper));

  return table == NULL ? 0 : table_paddr(table);
}

/* All leaves in a process root are owned by that address environment.
 * NuttX releases its last active-CPU reference before destroying the root.
 * Shared mappings require separate ownership accounting and are not enabled.
 */

/****************************************************************************
 * Name: loongarch_mmu_freetable
 *
 * Description:
 *   Recursively release an inactive process table and its owned backing
 *   pages. Level zero denotes the root and level three a leaf table. The
 *   caller must hold the final environment reference and exclude all
 *   walkers. Shared backing pages require separate ownership accounting.
 *
 ****************************************************************************/

void loongarch_mmu_freetable(uintptr_t root, unsigned int level)
{
  uint64_t *table = loongarch_virtaddr(root);
  uintptr_t empty = level == 0 ? table_paddr(g_empty_upper) :
                    level == 1 ? table_paddr(g_empty_middle) :
                    level == 2 ? table_paddr(g_empty_leaf) : 0;
  unsigned int i;

  for (i = 0; i < LOONGARCH_PT_ENTRIES; i++)
    {
      if (table[i] != empty)
        {
          uintptr_t paddr = table[i] & LOONGARCH_PTE_PADDR_MASK;

          if (level == 3)
            {
              mm_pgfree(paddr, 1);
            }
          else
            {
              loongarch_mmu_freetable(paddr, level + 1);
            }
        }
    }

  mm_pgfree(root, 1);
}
