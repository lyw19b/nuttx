/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_strchr.S
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

#ifdef LIBC_BUILD_STRCHR

/****************************************************************************
 * Name: strchr
 *
 * Description:
 *   Return the first occurrence of the low byte of a1 in the string
 *   at a0, or NULL. Test the requested byte before checking for the NUL so
 *   searching for zero returns the terminator itself.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(strchr)
  .type ARCH_LIBCFUN(strchr), function
ARCH_LIBCFUN(strchr):
  andi    $a1, $a1, 255

.Lsearch:
  ld.bu   $t0, $a0, 0
  beq     $t0, $a1, .Ldone
  beqz    $t0, .Lmissing
  addi.d  $a0, $a0, 1
  b       .Lsearch

.Lmissing:
  move    $a0, $zero

.Ldone:
  jr      $ra
  .size ARCH_LIBCFUN(strchr), . - ARCH_LIBCFUN(strchr)

#endif /* LIBC_BUILD_STRCHR */

  .section .note.GNU-stack,"",@progbits
