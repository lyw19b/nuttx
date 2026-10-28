/****************************************************************************
 * arch/loongarch/src/common/loongarch_timer.c
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

#include <stdint.h>
#include <time.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <arch/csr.h>

#include "loongarch_internal.h"
#include <assert.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_const_freq
 *
 * Description:
 *   Compute the invariant timer frequency from CPUCFG clock and
 *   multiplier fields. Return the frequency in Hz.
 *
 ****************************************************************************/

static uint64_t loongarch_const_freq(void)
{
  uint32_t base_freq;
  uint32_t cfg5;
  uint32_t cfm;
  uint32_t cfd;

  base_freq = read_cpucfg(LOONGARCH_CPUCFG4);
  cfg5 = read_cpucfg(LOONGARCH_CPUCFG5);
  cfm = cfg5 & 0xffff;
  cfd = (cfg5 >> 16) & 0xffff;

  if (!base_freq || !cfm || !cfd)
    {
      PANIC();
      return 0;
    }

  return (uint64_t)base_freq * cfm / cfd;
}

/****************************************************************************
 * Name: loongarch_timer_interrupt
 *
 * Description:
 *   Acknowledge the local timer interrupt and advance the NuttX system
 *   tick. The periodic timer is installed on the boot CPU.
 *
 ****************************************************************************/

static int loongarch_timer_interrupt(int irq, void *context, void *arg)
{
  /* Clear timer intr status */

  csr_writeq(CSR_TINTCLR_TI, LOONGARCH_CSR_TINTCLR);

  nxsched_process_timer();

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_timer_initialize
 *
 * Description:
 *   Program the invariant timer for the NuttX tick interval and attach
 *   its interrupt handler. Round the reload value to the timer register
 *   alignment.
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
  uint64_t tcfg;
  uint64_t tval;
  uint64_t timer_freq;

  timer_freq = loongarch_const_freq();
  tval = timer_freq / TICK_PER_SEC;

  tcfg  = (tval + 3) & ~3ul;
  tcfg |= CSR_TCFG_PERIOD | CSR_TCFG_EN;

  csr_writeq(tcfg, LOONGARCH_CSR_TCFG);

  irq_attach(LOONGARCH_IRQ_TIMER, loongarch_timer_interrupt, NULL);
  up_enable_irq(LOONGARCH_IRQ_TIMER);
}
