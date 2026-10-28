/****************************************************************************
 * arch/loongarch/src/common/loongarch_vectors.S
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
#include <arch/irq.h>
#include <arch/csr.h>
#include <arch/syscall.h>

/****************************************************************************
 * Exception and interrupt vectors
 *
 * ECFG.VS = 5 selects 128-byte slots. Exceptions occupy slots 0..63;
 * local interrupt inputs occupy slots 64..76. Every slot supplies its
 * own IRQ number, without scanning ESTAT pending bits. SAVE0 is private
 * to normal exception entry; refill uses TLBRSAVE instead.
 ****************************************************************************/

  .section .text.loongarch_vectors, "ax", @progbits
  .balign 16384
  .global loongarch_vectors
  .type loongarch_vectors, function
/****************************************************************************
 * Name: loongarch_vectors
 *
 * Description:
 *   Provide separate aligned entries for synchronous exceptions and local
 *   interrupt inputs. Each slot saves t0 in SAVE0 and supplies its own
 *   IRQ number to the shared context-save code; no pending-bit scan is
 *   needed.
 *
 ****************************************************************************/

loongarch_vectors:
  .altmacro
  .macro VECTOR number
  csrwr   $t0, LOONGARCH_CSR_SAVE0
  li.w    $t0, \number
  b       loongarch_exception_entry
  .balign 128
  .endm

  /* Synchronous exception vectors. */

  .set vector, 0
  .rept 64
  VECTOR %vector
  .set vector, vector + 1
  .endr

  /* CPU interrupt vectors. */

  .set vector, EXCCODE_INT_START
  .rept 13
  VECTOR %vector
  .set vector, vector + 1
  .endr
  .noaltmacro

  .size loongarch_vectors, . - loongarch_vectors

/****************************************************************************
 * Context save and restore
 *
 * Interrupts remain masked throughout dispatch. Save the interrupted task
 * on its own stack, then run C handlers on the per-CPU interrupt stack.
 * A task can migrate as soon as the scheduler releases its lock.
 ****************************************************************************/

  .text
  .balign 16
loongarch_exception_entry:
#ifdef CONFIG_ARCH_KERNEL_STACK
  /* SAVE3 holds the trusted kernel stack when returning to PLV3. Never
   * use an untrusted user SP to save the exception frame.
   */

  csrwr   $sp, LOONGARCH_CSR_SAVE3
  csrwr   $t0, LOONGARCH_CSR_SAVE4
  csrrd   $t0, LOONGARCH_CSR_PRMD
  andi    $t0, $t0, CSR_PRMD_PPLV
  bnez    $t0, .Lkernel_stack
  csrrd   $sp, LOONGARCH_CSR_SAVE3
.Lkernel_stack:
  csrrd   $t0, LOONGARCH_CSR_SAVE4
#endif

  /* Save every general register before calling C or enabling interrupts. */

  addi.d  $sp, $sp, -XCPTCONTEXT_SIZE
  st.d    $r1, $sp, (REGSIZE * REG_R1_NDX)
  st.d    $r2, $sp, (REGSIZE * REG_R2_NDX)
  st.d    $r4, $sp, (REGSIZE * REG_R4_NDX)
  st.d    $r5, $sp, (REGSIZE * REG_R5_NDX)
  st.d    $r6, $sp, (REGSIZE * REG_R6_NDX)
  st.d    $r7, $sp, (REGSIZE * REG_R7_NDX)
  st.d    $r8, $sp, (REGSIZE * REG_R8_NDX)
  st.d    $r9, $sp, (REGSIZE * REG_R9_NDX)
  st.d    $r10, $sp, (REGSIZE * REG_R10_NDX)
  st.d    $r11, $sp, (REGSIZE * REG_R11_NDX)
  st.d    $r13, $sp, (REGSIZE * REG_R13_NDX)
  st.d    $r14, $sp, (REGSIZE * REG_R14_NDX)
  st.d    $r15, $sp, (REGSIZE * REG_R15_NDX)
  st.d    $r16, $sp, (REGSIZE * REG_R16_NDX)
  st.d    $r17, $sp, (REGSIZE * REG_R17_NDX)
  st.d    $r18, $sp, (REGSIZE * REG_R18_NDX)
  st.d    $r19, $sp, (REGSIZE * REG_R19_NDX)
  st.d    $r20, $sp, (REGSIZE * REG_R20_NDX)
  st.d    $r21, $sp, (REGSIZE * REG_R21_NDX)
  st.d    $r22, $sp, (REGSIZE * REG_R22_NDX)
  st.d    $r23, $sp, (REGSIZE * REG_R23_NDX)
  st.d    $r24, $sp, (REGSIZE * REG_R24_NDX)
  st.d    $r25, $sp, (REGSIZE * REG_R25_NDX)
  st.d    $r26, $sp, (REGSIZE * REG_R26_NDX)
  st.d    $r27, $sp, (REGSIZE * REG_R27_NDX)
  st.d    $r28, $sp, (REGSIZE * REG_R28_NDX)
  st.d    $r29, $sp, (REGSIZE * REG_R29_NDX)
  st.d    $r30, $sp, (REGSIZE * REG_R30_NDX)
  st.d    $r31, $sp, (REGSIZE * REG_R31_NDX)
  move    $a0, $t0
  csrrd   $t0, LOONGARCH_CSR_SAVE0
  st.d    $t0, $sp, (REGSIZE * REG_R12_NDX)
