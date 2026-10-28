/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_strrchr.S
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

#ifdef LIBC_BUILD_STRRCHR

/****************************************************************************
 * Name: strrchr
 *
 * Description:
 *   Return the final occurrence of the low byte of a1 in the string
 *   at a0, or NULL. Retain the latest match while scanning through the NUL;
 *   searching for zero therefore returns the terminator.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(strrchr)
  .type ARCH_LIBCFUN(strrchr), function
ARCH_LIBCFUN(strrchr):
  andi    $a1, $a1, 255
  move    $t1, $zero

.Lsearch:
  ld.bu   $t0, $a0, 0
  bne     $t0, $a1, .Lnext
  move    $t1, $a0

.Lnext:
  beqz    $t0, .Ldone
  addi.d  $a0, $a0, 1
  b       .Lsearch

.Ldone:
  move    $a0, $t1
  jr      $ra
  .size ARCH_LIBCFUN(strrchr), . - ARCH_LIBCFUN(strrchr)

#endif /* LIBC_BUILD_STRRCHR */

  .section .note.GNU-stack,"",@progbits
