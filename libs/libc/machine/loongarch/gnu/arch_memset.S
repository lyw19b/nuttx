/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_memset.S
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

#ifdef LIBC_BUILD_MEMSET

/****************************************************************************
 * Name: memset
 *
 * Description:
 *   Fill a2 bytes at a0 with the low eight bits of a1 and return a0.
 *   Align the destination before using doubleword stores. Construct the
 *   repeated byte in an integer register without requiring LSX or LASX.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(memset)
  .type ARCH_LIBCFUN(memset), function
ARCH_LIBCFUN(memset):
  move    $t0, $a0
  andi    $a1, $a1, 255
  beqz    $a2, .Ldone

  /* Align using byte stores; short fills can finish in this loop. */

.Lalign:
  andi    $t1, $t0, 7
  beqz    $t1, .Lpattern
  st.b    $a1, $t0, 0
  addi.d  $t0, $t0, 1
  addi.d  $a2, $a2, -1
  bnez    $a2, .Lalign
  b       .Ldone

.Lpattern:
  slli.d  $t1, $a1, 8
  or      $t2, $t1, $a1
  slli.d  $t1, $t2, 16
  or      $t2, $t2, $t1
  slli.d  $t1, $t2, 32
  or      $t2, $t2, $t1

.Lwords:
  sltui   $t1, $a2, 8
  bnez    $t1, .Lbytes
  st.d    $t2, $t0, 0
  addi.d  $t0, $t0, 8
  addi.d  $a2, $a2, -8
  b       .Lwords

  /* The remainder is smaller than one doubleword. */

.Lbytes:
  beqz    $a2, .Ldone
  st.b    $a1, $t0, 0
  addi.d  $t0, $t0, 1
  addi.d  $a2, $a2, -1
  b       .Lbytes

.Ldone:
  jr      $ra
  .size ARCH_LIBCFUN(memset), . - ARCH_LIBCFUN(memset)

#endif /* LIBC_BUILD_MEMSET */

  .section .note.GNU-stack,"",@progbits
