/****************************************************************************
 * boards/loongarch/qemu/qemu-loongarch/src/qemu_usertest.c
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

#include <sys/mount.h>
#include <sys/wait.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <nuttx/binfmt/binfmt.h>
#include <nuttx/board.h>
#include <nuttx/kthread.h>
#include <nuttx/sched.h>
#include <arch/csr.h>
#include "sched/sched.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int usertest(int argc, char **argv)
{
  char *normal[] = {"user", "n", NULL};
  char mode[] = "r";
  char *fault[] = {"user", mode, NULL};
  const char modes[] = "rwtxs";
  pid_t children[2];
  unsigned int i;
  int status;
  irqstate_t flags;

  flags = enter_critical_section();
  this_task()->group->tg_flags &= ~GROUP_FLAG_NOCLDWAIT;
  leave_critical_section(flags);

  printf("LoongArch userspace: %s, %d CPU(s)\n",
         (csr_readq(LOONGARCH_CSR_PWCH) & CSR_PWCH_HPTW) ?
         "hardware PTW" : "software refill", CONFIG_SMP_NCPUS);

  for (i = 0; i < 2; i++)
    {
      children[i] = exec_spawn("/test/user", normal, NULL, NULL, 0, NULL, NULL);
      ASSERT(children[i] > 0);
    }

  for (i = 0; i < 2; i++)
    {
      ASSERT(waitpid(children[i], &status, 0) == children[i]);
      ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

  for (i = 0; i < sizeof(modes) - 1; i++)
    {
      mode[0] = modes[i];
      children[0] = exec_spawn("/test/user", fault, NULL, NULL, 0, NULL, NULL);
      ASSERT(children[0] > 0);
      ASSERT(waitpid(children[0], &status, 0) == children[0]);
      ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == SIGSEGV);
    }

  children[0] = exec_spawn("/test/user", normal, NULL, NULL, 0, NULL, NULL);
  ASSERT(children[0] > 0);
  ASSERT(waitpid(children[0], &status, 0) == children[0]);
  ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  puts("LoongArch userspace isolation/syscall tests: PASS");
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void board_late_initialize(void)
{
  extern const unsigned char g_loongarch_user_elf[];
  extern const unsigned char g_loongarch_user_elf_end[];
  size_t size = g_loongarch_user_elf_end - g_loongarch_user_elf;
  int fd;

  ASSERT(mount(NULL, "/test", "tmpfs", 0, NULL) == OK);
  fd = open("/test/user", O_WRONLY | O_CREAT | O_TRUNC, 0755);
  ASSERT(fd >= 0);
  ASSERT(write(fd, g_loongarch_user_elf, size) == size);
  ASSERT(close(fd) == OK);


  ASSERT(kthread_create("usertest", 100, 8192, usertest, NULL) > 0);
}