#ifdef CONFIG_ARCH_KERNEL_STACK
  csrrd   $t0, LOONGARCH_CSR_SAVE3
#else
  addi.d  $t0, $sp, XCPTCONTEXT_SIZE
#endif
  st.d    $t0, $sp, (REGSIZE * REG_R3_NDX)
  csrrd   $t0, LOONGARCH_CSR_ERA
  st.d    $t0, $sp, (REGSIZE * REG_EPC_NDX)
  csrrd   $t0, LOONGARCH_CSR_PRMD
  st.d    $t0, $sp, (REGSIZE * REG_INT_CTX_NDX)

  /* LP64D tasks may have live caller-saved FPU state at any trap point. */

  fst.d   $f0, $sp, (REGSIZE * (REG_F0_NDX + 0))
  fst.d   $f1, $sp, (REGSIZE * (REG_F0_NDX + 1))
  fst.d   $f2, $sp, (REGSIZE * (REG_F0_NDX + 2))
  fst.d   $f3, $sp, (REGSIZE * (REG_F0_NDX + 3))
  fst.d   $f4, $sp, (REGSIZE * (REG_F0_NDX + 4))
  fst.d   $f5, $sp, (REGSIZE * (REG_F0_NDX + 5))
  fst.d   $f6, $sp, (REGSIZE * (REG_F0_NDX + 6))
  fst.d   $f7, $sp, (REGSIZE * (REG_F0_NDX + 7))
  fst.d   $f8, $sp, (REGSIZE * (REG_F0_NDX + 8))
  fst.d   $f9, $sp, (REGSIZE * (REG_F0_NDX + 9))
  fst.d   $f10, $sp, (REGSIZE * (REG_F0_NDX + 10))
  fst.d   $f11, $sp, (REGSIZE * (REG_F0_NDX + 11))
  fst.d   $f12, $sp, (REGSIZE * (REG_F0_NDX + 12))
  fst.d   $f13, $sp, (REGSIZE * (REG_F0_NDX + 13))
  fst.d   $f14, $sp, (REGSIZE * (REG_F0_NDX + 14))
  fst.d   $f15, $sp, (REGSIZE * (REG_F0_NDX + 15))
  fst.d   $f16, $sp, (REGSIZE * (REG_F0_NDX + 16))
  fst.d   $f17, $sp, (REGSIZE * (REG_F0_NDX + 17))
  fst.d   $f18, $sp, (REGSIZE * (REG_F0_NDX + 18))
  fst.d   $f19, $sp, (REGSIZE * (REG_F0_NDX + 19))
  fst.d   $f20, $sp, (REGSIZE * (REG_F0_NDX + 20))
  fst.d   $f21, $sp, (REGSIZE * (REG_F0_NDX + 21))
  fst.d   $f22, $sp, (REGSIZE * (REG_F0_NDX + 22))
  fst.d   $f23, $sp, (REGSIZE * (REG_F0_NDX + 23))
  fst.d   $f24, $sp, (REGSIZE * (REG_F0_NDX + 24))
  fst.d   $f25, $sp, (REGSIZE * (REG_F0_NDX + 25))
  fst.d   $f26, $sp, (REGSIZE * (REG_F0_NDX + 26))
  fst.d   $f27, $sp, (REGSIZE * (REG_F0_NDX + 27))
  fst.d   $f28, $sp, (REGSIZE * (REG_F0_NDX + 28))
  fst.d   $f29, $sp, (REGSIZE * (REG_F0_NDX + 29))
  fst.d   $f30, $sp, (REGSIZE * (REG_F0_NDX + 30))
  fst.d   $f31, $sp, (REGSIZE * (REG_F0_NDX + 31))
  movfcsr2gr $t0, $fcsr0
  st.d    $t0, $sp, (REGSIZE * REG_FCSR_NDX)
  movcf2gr $t0, $fcc0
  st.b    $t0, $sp, (REGSIZE * REG_FCC_NDX + 0)
  movcf2gr $t0, $fcc1
  st.b    $t0, $sp, (REGSIZE * REG_FCC_NDX + 1)
  movcf2gr $t0, $fcc2
  st.b    $t0, $sp, (REGSIZE * REG_FCC_NDX + 2)
  movcf2gr $t0, $fcc3
  st.b    $t0, $sp, (REGSIZE * REG_FCC_NDX + 3)
  movcf2gr $t0, $fcc4
  st.b    $t0, $sp, (REGSIZE * REG_FCC_NDX + 4)
  movcf2gr $t0, $fcc5
  st.b    $t0, $sp, (REGSIZE * REG_FCC_NDX + 5)
  movcf2gr $t0, $fcc6
  st.b    $t0, $sp, (REGSIZE * REG_FCC_NDX + 6)
  movcf2gr $t0, $fcc7
  st.b    $t0, $sp, (REGSIZE * REG_FCC_NDX + 7)
