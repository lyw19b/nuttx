/****************************************************************************
 * arch/loongarch/src/common/loongarch_secondary.S
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
#include <arch/csr.h>
#include <arch/irq.h>

/****************************************************************************
 * Secondary CPU entry
 *
 * Firmware may enter with direct addressing or with its own DMWs. Set
 * the kernel's PLV0-only windows before touching any shared data.
 ****************************************************************************/

  .text
  .balign 16
  .global loongarch_secondary_entry
  .type loongarch_secondary_entry, function
loongarch_secondary_entry:
  li.d    $t0, 0x8000000000000001
  csrwr   $t0, LOONGARCH_CSR_DMWIN0
  li.d    $t0, 0x9000000000000011
  csrwr   $t0, LOONGARCH_CSR_DMWIN1
  li.w    $t0, 0xb0
  csrwr   $t0, LOONGARCH_CSR_CRMD
  li.w    $t0, CSR_EUEN_FPEN
  csrwr   $t0, LOONGARCH_CSR_EUEN
  csrrd   $t0, LOONGARCH_CSR_CPUID
  andi    $t0, $t0, 0x3ff
  li.w    $t1, CONFIG_SMP_NCPUS
  bgeu    $t0, $t1, .Lpark
  addi.d  $t0, $t0, 1
  li.d    $t1, SMP_STACK_SIZE
  mul.d   $t0, $t0, $t1
  la.local $sp, _ebss
  add.d   $sp, $sp, $t0
  bl      loongarch_secondary_start
.Lpark:
  idle    0
  b       .Lpark
  .size loongarch_secondary_entry, . - loongarch_secondary_entry
