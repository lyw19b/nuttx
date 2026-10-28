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

#include <debug.h>
#include <errno.h>
#include <string.h>
#include <nuttx/elf.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_signed_range
 *
 * Description:
 *   Check a two's-complement displacement without signed overflow.
 *   The unsigned subtraction used to form value also handles high addresses.
 *
 ****************************************************************************/

static bool loongarch_signed_range(uint64_t value, unsigned int bits)
{
  uint64_t limit = UINT64_C(1) << (bits - 1);

  return value + limit < (limit << 1);
}

/****************************************************************************
 * Name: loongarch_elf_addsub
 *
 * Description:
 *   Update an in-place little-endian field modulo its width. Unwind
 *   records can be unaligned, so access bytes rather than typed pointers.
 *
 ****************************************************************************/

static void loongarch_elf_addsub(uintptr_t addr, uint64_t value,
                                unsigned int size, bool subtract)
{
  uint8_t *field = (uint8_t *)addr;
  uint64_t old = 0;
  unsigned int i;

  for (i = 0; i < size; i++)
    {
      old |= (uint64_t)field[i] << (8 * i);
    }

  value = subtract ? old - value : old + value;

  for (i = 0; i < size; i++)
    {
      field[i] = value >> (8 * i);
    }
}

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
 *   Relocate ABI version-one data and normal-code-model instructions. Branch
 *   displacements are checked before modifying their split immediate fields.
 *   Unsupported GOT, TLS and legacy stack-machine relocations fail.
 *
 ****************************************************************************/

int up_relocateadd(const Elf_Rela *rel, const Elf_Sym *sym, uintptr_t addr,
                   void *arch_data)
{
  unsigned int type = ELF_R_TYPE(rel->r_info);
  uint64_t value;
  uint64_t offset;
  uint32_t insn;
  uint32_t mask;
  uint32_t field;
  unsigned int bits;
  unsigned int size;

  /* Relaxation markers do not change instructions at load time. */

  if (type == R_LARCH_NONE || type == R_LARCH_RELAX)
    {
      return OK;
    }

  if (sym == NULL)
    {
      return -EINVAL;
    }

  value = sym->st_value + rel->r_addend;
  offset = value - addr;

  /* Data relocations can target unaligned fields, including .eh_frame. */

  switch (type)
    {
      case R_LARCH_32:
        if (value > UINT32_MAX)
          {
            return -ERANGE;
          }

        field = value;
        memcpy((void *)addr, &field, sizeof(field));
        return OK;

      case R_LARCH_64:
        memcpy((void *)addr, &value, sizeof(value));
        return OK;

      case R_LARCH_32_PCREL:
        if (!loongarch_signed_range(offset, 32))
          {
            return -ERANGE;
          }

        field = offset;
        memcpy((void *)addr, &field, sizeof(field));
        return OK;

      case R_LARCH_ADD8:
      case R_LARCH_ADD16:
      case R_LARCH_ADD24:
      case R_LARCH_ADD32:
      case R_LARCH_ADD64:
        size = type == R_LARCH_ADD64 ? 8 : type - R_LARCH_ADD8 + 1;
        loongarch_elf_addsub(addr, value, size, false);
        return OK;

      case R_LARCH_SUB8:
      case R_LARCH_SUB16:
      case R_LARCH_SUB24:
      case R_LARCH_SUB32:
      case R_LARCH_SUB64:
        size = type == R_LARCH_SUB64 ? 8 : type - R_LARCH_SUB8 + 1;
        loongarch_elf_addsub(addr, value, size, true);
        return OK;

      case R_LARCH_ADD6:
      case R_LARCH_SUB6:
        {
          uint8_t *byte = (uint8_t *)addr;
          uint8_t low = type == R_LARCH_ADD6 ? *byte + value : *byte - value;

          *byte = (*byte & 0xc0) | (low & 0x3f);
        }

        return OK;
    }

  /* Instructions are always four bytes wide and four-byte aligned. */

  if ((addr & 3) != 0)
    {
      return -EINVAL;
    }

  switch (type)
    {
      case R_LARCH_B16:
      case R_LARCH_B21:
      case R_LARCH_B26:
        bits = type == R_LARCH_B16 ? 18 :
               type == R_LARCH_B21 ? 23 : 28;
        if ((offset & 3) != 0 || !loongarch_signed_range(offset, bits))
          {
            return -ERANGE;
          }

        mask = 0x03fffc00;
        field = ((offset >> 2) & 0xffff) << 10;

        if (type != R_LARCH_B16)
          {
            uint32_t highmask = type == R_LARCH_B21 ? 0x1f : 0x3ff;

            mask |= highmask;
            field |= (offset >> 18) & highmask;
          }

        break;

      case R_LARCH_PCALA_HI20:
        /* addi.d sign-extends the low twelve bits. Round the target page
         * first, then subtract the page of the pcalau12i instruction.
         */

        offset = ((value + 0x800) & ~UINT64_C(0xfff)) -
                 (addr & ~UINT64_C(0xfff));
        if (!loongarch_signed_range(offset, 32))
          {
            return -ERANGE;
          }

        mask = 0x01ffffe0;
        field = ((offset >> 12) & 0xfffff) << 5;
        break;

      case R_LARCH_ABS_HI20:
        mask = 0x01ffffe0;
        field = ((value >> 12) & 0xfffff) << 5;
        break;

      case R_LARCH_ABS64_LO20:
        mask = 0x01ffffe0;
        field = ((value >> 32) & 0xfffff) << 5;
        break;

      case R_LARCH_PCALA_LO12:
      case R_LARCH_ABS_LO12:
        mask = 0x003ffc00;
        field = (value & 0xfff) << 10;
        break;

      case R_LARCH_ABS64_HI12:
        mask = 0x003ffc00;
        field = ((value >> 52) & 0xfff) << 10;
        break;

      case R_LARCH_PCREL20_S2:
        if ((offset & 3) != 0 || !loongarch_signed_range(offset, 22))
          {
            return -ERANGE;
          }

        mask = 0x01ffffe0;
        field = ((offset >> 2) & 0xfffff) << 5;
        break;

      default:
        berr("ERROR: Unsupported LoongArch relocation %u\n", type);
        return -ENOSYS;
    }

  /* Preserve the opcode and register operands outside the immediate. */

  memcpy(&insn, (const void *)addr, sizeof(insn));
  insn = (insn & ~mask) | field;
  memcpy((void *)addr, &insn, sizeof(insn));
  return OK;
}
