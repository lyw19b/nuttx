/****************************************************************************
 * arch/loongarch/src/common/loongarch_tcbinfo.c
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

#include <nuttx/sched.h>
#include <arch/irq.h>
#include <sys/param.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const uint16_t g_reg_offs[] =
{
  UINT16_MAX,
  TCB_REG_OFF(REG_R1_NDX),
  TCB_REG_OFF(REG_R2_NDX),
  TCB_REG_OFF(REG_R3_NDX),
  TCB_REG_OFF(REG_R4_NDX),
  TCB_REG_OFF(REG_R5_NDX),
  TCB_REG_OFF(REG_R6_NDX),
  TCB_REG_OFF(REG_R7_NDX),
  TCB_REG_OFF(REG_R8_NDX),
  TCB_REG_OFF(REG_R9_NDX),
  TCB_REG_OFF(REG_R10_NDX),
  TCB_REG_OFF(REG_R11_NDX),
  TCB_REG_OFF(REG_R12_NDX),
  TCB_REG_OFF(REG_R13_NDX),
  TCB_REG_OFF(REG_R14_NDX),
  TCB_REG_OFF(REG_R15_NDX),
  TCB_REG_OFF(REG_R16_NDX),
  TCB_REG_OFF(REG_R17_NDX),
  TCB_REG_OFF(REG_R18_NDX),
  TCB_REG_OFF(REG_R19_NDX),
  TCB_REG_OFF(REG_R20_NDX),
  TCB_REG_OFF(REG_R21_NDX),
  TCB_REG_OFF(REG_R22_NDX),
  TCB_REG_OFF(REG_R23_NDX),
  TCB_REG_OFF(REG_R24_NDX),
  TCB_REG_OFF(REG_R25_NDX),
  TCB_REG_OFF(REG_R26_NDX),
  TCB_REG_OFF(REG_R27_NDX),
  TCB_REG_OFF(REG_R28_NDX),
  TCB_REG_OFF(REG_R29_NDX),
  TCB_REG_OFF(REG_R30_NDX),
  TCB_REG_OFF(REG_R31_NDX),
  TCB_REG_OFF(REG_EPC_NDX),
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct tcbinfo_s g_tcbinfo used_data =
{
  .pid_off        = TCB_PID_OFF,
  .state_off      = TCB_STATE_OFF,
  .pri_off        = TCB_PRI_OFF,
  .name_off       = TCB_NAME_OFF,
  .stack_off      = TCB_STACK_OFF,
  .stack_size_off = TCB_STACK_SIZE_OFF,
  .regs_off       = TCB_REGS_OFF,
  .regs_num       = nitems(g_reg_offs),
  {
    .p            = g_reg_offs,
  }
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/
