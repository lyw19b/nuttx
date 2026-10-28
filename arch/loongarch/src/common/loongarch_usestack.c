/****************************************************************************
 * arch/loongarch/src/common/loongarch_usestack.c
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

#include <sys/types.h>
#include <stdint.h>
#include <sched.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/kmalloc.h>
#include <nuttx/arch.h>
#include <nuttx/tls.h>

#include "loongarch_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_use_stack
 *
 * Description:
 *   Attach a caller-owned aligned stack to an unstarted thread, releasing
 *   any prior stack allocation. The caller retains ownership of this
 *   buffer.
 *
 ****************************************************************************/

int up_use_stack(struct tcb_s *tcb, void *stack, size_t stack_size)
{
  uintptr_t top_of_stack;
  size_t size_of_stack;

#ifdef CONFIG_TLS_ALIGNED
  DEBUGASSERT(((uintptr_t)stack & TLS_STACK_MASK) == 0);
#endif
  DEBUGASSERT(((uintptr_t)stack & STACK_ALIGN_MASK) == 0);

  if (tcb->stack_alloc_ptr)
    {
      up_release_stack(tcb, tcb->flags & TCB_FLAG_TTYPE_MASK);
    }

  tcb->stack_alloc_ptr = stack;

  top_of_stack = (uintptr_t)tcb->stack_alloc_ptr + stack_size;

  top_of_stack = STACKFRAME_ALIGN_DOWN(top_of_stack);
  size_of_stack = top_of_stack - (uintptr_t)tcb->stack_alloc_ptr;

  tcb->stack_base_ptr = tcb->stack_alloc_ptr;
  tcb->adj_stack_size = size_of_stack;

#if defined(CONFIG_STACK_COLORATION)
  loongarch_stack_color(tcb->stack_base_ptr, tcb->adj_stack_size);
#endif

  return OK;
}
