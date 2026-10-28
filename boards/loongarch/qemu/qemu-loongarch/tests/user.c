/****************************************************************************
 * boards/loongarch/qemu/qemu-loongarch/tests/user.c
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

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <syscall.h>
#include <time.h>
#include <nuttx/addrenv.h>
#include <nuttx/arch.h>

void up_signal_handler(_sa_sigaction_t handler, int signo,
                       siginfo_t *info, void *context);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile uintptr_t g_value;
static volatile unsigned int g_signals;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void finish(int status)
{
  sys_call1(SYS__exit, status);
  for (; ; );
}

static void handler(int signo)
{
  g_signals++;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void __start(int argc, char **argv)
{
  struct timespec delay = {0, 1000000};
  struct sigaction action = {0};
  uintptr_t pid = sys_call0(SYS_getpid);
  unsigned int i;
  char mode = argc > 1 ? argv[1][0] : 'n';
  static const char message[] = "LoongArch user process: PASS\n";

  if (g_value != 0 || pid == 0)
    {
      finish(1);
    }

  g_value = pid;
  if ((intptr_t)sys_call0(SYS_maxsyscall) != -ENOSYS ||
      (intptr_t)sys_call0(UINT32_MAX) != -ENOSYS ||
      (intptr_t)sys_call0(SYS_restore_context) != -ENOSYS ||
      (intptr_t)sys_call0(SYS_switch_context) != -ENOSYS ||
      (intptr_t)sys_call0(SYS_signal_handler_return) != -ENOSYS)
    {
      finish(2);
    }

  for (i = 0; i < 40; i++)
    {
      sys_call2(SYS_nanosleep, (uintptr_t)&delay, 0);
      if (g_value != pid || sys_call0(SYS_getpid) != pid)
        {
          finish(3);
        }
    }

  ARCH_DATA_RESERVE->ar_sigtramp = up_signal_handler;
  action.sa_handler = handler;
  if (sys_call3(SYS_sigaction, SIGUSR1, (uintptr_t)&action, 0) != 0 ||
      sys_call2(SYS_kill, pid, SIGUSR1) != 0 || g_signals != 1)
    {
      finish(4);
    }

  switch (mode)
    {
      case 'r':
        g_value = *(volatile uintptr_t *)0x9000000000200000ul;
        break;

      case 'w':
        *(volatile uintptr_t *)0x9000000000200000ul = 0;
        break;

      case 't':
        *(volatile uint32_t *)(uintptr_t)__start = 0;
        break;

      case 'x':
        ((void (*)(void))(uintptr_t)&g_value)();
        break;

      case 's':
        __asm__ __volatile__("move $sp, $zero\n\tbreak 0" : : : "memory");
        break;

      default:
        sys_call3(SYS_write, 1, (uintptr_t)message, sizeof(message) - 1);
        finish(0);
    }

  finish(5);
}
