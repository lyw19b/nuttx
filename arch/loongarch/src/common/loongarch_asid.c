/****************************************************************************
 * arch/loongarch/src/common/loongarch_asid.c
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
#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <arch/csr.h>

#include "loongarch_mmu.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct loongarch_asid_cpu_s
{
  uintptr_t last;       /* Generation above the hardware ASID bits */
  uintptr_t mask;       /* Usable hardware ASID bits on this CPU */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Each CPU accesses only its own allocator and its own slot in env->asid.
 * Local interrupts exclude context switches; no cross-CPU lock is needed.
 */

static struct loongarch_asid_cpu_s g_asid[CONFIG_SMP_NCPUS];

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_asid_initialize
 *
 * Description:
 *   Initialize the calling CPU's ASID allocator after the boot-time TLB
 *   flush. Interrupts must be disabled and no address environment may have
 *   been selected yet. A zero-width implementation uses ASID zero and
 *   flushes on every switch. CPU hotplug is not supported.
 *
 ****************************************************************************/

void loongarch_asid_initialize(void)
{
  struct loongarch_asid_cpu_s *state = &g_asid[this_cpu()];
  unsigned int bits;

  bits = (csr_readq(LOONGARCH_CSR_ASID) & CSR_ASID_BITS_MASK) >>
         CSR_ASID_BITS_SHIFT;
  if (bits > CONFIG_LOONGARCH_ASID_BITS)
    {
      bits = CONFIG_LOONGARCH_ASID_BITS;
    }

  state->mask = (1ul << bits) - 1;

  /* Generation zero marks an environment that has not run on this CPU.
   * Reserve hardware ASID zero for the boot page-table root.
   */

  state->last = state->mask + 1;
}

/****************************************************************************
 * Name: loongarch_asid_select
 *
 * Description:
 *   Select a private root and its local ASID with interrupts disabled.
 *   Contexts combine a software generation with a nonzero hardware ASID.
 *   Within a generation, each identifier belongs to exactly one address
 *   environment. On exhaustion, flush the local TLB before reusing any
 *   identifiers. Other CPUs have independent allocators and generations.
 *
 *   The caller holds an address-environment reference throughout selection
 *   and subsequent use. The kernel code and stack must use direct mappings
 *   while PGDL and ASID are changed. Page-table edits still require the
 *   synchronous all-CPU invalidation provided by loongarch_tlb_sync.
 *
 ****************************************************************************/

void loongarch_asid_select(const arch_addrenv_t *env)
{
  int cpu = this_cpu();
  struct loongarch_asid_cpu_s *state = &g_asid[cpu];
  uintptr_t context;

  DEBUGASSERT((csr_readq(LOONGARCH_CSR_CRMD) & CSR_CRMD_IE) == 0);
  DEBUGASSERT(env->root != 0 && env->asid != NULL);

  if (state->mask == 0)
    {
      csr_writeq(env->root, LOONGARCH_CSR_PGDL);
      csr_writeq(0, LOONGARCH_CSR_ASID);
      loongarch_tlb_invalidate();
      return;
    }

  context = env->asid[cpu];
  if ((context & ~state->mask) != (state->last & ~state->mask))
    {
      /* Never let the full software generation wrap back to an old value.
       * On LA64 this would require almost 2^64 context allocations.
       */

      ASSERT(state->last != UINTPTR_MAX);
      context = ++state->last;
      if ((context & state->mask) == 0)
        {
          loongarch_tlb_invalidate();
          context = ++state->last;
        }

      env->asid[cpu] = context;
    }

  /* No low virtual address may be accessed between these CSR writes.
   * Synchronize instruction fetch before returning to the selected image.
   */

  csr_writeq(env->root, LOONGARCH_CSR_PGDL);
  csr_writeq(context & state->mask, LOONGARCH_CSR_ASID);
  __asm__ __volatile__("ibar 0" : : : "memory");
}
