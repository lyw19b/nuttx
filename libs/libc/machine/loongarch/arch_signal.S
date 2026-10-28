/****************************************************************************
 * libs/libc/machine/loongarch/arch_signal.S
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
#include <arch/syscall.h>

/****************************************************************************
 * User signal trampoline
 ****************************************************************************/

  .text
  .balign 4
  .global up_signal_handler
  .type up_signal_handler, function
/****************************************************************************
 * Name: up_signal_handler
 *
 * Description:
 *   Run at PLV3 with the handler in a0 and its arguments in a1-a3.
 *   Preserve stack alignment, call the handler, then invoke the reserved
 *   signal-return syscall. The trusted kernel continuation is never
 *   stored on this user stack. Trap if signal return unexpectedly comes
 *   back.
 *
 ****************************************************************************/

up_signal_handler:
  addi.d  $sp, $sp, -16
  move    $t0, $a0
  move    $a0, $a1
  move    $a1, $a2
  move    $a2, $a3

  jirl    $ra, $t0, 0

  li.w    $a0, SYS_signal_handler_return

  syscall 0

  break   0
  .size up_signal_handler, . - up_signal_handler
