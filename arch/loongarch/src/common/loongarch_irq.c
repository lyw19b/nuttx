/****************************************************************************
 * arch/loongarch/src/common/loongarch_irq.c
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

#include <stdint.h>
#include <assert.h>
#include <syscall.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/addrenv.h>
#include <nuttx/board.h>
#include <arch/barriers.h>
#include <arch/board/board.h>
#include <sched/sched.h>

#include "loongarch_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_doirq_top
 *
 * Description:
 *   Save the interrupted context, dispatch its vector and select the
 *   context to resume. A kernel restore syscall supplies an existing frame
 *   and must not overwrite it. Switch address environments and publish the
 *   selected trusted stack before returning to assembly.
 *
 ****************************************************************************/

static uintreg_t *loongarch_doirq_top(int irq, uintreg_t *regs)
{
  struct tcb_s **running_task = &g_running_tasks[this_cpu()];
  bool restore_context = false;
  struct tcb_s *tcb = this_task();

  board_autoled_on(LED_INIRQ);
#ifdef CONFIG_SUPPRESS_INTERRUPTS
  PANIC();
#else

  if (irq == LOONGARCH_IRQ_SYS)
    {
      if (regs[REG_R4] != SYS_restore_context ||
          (regs[REG_INT_CTX] & CSR_PRMD_PPLV) != PLV_KERN)
        {
          (*running_task)->xcp.regs = regs;
        }
      else
        {
          restore_context = true;
        }
    }
  else
    {
      (*running_task)->xcp.regs = regs;
    }

  up_set_interrupt_context(true);
  UP_DMB();

  irq_dispatch(irq, regs);

  /* The handler may have selected another ready task. */

  tcb = this_task();

  if (*running_task != tcb || restore_context)
    {
#ifdef CONFIG_ARCH_ADDRENV
      addrenv_switch(tcb);
#endif

      nxsched_suspend_scheduler(*running_task);
      nxsched_resume_scheduler(tcb);
      *running_task = tcb;
    }

  up_set_interrupt_context(false);

#endif
  board_autoled_off(LED_INIRQ);

#ifdef CONFIG_ARCH_KERNEL_STACK
  csr_writeq(tcb->xcp.kstkptr, LOONGARCH_CSR_SAVE3);
#endif

  regs = tcb->xcp.regs;
  (*running_task)->xcp.regs = NULL;
  return regs;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_doirq
 *
 * Description:
 *   Dispatch a vectored interrupt or reserved syscall with interrupts
 *   masked. Only the outermost invocation may change the running task and
 *   return frame; nested dispatch returns its original frame.
 *
 ****************************************************************************/

uintreg_t *loongarch_doirq(int irq, uintreg_t *regs)
{
  if (up_interrupt_context())
    {
      irq_dispatch(irq, regs);
      return regs;
    }

  return loongarch_doirq_top(irq, regs);
}
