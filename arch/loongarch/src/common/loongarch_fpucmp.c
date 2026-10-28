/****************************************************************************
 * arch/loongarch/src/common/loongarch_fpucmp.c
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

#include <stdbool.h>
#include <stdint.h>
#include <nuttx/arch.h>
#include <arch/irq.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_fpucmp
 *
 * Description:
 *   Compare the floating-point portion of two full exception frames. All
 *   32 doubleword registers and eight condition flags must match. Only the
 *   architectural low 32 bits of the FCSR save slot participate; integer
 *   registers and padding are unrelated to the saved floating-point state.
 *
 * Returned Value:
 *   True when both frames contain identical floating-point state.
 *
 ****************************************************************************/

bool up_fpucmp(const void *saveregs1, const void *saveregs2)
{
  const uintreg_t *first = saveregs1;
  const uintreg_t *second = saveregs2;
  unsigned int reg;

  for (reg = REG_F0_NDX; reg < REG_FCSR_NDX; reg++)
    {
      if (first[reg] != second[reg])
        {
          return false;
        }
    }

  return (uint32_t)first[REG_FCSR_NDX] ==
         (uint32_t)second[REG_FCSR_NDX] &&
         first[REG_FCC_NDX] == second[REG_FCC_NDX];
}
