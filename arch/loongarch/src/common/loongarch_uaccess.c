/****************************************************************************
 * arch/loongarch/src/common/loongarch_uaccess.c
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
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>
#include <nuttx/kmalloc.h>
#include <nuttx/sched.h>
#include <nuttx/tls.h>

#include "sched/sched.h"
#include "loongarch_mmu.h"
#include "loongarch_uaccess.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define USER_VECTOR_MAX 32
#define USER_COPY_IN    1
#define USER_COPY_OUT   2
#define USER_COPY_COUNT 4

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Each allocation belongs to the thread until the syscall returns or the
 * architecture releases its kernel stack. This also covers cancellation,
 * execve and exit, which need not return through the dispatch epilogue.
 */

struct user_buffer_s
{
  struct user_buffer_s *next;
  uintptr_t user;
  size_t size;
  unsigned int flags;
  uint64_t data[];
};

struct loongarch_uaccess_s
{
  struct loongarch_uaccess_s *previous;
  struct user_buffer_s *buffers;
  size_t total;
  uintptr_t args[6];
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: release_call
 *
 * Description:
 *   Release all kernel staging memory belonging to one syscall. The caller
 *   must unlink the context from the thread before freeing it.
 *
 ****************************************************************************/

static void release_call(struct loongarch_uaccess_s *call)
{
  struct user_buffer_s *buffer;

  while (call->buffers != NULL)
    {
      buffer = call->buffers;
      call->buffers = buffer->next;
      kmm_free(buffer);
    }

  kmm_free(call);
}

/****************************************************************************
 * Name: allocate_buffer
 *
 * Description:
 *   Allocate zeroed staging storage within the per-call limit. Zeroing
 *   prevents padding or partially written structures from leaking heap data.
 *
 ****************************************************************************/

static void *allocate_buffer(struct loongarch_uaccess_s *call, size_t size)
{
  struct user_buffer_s *buffer;

  if (size > CONFIG_LOONGARCH_USER_COPY_MAX - call->total)
    {
      return NULL;
    }

  buffer = kmm_zalloc(sizeof(*buffer) + size);
  if (buffer == NULL)
    {
      return NULL;
    }

  call->total += size;
  buffer->size = size;
  buffer->next = call->buffers;
  call->buffers = buffer;
  return buffer->data;
}

/****************************************************************************
 * Name: stage_buffer
 *
 * Description:
 *   Replace a user argument with a kernel buffer after checking its complete
 *   range. Input is copied once; output is copied back after the service.
 *   Optional null pointers retain their API-specific null semantics.
 *
 ****************************************************************************/

static int stage_buffer(struct loongarch_uaccess_s *call, unsigned int arg,
                        size_t size, unsigned int flags, bool optional)
{
  uintptr_t user = call->args[arg];
  void *data;
  int ret;

  if (optional && user == 0)
    {
      return OK;
    }

  if (size > CONFIG_LOONGARCH_USER_COPY_MAX - call->total)
    {
      return -E2BIG;
    }

  ret = loongarch_user_access(user, size,
          (flags & USER_COPY_OUT ? PROT_WRITE : 0) |
          (flags & USER_COPY_IN ? PROT_READ : 0));
  if (ret < 0)
    {
      return ret;
    }

  data = allocate_buffer(call, size);
  if (data == NULL)
    {
      return -ENOMEM;
    }

  call->buffers->user = user;
  call->buffers->flags = flags;
  if (flags & USER_COPY_IN)
    {
      ret = loongarch_copyfromuser(data, (const void *)user, size);
      if (ret < 0)
        {
          return ret;
        }
    }

  call->args[arg] = (uintptr_t)data;
  return OK;
}

/****************************************************************************
 * Name: copy_string
 *
 * Description:
 *   Snapshot a bounded NUL-terminated string without reading beyond its
 *   terminator into an unmapped page. The destination remains thread-owned
 *   even when validation of a later vector element fails.
 *
 ****************************************************************************/

static int copy_string(struct loongarch_uaccess_s *call, uintptr_t user,
                       char **result)
{
  char *data;
  size_t i;
  int ret;

  if (user == 0 || user >= LOONGARCH_MMU_VLIMIT)
    {
      return -EFAULT;
    }

  data = allocate_buffer(call, PATH_MAX + 1);
  if (data == NULL)
    {
      return -ENOMEM;
    }

  for (i = 0; i <= PATH_MAX; i++)
    {
      ret = loongarch_copyfromuser(data + i, (const void *)(user + i), 1);
      if (ret < 0)
        {
          return ret;
        }

      if (data[i] == '\0')
        {
          *result = data;
          return OK;
        }
    }

  return -ENAMETOOLONG;
}

/****************************************************************************
 * Name: stage_string / stage_vector
 *
 * Description:
 *   Snapshot pathname arguments or complete exec argument vectors. No user
 *   pointer from a nested vector is passed to the loader. Bound both the
 *   vector length and cumulative staging storage.
 *
 ****************************************************************************/

static int stage_string(struct loongarch_uaccess_s *call, unsigned int arg)
{
  char *data;
  int ret = copy_string(call, call->args[arg], &data);

  if (ret == OK)
    {
      call->args[arg] = (uintptr_t)data;
    }

  return ret;
}

static int stage_vector(struct loongarch_uaccess_s *call, unsigned int arg)
{
  uintptr_t user = call->args[arg];
  uintptr_t entry;
  char **vector;
  unsigned int i;
  int ret;

  if (user == 0)
    {
      return OK;
    }

  if (user >= LOONGARCH_MMU_VLIMIT ||
      (USER_VECTOR_MAX + 1) * sizeof(entry) > LOONGARCH_MMU_VLIMIT - user)
    {
      return -EFAULT;
    }

  vector = allocate_buffer(call, (USER_VECTOR_MAX + 1) * sizeof(*vector));
  if (vector == NULL)
    {
      return -ENOMEM;
    }

  for (i = 0; i <= USER_VECTOR_MAX; i++)
    {
      ret = loongarch_copyfromuser(&entry,
                                  (const void *)(user + i * sizeof(entry)),
                                  sizeof(entry));
      if (ret < 0)
        {
          return ret;
        }

      if (entry == 0)
        {
          call->args[arg] = (uintptr_t)vector;
          return OK;
        }

      if (i == USER_VECTOR_MAX)
        {
          return -E2BIG;
        }

      ret = copy_string(call, entry, &vector[i]);
      if (ret < 0)
        {
          return ret;
        }
    }

  return -E2BIG;
}

/****************************************************************************
 * Name: check_function
 *
 * Description:
 *   Validate a user callback as a complete aligned LoongArch instruction.
 *   Callers handle non-address sentinel values such as SIG_IGN separately.
 *
 ****************************************************************************/

static int check_function(uintptr_t addr)
{
  if (addr == 0 || (addr & 3) != 0)
    {
      return -EFAULT;
    }

  return loongarch_user_access(addr, sizeof(uint32_t), PROT_EXEC);
}

/****************************************************************************
 * Name: prepare_call
 *
 * Description:
 *   Describe every supported syscall explicitly. Unknown calls, retained
 *   user buffers and unadapted nested objects fail closed with ENOSYS.
 *   A successful policy leaves only scalar arguments, validated callbacks
 *   and kernel-owned snapshots for the generated stub.
 *
 ****************************************************************************/

static int prepare_call(struct loongarch_uaccess_s *call,
                        unsigned int number)
{
  uintptr_t *a = call->args;
  int ret;

#define INPUT(n, type, optional) \
  stage_buffer(call, n, sizeof(type), USER_COPY_IN, optional)
#define OUTPUT(n, type, optional) \
  stage_buffer(call, n, sizeof(type), USER_COPY_OUT, optional)
#define REQUIRE(expr) do { ret = (expr); if (ret < 0) return ret; } while (0)

  switch (number)
    {
      case SYS__exit:
      case SYS_getpid:
      case SYS_gettid:
      case SYS_sched_getcpu:
      case SYS_sched_yield:
      case SYS_sched_getscheduler:
      case SYS_sched_lockcount:
      case SYS_close:
      case SYS_dup:
      case SYS_dup2:
      case SYS_lseek:
      case SYS_fsync:
      case SYS_ftruncate:
      case SYS_fchmod:
      case SYS_fchown:
      case SYS_clock:
      case SYS_kill:
      case SYS_tgkill:
      case SYS_sigqueue:
      case SYS_pgalloc:
      case SYS_sync:
#ifdef CONFIG_SCHED_HAVE_PARENT
      case SYS_getppid:
#endif
#ifndef CONFIG_DISABLE_PTHREAD
      case SYS_nx_pthread_exit:
      case SYS_pthread_detach:
      case SYS_pthread_setschedprio:
#endif
        return OK;

      case SYS_read:
      case SYS_pread:
        return stage_buffer(call, 1, a[2], USER_COPY_OUT | USER_COPY_COUNT,
                            false);

      case SYS_write:
      case SYS_pwrite:
        return stage_buffer(call, 1, a[2], USER_COPY_IN, false);

      case SYS_open:
      case SYS_chmod:
      case SYS_chown:
      case SYS_lchmod:
      case SYS_lchown:
#ifndef CONFIG_DISABLE_MOUNTPOINT
      case SYS_unlink:
      case SYS_rmdir:
      case SYS_mkdir:
#endif
        return stage_string(call, 0);

      case SYS_rename:
#ifdef CONFIG_PSEUDOFS_SOFTLINKS
      case SYS_link:
      case SYS_symlink:
#endif
        REQUIRE(stage_string(call, 0));
        return stage_string(call, 1);

      case SYS_stat:
      case SYS_lstat:
        REQUIRE(stage_string(call, 0));
        return OUTPUT(1, struct stat, false);

      case SYS_fstat:
        return OUTPUT(1, struct stat, false);

      case SYS_statfs:
        REQUIRE(stage_string(call, 0));
        return OUTPUT(1, struct statfs, false);

      case SYS_fstatfs:
        return OUTPUT(1, struct statfs, false);

      case SYS_clock_gettime:
      case SYS_sched_rr_get_interval:
        return OUTPUT(1, struct timespec, false);

      case SYS_clock_settime:
        return INPUT(1, struct timespec, false);

      case SYS_nanosleep:
        REQUIRE(INPUT(0, struct timespec, false));
        return OUTPUT(1, struct timespec, true);

      case SYS_clock_nanosleep:
        REQUIRE(INPUT(2, struct timespec, false));
        return OUTPUT(3, struct timespec, true);

      case SYS_gettimeofday:
        REQUIRE(OUTPUT(0, struct timeval, true));
        return OUTPUT(1, struct timezone, true);

      case SYS_time:
        return OUTPUT(0, time_t, true);

      case SYS_gethostname:
        return stage_buffer(call, 0, a[1], USER_COPY_OUT, false);

      case SYS_sethostname:
        return stage_buffer(call, 0, a[1], USER_COPY_IN, false);

      case SYS_sched_getparam:
        return OUTPUT(1, struct sched_param, false);

      case SYS_sched_setparam:
        return INPUT(1, struct sched_param, false);

      case SYS_sched_setscheduler:
        return INPUT(2, struct sched_param, false);

#ifdef CONFIG_SMP
      case SYS_sched_getaffinity:
#ifndef CONFIG_DISABLE_PTHREAD
      case SYS_pthread_getaffinity_np:
#endif
        if (a[1] != sizeof(cpu_set_t))
          {
            return -EINVAL;
          }

        return OUTPUT(2, cpu_set_t, false);

      case SYS_sched_setaffinity:
#ifndef CONFIG_DISABLE_PTHREAD
      case SYS_pthread_setaffinity_np:
#endif
        if (a[1] != sizeof(cpu_set_t))
          {
            return -EINVAL;
          }

        return INPUT(2, cpu_set_t, false);
#endif

      case SYS_sigaction:
        REQUIRE(INPUT(1, struct sigaction, true));
        REQUIRE(OUTPUT(2, struct sigaction, true));
        if (a[1] != 0)
          {
            struct sigaction *action = (struct sigaction *)a[1];
            uintptr_t handler = (uintptr_t)action->sa_handler;

            if (handler != (uintptr_t)SIG_DFL &&
                handler != (uintptr_t)SIG_IGN)
              {
                REQUIRE(check_function(handler));
              }

#ifdef SA_RESTORER
            if (action->sa_flags & SA_RESTORER)
              {
                return -ENOSYS;
              }
#endif
          }

        return OK;

      case SYS_sigprocmask:
#ifndef CONFIG_DISABLE_PTHREAD
      case SYS_pthread_sigmask:
#endif
        REQUIRE(INPUT(1, sigset_t, true));
        return OUTPUT(2, sigset_t, true);

      case SYS_sigpending:
        return OUTPUT(0, sigset_t, false);

      case SYS_sigsuspend:
        return INPUT(0, sigset_t, false);

      case SYS_sigwaitinfo:
      case SYS_sigtimedwait:
        REQUIRE(INPUT(0, sigset_t, false));
        REQUIRE(OUTPUT(1, siginfo_t, true));
        return number == SYS_sigtimedwait ?
               INPUT(2, struct timespec, true) : OK;

#ifdef CONFIG_SCHED_WAITPID
      case SYS_waitpid:
        return OUTPUT(1, int, true);
#ifdef CONFIG_SCHED_HAVE_PARENT
      case SYS_wait:
        return OUTPUT(0, int, true);

      case SYS_waitid:
        return OUTPUT(2, siginfo_t, false);
#endif
#endif

      case SYS_nxsched_get_stackinfo:
        if (a[0] != 0 && a[0] != (uintptr_t)nxsched_gettid())
          {
            return -EPERM;
          }

        return OUTPUT(1, struct stackinfo_s, false);

#ifndef CONFIG_DISABLE_PTHREAD
      case SYS_nx_pthread_create:
        REQUIRE(check_function(a[0]));
        REQUIRE(check_function(a[3]));
        REQUIRE(OUTPUT(1, pthread_t, false));
        REQUIRE(INPUT(2, pthread_attr_t, true));
        if (a[2] != 0)
          {
            pthread_attr_t *attr = (pthread_attr_t *)a[2];

            /* User-supplied stacks need a lifetime pin across the new
             * thread. Reject them until that ownership protocol exists.
             */

            if (attr->stackaddr != NULL)
              {
                return -ENOSYS;
              }
          }

        return OK;

      case SYS_pthread_join:
        return OUTPUT(1, pthread_addr_t, true);

      case SYS_pthread_getschedparam:
        REQUIRE(OUTPUT(1, int, false));
        return OUTPUT(2, struct sched_param, false);

      case SYS_pthread_setschedparam:
        return INPUT(2, struct sched_param, false);
#endif

#if !defined(CONFIG_BINFMT_DISABLE) && defined(CONFIG_LIBC_EXECFUNCS)
      case SYS_execve:
        REQUIRE(stage_string(call, 0));
        REQUIRE(stage_vector(call, 1));
        return stage_vector(call, 2);
#endif

      default:
        return -ENOSYS;
    }

#undef INPUT
#undef OUTPUT
#undef REQUIRE
}

/****************************************************************************
 * Name: error_convention
 *
 * Description:
 *   Preserve each public syscall family's error convention. POSIX calls
 *   return minus one and update errno through a checked TLS copy; pthread
 *   calls return a positive errno and internal nx interfaces a negative one.
 *
 ****************************************************************************/

static int error_convention(unsigned int number)
{
  switch (number)
    {
      case SYS_clock_nanosleep:
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_nx_pthread_create:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_cancel:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_cond_broadcast:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_cond_clockwait:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_cond_signal:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_cond_wait:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_detach:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD) && defined(CONFIG_SMP)
      case SYS_pthread_getaffinity_np:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_getschedparam:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_join:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD) && !defined(CONFIG_PTHREAD_MUTEX_UNSAFE)
      case SYS_pthread_mutex_consistent:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_mutex_destroy:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_mutex_init:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_mutex_timedlock:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_mutex_trylock:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_mutex_unlock:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD) && defined(CONFIG_SMP)
      case SYS_pthread_setaffinity_np:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_setschedparam:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_setschedprio:
