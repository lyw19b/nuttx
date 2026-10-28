/****************************************************************************
 * arch/loongarch/src/common/loongarch_allocateheap.c
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

#include <sys/types.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <arch/board/board.h>

#include "loongarch_internal.h"
#include "loongarch_mmu.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void weak_function up_allocate_heap(void **heap_start, size_t *heap_size)
{
  uintptr_t end = CONFIG_RAM_END;

  board_autoled_on(LED_HEAPALLOCATE);
#ifdef CONFIG_MM_PGALLOC
  end = (end & ~(LOONGARCH_PAGE_SIZE - 1ul)) -
         CONFIG_LOONGARCH_PAGE_POOL_SIZE;
#endif

  ASSERT(end > g_idle_topstack && end <= CONFIG_RAM_END);
  *heap_start = (void *)g_idle_topstack;
  *heap_size = end - g_idle_topstack;
}

#ifdef CONFIG_MM_PGALLOC
void up_allocate_pgheap(void **heap_start, size_t *heap_size)
{
  uintptr_t base = (CONFIG_RAM_END & ~(LOONGARCH_PAGE_SIZE - 1ul)) -
                   CONFIG_LOONGARCH_PAGE_POOL_SIZE;

  DEBUGASSERT(base > g_idle_topstack);
  *heap_start = (void *)loongarch_physaddr(base);
  *heap_size = CONFIG_LOONGARCH_PAGE_POOL_SIZE;
}
#endif