#ifdef CONFIG_BUILD_KERNEL
  /* Blocking system calls run on this thread's kernel stack, outside
   * interrupt context. Reserved operations go through the IRQ dispatcher.
   */

  li.w    $t0, LOONGARCH_IRQ_SYS
  bne     $a0, $t0, .Lirq
  ld.d    $t0, $sp, REG_INT_CTX
  andi    $t0, $t0, CSR_PRMD_PPLV
  beqz    $t0, .Lirq
  ld.d    $t0, $sp, REG_R4
  li.w    $t1, CONFIG_SYS_RESERVED
  bltu    $t0, $t1, .Lirq
  move    $a0, $sp
  bl      loongarch_syscall_dispatch
  move    $a0, $sp
  b       loongarch_exception_return
.Lirq:
#endif
  /* Ordinary handlers run on this CPU interrupt stack with IRQs masked. */

  move    $a1, $sp
#if CONFIG_ARCH_INTERRUPTSTACK > 15
  csrrd   $t0, LOONGARCH_CSR_CPUID
  andi    $t0, $t0, 0x3ff
  li.d    $t1, INT_STACK_SIZE
  mul.d   $t0, $t0, $t1
  la.local $sp, g_intstacktop
  sub.d   $sp, $sp, $t0
#endif
  bl      loongarch_doirq

/* Restore the scheduler-selected frame, including its privilege level. */

