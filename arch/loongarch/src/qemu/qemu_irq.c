/****************************************************************************
 * arch/loongarch/src/qemu/qemu_irq.c
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
#include <nuttx/irq.h>
#include <arch/csr.h>
#include "loongarch_internal.h"
#include "loongarch_smp.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define QEMU_PIC_BASE  0x8000000010000000ul
#define QEMU_PIC_MASK  (QEMU_PIC_BASE + 0x20)
#define QEMU_PIC_EDGE  (QEMU_PIC_BASE + 0x60)
#define QEMU_PIC_CLEAR (QEMU_PIC_BASE + 0x80)
#define QEMU_PIC_ROUTE (QEMU_PIC_BASE + 0x100)
#define QEMU_PIC_VECTOR (QEMU_PIC_BASE + 0x200)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qemu_external_interrupt
 *
 * Description:
 *   Acknowledge pending external interrupt bits and dispatch each source
 *   through the NuttX IRQ table. The CPU vector has already selected the
 *   external interrupt input.
 *
 ****************************************************************************/

static int qemu_external_interrupt(int irq, void *context, void *arg)
{
  uint64_t pending = iocsr_read64(EIOINTC_REG_ISR);

  iocsr_write64(pending, EIOINTC_REG_ISR);
  while (pending != 0)
    {
      int bit = __builtin_ctzll(pending);

      irq_dispatch(QEMU_IRQ_EXT + bit, context);
      pending &= pending - 1;
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irqinitialize
 *
 * Description:
 *   Install exception vectors and route the QEMU PIC through EIOINTC to
 *   CPU zero. Mask device sources before routing them, then attach the
 *   external handler and enable configured local inputs.
 *
 ****************************************************************************/

void up_irqinitialize(void)
{
  unsigned int i;

  up_irq_save();
  loongarch_vector_initialize();
  loongarch_exception_attach();
  putreg64(UINT64_MAX, QEMU_PIC_MASK);
  putreg64(0, QEMU_PIC_EDGE);
  putreg64(UINT64_MAX, QEMU_PIC_CLEAR);
  for (i = 0; i < 64; i++)
    {
      putreg8(1, QEMU_PIC_ROUTE + i);
      putreg8(i, QEMU_PIC_VECTOR + i);
    }

  iocsr_write32(0x01010101, EIOINTC_REG_IPMAP);
  for (i = 0; i < 16; i++)
    {
      iocsr_write32(0x01010101, EIOINTC_REG_ROUTE + 4 * i);
    }

  iocsr_write32(0xffffffff, EIOINTC_REG_ENABLE);
  iocsr_write32(0xffffffff, EIOINTC_REG_ENABLE + 4);
  irq_attach(LOONGARCH_IRQ_IP0, qemu_external_interrupt, NULL);
  up_enable_irq(LOONGARCH_IRQ_IP0);
  loongarch_color_intstack();
#ifdef CONFIG_SMP
  loongarch_smp_initialize();
#endif
#ifndef CONFIG_SUPPRESS_INTERRUPTS
  up_irq_enable();
#endif
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Unmask a local CPU input or QEMU PIC source. Serialize PIC read-
 *   modify-write operations with a critical section.
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  if (irq >= LOONGARCH_IRQ_SIP0 && irq <= LOONGARCH_IRQ_IPI)
    {
      csr_xchgq(1ul << (irq - EXCCODE_INT_START),
                1ul << (irq - EXCCODE_INT_START), LOONGARCH_CSR_ECFG);
    }
  else if (irq >= QEMU_IRQ_EXT && irq < NR_IRQS)
    {
      irqstate_t flags = enter_critical_section();
      uint64_t mask = getreg64(QEMU_PIC_MASK);

      putreg64(mask & ~(1ul << (irq - QEMU_IRQ_EXT)), QEMU_PIC_MASK);
      leave_critical_section(flags);
    }
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Mask a local CPU input or QEMU PIC source while preserving the other
 *   interrupt enables.
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  if (irq >= LOONGARCH_IRQ_SIP0 && irq <= LOONGARCH_IRQ_IPI)
    {
      csr_xchgq(0, 1ul << (irq - EXCCODE_INT_START), LOONGARCH_CSR_ECFG);
    }
  else if (irq >= QEMU_IRQ_EXT && irq < NR_IRQS)
    {
      irqstate_t flags = enter_critical_section();
      uint64_t mask = getreg64(QEMU_PIC_MASK);

      putreg64(mask | (1ul << (irq - QEMU_IRQ_EXT)), QEMU_PIC_MASK);
      leave_critical_section(flags);
    }
}

/****************************************************************************
 * Name: loongarch_ack_irq
 *
 * Description:
 *   Complete the architecture acknowledgement hook. Individual interrupt-
 *   controller and device handlers perform the required acknowledgements.
 *
 ****************************************************************************/

void loongarch_ack_irq(int irq)
{
}
