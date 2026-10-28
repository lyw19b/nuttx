/****************************************************************************
 * arch/loongarch/src/common/loongarch_user.ld
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

#include <nuttx/config.h>

OUTPUT_ARCH(loongarch)
ENTRY(__start)

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = CONFIG_ARCH_TEXT_VBASE;
  .text :
  {
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(8);
    _sctors = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    _ectors = .;
    _sdtors = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
    _edtors = .;
  } :text

  ASSERT(. <= CONFIG_ARCH_TEXT_VBASE +
         CONFIG_ARCH_TEXT_NPAGES * CONFIG_MM_PGSIZE, "User text is too large")

  /* Leave the first data page for the NuttX address-environment header.
   * Merge the statically resolved GOT into data: a separate .got section
   * would make this loader treat the fixed-address executable as PIC.
   */

  . = CONFIG_ARCH_DATA_VBASE + CONFIG_MM_PGSIZE;
  .data :
  {
    *(.data .data.*)
    *(.sdata .sdata.*)
    *(.got .got.*)
  } :data

  .bss :
  {
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
  } :data

  ASSERT(. <= CONFIG_ARCH_DATA_VBASE +
         CONFIG_ARCH_DATA_NPAGES * CONFIG_MM_PGSIZE, "User data is too large")

  /DISCARD/ :
  {
    *(.eh_frame*)
    *(.note*)
    *(.comment*)
  }
}
