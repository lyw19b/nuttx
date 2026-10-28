/****************************************************************************
 * arch/loongarch/src/common/loongarch_mmutest.c
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
#include <stdio.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <nuttx/arch.h>
#include <nuttx/pgalloc.h>
#include <nuttx/sched.h>
#include <arch/csr.h>

#include "loongarch_mmu.h"
#include "loongarch_smp.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TEST_VADDR 0x40000000ul
#define TEST_PAGES 4

/****************************************************************************
 * Private Data
 ****************************************************************************/

static unsigned int g_signals;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: check_signal
 *
 * Description:
 *   Record delivery of the regression-test signal so the test can verify
 *   asynchronous signal handling.
 *
 ****************************************************************************/

static void check_signal(int signo)
{
  __atomic_fetch_add(&g_signals, 1, __ATOMIC_RELAXED);
}

/* Keep callee-saved integer and floating-point values live across blocking
 * calls. Every worker is pinned to a different CPU in an SMP build.
 */

/****************************************************************************
 * Name: check_context
 *
 * Description:
 *   Exercise context switching and verify the regression thread register
 *   state across scheduling points.
 *
 ****************************************************************************/

static void *check_context(void *arg)
{
  unsigned int cpu = (uintptr_t)arg;
  register uintptr_t saved asm("$s8") = 0x12345678 + cpu;
  register double fp asm("$fs0") = 1.25 + cpu;
  unsigned int i;

  for (i = 0; i < 100; i++)
    {
      __asm__ __volatile__("" : "+r"(saved), "+f"(fp));
      usleep(1000);
      __asm__ __volatile__("" : "+r"(saved), "+f"(fp));
      ASSERT(saved == 0x12345678 + cpu);
      ASSERT(fp == 1.25 + cpu);
      ASSERT(up_cpu_index() == cpu);
    }

  return NULL;
}

#ifdef CONFIG_SMP
/****************************************************************************
 * Name: check_remote
 *
 * Description:
 *   Check a test mapping on the CPU executing the synchronous remote
 *   callback. Return an error if the expected translation is not
 *   observed.
 *
 ****************************************************************************/