#endif
#if !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_sigmask:
#endif
        return 1;

      case SYS_nxsched_get_stackinfo:
      case SYS_nxsem_clockwait:
#if defined(CONFIG_FS_NAMED_SEMAPHORES)
      case SYS_nxsem_close:
#endif
      case SYS_nxsem_destroy:
#if defined(CONFIG_PRIORITY_PROTECT)
      case SYS_nxsem_getprioceiling:
#endif
#if defined(CONFIG_FS_NAMED_SEMAPHORES)
      case SYS_nxsem_open:
#endif
      case SYS_nxsem_post:
#if defined(CONFIG_PRIORITY_INHERITANCE)
      case SYS_nxsem_set_protocol:
#endif
#if defined(CONFIG_PRIORITY_PROTECT)
      case SYS_nxsem_setprioceiling:
#endif
      case SYS_nxsem_timedwait:
      case SYS_nxsem_trywait:
#if defined(CONFIG_FS_NAMED_SEMAPHORES)
      case SYS_nxsem_unlink:
#endif
      case SYS_nxsem_wait:
        return -1;

      default:
        return 0;
    }
}

/****************************************************************************
 * Name: user_error
 *
 * Description:
 *   Encode a validation error using the service's error convention. Update
 *   POSIX errno through a checked copy instead of trusting a user pointer.
 *
 ****************************************************************************/

