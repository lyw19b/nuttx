/****************************************************************************
 * arch/loongarch/src/common/loongarch_fork.c
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
#include "sched/sched.h"
#include "task/task.h"
#include "loongarch_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* NuttX flat-build fork duplicates the active stack. It does not create
 * an isolated process address space; pointers into the old stack remain
 * subject to the restrictions documented by the NuttX fork interface.
 */

pid_t loongarch_fork(const uintreg_t *context)
{
  struct tcb_s *parent = this_task();
  struct task_tcb_s *child;
  struct tcb_s *tcb;
  uintptr_t top;
  uintptr_t sp;
  uintptr_t tp;
  size_t used;

  child = nxtask_setup_fork((start_t)context[REG_RA]);
  if (child == NULL)
    {
      return ERROR;
    }

  tcb = &child->cmn;
  top = (uintptr_t)parent->stack_base_ptr + parent->adj_stack_size;
  used = top - context[REG_SP];
  if (used + XCPTCONTEXT_SIZE > tcb->adj_stack_size)
    {
      nxtask_abort_fork(child, ENOMEM);
      return ERROR;
    }

  sp = (uintptr_t)tcb->stack_base_ptr + tcb->adj_stack_size - used;
  tp = tcb->xcp.regs[REG_R2];
  memcpy((void *)sp, (const void *)context[REG_SP], used);
  tcb->xcp.regs = (uintreg_t *)(sp - XCPTCONTEXT_SIZE);
  memcpy(tcb->xcp.regs, context, XCPTCONTEXT_SIZE);
  tcb->xcp.regs[REG_SP] = sp;
  tcb->xcp.regs[REG_R2] = tp;
  tcb->xcp.regs[REG_R4] = 0;
  tcb->xcp.regs[REG_EPC] = context[REG_RA];
  tcb->xcp.regs[REG_INT_CTX] = CSR_PRMD_PIE;

  /* Relocate the frame pointer when it points into the copied stack. */

  if (context[REG_R22] >= context[REG_SP] && context[REG_R22] < top)
    {
      tcb->xcp.regs[REG_R22] = sp + context[REG_R22] - context[REG_SP];
    }

  return nxtask_start_fork(child);
}
