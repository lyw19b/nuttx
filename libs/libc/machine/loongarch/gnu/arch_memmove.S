/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_memmove.S
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

#ifdef LIBC_BUILD_MEMMOVE

/****************************************************************************
 * Name: memmove
 *
 * Description:
 *   Move a2 bytes from a1 to a0 and return the original destination.
 *   Copy backward only when a higher destination overlaps the source.
 *   Directional byte transfers handle all alignments and overlap distances
 *   without reading or writing outside the range. No memcpy dependency is
 *   required, so either architecture implementation can be selected alone.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(memmove)
  .type ARCH_LIBCFUN(memmove), function
ARCH_LIBCFUN(memmove):
  beqz    $a2, .Ldone
  beq     $a0, $a1, .Ldone
  move    $t0, $a0
  bltu    $a0, $a1, .Lforward
  sub.d   $t1, $a0, $a1
  bgeu    $t1, $a2, .Lforward

  /* Copy from the end when a forward store would destroy unread input. */

  add.d   $t0, $t0, $a2
  add.d   $a1, $a1, $a2

.Lbackward:
  addi.d  $a1, $a1, -1
  addi.d  $t0, $t0, -1
  ld.bu   $t1, $a1, 0
  st.b    $t1, $t0, 0
  addi.d  $a2, $a2, -1
  bnez    $a2, .Lbackward
  b       .Ldone

  /* A lower destination or disjoint range can be copied from the start. */

.Lforward:
  ld.bu   $t1, $a1, 0
  st.b    $t1, $t0, 0
  addi.d  $a1, $a1, 1
  addi.d  $t0, $t0, 1
  addi.d  $a2, $a2, -1

  bnez    $a2, .Lforward

.Ldone:
  jr      $ra
  .size ARCH_LIBCFUN(memmove), . - ARCH_LIBCFUN(memmove)

#endif /* LIBC_BUILD_MEMMOVE */

  .section .note.GNU-stack,"",@progbits
