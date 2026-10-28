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
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>
#include <nuttx/addrenv.h>
#include <nuttx/arch.h>
#include <nuttx/semaphore.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile uintptr_t g_value;
static volatile unsigned int g_signals;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: finish
 *
 * Description:
 *   Exit the regression process with a status returned to the kernel
 *   test. Remain in place if the exit syscall unexpectedly returns.
 *
 ****************************************************************************/

static void finish(int status)
{
  sys_call1(SYS__exit, status);
  for (; ; );
}

/****************************************************************************
 * Name: handler
 *
 * Description:
 *   Record a user signal delivery in process-private data.
 *
 ****************************************************************************/

static void handler(int signo)
{
  g_signals++;
}

/****************************************************************************
 * Name: user_thread
 *
 * Description:
 *   Verify that a pthread shares its process data and can block in a user
 *   syscall before returning through the libc pthread trampoline.
 *
 ****************************************************************************/

static void *user_thread(void *arg)
{
  struct timespec delay =
  {
    .tv_sec = 0,
    .tv_nsec = 1000000
  };

  if (getpid() != g_value || nanosleep(&delay, NULL) != 0)
    {
      return NULL;
    }

  return arg;
}

/****************************************************************************
 * Name: check_user_access
 *
 * Description:
 *   Verify strict syscall rejection before any service follows an invalid
 *   pointer. Exercise input, output, strings, nested exec arguments and
 *   executable callbacks using their documented error conventions.
 *
 ****************************************************************************/

#ifdef CONFIG_LOONGARCH_USER_ACCESS
static void check_user_access(void)
{
  uintptr_t kernel = 0x9000000000200000ul;
  char *badargv[] =
  {
    (char *)kernel, NULL
  };

  pthread_t thread;
  sem_t sem = SEM_INITIALIZER(1);

  errno = 0;
  if ((intptr_t)sys_call3(SYS_write, 1, kernel, 8) != -1 || errno != EFAULT)
    {
      finish(11);
    }

  errno = 0;
  if ((intptr_t)sys_call3(SYS_read, 0, (uintptr_t)handler, 4) != -1 ||
      errno != EFAULT)
    {
      finish(12);
    }

  errno = 0;
  if ((intptr_t)sys_call3(SYS_write, 1, UINTPTR_MAX - 1, 8) != -1 ||
      errno != EFAULT)
    {
      finish(13);
    }

  errno = 0;
  if ((intptr_t)sys_call3(SYS_open, kernel, 0, 0) != -1 || errno != EFAULT)
    {
      finish(14);
    }

  errno = 0;
  if (execve("/test/user", badargv, NULL) != -1 || errno != EFAULT)
    {
      finish(15);
    }

  errno = 0;
  if ((intptr_t)sys_call3(SYS_ioctl, 1, 0, kernel) != -1 || errno != ENOSYS)
    {
      finish(16);
    }

  if (pthread_create(&thread, NULL, (pthread_startroutine_t)kernel,
                     NULL) != EFAULT)
    {
      finish(17);
    }

  /* Slow counting operations must preserve the new libc fast-path ABI.
   * A mutex holder word and a kernel pointer must never reach native queues.
   */

  if ((intptr_t)sys_call1(SYS_nxsem_wait_slow, (uintptr_t)&sem) != 0 ||
      (intptr_t)sys_call1(SYS_nxsem_trywait_slow, (uintptr_t)&sem) !=
      -EAGAIN ||
      (intptr_t)sys_call1(SYS_nxsem_post_slow, (uintptr_t)&sem) != 0 ||
      (intptr_t)sys_call1(SYS_nxsem_wait_slow, kernel) != -EFAULT)
    {
      finish(24);
    }

  sem.flags = SEM_TYPE_MUTEX;
  if ((intptr_t)sys_call1(SYS_nxsem_wait_slow, (uintptr_t)&sem) != -ENOSYS ||
      (intptr_t)sys_call1(SYS_nxsem_post_slow, (uintptr_t)&sem) != -ENOSYS)
    {
      finish(25);
    }
}
#endif

/****************************************************************************
 * Name: check_demand_heap
 *
 * Description:
 *   Grow the libc heap beyond its resident bootstrap and touch each page.
 *   Separately reserve the final two heap pages to verify zero-fill on read,
 *   private allocation on write and syscall output into a lazy user page.
 *
 ****************************************************************************/

