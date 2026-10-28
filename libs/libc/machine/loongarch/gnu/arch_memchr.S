/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_memchr.S
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

#ifdef LIBC_BUILD_MEMCHR

/****************************************************************************
 * Name: memchr
 *
 * Description:
 *   Find the first occurrence of the low byte of a1 in the a2 bytes
 *   starting at a0. Return its address or NULL. A zero count performs no
 *   load, and a successful match stops before inspecting later bytes.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(memchr)
  .type ARCH_LIBCFUN(memchr), function
ARCH_LIBCFUN(memchr):
  andi    $a1, $a1, 255
  beqz    $a2, .Lmissing

.Lsearch:
  ld.bu   $t0, $a0, 0
  beq     $t0, $a1, .Ldone
  addi.d  $a0, $a0, 1
  addi.d  $a2, $a2, -1
  bnez    $a2, .Lsearch

.Lmissing:
  move    $a0, $zero

.Ldone:
  jr      $ra
  .size ARCH_LIBCFUN(memchr), . - ARCH_LIBCFUN(memchr)

#endif /* LIBC_BUILD_MEMCHR */

  .section .note.GNU-stack,"",@progbits
