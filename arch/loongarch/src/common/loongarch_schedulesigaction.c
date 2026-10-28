/****************************************************************************
 * arch/loongarch/src/common/loongarch_schedulesigaction.c
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

#include <inttypes.h>
#include <stdint.h>
#include <sched.h>
#include <assert.h>
#include <nuttx/debug.h>

#include <nuttx/arch.h>
#include <nuttx/spinlock.h>

#include "sched/sched.h"
#include "signal/signal.h"
#include "loongarch_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_schedule_sigaction
 *
 * Description:
 *   Redirect a saved thread context to the signal trampoline while keeping
 *   the interrupted frame on the trusted stack. The scheduler installs the
 *   delivery callback and handles synchronous self-delivery before calling
 *   this hook. The target is either interrupted locally or not running.
 *
 * Assumptions:
 *   Called from critical section
 *
 ****************************************************************************/

void up_schedule_sigaction(struct tcb_s *tcb)
{
  uintptr_t int_ctx;

  sinfo("tcb=%p, rtcb=%p current_regs=%p\n", tcb, this_task(),
        this_task()->xcp.regs);

  /* Save the current register context location */

  tcb->xcp.saved_regs        = tcb->xcp.regs;

  /* Duplicate the register context.  These will be
   * restored by the signal trampoline after the signal has been
   * delivered.
   */

  tcb->xcp.regs              = (uintreg_t *)
                               ((uintptr_t)tcb->xcp.regs -
                                           XCPTCONTEXT_SIZE);

  memcpy(tcb->xcp.regs, tcb->xcp.saved_regs, XCPTCONTEXT_SIZE);

  tcb->xcp.regs[REG_SP]      = (uintptr_t)tcb->xcp.regs +
                                          XCPTCONTEXT_SIZE;

  /* Then set up to vector to the trampoline with interrupts
   * disabled.  We must already be in privileged thread mode to be
   * here.
   */

  tcb->xcp.regs[REG_EPC]     = (uintptr_t)loongarch_sigdeliver;

  int_ctx                    = tcb->xcp.regs[REG_INT_CTX];
  int_ctx                   &= ~CSR_PRMD_PIE;
#ifndef CONFIG_BUILD_FLAT
  int_ctx                   &= ~CSR_PRMD_PPLV;
#endif

  tcb->xcp.regs[REG_INT_CTX] = int_ctx;
}
