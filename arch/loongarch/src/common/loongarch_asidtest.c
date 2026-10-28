/****************************************************************************
 * arch/loongarch/src/common/loongarch_asidtest.c
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
#include <stdint.h>
#include <stdio.h>
#include <nuttx/addrenv.h>
#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <arch/csr.h>

#include "loongarch_mmu.h"
#include "loongarch_smp.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ASID_TEST_ENVS (1 << CONFIG_LOONGARCH_ASID_BITS)
#define ASID_TEST_VALUE 0x12340000ul

/****************************************************************************
 * Private Data
 ****************************************************************************/

static arch_addrenv_t g_env[ASID_TEST_ENVS];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tlb_present
 *
 * Description:
 *   Query the current ASID's translation without accessing the virtual
 *   address and accidentally refilling a missing entry. The caller masks
 *   local interrupts and restores the scratch CSRs before leaving.
 *
 ****************************************************************************/

static bool tlb_present(uintptr_t addr)
{
  csr_writeq(addr & ~(2 * LOONGARCH_PAGE_SIZE - 1), LOONGARCH_CSR_TLBEHI);
  __asm__ __volatile__("tlbsrch" : : : "memory");
  return (csr_readq(LOONGARCH_CSR_TLBIDX) & CSR_TLBIDX_NE) == 0;
}

/****************************************************************************
 * Name: check_asids
 *
 * Description:
 *   Exercise retained translations, descriptor clones and multiple ASID
 *   generations on one CPU. Each environment maps the same virtual page
 *   to different data. All roots are prepared beforehand so page-table
 *   edits cannot hide a missing rollover flush. Restore the boot root
 *   before returning; this test must run before user processes start.
 *
 ****************************************************************************/

static int check_asids(void *arg)
{
  unsigned int owner[ASID_TEST_ENVS] =
  {
    0
  };

  int cpu = this_cpu();
  irqstate_t flags = up_irq_save();
  uintptr_t root = csr_readq(LOONGARCH_CSR_PGDL);
  uintptr_t index = csr_readq(LOONGARCH_CSR_TLBIDX);
  uintptr_t hi = csr_readq(LOONGARCH_CSR_TLBEHI);
  uintptr_t first;
  uintptr_t id;
  volatile uintptr_t *value;
  arch_addrenv_t clone;
  unsigned int pass;
  unsigned int i;
  bool reused = false;

  ASSERT((csr_readq(LOONGARCH_CSR_ASID) & CSR_ASID_MASK) == 0);

  /* Revisiting an environment must preserve its ASID and cached entry.
   * A descriptor clone must use the same mutable per-CPU context storage.
   */

  up_addrenv_select(&g_env[0]);
  value = (volatile uintptr_t *)g_env[0].datavbase + cpu;
  ASSERT(*value == ASID_TEST_VALUE + cpu);
  first = g_env[0].asid[cpu];
  ASSERT(up_addrenv_clone(&g_env[0], &clone) == OK);
  up_addrenv_select(&clone);
  ASSERT(clone.asid == g_env[0].asid && clone.asid[cpu] == first);
  ASSERT(tlb_present((uintptr_t)value));

  up_addrenv_select(&g_env[1]);
  value = (volatile uintptr_t *)g_env[1].datavbase + cpu;
  ASSERT(*value == ASID_TEST_VALUE + 0x100 + cpu);
  up_addrenv_select(&g_env[0]);
  ASSERT(g_env[0].asid[cpu] == first);
  ASSERT(tlb_present((uintptr_t)value));
  ASSERT(*value == ASID_TEST_VALUE + cpu);

  /* There are fewer nonzero ASIDs than environments. Repeated complete
   * traversals must reuse hardware tags without aliasing old translations.
   */

  for (pass = 0; pass < 3; pass++)
    {
      for (i = 0; i < ASID_TEST_ENVS; i++)
        {
          up_addrenv_select(&g_env[i]);
          id = csr_readq(LOONGARCH_CSR_ASID) & CSR_ASID_MASK;
          ASSERT(id != 0 && id < ASID_TEST_ENVS);
          if (owner[id] != 0 && owner[id] != i + 1)
            {
              reused = true;
            }

          owner[id] = i + 1;
          value = (volatile uintptr_t *)g_env[i].datavbase + cpu;
          ASSERT(*value == ASID_TEST_VALUE + (i << 8) + cpu);
        }
    }

  ASSERT(reused && g_env[0].asid[cpu] != first);

  /* ASID zero is never allocated, so the boot mappings remain safe even
   * though the test has advanced this CPU's software generation.
   */

  csr_writeq(root, LOONGARCH_CSR_PGDL);
  csr_writeq(0, LOONGARCH_CSR_ASID);
  csr_writeq(hi, LOONGARCH_CSR_TLBEHI);
  csr_writeq(index, LOONGARCH_CSR_TLBIDX);
  __asm__ __volatile__("ibar 0" : : : "memory");
  up_irq_restore(flags);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_asid_test
 *
 * Description:
 *   Prepare small private environments, run the ASID regression on every
 *   online CPU and release the environments after all callbacks finish.
 *   Call in thread context before the first user environment is selected.
 *   The test configuration limits ASID width to keep memory use bounded.
 *
 ****************************************************************************/

void loongarch_asid_test(void)
{
  uintptr_t page;
  uintptr_t *data;
  unsigned int cpu;
  unsigned int i;

  for (i = 0; i < ASID_TEST_ENVS; i++)
    {
      ASSERT(up_addrenv_create(0, sizeof(uintptr_t) * CONFIG_SMP_NCPUS,
                               0, &g_env[i]) == OK);
      page = up_addrenv_find_page(&g_env[i], g_env[i].datavbase);
      ASSERT(page != 0);
      data = loongarch_virtaddr(page);
      for (cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
        {
          data[cpu] = ASID_TEST_VALUE + (i << 8) + cpu;
        }
    }

#ifdef CONFIG_SMP
  ASSERT(nxsched_smp_call(loongarch_cpu_online(), check_asids, NULL) == OK);
#else
  ASSERT(check_asids(NULL) == OK);
#endif

  for (i = 0; i < ASID_TEST_ENVS; i++)
    {
      ASSERT(up_addrenv_destroy(&g_env[i]) == OK);
    }

  printf("LoongArch ASID retention/rollover tests: PASS (%d CPU(s))\n",
         CONFIG_SMP_NCPUS);
}
