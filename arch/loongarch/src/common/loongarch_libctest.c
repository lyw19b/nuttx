/****************************************************************************
 * arch/loongarch/src/common/loongarch_libctest.c
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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <nuttx/addrenv.h>
#include <nuttx/arch.h>

#include "loongarch_mmu.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Volatile function pointers prevent compiler builtins from replacing the
 * functions under test with inlined operations or folded constant results.
 */

static void *(*volatile g_copy)(void *, const void *, size_t) = memcpy;
static void *(*volatile g_move)(void *, const void *, size_t) = memmove;
static void *(*volatile g_fill)(void *, int, size_t) = memset;
static int (*volatile g_compare)(const void *, const void *, size_t) =
  memcmp;
static void *(*volatile g_find)(const void *, int, size_t) = memchr;
static size_t (*volatile g_length)(const char *) = strlen;
static size_t (*volatile g_bound_length)(const char *, size_t) = strnlen;
static int (*volatile g_string_compare)(const char *, const char *) = strcmp;
static int (*volatile g_bound_compare)(const char *, const char *, size_t) =
  strncmp;
static char *(*volatile g_string_copy)(char *, const char *) = strcpy;
static char *(*volatile g_first)(const char *, int) = strchr;
static char *(*volatile g_first_or_end)(const char *, int) = strchrnul;
static char *(*volatile g_last)(const char *, int) = strrchr;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: check_memory
 *
 * Description:
 *   Test every low-four-bit source/destination alignment around word and
 *   loop boundaries. Verify unchanged bytes around each transfer against
 *   independent byte loops, then check both overlap directions and identity.
 *
 ****************************************************************************/

static void check_memory(void)
{
  static const unsigned int lengths[] =
  {
    0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65,
    127, 128, 129, 255
  };

  uint8_t input[320] aligned_data(16);
  uint8_t output[320] aligned_data(16);
  uint8_t expected[320] aligned_data(16);
  unsigned int source;
  unsigned int dest;
  unsigned int index;
  unsigned int size;
  unsigned int i;

  for (i = 0; i < sizeof(input); i++)
    {
      input[i] = (uint8_t)(i * 37 + 11);
    }

  for (source = 0; source < 16; source++)
    {
      for (dest = 0; dest < 16; dest++)
        {
          for (index = 0; index < nitems(lengths); index++)
            {
              size = lengths[index];
              for (i = 0; i < sizeof(output); i++)
                {
                  output[i] = 0xa5;
                }

              ASSERT(g_copy(output + dest, input + source, size) ==
                     output + dest);
              for (i = 0; i < sizeof(output); i++)
                {
                  ASSERT(output[i] == (i >= dest && i < dest + size ?
                         input[source + i - dest] : 0xa5));
                }

              ASSERT(g_compare(output + dest, input + source, size) == 0);
              ASSERT(g_fill(output + dest, 0x1fe, size) == output + dest);
              for (i = 0; i < sizeof(output); i++)
                {
                  ASSERT(output[i] ==
                         (i >= dest && i < dest + size ? 0xfe : 0xa5));
                  output[i] = input[i];
                  expected[i] = input[i];
                }

              /* Compute expected overlap results from an untouched copy. */

              for (i = 0; i < size; i++)
                {
                  expected[dest + i] = input[source + i];
                }

              ASSERT(g_move(output + dest, output + source, size) ==
                     output + dest);
              for (i = 0; i < sizeof(output); i++)
                {
                  ASSERT(output[i] == expected[i]);
                }
            }
        }
    }

  input[0] = 0xff;
  output[0] = 1;
  ASSERT(g_compare(input, output, 1) > 0);
  ASSERT(g_compare(output, input, 1) < 0);
  ASSERT(g_find(input, 0x1ff, 1) == input);
  ASSERT(g_find(input, 0, 1) == NULL);
}

/****************************************************************************
 * Name: check_strings
 *
 * Description:
 *   Check terminators, character truncation, unsigned comparison and exact
 *   bounds for strings at every alignment. The destination's byte after
 *   the copied NUL must remain untouched.
 *
 ****************************************************************************/

