/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_strncmp.S
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

#ifdef LIBC_BUILD_STRNCMP

/****************************************************************************
 * Name: strncmp
 *
 * Description:
 *   Compare at most a2 bytes of the strings at a0 and a1. Return the
 *   unsigned-byte difference at the first mismatch, or zero at a common
 *   NUL or the bound. A zero bound must not load either string.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(strncmp)
  .type ARCH_LIBCFUN(strncmp), function
ARCH_LIBCFUN(strncmp):
  beqz    $a2, .Lequal

.Lcompare:
  ld.bu   $t0, $a0, 0
  ld.bu   $t1, $a1, 0
  bne     $t0, $t1, .Ldifferent
  beqz    $t0, .Lequal
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
  .size ARCH_LIBCFUN(strncmp), . - ARCH_LIBCFUN(strncmp)

#endif /* LIBC_BUILD_STRNCMP */

  .section .note.GNU-stack,"",@progbits