#ifdef CONFIG_LOONGARCH_DEMAND_PAGING
static void check_demand_heap(void)
{
  const size_t size = 128 * 1024;
  volatile uint8_t *memory = malloc(size);
  uintptr_t base = ARCH_HEAP_VEND - 2 * CONFIG_MM_PGSIZE;
  volatile uintptr_t *lazy = (volatile uintptr_t *)base;
  unsigned int i;

  if (memory == NULL || sys_call2(SYS_pgalloc, base, 2) != base)
    {
      finish(18);
    }

  for (i = 0; i < size; i += CONFIG_MM_PGSIZE)
    {
      memory[i] = i / CONFIG_MM_PGSIZE;
    }

  for (i = 0; i < size; i += CONFIG_MM_PGSIZE)
    {
      if (memory[i] != i / CONFIG_MM_PGSIZE)
        {
          finish(19);
        }
    }

  if (*lazy != 0)
    {
      finish(20);
    }

  *lazy = 0xabcd;
  if (*lazy != 0xabcd ||
      sys_call2(SYS_clock_gettime, CLOCK_MONOTONIC,
                 base + CONFIG_MM_PGSIZE) != 0)
    {
      finish(21);
    }

  free((void *)memory);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 *
 * Description:
 *   Run the user regression at PLV3. Verify private data,
 *   blocking syscalls and signal return, then execute the requested
 *   permission-fault case or exit successfully.
 *
 ****************************************************************************/

int main(int argc, char **argv)
{
  struct timespec delay =
  {
    .tv_sec = 0,
    .tv_nsec = 1000000
  };

  struct sigaction action =
  {
    0
  };

  uintptr_t pid = getpid();
  register uintptr_t saved asm("$s8") = 0x12345678;
  register double fp asm("$fs0") = 1.25;
  pthread_t thread;
  void *result;
  void *allocation;
  unsigned int i;
  char mode = argc > 1 ? argv[1][0] : 'n';
  static const char message[] = "LoongArch user process: PASS\n";

  if (g_value != 0 || pid == 0)
    {
      finish(1);
    }

  g_value = pid;

  /* Reserved kernel operations and out-of-range numbers must be rejected. */

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
#ifdef CONFIG_SMP
      cpu_set_t cpuset;
      unsigned int cpu = (i + pid) % CONFIG_SMP_NCPUS;

      /* Force each process through every CPU while its syscall is blocked.
       * Equal virtual addresses must keep resolving to that process pages.
       */

      CPU_ZERO(&cpuset);
      CPU_SET(cpu, &cpuset);
      if (sys_call3(SYS_sched_setaffinity, 0, sizeof(cpuset),
                    (uintptr_t)&cpuset) != 0)
        {
          finish(6);
        }
#endif

      __asm__ __volatile__("" : "+r"(saved), "+f"(fp));
      sys_call2(SYS_nanosleep, (uintptr_t)&delay, 0);
      __asm__ __volatile__("" : "+r"(saved), "+f"(fp));
#ifdef CONFIG_SMP
      /* Blocking makes the scheduler enqueue this task using its new mask. */

      if (sys_call0(SYS_sched_getcpu) != cpu)
        {
          finish(8);
        }
#endif

      if (saved != 0x12345678 || fp != 1.25)
        {
          finish(7);
        }

      if (g_value != pid || getpid() != pid)
        {
          finish(3);
        }
    }

  action.sa_handler = handler;
  if (sys_call3(SYS_sigaction, SIGUSR1, (uintptr_t)&action, 0) != 0 ||
      sys_call2(SYS_kill, pid, SIGUSR1) != 0 || g_signals != 1)
    {
      finish(4);
    }

  /* Exercise libc heap allocation and the user pthread startup/exit path. */

  allocation = malloc(64);
  if (allocation == NULL ||
      pthread_create(&thread, NULL, user_thread, allocation) != 0 ||
      pthread_join(thread, &result) != 0 || result != allocation)
    {
      finish(9);
    }

  free(allocation);

#ifdef CONFIG_LOONGARCH_USER_ACCESS
  check_user_access();
#endif
#ifdef CONFIG_LOONGARCH_DEMAND_PAGING
  check_demand_heap();
#endif

  switch (mode)
    {
      case 'e':
        {
          char *next[] =
          {
            "user", "q", NULL
          };

          /* Replacing this process must not rely on its old user stack. */

          execve("/test/user", next, NULL);
          printf("execve failed: errno=%d\n", errno);
          finish(10);
        }
        break;

      case 'r':
        g_value = *(volatile uintptr_t *)0x9000000000200000ul;
        break;

      case 'w':
        *(volatile uintptr_t *)0x9000000000200000ul = 0;
        break;

      case 't':
        *(volatile uint32_t *)(uintptr_t)main = 0;
        break;

      case 'x':
        ((void (*)(void))(uintptr_t)&g_value)();
        break;

      case 's':
        __asm__ __volatile__("move $sp, $zero\n\tbreak 0" : : : "memory");
        break;

      default:
        if (mode != 'q')
          {
            sys_call3(SYS_write, 1, (uintptr_t)message, sizeof(message) - 1);
          }

        return 0;
    }

  finish(5);
}
