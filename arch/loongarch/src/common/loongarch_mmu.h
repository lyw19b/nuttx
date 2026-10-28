/****************************************************************************
 * arch/loongarch/src/common/loongarch_mmu.h
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

#ifndef __ARCH_LOONGARCH_SRC_COMMON_LOONGARCH_MMU_H
#define __ARCH_LOONGARCH_SRC_COMMON_LOONGARCH_MMU_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#  include <stdbool.h>
#  include <stddef.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LOONGARCH_PAGE_SHIFT     12
#define LOONGARCH_PAGE_SIZE      4096
#define LOONGARCH_PT_ENTRIES     512
#define LOONGARCH_PTE_VALID      (1ul << 0)
#define LOONGARCH_PTE_DIRTY      (1ul << 1)
#define LOONGARCH_PTE_USER       (3ul << 2)
#define LOONGARCH_PTE_CACHE      (1ul << 4)
#define LOONGARCH_PTE_GLOBAL     (1ul << 6)
#define LOONGARCH_PTE_PRESENT    (1ul << 7)
#define LOONGARCH_PTE_WRITE      (1ul << 8)
#define LOONGARCH_PTE_NOREAD     (1ul << 61)
#define LOONGARCH_PTE_NOEXEC     (1ul << 62)
#define LOONGARCH_PTE_PADDR_MASK 0x0000fffffffff000ul
#define LOONGARCH_PADDR_MASK    0x0000fffffffffffful
#define LOONGARCH_CACHED_BASE   0x9000000000000000ul
#define LOONGARCH_MMU_VLIMIT    (1ul << 47)

#define LOONGARCH_MMU_READ      (LOONGARCH_PTE_VALID | \
                                LOONGARCH_PTE_PRESENT | \
                                LOONGARCH_PTE_CACHE)
#define LOONGARCH_MMU_WRITE     (LOONGARCH_MMU_READ | \
                                LOONGARCH_PTE_DIRTY | \
                                LOONGARCH_PTE_WRITE | \
                                LOONGARCH_PTE_NOEXEC)

#ifndef __ASSEMBLY__

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Map operations accept page-aligned addresses in the lower canonical VA
 * range. They run in thread context with interrupts enabled, serialize
 * internally, and complete remote invalidation before returning. Mapped
 * data pages remain owned by the caller. An existing mapping is never
 * silently replaced. Unmap releases empty page tables, not data pages.
 */

int loongarch_mmu_map(uintptr_t vaddr, uintptr_t paddr, size_t size,
                      uint64_t flags);
int loongarch_mmu_unmap(uintptr_t vaddr, size_t size);
int loongarch_mmu_translate(uintptr_t vaddr, uintptr_t *paddr);
void loongarch_mmu_initialize(void);
void loongarch_mmu_cpu_initialize(void);
void loongarch_tlb_invalidate(void);
void loongarch_tlb_sync(void);

/* Internal root operations. Callers must hold the address environment's
 * lock when walking or editing leaves directly.
 */

uintptr_t loongarch_mmu_newroot(void);
void loongarch_mmu_freetable(uintptr_t root, unsigned int level);
uint64_t *loongarch_mmu_pte(uintptr_t root, uintptr_t vaddr, bool create);
int loongarch_mmu_maproot(uintptr_t root, uintptr_t vaddr,
                          uintptr_t paddr, size_t size, uint64_t flags);
int loongarch_mmu_unmaproot(uintptr_t root, uintptr_t vaddr, size_t size);

/****************************************************************************
 * Name: loongarch_physaddr
 *
 * Description:
 *   Strip the direct-mapping window bits from a kernel address to obtain
 *   its physical address. The caller supplies a direct-mapped address.
 *
 ****************************************************************************/

static inline uintptr_t loongarch_physaddr(uintptr_t vaddr)
{
  return vaddr & LOONGARCH_PADDR_MASK;
}

/****************************************************************************
 * Name: loongarch_virtaddr
 *
 * Description:
 *   Return the cached PLV0 direct-mapping address for a physical page.
 *
 ****************************************************************************/

static inline void *loongarch_virtaddr(uintptr_t paddr)
{
  return (void *)(paddr | LOONGARCH_CACHED_BASE);
}

#endif /* __ASSEMBLY__ */
#endif /* __ARCH_LOONGARCH_SRC_COMMON_LOONGARCH_MMU_H */
