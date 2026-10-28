/****************************************************************************
 * arch/loongarch/include/elf.h
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

#ifndef __ARCH_LOONGARCH_INCLUDE_ELF_H
#define __ARCH_LOONGARCH_INCLUDE_ELF_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define R_LARCH_NONE               0
#define R_LARCH_32                 1
#define R_LARCH_64                 2
#define R_LARCH_RELATIVE           3
#define R_LARCH_COPY               4
#define R_LARCH_JUMP_SLOT          5
#define R_LARCH_TLS_DTPMOD32       6
#define R_LARCH_TLS_DTPMOD64       7
#define R_LARCH_TLS_DTPREL32       8
#define R_LARCH_TLS_DTPREL64       9
#define R_LARCH_TLS_TPREL32       10
#define R_LARCH_TLS_TPREL64       11

#define R_LARCH_32_PCREL          57

#define EM_ARCH                   EM_LOONGARCH
#define EF_LARCH_ABI_MODIFIER_MASK 0x07
#define EF_LARCH_ABI_DOUBLE_FLOAT  0x03
#define EF_LARCH_OBJABI_V1         0x40

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifndef __ASSEMBLY__

struct user_pt_regs
{
  uintreg_t regs[32];
};

#define ELF_NGREG (sizeof(struct user_pt_regs) / sizeof(uintreg_t))
typedef uintreg_t elf_gregset_t[ELF_NGREG];

#endif /* __ASSEMBLY__ */
#endif /* __ARCH_LOONGARCH_INCLUDE_ELF_H */
