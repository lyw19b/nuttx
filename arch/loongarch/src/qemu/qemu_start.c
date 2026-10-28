/****************************************************************************
 * arch/loongarch/src/qemu/qemu_start.c
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
#include <nuttx/init.h>
#include <nuttx/serial/uart_16550.h>
#include "loongarch_internal.h"
#include "loongarch_mmu.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qemu_start
 *
 * Description:
 *   Initialize the QEMU platform after assembly has established direct
 *   mappings and the boot stack, then enter the NuttX scheduler.
 *
 ****************************************************************************/

void qemu_start(void)
{
  uint64_t *dest;

  for (dest = (uint64_t *)_sbss; dest < (uint64_t *)_ebss; dest++)
    {
      *dest = 0;
    }

  loongarch_vector_initialize();
#ifdef CONFIG_ARCH_USE_MMU
  loongarch_mmu_initialize();
#endif
  u16550_earlyserialinit();
  nx_start();
}

/****************************************************************************
 * Name: loongarch_serialinit
 *
 * Description:
 *   Register the configured UART devices and console with the NuttX
 *   serial upper half. The no-driver variant requires no registration.
 *
 ****************************************************************************/

void loongarch_serialinit(void)
{
  u16550_serialinit();
}
