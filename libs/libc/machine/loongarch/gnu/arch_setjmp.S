/****************************************************************************
 * libs/libc/machine/loongarch/gnu/arch_setjmp.S
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

/* LP64D preserves RA, SP, FP, S0..S8 and FS0..FS7. */

  .text
  .balign 16
  .global setjmp
  .global _setjmp
  .type setjmp, function
/****************************************************************************
 * Name: setjmp
 *
 * Description:
 *   Save the LP64D callee-saved integer and floating-point registers in
 *   the jmp_buf addressed by a0. Return zero. The _setjmp alias uses the
 *   same implementation; signal masks are not saved here.
 *
 ****************************************************************************/

setjmp:
_setjmp:
  st.d    $ra, $a0, 0
  st.d    $sp, $a0, 8
  st.d    $r22, $a0, 16
  st.d    $r23, $a0, 24
  st.d    $r24, $a0, 32
  st.d    $r25, $a0, 40
  st.d    $r26, $a0, 48
  st.d    $r27, $a0, 56
  st.d    $r28, $a0, 64
  st.d    $r29, $a0, 72
  st.d    $r30, $a0, 80
  st.d    $r31, $a0, 88

  fst.d   $fs0, $a0, 96
  fst.d   $fs1, $a0, 104
  fst.d   $fs2, $a0, 112
  fst.d   $fs3, $a0, 120
  fst.d   $fs4, $a0, 128
  fst.d   $fs5, $a0, 136
  fst.d   $fs6, $a0, 144
  fst.d   $fs7, $a0, 152

  move    $a0, $zero
  jr      $ra
  .size setjmp, . - setjmp

  .balign 16
  .global longjmp
  .global _longjmp
  .type longjmp, function
/****************************************************************************
 * Name: longjmp
 *
 * Description:
 *   Restore the register state from the jmp_buf addressed by a0 and
 *   return through its saved ra. Use a1 as the resumed setjmp result,
 *   replacing zero with one. The _longjmp alias uses the same
 *   implementation.
 *
 ****************************************************************************/

longjmp:
_longjmp:
  ld.d    $ra, $a0, 0
  ld.d    $sp, $a0, 8
  ld.d    $r22, $a0, 16
  ld.d    $r23, $a0, 24
  ld.d    $r24, $a0, 32
  ld.d    $r25, $a0, 40
  ld.d    $r26, $a0, 48
  ld.d    $r27, $a0, 56
  ld.d    $r28, $a0, 64
  ld.d    $r29, $a0, 72
  ld.d    $r30, $a0, 80
  ld.d    $r31, $a0, 88

  fld.d   $fs0, $a0, 96
  fld.d   $fs1, $a0, 104
  fld.d   $fs2, $a0, 112
  fld.d   $fs3, $a0, 120
  fld.d   $fs4, $a0, 128
  fld.d   $fs5, $a0, 136
  fld.d   $fs6, $a0, 144
  fld.d   $fs7, $a0, 152

  sltui   $t0, $a1, 1
  add.d   $a0, $a1, $t0

  jr      $ra
  .size longjmp, . - longjmp
