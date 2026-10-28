/****************************************************************************
 * arch/loongarch/src/common/loongarch_forkentry.S
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

#include "loongarch_context.h"

  .text
  .balign 16
  .global up_fork
  .type up_fork, function
/****************************************************************************
 * Name: up_fork
 *
 * Description:
 *   Capture the caller register frame on its stack and pass it to the
 *   flat-build fork helper. Restore the parent return address and stack
 *   pointer after the helper creates the child.
 *
 ****************************************************************************/

up_fork:
  addi.d  $sp, $sp, -XCPTCONTEXT_SIZE
  save_ctx $sp
  addi.d  $t0, $sp, XCPTCONTEXT_SIZE
  st.d    $t0, $sp, (REGSIZE * REG_R3_NDX)
  move    $a0, $sp
  bl      loongarch_fork
  ld.d    $ra, $sp, (REGSIZE * REG_R1_NDX)
  addi.d  $sp, $sp, XCPTCONTEXT_SIZE
  jr      $ra
  .size up_fork, . - up_fork
