/****************************************************************************
 * libs/libc/machine/loongarch/arch_elf.c
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

#include <errno.h>
#include <string.h>
#include <nuttx/elf.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_checkarch
 *
 * Description:
 *   Accept little-endian ELF64 LoongArch objects using the version-one
 *   LP64D ABI and an instruction-aligned entry address.
 *
 ****************************************************************************/

bool up_checkarch(const Elf_Ehdr *ehdr)
{
  return ehdr->e_machine == EM_LOONGARCH &&
         ehdr->e_ident[EI_CLASS] == ELFCLASS64 &&
         ehdr->e_ident[EI_DATA] == ELFDATA2LSB &&
         (ehdr->e_entry & 3) == 0 &&
         (ehdr->e_flags & 0xff) ==
         (EF_LARCH_OBJABI_V1 | EF_LARCH_ABI_DOUBLE_FLOAT);
}

/****************************************************************************
 * Name: up_relocate
 *
 * Description:
 *   Reject implicit-addend relocations with -ENOSYS. LoongArch ELF uses
 *   explicit-addend RELA records.
 *
 ****************************************************************************/

int up_relocate(const Elf_Rel *rel, const Elf_Sym *sym, uintptr_t addr,
                void *arch_data)
{
  /* LoongArch uses explicit-addend relocations. */

  return -ENOSYS;
}

/****************************************************************************
 * Name: up_relocateadd
 *
 * Description:
 *   Apply supported absolute 32-bit or 64-bit RELA relocations. Reject
 *   missing symbols, 32-bit overflow and unsupported relocation types
 *   instead of silently generating incorrect code.
 *
 ****************************************************************************/

int up_relocateadd(const Elf_Rela *rel, const Elf_Sym *sym, uintptr_t addr,
                   void *arch_data)
{
  uint64_t value;

  if (ELF_R_TYPE(rel->r_info) == R_LARCH_NONE)
    {
      return OK;
    }

  if (sym == NULL)
    {
      return -EINVAL;
    }

  value = sym->st_value + rel->r_addend;
  switch (ELF_R_TYPE(rel->r_info))
    {
      case R_LARCH_32:
        {
          uint32_t value32 = value;

          if (value > UINT32_MAX)
            {
              return -ERANGE;
            }

          memcpy((void *)addr, &value32, sizeof(value32));
        }

        return OK;

      case R_LARCH_64:
        memcpy((void *)addr, &value, sizeof(value));
        return OK;

      default:
        return -ENOSYS;
    }
}
