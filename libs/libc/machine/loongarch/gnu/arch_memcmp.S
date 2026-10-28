/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_memcmp.S
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

#include "libc.h"

#ifdef LIBC_BUILD_MEMCMP

/****************************************************************************
 * Name: memcmp
 *
 * Description:
 *   Compare a2 bytes at a0 and a1 as unsigned bytes. Return the
 *   difference at the first mismatch, or zero for equal or empty ranges.
 *   Read exactly the requested bytes, including at page boundaries.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(memcmp)
  .type ARCH_LIBCFUN(memcmp), function
ARCH_LIBCFUN(memcmp):
  beqz    $a2, .Lequal

.Lcompare:
  ld.bu   $t0, $a0, 0
  ld.bu   $t1, $a1, 0

  bne     $t0, $t1, .Ldifferent

  addi.d  $a0, $a0, 1
  addi.d  $a1, $a1, 1
  addi.d  $a2, $a2, -1

  bnez    $a2, .Lcompare

.Lequal:
  move    $a0, $zero
  jr      $ra

.Ldifferent:
  sub.d   $a0, $t0, $t1
  jr      $ra
  .size ARCH_LIBCFUN(memcmp), . - ARCH_LIBCFUN(memcmp)

#endif /* LIBC_BUILD_MEMCMP */

  .section .note.GNU-stack,"",@progbits