static int check_remote(void *arg)
{
  uint64_t expected = *(uint64_t *)arg;

  return *(volatile uint64_t *)TEST_VADDR == expected ? OK : -EIO;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_mmutest_main
 *
 * Description:
 *   Exercise page allocation, mapping, unmapping and remote TLB
 *   invalidation. Optional fault modes deliberately trigger an exception
 *   after the positive checks.
 *
 ****************************************************************************/

int loongarch_mmutest_main(int argc, char **argv)
{
  struct pginfo_s before;
  struct pginfo_s after;
  uintptr_t paddr;
  uintptr_t translated;
  volatile uint64_t *mapped = (volatile uint64_t *)TEST_VADDR;
  uint64_t *physical;
  unsigned int i;
  pthread_t threads[CONFIG_SMP_NCPUS];
  pthread_attr_t attr;
  struct sigaction action;
  struct sigaction oldaction;
  jmp_buf env;
#ifdef CONFIG_SMP
  cpu_set_t affinity;
  cpu_set_t online;
  uint64_t expected;

  for (i = 0; i < 100; i++)
    {
      online = loongarch_cpu_online();
      if (CPU_COUNT(&online) == CONFIG_SMP_NCPUS)
        {
          break;
        }

      usleep(10000);
    }

  ASSERT(CPU_COUNT(&online) == CONFIG_SMP_NCPUS);
#endif

  printf("LoongArch MMU: %s, %d CPU(s)\n",
         (csr_readq(LOONGARCH_CSR_PWCH) & CSR_PWCH_HPTW) ?
         "hardware PTW" : "software refill", CONFIG_SMP_NCPUS);
  if (setjmp(env) == 0)
    {
      longjmp(env, 1);
    }

  memset(&action, 0, sizeof(action));
  action.sa_handler = check_signal;
  sigemptyset(&action.sa_mask);
  ASSERT(sigaction(SIGUSR1, &action, &oldaction) == OK);

  for (i = 0; i < CONFIG_SMP_NCPUS; i++)
    {
      ASSERT(pthread_attr_init(&attr) == OK);
      ASSERT(pthread_attr_setstacksize(&attr, 4096) == OK);
#ifdef CONFIG_SMP
      CPU_ZERO(&affinity);
      CPU_SET(i, &affinity);
      ASSERT(pthread_attr_setaffinity_np(&attr, sizeof(affinity),
                                         &affinity) == OK);
#endif
      ASSERT(pthread_create(&threads[i], &attr, check_context,
                             (void *)(uintptr_t)i) == OK);
      pthread_attr_destroy(&attr);
    }

  /* Exercise signal trampolines while workers retain their register state. */

  usleep(10000);
  for (i = 0; i < CONFIG_SMP_NCPUS; i++)
    {
      ASSERT(pthread_kill(threads[i], SIGUSR1) == OK);
    }

  for (i = 0; i < CONFIG_SMP_NCPUS; i++)
    {
      ASSERT(pthread_join(threads[i], NULL) == OK);
    }

  ASSERT(__atomic_load_n(&g_signals, __ATOMIC_RELAXED) == CONFIG_SMP_NCPUS);
  ASSERT(sigaction(SIGUSR1, &oldaction, NULL) == OK);

  mm_pginfo(&before);
  paddr = mm_pgalloc(TEST_PAGES);
  ASSERT(paddr != 0);
  physical = loongarch_virtaddr(paddr);
  memset(physical, 0, TEST_PAGES * LOONGARCH_PAGE_SIZE);

  ASSERT(loongarch_mmu_map(TEST_VADDR + 1, paddr, LOONGARCH_PAGE_SIZE,
                           LOONGARCH_MMU_WRITE) == -EINVAL);
  ASSERT(loongarch_mmu_map(TEST_VADDR, paddr,
                           TEST_PAGES * LOONGARCH_PAGE_SIZE,
                           LOONGARCH_MMU_WRITE) == OK);
  ASSERT(loongarch_mmu_map(TEST_VADDR, paddr, LOONGARCH_PAGE_SIZE,
                           LOONGARCH_MMU_WRITE) == -EEXIST);

  /* Exercise even and odd halves of two distinct TLB entries. */

  for (i = 0; i < TEST_PAGES; i++)
    {
      mapped[i * LOONGARCH_PAGE_SIZE / sizeof(uint64_t)] = 0x1234 + i;
      ASSERT(physical[i * LOONGARCH_PAGE_SIZE / sizeof(uint64_t)] ==
             0x1234 + i);
    }

  ASSERT(loongarch_mmu_translate(TEST_VADDR + 123, &translated) == OK);
  ASSERT(translated == paddr + 123);
#ifdef CONFIG_SMP
  expected = 0x1234;
  ASSERT(nxsched_smp_call(online, check_remote, &expected) == OK);
#endif

  ASSERT(loongarch_mmu_unmap(TEST_VADDR,
                             TEST_PAGES * LOONGARCH_PAGE_SIZE) == OK);
  ASSERT(loongarch_mmu_translate(TEST_VADDR, &translated) == -ENOENT);

  /* Reuse the VA with a different physical page. Remote CPUs must not
   * retain the translation populated by the preceding callback.
   */

  ASSERT(loongarch_mmu_map(TEST_VADDR, paddr + LOONGARCH_PAGE_SIZE,
                           LOONGARCH_PAGE_SIZE,
                           LOONGARCH_MMU_WRITE) == OK);
  ASSERT(*mapped == 0x1235);
#ifdef CONFIG_SMP
  expected = 0x1235;
  ASSERT(nxsched_smp_call(online, check_remote, &expected) == OK);
#endif
  ASSERT(loongarch_mmu_unmap(TEST_VADDR, LOONGARCH_PAGE_SIZE) == OK);
  mm_pgfree(paddr, TEST_PAGES);
  mm_pginfo(&after);
  ASSERT(before.nfree == after.nfree);

  /* Exhaustion must leave neither a partial map nor orphaned tables. */

  paddr = mm_pgalloc(before.mxfree);
  ASSERT(paddr != 0);
  ASSERT(loongarch_mmu_map(TEST_VADDR, paddr, LOONGARCH_PAGE_SIZE,
                           LOONGARCH_MMU_WRITE) == -ENOMEM);
  ASSERT(loongarch_mmu_translate(TEST_VADDR, &translated) == -ENOENT);
  mm_pgfree(paddr, before.mxfree);
  mm_pginfo(&after);
  ASSERT(before.nfree == after.nfree);

  /* Cross each directory boundary, including the root table, then reclaim
   * both sides. Distinct values detect aliases from missing walk levels.
   */

  paddr = mm_pgalloc(2);
  ASSERT(paddr != 0);
  for (i = 21; i <= 39; i += 9)
    {
      uintptr_t addr = (1ul << i) - LOONGARCH_PAGE_SIZE;

      ASSERT(loongarch_mmu_map(addr, paddr, 2 * LOONGARCH_PAGE_SIZE,
                               LOONGARCH_MMU_WRITE) == OK);
      *(volatile uint64_t *)addr = i;
      *(volatile uint64_t *)(addr + LOONGARCH_PAGE_SIZE) = i + 1;
      physical = loongarch_virtaddr(paddr);
      ASSERT(physical[0] == i);
      ASSERT(physical[LOONGARCH_PAGE_SIZE / sizeof(uint64_t)] == i + 1);
      ASSERT(loongarch_mmu_unmap(addr, 2 * LOONGARCH_PAGE_SIZE) == OK);
    }

  mm_pgfree(paddr, 2);
  mm_pginfo(&after);
  ASSERT(before.nfree == after.nfree);
#if CONFIG_LOONGARCH_MMU_TEST_FAULT == 1
  paddr = mm_pgalloc(1);
  ASSERT(paddr != 0);
  ASSERT(loongarch_mmu_map(TEST_VADDR, paddr, LOONGARCH_PAGE_SIZE,
                           LOONGARCH_MMU_READ) == OK);
  puts("LoongArch MMU: expecting write protection fault");
  *mapped = 0;
  PANIC();
#elif CONFIG_LOONGARCH_MMU_TEST_FAULT == 2
  puts("LoongArch MMU: expecting unmapped read fault");
  translated = *mapped;
  UNUSED(translated);
  PANIC();
#else
  puts("LoongArch MMU/SMP tests: PASS");
#endif
  return 0;
}
