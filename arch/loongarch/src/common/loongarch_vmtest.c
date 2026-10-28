/****************************************************************************
 * arch/loongarch/src/common/loongarch_vmtest.c
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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <nuttx/addrenv.h>
#include <nuttx/arch.h>
#include <nuttx/pgalloc.h>

#include "loongarch_mmu.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_vm_test
 *
 * Description:
 *   Exercise actual demand-zero and COW faults using selected environments.
 *   Check shared reads, private writes, sole-owner reuse, read-only leaves,
 *   checked copyout and complete-range validation. Restore the caller's
 *   environment and drop owned references; the scheduler retains any roots
 *   still selected on a CPU until its next address-environment switch.
 *
 ****************************************************************************/

void loongarch_vm_test(void)
{
  struct addrenv_s *parent = addrenv_allocate();
  struct addrenv_s *child = addrenv_allocate();
  struct addrenv_s *old;
  struct addrenv_s *saved;
  arch_addrenv_t temporary;
  arch_addrenv_t failed;
  volatile uintptr_t *data;
  volatile uintptr_t *lazy;
  uintptr_t shared;
  uintptr_t private;
  uintptr_t value = 0x5678;
  uint8_t untouched[2] =
  {
    0xa5, 0xa5
  };

  uintptr_t exhausted = 0;
  uintptr_t page;
  uint64_t before;
  unsigned int i;

  ASSERT(parent != NULL && child != NULL);
  ASSERT(up_addrenv_create(LOONGARCH_PAGE_SIZE, LOONGARCH_PAGE_SIZE,
                           0, &parent->addrenv) == OK);
  ASSERT(loongarch_mmu_maproot(parent->addrenv.root,
          parent->addrenv.heapvbase, 0, 2 * LOONGARCH_PAGE_SIZE,
          LOONGARCH_PTE_LAZY | LOONGARCH_PTE_USER | LOONGARCH_PTE_WRITE |
          LOONGARCH_PTE_CACHE | LOONGARCH_PTE_NOEXEC) == OK);
  ASSERT(up_addrenv_mprot(&parent->addrenv, parent->addrenv.textvbase,
                          LOONGARCH_PAGE_SIZE, PROT_READ | PROT_EXEC) == OK);
  ASSERT(addrenv_select(parent, &old) == OK);
  data = (volatile uintptr_t *)parent->addrenv.datavbase;
  lazy = (volatile uintptr_t *)parent->addrenv.heapvbase;
  *data = 0x1234;

  /* Reading an untouched reservation installs the immutable zero page.
   * A second untouched page remains lazy across the snapshot operation.
   */

  ASSERT(up_addrenv_find_page(&parent->addrenv, (uintptr_t)lazy) == 0);
  ASSERT(*lazy == 0);
  shared = up_addrenv_find_page(&parent->addrenv, (uintptr_t)lazy);
  ASSERT(shared != 0);
  ASSERT(loongarch_addrenv_cow(&parent->addrenv, &child->addrenv) == OK);
  ASSERT(up_addrenv_find_page(&child->addrenv, (uintptr_t)lazy) == shared);
  ASSERT(addrenv_select(child, &saved) == OK);
  ASSERT(*data == 0x1234 && *lazy == 0);
  *data = 0xabcd;
  *lazy = 0x9876;
  lazy[LOONGARCH_PAGE_SIZE / sizeof(*lazy)] = 0x4321;
  private = up_addrenv_find_page(&child->addrenv, (uintptr_t)data);
  ASSERT(private != up_addrenv_find_page(&parent->addrenv, (uintptr_t)data));
  ASSERT(up_addrenv_find_page(&child->addrenv, (uintptr_t)lazy) != shared);
  ASSERT(addrenv_restore(saved) == OK);
  ASSERT(*data == 0x1234 && *lazy == 0);
  ASSERT(lazy[LOONGARCH_PAGE_SIZE / sizeof(*lazy)] == 0);

  /* No second allocation is needed after the child has split away. */

  shared = up_addrenv_find_page(&parent->addrenv, (uintptr_t)data);
  *data = 0xbeef;
  ASSERT(up_addrenv_find_page(&parent->addrenv, (uintptr_t)data) == shared);

  /* Repeat snapshot/copyout/release to catch backing-page reference leaks.
   * Copyout must split COW just like a faulting user store.
   */

  for (i = 0; i < 64; i++)
    {
      ASSERT(loongarch_addrenv_cow(&parent->addrenv, &temporary) == OK);
      ASSERT(loongarch_copytouser((void *)data, &value,
                                  sizeof(value)) == OK);
      ASSERT(*(uintptr_t *)loongarch_virtaddr(
               up_addrenv_find_page(&temporary, (uintptr_t)data)) != value);
      ASSERT(up_addrenv_destroy(&temporary) == OK);
      *data = 0xbeef;
    }

  /* Reject invalid ranges before transferring any byte. The first byte
   * below is valid, but the rest extends beyond the mapped data page.
   */

  page = parent->addrenv.datavbase + LOONGARCH_PAGE_SIZE - 1;
  ASSERT(loongarch_copyfromuser(untouched, (const void *)page,
                                sizeof(untouched)) == -EFAULT);
  ASSERT(untouched[0] == 0xa5 && untouched[1] == 0xa5);
  ASSERT(loongarch_user_access(UINTPTR_MAX - 1, 4, PROT_READ) == -EFAULT);
  ASSERT(loongarch_user_access(0, 1, PROT_READ) == -EFAULT);
  ASSERT(loongarch_user_access(UINTPTR_MAX, 0, PROT_READ) == OK);
  ASSERT(loongarch_copytouser((void *)parent->addrenv.textvbase,
                              &value, sizeof(value)) == -EFAULT);
  ASSERT(loongarch_user_access(parent->addrenv.datavbase, 4,
                               PROT_EXEC) == -EFAULT);
  /* Exhaust backing storage after establishing a shared leaf. Failed COW
   * must retain the old mapping and reference. Failed root allocation must
   * leave a safely destroyable empty descriptor.
   */

  ASSERT(loongarch_addrenv_cow(&parent->addrenv, &temporary) == OK);
  before = *loongarch_mmu_pte(parent->addrenv.root, (uintptr_t)data, false);
  while ((page = mm_pgalloc(1)) != 0)
    {
      *(uintptr_t *)loongarch_virtaddr(page) = exhausted;
      exhausted = page;
    }

  ASSERT(loongarch_vm_fault(&parent->addrenv, (uintptr_t)data,
                            PROT_WRITE) == -ENOMEM);
  ASSERT(*loongarch_mmu_pte(parent->addrenv.root,
                            (uintptr_t)data, false) == before);
  ASSERT(loongarch_addrenv_cow(&parent->addrenv, &failed) == -ENOMEM);
  ASSERT(failed.root == 0 && failed.asid == NULL);

  ASSERT(up_addrenv_destroy(&temporary) == OK);
  while (exhausted != 0)
    {
      page = exhausted;
      exhausted = *(uintptr_t *)loongarch_virtaddr(page);
      mm_pgfree(page, 1);
    }

  ASSERT(addrenv_restore(old) == OK);
  addrenv_drop(child, false);
  addrenv_drop(parent, false);
  puts("LoongArch demand-zero/COW/user-copy tests: PASS");
}
