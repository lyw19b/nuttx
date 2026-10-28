/****************************************************************************
 * arch/loongarch/src/common/loongarch_context.h
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

#include <nuttx/config.h>

#include <arch/arch.h>
#include <arch/csr.h>
#include <arch/irq.h>

#include <sys/types.h>

#include "loongarch_internal.h"

/****************************************************************************
 * Name: save_ctx
 *
 * Parameter:
 *   base - Pointer to where the save is performed
 *
 * Description:
 *   Save the common context registers.
 *
 ****************************************************************************/

.macro save_ctx base
  st.d    $tp,  \base, (REGSIZE * REG_R2_NDX)
  st.d    $ra,  \base, (REGSIZE * REG_R1_NDX)
  st.d    $sp,  \base, (REGSIZE * REG_R3_NDX)
  st.d    $r4,  \base, (REGSIZE * REG_R4_NDX)
  st.d    $r5,  \base, (REGSIZE * REG_R5_NDX)
  st.d    $r6,  \base, (REGSIZE * REG_R6_NDX)
  st.d    $r7,  \base, (REGSIZE * REG_R7_NDX)
  st.d    $r8,  \base, (REGSIZE * REG_R8_NDX)
  st.d    $r9,  \base, (REGSIZE * REG_R9_NDX)
  st.d    $r10, \base, (REGSIZE * REG_R10_NDX)
  st.d    $r11, \base, (REGSIZE * REG_R11_NDX)
  st.d    $r12, \base, (REGSIZE * REG_R12_NDX)
  st.d    $r13, \base, (REGSIZE * REG_R13_NDX)
  st.d    $r14, \base, (REGSIZE * REG_R14_NDX)
  st.d    $r15, \base, (REGSIZE * REG_R15_NDX)
  st.d    $r16, \base, (REGSIZE * REG_R16_NDX)
  st.d    $r17, \base, (REGSIZE * REG_R17_NDX)
  st.d    $r18, \base, (REGSIZE * REG_R18_NDX)
  st.d    $r19, \base, (REGSIZE * REG_R19_NDX)
  st.d    $r20, \base, (REGSIZE * REG_R20_NDX)
  st.d    $r21, \base, (REGSIZE * REG_R21_NDX)
  st.d    $r22, \base, (REGSIZE * REG_R22_NDX)
  st.d    $r23, \base, (REGSIZE * REG_R23_NDX)
  st.d    $r24, \base, (REGSIZE * REG_R24_NDX)
  st.d    $r25, \base, (REGSIZE * REG_R25_NDX)
  st.d    $r26, \base, (REGSIZE * REG_R26_NDX)
  st.d    $r27, \base, (REGSIZE * REG_R27_NDX)
  st.d    $r28, \base, (REGSIZE * REG_R28_NDX)
  st.d    $r29, \base, (REGSIZE * REG_R29_NDX)
  st.d    $r30, \base, (REGSIZE * REG_R30_NDX)
  st.d    $r31, \base, (REGSIZE * REG_R31_NDX)
  fst.d   $f0, \base, (REGSIZE * (REG_F0_NDX + 0))
  fst.d   $f1, \base, (REGSIZE * (REG_F0_NDX + 1))
  fst.d   $f2, \base, (REGSIZE * (REG_F0_NDX + 2))
  fst.d   $f3, \base, (REGSIZE * (REG_F0_NDX + 3))
  fst.d   $f4, \base, (REGSIZE * (REG_F0_NDX + 4))
  fst.d   $f5, \base, (REGSIZE * (REG_F0_NDX + 5))
  fst.d   $f6, \base, (REGSIZE * (REG_F0_NDX + 6))
  fst.d   $f7, \base, (REGSIZE * (REG_F0_NDX + 7))
  fst.d   $f8, \base, (REGSIZE * (REG_F0_NDX + 8))
  fst.d   $f9, \base, (REGSIZE * (REG_F0_NDX + 9))
  fst.d   $f10, \base, (REGSIZE * (REG_F0_NDX + 10))
  fst.d   $f11, \base, (REGSIZE * (REG_F0_NDX + 11))
  fst.d   $f12, \base, (REGSIZE * (REG_F0_NDX + 12))
  fst.d   $f13, \base, (REGSIZE * (REG_F0_NDX + 13))
  fst.d   $f14, \base, (REGSIZE * (REG_F0_NDX + 14))
  fst.d   $f15, \base, (REGSIZE * (REG_F0_NDX + 15))
  fst.d   $f16, \base, (REGSIZE * (REG_F0_NDX + 16))
  fst.d   $f17, \base, (REGSIZE * (REG_F0_NDX + 17))
  fst.d   $f18, \base, (REGSIZE * (REG_F0_NDX + 18))
  fst.d   $f19, \base, (REGSIZE * (REG_F0_NDX + 19))
  fst.d   $f20, \base, (REGSIZE * (REG_F0_NDX + 20))
  fst.d   $f21, \base, (REGSIZE * (REG_F0_NDX + 21))
  fst.d   $f22, \base, (REGSIZE * (REG_F0_NDX + 22))
  fst.d   $f23, \base, (REGSIZE * (REG_F0_NDX + 23))
  fst.d   $f24, \base, (REGSIZE * (REG_F0_NDX + 24))
  fst.d   $f25, \base, (REGSIZE * (REG_F0_NDX + 25))
  fst.d   $f26, \base, (REGSIZE * (REG_F0_NDX + 26))
  fst.d   $f27, \base, (REGSIZE * (REG_F0_NDX + 27))
  fst.d   $f28, \base, (REGSIZE * (REG_F0_NDX + 28))
  fst.d   $f29, \base, (REGSIZE * (REG_F0_NDX + 29))
  fst.d   $f30, \base, (REGSIZE * (REG_F0_NDX + 30))
  fst.d   $f31, \base, (REGSIZE * (REG_F0_NDX + 31))
  movfcsr2gr $t0, $fcsr0
  st.d    $t0, \base, (REGSIZE * REG_FCSR_NDX)
  movcf2gr $t0, $fcc0
  st.b    $t0, \base, (REGSIZE * REG_FCC_NDX + 0)
  movcf2gr $t0, $fcc1
  st.b    $t0, \base, (REGSIZE * REG_FCC_NDX + 1)
  movcf2gr $t0, $fcc2
  st.b    $t0, \base, (REGSIZE * REG_FCC_NDX + 2)
  movcf2gr $t0, $fcc3
  st.b    $t0, \base, (REGSIZE * REG_FCC_NDX + 3)
  movcf2gr $t0, $fcc4
  st.b    $t0, \base, (REGSIZE * REG_FCC_NDX + 4)
  movcf2gr $t0, $fcc5
  st.b    $t0, \base, (REGSIZE * REG_FCC_NDX + 5)
  movcf2gr $t0, $fcc6
  st.b    $t0, \base, (REGSIZE * REG_FCC_NDX + 6)
  movcf2gr $t0, $fcc7
  st.b    $t0, \base, (REGSIZE * REG_FCC_NDX + 7)
.endm
