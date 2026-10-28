/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_strlen.S
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

#ifdef LIBC_BUILD_STRLEN

/****************************************************************************
 * Name: strlen
 *
 * Description:
 *   Return the number of bytes preceding the first NUL at a0.
 *   Scan bytewise so a terminator at a mapped page's end never causes a
 *   load from the following page. Only caller-saved registers are used.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(strlen)
  .type ARCH_LIBCFUN(strlen), function
ARCH_LIBCFUN(strlen):
  move    $t0, $a0

.Lscan:
  ld.bu   $t1, $t0, 0
  beqz    $t1, .Ldone
  addi.d  $t0, $t0, 1
  b       .Lscan

.Ldone:
  sub.d   $a0, $t0, $a0
  jr      $ra
  .size ARCH_LIBCFUN(strlen), . - ARCH_LIBCFUN(strlen)

#endif /* LIBC_BUILD_STRLEN */

  .section .note.GNU-stack,"",@progbits
