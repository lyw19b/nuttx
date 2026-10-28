/****************************************************************************
 * arch/loongarch/src/common/crt0.c
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

#include <stdlib.h>
#include <nuttx/addrenv.h>
#include <nuttx/arch.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int main(int argc, char *argv[]);
#ifdef CONFIG_BUILD_KERNEL
void up_signal_handler(_sa_sigaction_t handler, int signo,
                       siginfo_t *info, void *context);
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_HAVE_CXX
/* These bounds belong to this ELF, including in a relocatable flat build.
 * Hidden visibility lets the compiler address them without a runtime GOT.
 */

extern initializer_t _sctors[] visibility_hidden;
extern initializer_t _ectors[] visibility_hidden;
extern initializer_t _sdtors[] visibility_hidden;
extern initializer_t _edtors[] visibility_hidden;

/****************************************************************************
 * Name: user_destructors
 *
 * Description:
 *   Run finalizers in reverse registration order when exit processes the
 *   user program cleanup handlers. The linker script bounds this array.
 *
 ****************************************************************************/

static void user_destructors(void)
{
  initializer_t *entry;

  for (entry = _edtors; entry != _sdtors; )
    {
      (*--entry)();
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __start
 *
 * Description:
 *   Enter a loaded ELF with argc and argv prepared by the loader. Kernel
 *   builds also install a signal trampoline in the reserved user data page.
 *   Run optional constructors and call main. Normal return from main
 *   passes through libc exit so thread-local and registered cleanup runs.
 *   This function does not return to the kernel startup frame.
 *
 ****************************************************************************/

void __start(int argc, char *argv[])
{
#ifdef CONFIG_HAVE_CXX
  initializer_t *entry;
#endif
  int status;

#ifdef CONFIG_BUILD_KERNEL
  /* Only a kernel-build user address space has a reserved data page. */

  ARCH_DATA_RESERVE->ar_sigtramp = up_signal_handler;
#endif

#ifdef CONFIG_HAVE_CXX
  /* Register finalizers before invoking constructors that may call exit. */

#  if CONFIG_LIBC_MAX_EXITFUNS > 0
  atexit(user_destructors);
#  endif

  for (entry = _sctors; entry != _ectors; entry++)
    {
      (*entry)();
    }
#endif

  status = main(argc, argv);

#if defined(CONFIG_HAVE_CXX) && CONFIG_LIBC_MAX_EXITFUNS <= 0
  user_destructors();
#endif

  exit(status);
}