static uintptr_t user_error(unsigned int number, int error)
{
  struct tcb_s *tcb = this_task();
  uintptr_t address;
  int convention = error_convention(number);

  if (convention != 0)
    {
      return convention * error;
    }

  address = (uintptr_t)tcb->stack_alloc_ptr +
            offsetof(struct tls_info_s, tl_errno);
  loongarch_copytouser((void *)address, &error, sizeof(error));
  return (uintptr_t)-1;
}

/****************************************************************************
 * Name: user_semaphore
 *
 * Description:
 *   Support uncontended user semaphore counters without following user
 *   wait-list or holder pointers. Blocking queues and priority protocols
 *   need a kernel-owned object adapter and fail with ENOSYS for now. A
 *   checked physical-alias compare/exchange keeps COW and count updates
 *   serialized. Limit retries against a concurrently changing user counter.
 *
 ****************************************************************************/

static int user_semaphore(unsigned int number, uintptr_t addr)
{
  int32_t expected;
  int32_t desired;
  uint8_t flags;
  unsigned int retry;
  int ret;

  if (addr & (sizeof(int32_t) - 1))
    {
      return -EINVAL;
    }

  ret = loongarch_user_access(addr, sizeof(sem_t), PROT_READ | PROT_WRITE);
  if (ret < 0)
    {
      return ret;
    }

  ret = loongarch_copyfromuser(&flags,
          (const void *)(addr + offsetof(sem_t, flags)), sizeof(flags));
  if (ret < 0)
    {
      return ret;
    }

  if (flags & ~SEM_TYPE_MUTEX)
    {
      return -ENOSYS;
    }

  for (retry = 0; retry < 64; retry++)
    {
      ret = loongarch_copyfromuser(&expected, (const void *)addr,
                                  sizeof(expected));
      if (ret < 0)
        {
          return ret;
        }

      if (expected < 0)
        {
          return -ENOSYS;
        }

      if (number == SYS_nxsem_destroy)
        {
          return OK;
        }

      if (number == SYS_nxsem_post)
        {
          if (expected == SEM_VALUE_MAX)
            {
              return -EOVERFLOW;
            }

          desired = expected + 1;
        }
      else
        {
          if (expected == 0)
            {
              return number == SYS_nxsem_trywait ? -EAGAIN : -ENOSYS;
            }

          desired = expected - 1;
        }

      ret = loongarch_user_cmpxchg(addr, expected, desired);
      if (ret != -EAGAIN)
        {
          return ret;
        }
    }

  return -EAGAIN;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: loongarch_user_cleanup
 *
 * Description:
 *   Release abandoned syscall staging buffers after a thread has stopped.
 *   The architecture kernel-stack release hook owns this final cleanup.
 *
 ****************************************************************************/

void loongarch_user_cleanup(struct tcb_s *tcb)
{
  struct loongarch_uaccess_s *call;

  while (tcb->xcp.uaccess != NULL)
    {
      call = tcb->xcp.uaccess;
      tcb->xcp.uaccess = call->previous;
      release_call(call);
    }
}

/****************************************************************************
 * Name: loongarch_user_syscall
 *
 * Description:
 *   Stage user arguments, invoke one permitted generated stub and copy its
 *   results back through checked mappings. Called on the kernel thread
 *   stack with interrupts enabled and signals deferred. Cleanup is attached
 *   to the thread before any operation that can block or terminate it.
 *
 ****************************************************************************/

uintptr_t loongarch_user_syscall(unsigned int number, const uintptr_t *args)
{
  typedef uintptr_t (*stub_t)(unsigned int, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t, uintptr_t, uintptr_t);
  struct tcb_s *tcb = this_task();
  struct loongarch_uaccess_s *call;
  struct user_buffer_s *buffer;
  uintptr_t result;
  uintptr_t *a;
  size_t size;
  stub_t stub;
  int ret;

  /* These user objects cannot be passed to the native kernel semaphore
   * implementation: their embedded queue links are user-writable.
   */

  if (number == SYS_nxsem_wait || number == SYS_nxsem_trywait ||
      number == SYS_nxsem_post || number == SYS_nxsem_destroy)
    {
      return user_semaphore(number, args[0]);
    }

  call = kmm_zalloc(sizeof(*call));
  if (call == NULL)
    {
      return user_error(number, ENOMEM);
    }

  call->previous = tcb->xcp.uaccess;
  tcb->xcp.uaccess = call;
  memcpy(call->args, args, sizeof(call->args));
  ret = prepare_call(call, number);
  if (ret < 0)
    {
      result = user_error(number, -ret);
      goto out;
    }

  /* Native POSIX services can write TLS errno. Check that location before
   * entering them, including a COW split if this stack is shared.
   */

  ret = loongarch_user_access((uintptr_t)tcb->stack_alloc_ptr +
          offsetof(struct tls_info_s, tl_errno), sizeof(int), PROT_WRITE);
  if (ret < 0)
    {
      result = user_error(number, -ret);
      goto out;
    }

  a = call->args;
  stub = (stub_t)g_stublookup[number - CONFIG_SYS_RESERVED];
  result = stub(number - CONFIG_SYS_RESERVED,
                a[0], a[1], a[2], a[3], a[4], a[5]);

  for (buffer = call->buffers; buffer != NULL; buffer = buffer->next)
    {
      if (!(buffer->flags & USER_COPY_OUT))
        {
          continue;
        }

      /* A failed sleep may still supply the remaining interval. Other
       * failed services do not publish their temporary output buffers.
       */

      if (((intptr_t)result < 0 && number != SYS_nanosleep) ||
          (error_convention(number) > 0 && result != 0 &&
           number != SYS_clock_nanosleep))
        {
          continue;
        }

      size = buffer->size;
      if (buffer->flags & USER_COPY_COUNT)
        {
          if (result > size)
            {
              result = user_error(number, EIO);
              break;
            }

          size = result;
        }

      ret = loongarch_copytouser((void *)buffer->user, buffer->data, size);
      if (ret < 0)
        {
          result = user_error(number, -ret);
          break;
        }
    }

out:
  tcb->xcp.uaccess = call->previous;
  release_call(call);
  return result;
}
