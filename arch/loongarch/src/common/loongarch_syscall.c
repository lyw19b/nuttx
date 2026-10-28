/****************************************************************************
 * arch/loongarch/src/common/loongarch_syscall.c
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
#include <syscall.h>
#include <unistd.h>
#include <nuttx/addrenv.h>
#include <nuttx/arch.h>
#include <nuttx/sched.h>

#include "sched/sched.h"
#include "signal/signal.h"
#include "loongarch_internal.h"
#include "loongarch_mmu.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_ARCH_KERNEL_STACK
/****************************************************************************
 * Name: writable_user_range
 *
 * Description:
 *   Check every page touched by a signal frame before copying it to the
 *   user stack. Reject wraparound, missing mappings and nonwritable pages.
 *   The caller holds the current environment through the running task.
 *
 ****************************************************************************/

static bool writable_user_range(struct tcb_s *tcb, uintptr_t addr,
                                size_t size)
{
  const uint64_t required = LOONGARCH_PTE_USER | LOONGARCH_PTE_VALID |
                            LOONGARCH_PTE_DIRTY;
  uintptr_t end;
  uint64_t *pte;

  if (tcb->addrenv_own == NULL || addr >= LOONGARCH_MMU_VLIMIT ||
      size > LOONGARCH_MMU_VLIMIT - addr)
    {
      return false;
    }

  end = addr + size;
  addr &= ~(LOONGARCH_PAGE_SIZE - 1ul);

  for (; addr < end; addr += LOONGARCH_PAGE_SIZE)
    {
      pte = loongarch_mmu_pte(tcb->addrenv_own->addrenv.root, addr, false);
      if (pte == NULL || (*pte & required) != required)
        {
          return false;
        }
    }

  return true;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
/****************************************************************************
 * Name: loongarch_syscall_dispatch
 *
 * Description:
 *   Dispatch a PLV3 syscall using the saved a0 number and a1-a6 arguments.
 *   The vector entry calls this on the thread kernel stack with interrupts
 *   disabled and outside interrupt context. Enable interrupts around the
 *   syscall so it can block, then restore the user frame and trusted stack
 *   pointer before exception return.
 *
 ****************************************************************************/

void loongarch_syscall_dispatch(uintreg_t *regs)
{
  typedef uintptr_t (*stub_t)(unsigned int, uintptr_t, uintptr_t,
                             uintptr_t, uintptr_t, uintptr_t, uintptr_t);
  struct tcb_s *tcb = this_task();
  uintptr_t number = regs[REG_R4];
  uintreg_t *previous = tcb->xcp.sregs;
  uintptr_t result;
  stub_t stub;

  regs[REG_EPC] += 4;

  /* Validate the full register width before indexing the generated table. */

  if (number < CONFIG_SYS_RESERVED || number >= SYS_maxsyscall)
    {
      regs[REG_R4] = -ENOSYS;
      csr_writeq(tcb->xcp.kstkptr, LOONGARCH_CSR_SAVE3);
      return;
    }

  /* Keep the original user frame available to signal delivery while the
   * syscall executes, blocks, or migrates to another CPU.
   */

  tcb->xcp.sregs = regs;
  tcb->flags |= TCB_FLAG_SYSCALL;
  up_irq_enable();

  number -= CONFIG_SYS_RESERVED;
  stub = (stub_t)g_stublookup[number];
  result = stub(number, regs[REG_R5], regs[REG_R6], regs[REG_R7],
                regs[REG_R8], regs[REG_R9], regs[REG_R10]);

  /* Deliver deferred signals before releasing the syscall continuation. */

  tcb->flags &= ~TCB_FLAG_SYSCALL;
  nxsig_unmask_pendingsignal();
  up_irq_save();

  tcb->xcp.sregs = previous;
  regs[REG_R4] = result;
  csr_writeq(tcb->xcp.kstkptr, LOONGARCH_CSR_SAVE3);
}
#endif

/****************************************************************************
 * Name: loongarch_syscall_handler
 *
 * Description:
 *   Handle reserved architecture syscalls through the IRQ dispatcher. Only
 *   PLV0 may request context switches, assertions or signal delivery. PLV3
 *   may return from a signal only through a saved kernel continuation. The
 *   selected return frame is stored in the current TCB.
 *
 ****************************************************************************/

int loongarch_syscall_handler(int irq, void *context, void *arg)
{
  uintreg_t *regs = context;
  struct tcb_s *tcb = this_task();
  int cpu = this_cpu();
  bool user = (regs[REG_INT_CTX] & CSR_PRMD_PPLV) == PLV_USER;

  regs[REG_EPC] += 4;

  /* Context switching, assertions and signal dispatch are kernel-only
   * operations. A user may return from a signal only while a trusted
   * kernel continuation exists. Never trust user-supplied register frames.
   */

  if (user)
    {
#ifdef CONFIG_ARCH_KERNEL_STACK
      if (regs[REG_R4] == SYS_signal_handler_return &&
          tcb->xcp.sigregs != NULL)
        {
          tcb->xcp.regs = tcb->xcp.sigregs;
          tcb->xcp.sigregs = NULL;
          tcb->xcp.kstkptr = STACKFRAME_ALIGN_DOWN(
            (uintptr_t)tcb->xcp.kstack + CONFIG_ARCH_KERNEL_STACKSIZE);
          return OK;
        }
#endif

      regs[REG_R4] = -ENOSYS;
      return OK;
    }

  switch (regs[REG_R4])
    {
      case SYS_restore_context:
        loongarch_restorecontext(tcb);
        restore_critical_section(tcb, cpu);
        break;

      case SYS_switch_context:
        loongarch_savecontext(g_running_tasks[cpu]);
        loongarch_restorecontext(tcb);
        restore_critical_section(tcb, cpu);
        break;

#ifdef CONFIG_ARCH_KERNEL_STACK
      case SYS_signal_handler:
        {
          uintreg_t *source = tcb->xcp.sregs != NULL ?
                              tcb->xcp.sregs : tcb->xcp.saved_regs;
          uintreg_t *userregs = regs - XCPTCONTEXT_REGS;
          uintptr_t usp;

          DEBUGASSERT(source != NULL && tcb->xcp.sigregs == NULL);
          usp = STACKFRAME_ALIGN_DOWN(source[REG_SP] - sizeof(siginfo_t));
          if (!writable_user_range(tcb, usp, sizeof(siginfo_t)))
            {
              regs[REG_EPC] = (uintptr_t)_exit;
              regs[REG_SP] = (uintptr_t)regs;
              regs[REG_R4] = SIGSEGV;
              regs[REG_INT_CTX] = CSR_PRMD_PIE;
              break;
            }

          /* Copy only user register state into the new PLV3 frame. Keep the
           * kernel continuation above it on the trusted stack so syscalls
           * issued by the handler cannot overwrite that continuation.
           */

          memcpy((void *)usp, (void *)regs[REG_R7], sizeof(siginfo_t));
          memcpy(userregs, source, XCPTCONTEXT_SIZE);
          userregs[REG_EPC] = (uintptr_t)ARCH_DATA_RESERVE->ar_sigtramp;
          userregs[REG_INT_CTX] = CSR_PRMD_PIE | PLV_USER;
          userregs[REG_SP] = usp;
          userregs[REG_R4] = regs[REG_R5];
          userregs[REG_R5] = regs[REG_R6];
          userregs[REG_R6] = usp;
          userregs[REG_R7] = 0;

          tcb->xcp.sigregs = regs;
          tcb->xcp.kstkptr = (uintptr_t)userregs;
          tcb->xcp.regs = userregs;
        }
        break;
#endif

      case SYS_assert_handler:
        _assert((const char *)regs[REG_R5], (int)regs[REG_R6],
                (const char *)regs[REG_R7], running_regs());
        break;

      default:
        regs[REG_R4] = -ENOSYS;
        break;
    }

  return OK;
}
