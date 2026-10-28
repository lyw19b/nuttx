/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_strcmp.S
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

#ifdef LIBC_BUILD_STRCMP

/****************************************************************************
 * Name: strcmp
 *
 * Description:
 *   Compare the strings at a0 and a1 in unsigned-byte order. Stop at
 *   the first mismatch or NUL and return the byte difference. No aligned
 *   word loads can cross beyond the terminating string into another page.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(strcmp)
  .type ARCH_LIBCFUN(strcmp), function
ARCH_LIBCFUN(strcmp):

.Lcompare:
  ld.bu   $t0, $a0, 0
  ld.bu   $t1, $a1, 0
  bne     $t0, $t1, .Ldone
  beqz    $t0, .Ldone
  addi.d  $a0, $a0, 1
  addi.d  $a1, $a1, 1
  b       .Lcompare

.Ldone:
  sub.d   $a0, $t0, $t1
  jr      $ra
  .size ARCH_LIBCFUN(strcmp), . - ARCH_LIBCFUN(strcmp)

#endif /* LIBC_BUILD_STRCMP */

  .section .note.GNU-stack,"",@progbits
