/****************************************************************************
 * arch/loongarch/src/common/loongarch_pagefault.c
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
#include <nuttx/addrenv.h>
#include <nuttx/arch.h>
#include <nuttx/sched.h>

#include "sched/sched.h"
#include "signal/signal.h"
#include "loongarch_internal.h"
#include "loongarch_mmu.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_pagefault
 *
 * Description:
 *   Resolve a data or instruction page fault on the interrupted thread's
 *   kernel stack, before entering IRQ context. Allocation and TLB shootdown
 *   may block, so reject faults from interrupt or IRQ-masked kernel code.
 *   Preserve the original user frame for deferred signal delivery. Return
 *   zero to retry the instruction, or the exception code for normal fatal
 *   exception handling. Ordinary kernel faults remain fatal.
 *
 ****************************************************************************/

int loongarch_pagefault(int excode, uintreg_t *regs)
{
  struct tcb_s *tcb = this_task();
  uintptr_t addr = csr_readq(LOONGARCH_CSR_BADV);
  uintreg_t *previous = tcb->xcp.sregs;
  bool user = (regs[REG_INT_CTX] & CSR_PRMD_PPLV) == PLV_USER;
  int prot = excode == EXCCODE_TLBS || excode == EXCCODE_TLBM ?
             PROT_WRITE : excode == EXCCODE_TLBI ? PROT_EXEC : PROT_READ;
  int ret;

  if (up_interrupt_context() || tcb->addrenv_curr == NULL ||
      !(regs[REG_INT_CTX] & CSR_PRMD_PIE))
    {
      return excode;
    }

  if (user)
    {
      tcb->xcp.sregs = regs;
      tcb->flags |= TCB_FLAG_SYSCALL;
    }

  up_irq_enable();
  ret = loongarch_vm_fault(&tcb->addrenv_curr->addrenv, addr, prot);

  if (user)
    {
      tcb->flags &= ~TCB_FLAG_SYSCALL;
      if (ret == OK)
        {
          nxsig_unmask_pendingsignal();
        }
    }

  up_irq_save();
  tcb->xcp.sregs = previous;
  csr_writeq(tcb->xcp.kstkptr, LOONGARCH_CSR_SAVE3);

  /* A simultaneous fault may already have repaired the PTE. Discard the
   * local stale entry even when this invocation did not replace the leaf.
   */

  if (ret == OK)
    {
      loongarch_tlb_invalidate();
      return 0;
    }

  return excode;
}
