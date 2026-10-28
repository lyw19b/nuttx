/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_strchrnul.S
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

#ifdef LIBC_BUILD_STRCHRNUL

/****************************************************************************
 * Name: strchrnul
 *
 * Description:
 *   Return the first occurrence of the low byte of a1 in the string
 *   at a0, or its terminating NUL if the byte is absent. Never read beyond
 *   the terminator, including when it is the last mapped byte.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(strchrnul)
  .type ARCH_LIBCFUN(strchrnul), function
ARCH_LIBCFUN(strchrnul):
  andi    $a1, $a1, 255

.Lsearch:
  ld.bu   $t0, $a0, 0
  beq     $t0, $a1, .Ldone
  beqz    $t0, .Ldone
  addi.d  $a0, $a0, 1
  b       .Lsearch

.Ldone:
  jr      $ra
  .size ARCH_LIBCFUN(strchrnul), . - ARCH_LIBCFUN(strchrnul)

#endif /* LIBC_BUILD_STRCHRNUL */

  .section .note.GNU-stack,"",@progbits
