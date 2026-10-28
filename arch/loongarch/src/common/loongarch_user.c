/****************************************************************************
 * arch/loongarch/src/common/loongarch_user.c
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

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <syscall.h>
#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <nuttx/tls.h>

#include "sched/sched.h"
#include "loongarch_internal.h"
#include "loongarch_mmu.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: enter_user
 *
 * Description:
 *   Build an initial PLV3 register frame on the trusted kernel stack and
 *   enter user mode. Clear unused registers to avoid exposing kernel
 *   state. The loader and scheduler must already have selected the task
 *   address environment and prepared its user stack. This function does
 *   not return.
 *
 ****************************************************************************/

static noreturn_function void enter_user(uintptr_t entry, uintptr_t arg0,
                                         uintptr_t arg1)
{
  struct tcb_s *tcb = this_task();
  uintreg_t regs[XCPTCONTEXT_REGS] aligned_data(STACKFRAME_ALIGN);

  /* exec swaps the task and group IDs after TLS allocation. Refresh the
   * cached user IDs at the final entry boundary so libc getpid/gettid and
   * the semaphore fast paths observe the scheduler's current identities.
   * Use checked copies because this storage belongs to the user mapping.
   */

  if (loongarch_copytouser(
        (char *)tcb->stack_alloc_ptr + offsetof(struct tls_info_s, tl_tid),
        &tcb->pid, sizeof(tcb->pid)) < 0 ||
      loongarch_copytouser(&tcb->group->tg_info->ta_pid,
                          &tcb->group->tg_pid,
                          sizeof(tcb->group->tg_pid)) < 0)
    {
      _exit(SIGSEGV);
    }

  /* Start from a clean frame. No kernel register or stack address is
   * exposed to the user task on its first return to PLV3.
   */

  memset(regs, 0, sizeof(regs));
  regs[REG_EPC] = entry;
  regs[REG_SP] = tcb->xcp.ustkptr;
  regs[REG_R4] = arg0;
  regs[REG_R5] = arg1;
  regs[REG_INT_CTX] = CSR_PRMD_PIE | PLV_USER;
#ifdef CONFIG_SCHED_THREAD_LOCAL
  regs[REG_R2] = (uintptr_t)tcb->stack_alloc_ptr + sizeof(struct tls_info_s);
#endif

  /* Publish the frame with interrupts masked until exception return. */

  up_irq_save();
  tcb->xcp.regs = regs;
  loongarch_fullcontextrestore();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_task_start
 *
 * Description:
 *   Start a user task at its ELF entry point with argc and argv in the
 *   LoongArch argument registers. This function does not return.
 *
 ****************************************************************************/

void up_task_start(main_t entry, int argc, char *argv[])
{
  enter_user((uintptr_t)entry, argc, (uintptr_t)argv);
}

#ifndef CONFIG_DISABLE_PTHREAD
/****************************************************************************
 * Name: up_pthread_start
 *
 * Description:
 *   Enter the user pthread trampoline with its start routine and argument.
 *   The trampoline handles user-side setup and thread exit.
 *
 ****************************************************************************/

void up_pthread_start(pthread_trampoline_t startup,
                      pthread_startroutine_t entry, pthread_addr_t arg)
{
  enter_user((uintptr_t)startup, (uintptr_t)entry, (uintptr_t)arg);
}
#endif

/****************************************************************************
 * Name: up_signal_dispatch
 *
 * Description:
 *   Request a user signal handler through the reserved kernel syscall. The
 *   trap handler preserves this kernel continuation until the user
 *   trampoline invokes SYS_signal_handler_return.
 *
 ****************************************************************************/

void up_signal_dispatch(_sa_sigaction_t handler, int signo,
                        siginfo_t *info, void *context)
{
  struct tcb_s *tcb = this_task();
  uintreg_t *source = tcb->xcp.sregs != NULL ?
                      tcb->xcp.sregs : tcb->xcp.saved_regs;
  uintptr_t usp;

  /* Materialize and copy the signal frame while allocation may still
   * block. The reserved trap below runs on the interrupt stack.
   */

  if (source == NULL || source[REG_SP] < sizeof(siginfo_t))
    {
      _exit(SIGSEGV);
    }

  usp = STACKFRAME_ALIGN_DOWN(source[REG_SP] - sizeof(siginfo_t));
  if (loongarch_copytouser((void *)usp, info, sizeof(*info)) < 0)
    {
      _exit(SIGSEGV);
    }

  sys_call4(SYS_signal_handler, (uintptr_t)handler, signo,
            usp, (uintptr_t)context);
}
