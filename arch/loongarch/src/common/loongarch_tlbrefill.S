/****************************************************************************
 * arch/loongarch/src/common/loongarch_tlbrefill.S
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
#include <arch/csr.h>

/****************************************************************************
 * Software TLB refill
 *
 * The refill handler runs in direct-address mode. It must use physical
 * page-table addresses and must not touch the interrupted stack. Normal
 * exceptions use SAVE0; these scratch CSRs are reserved for refill only.
 * Both halves of the even/odd pair are filled. Invalid leaves deliberately
 * produce a normal page fault on retry, with the original ERA and BADV.
 ****************************************************************************/

  .section .text.loongarch_tlbrefill, "ax", @progbits
  .balign 4096
  .global loongarch_tlbrefill
  .type loongarch_tlbrefill, function

/****************************************************************************
 * Name: loongarch_tlbrefill
 *
 * Description:
 *   Refill an even/odd TLB pair using physical page-table walks in
 *   direct-address mode. Save all scratch registers in dedicated CSRs,
 *   without accessing the interrupted stack. Invalid leaves remain
 *   invalid so retry raises an ordinary page fault. Return with ERTN to
 *   the faulting instruction.
 *
 ****************************************************************************/

loongarch_tlbrefill:
  csrwr   $t0, LOONGARCH_CSR_TLBRSAVE
  csrwr   $t1, LOONGARCH_CSR_SAVE1
  csrwr   $t2, LOONGARCH_CSR_SAVE2

  /* Walk root, upper and middle directories using nine-bit indices. */

  csrrd   $t0, LOONGARCH_CSR_PGD
  csrrd   $t1, LOONGARCH_CSR_TLBRBADV

  srli.d  $t2, $t1, 39
  andi    $t2, $t2, 511
  slli.d  $t2, $t2, 3
  ldx.d   $t0, $t0, $t2

  srli.d  $t2, $t1, 30
  andi    $t2, $t2, 511
  slli.d  $t2, $t2, 3
  ldx.d   $t0, $t0, $t2

  srli.d  $t2, $t1, 21
  andi    $t2, $t2, 511
  slli.d  $t2, $t2, 3
  ldx.d   $t0, $t0, $t2

  /* Clear the low index bit to fetch both pages in the hardware TLB pair. */

  srli.d  $t2, $t1, 12
  andi    $t2, $t2, 510
  slli.d  $t2, $t2, 3
  add.d   $t0, $t0, $t2

  ld.d    $t1, $t0, 0
  ld.d    $t2, $t0, 8

  csrwr   $t1, LOONGARCH_CSR_TLBRELO0
  csrwr   $t2, LOONGARCH_CSR_TLBRELO1

  li.w    $t1, 12
  li.w    $t2, 0x3f

  csrxchg $t1, $t2, LOONGARCH_CSR_TLBREHI

  tlbfill

  /* ERTN restores the refill-specific privilege and addressing state. */

  csrrd   $t2, LOONGARCH_CSR_SAVE2
  csrrd   $t1, LOONGARCH_CSR_SAVE1
  csrrd   $t0, LOONGARCH_CSR_TLBRSAVE

  ertn
  .size loongarch_tlbrefill, . - loongarch_tlbrefill
