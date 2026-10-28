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
#include <syscall.h>
#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <nuttx/tls.h>

#include "sched/sched.h"
#include "loongarch_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static noreturn_function void enter_user(uintptr_t entry, uintptr_t arg0,
                                         uintptr_t arg1)
{
  struct tcb_s *tcb = this_task();
  uintreg_t regs[XCPTCONTEXT_REGS] aligned_data(STACKFRAME_ALIGN);

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
  up_irq_save();
  tcb->xcp.regs = regs;
  loongarch_fullcontextrestore();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void up_task_start(main_t entry, int argc, char *argv[])
{
  enter_user((uintptr_t)entry, argc, (uintptr_t)argv);
}

#ifndef CONFIG_DISABLE_PTHREAD
void up_pthread_start(pthread_trampoline_t startup,
                      pthread_startroutine_t entry, pthread_addr_t arg)
{
  enter_user((uintptr_t)startup, (uintptr_t)entry, (uintptr_t)arg);
}
#endif

void up_signal_dispatch(_sa_sigaction_t handler, int signo,
                        siginfo_t *info, void *context)
{
  sys_call4(SYS_signal_handler, (uintptr_t)handler, signo,
            (uintptr_t)info, (uintptr_t)context);
}
