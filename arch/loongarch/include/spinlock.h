/****************************************************************************
 * arch/loongarch/include/spinlock.h
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

#ifndef __ARCH_LOONGARCH_INCLUDE_SPINLOCK_H
#define __ARCH_LOONGARCH_INCLUDE_SPINLOCK_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <arch/barriers.h>

#define SP_UNLOCKED 0
#define SP_LOCKED   1

#ifndef __ASSEMBLY__

typedef uint32_t spinlock_t;

/* AMswap with the DB suffix provides acquire and release ordering. */

/****************************************************************************
 * Name: up_testset
 *
 * Description:
 *   Atomically acquire a spinlock and return its previous value. The
 *   caller retries when the lock was already held.
 *
 ****************************************************************************/

static inline spinlock_t up_testset(volatile spinlock_t *lock)
{
  spinlock_t value;

  __asm__ __volatile__("amswap_db.w %0, %2, %1"
                       : "=&r"(value), "+ZB"(*lock)
                       : "r"(SP_LOCKED)
                       : "memory");
  return value;
}

#endif /* __ASSEMBLY__ */
#endif /* __ARCH_LOONGARCH_INCLUDE_SPINLOCK_H */
