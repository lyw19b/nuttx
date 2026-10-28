/****************************************************************************
 * arch/loongarch/src/common/loongarch_smp.c
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
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/sched.h>
#include <nuttx/sched_note.h>
#include <arch/barriers.h>
#include <arch/csr.h>

#include "init/init.h"
#include "sched/sched.h"
#include "loongarch_internal.h"
#include "loongarch_mmu.h"
#include "loongarch_smp.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static cpu_set_t g_online;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void loongarch_ipi_send(int cpu)
{
  UP_DMB();
  iocsr_write32((1u << 31) | (cpu << 16), LOONGARCH_IOCSR_IPI_SEND);
}

static int loongarch_ipi_interrupt(int irq, void *context, void *arg)
{
  uint32_t pending = iocsr_read32(LOONGARCH_IOCSR_IPI_STATUS);

  /* Acknowledge before draining requests, so a concurrent sender cannot
   * lose its notification while the handler is processing the queue.
   */

  iocsr_write32(pending, LOONGARCH_IOCSR_IPI_CLEAR);
  UP_DMB();
  nxsched_smp_call_handler(irq, context, arg);
  nxsched_process_delivered(this_cpu());
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

cpu_set_t loongarch_cpu_online(void)
{
  return __atomic_load_n(&g_online, __ATOMIC_ACQUIRE);
}

void loongarch_smp_initialize(void)
{
  irq_attach(LOONGARCH_IRQ_IPI, loongarch_ipi_interrupt, NULL);
  iocsr_write32(UINT32_MAX, LOONGARCH_IOCSR_IPI_CLEAR);
  iocsr_write32(1, LOONGARCH_IOCSR_IPI_EN);
  up_enable_irq(LOONGARCH_IRQ_IPI);
  __atomic_store_n(&g_online, 1, __ATOMIC_RELEASE);
}

int up_send_smp_sched(int cpu)
{
  DEBUGASSERT(cpu >= 0 && cpu < CONFIG_SMP_NCPUS);
  loongarch_ipi_send(cpu);
  return OK;
}

void up_send_smp_call(cpu_set_t cpuset)
{
  int cpu;

  for (cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
    {
      if (CPU_ISSET(cpu, &cpuset))
        {
          loongarch_ipi_send(cpu);
        }
    }
}

int up_cpu_start(int cpu)
{
  extern const char loongarch_secondary_entry[];
  uintptr_t entry = (uintptr_t)loongarch_secondary_entry;
  uint64_t target;

  if (cpu <= 0 || cpu >= CONFIG_SMP_NCPUS)
    {
      return -EINVAL;
    }

#ifdef CONFIG_SCHED_INSTRUMENTATION
  sched_note_cpu_start(this_task(), cpu);
#endif

  /* Firmware waits for a 64-bit entry address in IOCSR mailbox zero.
   * Publish both halves before sending the startup IPI. Supported
   * platforms use contiguous hardware CPU IDs starting at zero.
   */

  target = (1ul << 31) | ((uint64_t)cpu << 16);
  iocsr_write64(target | 4 | (entry & 0xffffffff00000000ul),
                LOONGARCH_IOCSR_MAIL_SEND);
  iocsr_write64(target | (entry << 32), LOONGARCH_IOCSR_MAIL_SEND);
  loongarch_ipi_send(cpu);
  return OK;
}

void loongarch_secondary_start(void)
{
  int cpu = this_cpu();

  loongarch_vector_initialize();
#ifdef CONFIG_ARCH_USE_MMU
  loongarch_mmu_cpu_initialize();
#endif
  iocsr_write32(UINT32_MAX, LOONGARCH_IOCSR_IPI_CLEAR);
  iocsr_write32(1, LOONGARCH_IOCSR_IPI_EN);
  up_enable_irq(LOONGARCH_IRQ_IPI);
  __atomic_fetch_or(&g_online, (cpu_set_t)1 << cpu, __ATOMIC_RELEASE);
#ifdef CONFIG_SCHED_INSTRUMENTATION
  sched_note_cpu_started(this_task());
#endif
  up_irq_enable();
  nx_idle_trampoline();
}
