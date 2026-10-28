/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_strnlen.S
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

#ifdef LIBC_BUILD_STRNLEN

/****************************************************************************
 * Name: strnlen
 *
 * Description:
 *   Return the length of the string at a0, limited to a1 bytes.
 *   Never read past the bound or a terminating NUL. An empty bound returns
 *   zero without accessing the supplied address.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(strnlen)
  .type ARCH_LIBCFUN(strnlen), function
ARCH_LIBCFUN(strnlen):
  move    $t0, $a0
  move    $t1, $zero

.Lscan:
  beq     $t1, $a1, .Ldone
  ld.bu   $t2, $t0, 0
  beqz    $t2, .Ldone
  addi.d  $t0, $t0, 1
  addi.d  $t1, $t1, 1
  b       .Lscan

.Ldone:
  move    $a0, $t1
  jr      $ra
  .size ARCH_LIBCFUN(strnlen), . - ARCH_LIBCFUN(strnlen)

#endif /* LIBC_BUILD_STRNLEN */

  .section .note.GNU-stack,"",@progbits
