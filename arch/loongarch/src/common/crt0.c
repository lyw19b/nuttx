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
void up_signal_handler(_sa_sigaction_t handler, int signo,
                       siginfo_t *info, void *context);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_HAVE_CXX
extern initializer_t _sctors[];
extern initializer_t _ectors[];
extern initializer_t _sdtors[];
extern initializer_t _edtors[];

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
 *   Enter a statically linked user ELF at PLV3 with argc and argv prepared
 *   by the loader. Install its signal trampoline in the reserved user data
 *   page, run optional constructors and call main. Normal return from main
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

  ARCH_DATA_RESERVE->ar_sigtramp = up_signal_handler;

#ifdef CONFIG_HAVE_CXX
  /* Register finalizers before invoking constructors that may call exit. */

  atexit(user_destructors);

  for (entry = _sctors; entry != _ectors; entry++)
    {
      (*entry)();
    }
#endif

  status = main(argc, argv);
  exit(status);
}
