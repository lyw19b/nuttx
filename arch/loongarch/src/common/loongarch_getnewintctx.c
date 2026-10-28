/****************************************************************************
 * arch/loongarch/src/common/loongarch_getnewintctx.c
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
#include <stdio.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <arch/csr.h>

#include "loongarch_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_get_newintctx
 *
 * Description:
 *   Return the initial PLV0 exception-return state with interrupts
 *   enabled. User startup later installs a separate PLV3 frame.
 *
 ****************************************************************************/

uintptr_t loongarch_get_newintctx(void)
{
  return CSR_PRMD_PIE;
}

/****************************************************************************
 * Name: loongarch_set_idleintctx
 *
 * Description:
 *   Complete idle interrupt-state setup. No additional per-task state is
 *   required because startup configures the CPU CSRs directly.
 *
 ****************************************************************************/

void loongarch_set_idleintctx(void)
{
}
