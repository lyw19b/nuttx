/****************************************************************************
 * arch/loongarch/src/common/loongarch_vector.c
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
#include <stdint.h>
#include <arch/csr.h>
#include "loongarch_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Set up each CPU before enabling any interrupt sources. */

/****************************************************************************
 * Name: loongarch_vector_initialize
 *
 * Description:
 *   Install the aligned vector table and select 128-byte vector spacing on
 *   the calling CPU. Enable the FPU before any entry can save floating-
 *   point context. Call during startup with interrupts disabled.
 *
 ****************************************************************************/

void loongarch_vector_initialize(void)
{
  extern const char loongarch_vectors[];

  csr_writeq((uintptr_t)loongarch_vectors, LOONGARCH_CSR_EBASE);
  csr_xchgq(5 << CSR_ECFG_VS_SHIFT, CSR_ECFG_VS, LOONGARCH_CSR_ECFG);
  csr_writeq(CSR_EUEN_FPEN, LOONGARCH_CSR_EUEN);
  __asm__ __volatile__("ibar 0" : : : "memory");
}