static void check_strings(void)
{
  char input[288];
  char output[288];
  char *src;
  char *dst;
  unsigned int offset;
  unsigned int size;
  unsigned int i;

  for (offset = 0; offset < 16; offset++)
    {
      src = input + offset;
      dst = output + 15 - offset;
      for (size = 0; size < 256; size++)
        {
          for (i = 0; i < size; i++)
            {
              src[i] = (char)(0x80 + i % 127);
            }

          src[size] = '\0';
          dst[size + 1] = 0x55;
          ASSERT(g_length(src) == size);
          ASSERT(g_bound_length(src, size / 2) == size / 2);
          ASSERT(g_bound_length(src, size + 1) == size);
          ASSERT(g_string_copy(dst, src) == dst);
          ASSERT(dst[size + 1] == 0x55);
          ASSERT(g_string_compare(src, dst) == 0);
          ASSERT(g_bound_compare(src, dst, size + 1) == 0);
          ASSERT(g_first(src, 0) == src + size);
          ASSERT(g_first_or_end(src, 0) == src + size);
          ASSERT(g_last(src, 0) == src + size);
          ASSERT(g_first(src, 0x7f) == NULL);
          ASSERT(g_first_or_end(src, 0x7f) == src + size);
          ASSERT(g_last(src, 0x7f) == NULL);
          if (size != 0)
            {
              ASSERT(g_first(src, 0x180) == src);
              ASSERT(g_last(src, 0x80) == src + ((size - 1) / 127) * 127);
              dst[size - 1] = 1;
              ASSERT(g_string_compare(src, dst) > 0);
              ASSERT(g_string_compare(dst, src) < 0);
              ASSERT(g_bound_compare(src, dst, size - 1) == 0);
              ASSERT(g_bound_compare(src, dst, size) > 0);
            }
        }
    }
}

/****************************************************************************
 * Name: check_page_edges
 *
 * Description:
 *   End source and destination buffers immediately before unmapped pages.
 *   Verify that no memory routine crosses a bound and no string routine
 *   reads beyond the NUL. A separate address environment supplies actual
 *   guard pages, so any accidental overread becomes a kernel test failure.
 *
 ****************************************************************************/

static void check_page_edges(void)
{
  struct addrenv_s *env = addrenv_allocate();
  struct addrenv_s *old;
  char *src;
  char *dst;
  unsigned int size;
  unsigned int i;

  ASSERT(env != NULL);
  ASSERT(up_addrenv_create(LOONGARCH_PAGE_SIZE, LOONGARCH_PAGE_SIZE,
                           0, &env->addrenv) == OK);
  ASSERT(addrenv_select(env, &old) == OK);
  src = (char *)(env->addrenv.textvbase + LOONGARCH_PAGE_SIZE);
  dst = (char *)(env->addrenv.datavbase + LOONGARCH_PAGE_SIZE);

  /* Neither pointer itself is mapped. Empty ranges must perform no load. */

  ASSERT(g_copy(dst, src, 0) == dst);
  ASSERT(g_move(dst, src, 0) == dst);
  ASSERT(g_fill(dst, 0, 0) == dst);
  ASSERT(g_compare(src, dst, 0) == 0);
  ASSERT(g_find(src, 1, 0) == NULL);
  ASSERT(g_bound_length(src, 0) == 0);
  ASSERT(g_bound_compare(src, dst, 0) == 0);

  for (size = 1; size <= 128; size++)
    {
      for (i = 1; i <= size; i++)
        {
          *(src - i) = 'a';
        }

      ASSERT(g_copy(dst - size, src - size, size) == dst - size);
      ASSERT(g_compare(dst - size, src - size, size) == 0);
      ASSERT(g_find(src - size, 'z', size) == NULL);
      ASSERT(g_bound_length(src - size, size) == size);
      *(src - 1) = '\0';
      ASSERT(g_length(src - size) == size - 1);
      ASSERT(g_bound_length(src - size, size + 1) == size - 1);
      ASSERT(g_string_copy(dst - size, src - size) == dst - size);
      ASSERT(g_string_compare(dst - size, src - size) == 0);
      ASSERT(g_bound_compare(dst - size, src - size, size + 1) == 0);
      ASSERT(g_first(src - size, 0) == src - 1);
      ASSERT(g_first_or_end(src - size, 'z') == src - 1);
      ASSERT(g_last(src - size, 0) == src - 1);
      ASSERT(g_move(dst - size, src - size, size) == dst - size);
      ASSERT(g_fill(dst - size, 0, size) == dst - size);
    }

  ASSERT(addrenv_restore(old) == OK);
  addrenv_drop(env, false);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_libc_test
 *
 * Description:
 *   Run the LA64 scalar libc regression before user tasks start. These
 *   checks exercise the actual linked assembly functions in kernel mode;
 *   the following userspace regression also links and uses their PLV3 copy.
 *
 ****************************************************************************/

void loongarch_libc_test(void)
{
  check_memory();
  check_strings();
  check_page_edges();
  puts("LoongArch libc assembly tests: PASS");
}
