/****************************************************************************
 * arch/loongarch/src/qemu/qemu_lowputc.c
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
#include <nuttx/arch.h>
#include "loongarch_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define QEMU_UART_BASE 0x800000001fe001e0ul
#define UART_LSR       5
#define UART_LSR_THRE  0x20

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_lowputc
 *
 * Description:
 *   Poll the console transmitter and emit one byte without using
 *   interrupt-driven serial buffers.
 *
 ****************************************************************************/

void loongarch_lowputc(char ch)
{
  while ((getreg8(QEMU_UART_BASE + UART_LSR) & UART_LSR_THRE) == 0)
    {
    }

  putreg8(ch, QEMU_UART_BASE);
}

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Emit one character through the configured low-level console. This
 *   diagnostic path is available before normal serial device
 *   registration.
 *
 ****************************************************************************/

void up_putc(int ch)
{
  loongarch_lowputc(ch);
}
