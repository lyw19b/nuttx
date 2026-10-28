/****************************************************************************
 * arch/loongarch/src/qemu/qemu_head.S
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

#include <nuttx/config.h>
#include <arch/csr.h>
#include <arch/irq.h>

  .section .text.__start, "ax", @progbits
  .global __start
  .type __start, function
__start:
  li.d    $t0, 0x8000000000000001
  csrwr   $t0, LOONGARCH_CSR_DMWIN0
  li.d    $t0, 0x9000000000000011
  csrwr   $t0, LOONGARCH_CSR_DMWIN1
  la.local $t0, .Lmapped
  li.d    $t1, 0x9000000000000000
  or      $t0, $t0, $t1
  jirl    $zero, $t0, 0
.Lmapped:
  li.w    $t0, 0xb0
  csrwr   $t0, LOONGARCH_CSR_CRMD
  li.w    $t0, CSR_EUEN_FPEN
  csrwr   $t0, LOONGARCH_CSR_EUEN
  la.local $sp, _ebss
  li.d    $t0, SMP_STACK_SIZE
  add.d   $sp, $sp, $t0
  bl      qemu_start
  .size __start, . - __start
