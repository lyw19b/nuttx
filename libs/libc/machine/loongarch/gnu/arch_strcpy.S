/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_strcpy.S
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

#ifdef LIBC_BUILD_STRCPY

/****************************************************************************
 * Name: strcpy
 *
 * Description:
 *   Copy the string at a1, including its terminating NUL, to a0.
 *   Return the original destination. The caller provides enough writable
 *   space and disjoint ranges. Byte accesses stop exactly at the NUL.
 *
 ****************************************************************************/

  .text
  .balign 16
  .global ARCH_LIBCFUN(strcpy)
  .type ARCH_LIBCFUN(strcpy), function
ARCH_LIBCFUN(strcpy):
  move    $t0, $a0

.Lcopy:
  ld.bu   $t1, $a1, 0
  st.b    $t1, $t0, 0
  beqz    $t1, .Ldone
  addi.d  $a1, $a1, 1
  addi.d  $t0, $t0, 1
  b       .Lcopy

.Ldone:
  jr      $ra
  .size ARCH_LIBCFUN(strcpy), . - ARCH_LIBCFUN(strcpy)

#endif /* LIBC_BUILD_STRCPY */

  .section .note.GNU-stack,"",@progbits