loongarch_exception_return:
  move    $sp, $a0

  ld.d    $t0, $sp, (REGSIZE * REG_EPC_NDX)
  csrwr   $t0, LOONGARCH_CSR_ERA
  ld.d    $t0, $sp, (REGSIZE * REG_INT_CTX_NDX)
  csrwr   $t0, LOONGARCH_CSR_PRMD
  ld.d    $t0, $sp, (REGSIZE * REG_FCSR_NDX)
  movgr2fcsr $fcsr0, $t0
  ld.bu   $t0, $sp, (REGSIZE * REG_FCC_NDX + 0)
  movgr2cf $fcc0, $t0
  ld.bu   $t0, $sp, (REGSIZE * REG_FCC_NDX + 1)
  movgr2cf $fcc1, $t0
  ld.bu   $t0, $sp, (REGSIZE * REG_FCC_NDX + 2)
  movgr2cf $fcc2, $t0
  ld.bu   $t0, $sp, (REGSIZE * REG_FCC_NDX + 3)
  movgr2cf $fcc3, $t0
  ld.bu   $t0, $sp, (REGSIZE * REG_FCC_NDX + 4)
  movgr2cf $fcc4, $t0
  ld.bu   $t0, $sp, (REGSIZE * REG_FCC_NDX + 5)
  movgr2cf $fcc5, $t0
  ld.bu   $t0, $sp, (REGSIZE * REG_FCC_NDX + 6)
  movgr2cf $fcc6, $t0
  ld.bu   $t0, $sp, (REGSIZE * REG_FCC_NDX + 7)
  movgr2cf $fcc7, $t0

  /* Restore FPU state before general registers reuse the scratch values. */

  fld.d   $f0, $sp, (REGSIZE * (REG_F0_NDX + 0))
  fld.d   $f1, $sp, (REGSIZE * (REG_F0_NDX + 1))
  fld.d   $f2, $sp, (REGSIZE * (REG_F0_NDX + 2))
  fld.d   $f3, $sp, (REGSIZE * (REG_F0_NDX + 3))
  fld.d   $f4, $sp, (REGSIZE * (REG_F0_NDX + 4))
  fld.d   $f5, $sp, (REGSIZE * (REG_F0_NDX + 5))
  fld.d   $f6, $sp, (REGSIZE * (REG_F0_NDX + 6))
  fld.d   $f7, $sp, (REGSIZE * (REG_F0_NDX + 7))
  fld.d   $f8, $sp, (REGSIZE * (REG_F0_NDX + 8))
  fld.d   $f9, $sp, (REGSIZE * (REG_F0_NDX + 9))
  fld.d   $f10, $sp, (REGSIZE * (REG_F0_NDX + 10))
  fld.d   $f11, $sp, (REGSIZE * (REG_F0_NDX + 11))
  fld.d   $f12, $sp, (REGSIZE * (REG_F0_NDX + 12))
  fld.d   $f13, $sp, (REGSIZE * (REG_F0_NDX + 13))
  fld.d   $f14, $sp, (REGSIZE * (REG_F0_NDX + 14))
  fld.d   $f15, $sp, (REGSIZE * (REG_F0_NDX + 15))
  fld.d   $f16, $sp, (REGSIZE * (REG_F0_NDX + 16))
  fld.d   $f17, $sp, (REGSIZE * (REG_F0_NDX + 17))
  fld.d   $f18, $sp, (REGSIZE * (REG_F0_NDX + 18))
  fld.d   $f19, $sp, (REGSIZE * (REG_F0_NDX + 19))
  fld.d   $f20, $sp, (REGSIZE * (REG_F0_NDX + 20))
  fld.d   $f21, $sp, (REGSIZE * (REG_F0_NDX + 21))
  fld.d   $f22, $sp, (REGSIZE * (REG_F0_NDX + 22))
  fld.d   $f23, $sp, (REGSIZE * (REG_F0_NDX + 23))
  fld.d   $f24, $sp, (REGSIZE * (REG_F0_NDX + 24))
  fld.d   $f25, $sp, (REGSIZE * (REG_F0_NDX + 25))
  fld.d   $f26, $sp, (REGSIZE * (REG_F0_NDX + 26))
  fld.d   $f27, $sp, (REGSIZE * (REG_F0_NDX + 27))
  fld.d   $f28, $sp, (REGSIZE * (REG_F0_NDX + 28))
  fld.d   $f29, $sp, (REGSIZE * (REG_F0_NDX + 29))
  fld.d   $f30, $sp, (REGSIZE * (REG_F0_NDX + 30))
  fld.d   $f31, $sp, (REGSIZE * (REG_F0_NDX + 31))

  /* Restore SP last: all preceding loads address the trusted save frame. */

  ld.d    $r1, $sp, (REGSIZE * REG_R1_NDX)
  ld.d    $r2, $sp, (REGSIZE * REG_R2_NDX)
  ld.d    $r4, $sp, (REGSIZE * REG_R4_NDX)
  ld.d    $r5, $sp, (REGSIZE * REG_R5_NDX)
  ld.d    $r6, $sp, (REGSIZE * REG_R6_NDX)
  ld.d    $r7, $sp, (REGSIZE * REG_R7_NDX)
  ld.d    $r8, $sp, (REGSIZE * REG_R8_NDX)
  ld.d    $r9, $sp, (REGSIZE * REG_R9_NDX)
  ld.d    $r10, $sp, (REGSIZE * REG_R10_NDX)
  ld.d    $r11, $sp, (REGSIZE * REG_R11_NDX)
  ld.d    $r12, $sp, (REGSIZE * REG_R12_NDX)
  ld.d    $r13, $sp, (REGSIZE * REG_R13_NDX)
  ld.d    $r14, $sp, (REGSIZE * REG_R14_NDX)
  ld.d    $r15, $sp, (REGSIZE * REG_R15_NDX)
  ld.d    $r16, $sp, (REGSIZE * REG_R16_NDX)
  ld.d    $r17, $sp, (REGSIZE * REG_R17_NDX)
  ld.d    $r18, $sp, (REGSIZE * REG_R18_NDX)
  ld.d    $r19, $sp, (REGSIZE * REG_R19_NDX)
  ld.d    $r20, $sp, (REGSIZE * REG_R20_NDX)
  ld.d    $r21, $sp, (REGSIZE * REG_R21_NDX)
  ld.d    $r22, $sp, (REGSIZE * REG_R22_NDX)
  ld.d    $r23, $sp, (REGSIZE * REG_R23_NDX)
  ld.d    $r24, $sp, (REGSIZE * REG_R24_NDX)
  ld.d    $r25, $sp, (REGSIZE * REG_R25_NDX)
  ld.d    $r26, $sp, (REGSIZE * REG_R26_NDX)
  ld.d    $r27, $sp, (REGSIZE * REG_R27_NDX)
  ld.d    $r28, $sp, (REGSIZE * REG_R28_NDX)
  ld.d    $r29, $sp, (REGSIZE * REG_R29_NDX)
  ld.d    $r30, $sp, (REGSIZE * REG_R30_NDX)
  ld.d    $r31, $sp, (REGSIZE * REG_R31_NDX)
  ld.d    $sp, $sp, (REGSIZE * REG_R3_NDX)
  ertn

#if CONFIG_ARCH_INTERRUPTSTACK > 15
  .bss
  .balign 16
  .global g_intstackalloc
  .global g_intstacktop
g_intstackalloc:
  .skip INT_STACK_SIZE * CONFIG_SMP_NCPUS
g_intstacktop:
#endif
