/****************************************************************************
 * arch/loongarch/src/common/loongarch_tlb.c
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
#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <arch/barriers.h>

#include "loongarch_mmu.h"
#include "loongarch_smp.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_SMP
static int loongarch_tlb_remote(void *arg)
{
  loongarch_tlb_invalidate();
  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Page-table callers hold a sleepable mutex, never an IRQ spinlock.
 * NuttX SMP calls acknowledge completion on every online CPU before any
 * detached table or backing page may be reused. Include the calling CPU
 * in the mask: the waiting thread may migrate while callbacks complete.
 */

void loongarch_tlb_sync(void)
{
  UP_DMB();
#ifdef CONFIG_SMP
  cpu_set_t online = loongarch_cpu_online();

  if (online != 0)
    {
      int ret = nxsched_smp_call(online, loongarch_tlb_remote, NULL);
      ASSERT(ret == OK);
      return;
    }
#endif

  loongarch_tlb_invalidate();
}
