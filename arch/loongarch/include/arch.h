/****************************************************************************
 * arch/loongarch/include/arch.h
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

#ifndef __ARCH_LOONGARCH_INCLUDE_ARCH_H
#define __ARCH_LOONGARCH_INCLUDE_ARCH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#  include <stddef.h>
#endif

#include <arch/barriers.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Types
 ****************************************************************************/

#if defined(CONFIG_ARCH_ADDRENV) && !defined(__ASSEMBLY__)
struct arch_addrenv_s
{
  uintptr_t root;       /* Physical address of the private page directory */
  uintptr_t textvbase;
  uintptr_t datavbase;
  uintptr_t heapvbase;
  size_t heapsize;
};

typedef struct arch_addrenv_s arch_addrenv_t;
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __ARCH_LOONGARCH_INCLUDE_ARCH_H */
