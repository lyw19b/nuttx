/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_memcpy.S
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

#ifdef LIBC_BUILD_MEMCPY

/****************************************************************************
 * Name: memcpy
 *
 * Description:
 *   Copy a nonoverlapping range from a1 to a0; a2 is the byte count.
 *   Return the original destination. Use aligned 64-bit transfers only
 *   when both addresses can be aligned together. Byte tails never access
 *   outside the requested range. Only caller-saved integer registers change.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(memcpy)
  .type ARCH_LIBCFUN(memcpy), function
ARCH_LIBCFUN(memcpy):
  move    $t0, $a0

  beqz    $a2, .Ldone

  xor     $t1, $a0, $a1
  andi    $t1, $t1, 7

  bnez    $t1, .Lbytes

  /* Bring co-aligned pointers to an eight-byte boundary. */

.Lalign:
  andi    $t1, $t0, 7

  beqz    $t1, .Lwords

  ld.bu   $t2, $a1, 0
  st.b    $t2, $t0, 0
  addi.d  $a1, $a1, 1
  addi.d  $t0, $t0, 1
  addi.d  $a2, $a2, -1

  bnez    $a2, .Lalign

  b       .Ldone

.Lwords:
  sltui   $t1, $a2, 8
  bnez    $t1, .Lbytes
  ld.d    $t2, $a1, 0
  st.d    $t2, $t0, 0
  addi.d  $a1, $a1, 8
  addi.d  $t0, $t0, 8
  addi.d  $a2, $a2, -8
  b       .Lwords

  /* Handle unrelated alignments and the final partial word bytewise. */

.Lbytes:
  beqz    $a2, .Ldone

  ld.bu   $t2, $a1, 0
  st.b    $t2, $t0, 0
  addi.d  $a1, $a1, 1
  addi.d  $t0, $t0, 1
  addi.d  $a2, $a2, -1
  b       .Lbytes

.Ldone:
  jr      $ra
  .size ARCH_LIBCFUN(memcpy), . - ARCH_LIBCFUN(memcpy)

#endif /* LIBC_BUILD_MEMCPY */

  .section .note.GNU-stack,"",@progbits
