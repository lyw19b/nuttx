=========
LoongArch
=========

The LA64 port supports the Loongson LS2K0300 and the QEMU ``virt`` machine.
It uses the LP64D ABI and saves all general-purpose and floating-point
registers, FCSR, and condition flags across exceptions and task switches.
LSX and LASX are not enabled.

The initial port was migrated from the LoongArch changes ending at
``dd86df106de`` in the supplied NuttX repository. The RT-Thread port was
consulted for platform behavior; its MMU and SMP implementation is not
included here. Scheduler, page allocation, locking, and SMP callbacks use
NuttX 13.1.0 interfaces. Use the matching ``nuttx-13.1.0`` apps tag.

Memory management
=================

``CONFIG_ARCH_USE_MMU`` enables four-level page tables with 4 KiB pages
and 512 entries per level. Flat builds use the boot page directory;
``CONFIG_ARCH_ADDRENV`` gives each process a private root and owned text,
data and heap pages. Kernel RAM and MMIO remain in PLV0-only cached and
uncached direct mapping windows. User mappings occupy the lower canonical
address range.

``CONFIG_LOONGARCH_PAGE_POOL_SIZE`` reserves page-aligned physical memory
at the top of configured RAM. NuttX's ``mm_pgalloc`` and ``mm_pgfree`` own
this pool. The ordinary heap excludes the page pool and all CPU idle
stacks. Page-table allocation uses this allocator as well.

The architecture interface is declared in
``arch/loongarch/src/common/loongarch_mmu.h``:

* ``loongarch_mmu_map`` maps a page-aligned physical range. It rejects
  overlaps and rolls back partially allocated page tables on failure.
* ``loongarch_mmu_unmap`` removes mappings and reclaims empty tables.
* ``loongarch_mmu_translate`` returns a physical address including the
  offset within its page, or ``-ENOENT`` for an absent mapping.

These operations are for thread context with interrupts enabled. They
serialize through a sleepable mutex. They do not allocate or free mapped
data pages; the caller retains ownership and may free them after unmap
returns. Do not access a mapping concurrently with its removal unless a
higher-level lifetime protocol prevents accesses after unmap completes.

Software refill walks physical page tables without using the interrupted
stack and fills both halves of a TLB pair. Invalid entries lead to normal
page exceptions. Empty directory entries refer to immutable empty tables,
so hardware walkers never follow an absent directory to physical address
zero.

``CONFIG_LOONGARCH_MMU_PTW`` enables hardware walking only when CPUCFG2
reports HPTW support. The same page tables work with both refill modes;
unsupported CPUs fall back to software refill. On SMP, every mapping
change waits for NuttX SMP callbacks to invalidate all online CPUs' TLBs
before detached tables can be reused.

Address environments use per-CPU ASIDs. Each CPU allocates identifiers in
a software generation, retaining translations when switching back to an
environment in that generation. Exhausting the identifiers flushes the
local TLB before advancing the generation and reusing any ASID. Migration
uses the destination CPU's context; descriptor clones share the same ASID
storage. ASID zero is reserved for the boot root. An implementation with
no ASID bits falls back to flushing on every environment switch.

``CONFIG_LOONGARCH_ASID_BITS`` limits the usable ASID width, further bounded
by the CPU's ASIDBITS field. The normal default is ten. Page-table changes
still invalidate every online CPU's TLB synchronously, including retained
translations belonging to inactive environments. Final environment
destruction releases its ASID context storage after all CPU references are
dropped; retired identifiers remain unavailable until local rollover.

Demand-zero and copy-on-write
=============================

``CONFIG_LOONGARCH_DEMAND_PAGING`` reserves heap extensions without backing
storage. A read fault maps an immutable zero page; a write fault allocates
and clears a private page. ``CONFIG_LOONGARCH_INITIAL_HEAP_SIZE`` keeps a
page-aligned bootstrap heap resident (64 KiB by default) for the ELF
loader to initialize allocator metadata and the initial user stack.
Text and data loading remain eager. This is anonymous demand-zero paging;
file-backed faults, swapping and reclaim policies are not implemented.

``loongarch_addrenv_cow`` creates an independently owned root with counted
backing-page references. Writable shared leaves clear both D and W, making
software refill and hardware PTW trap on writes. The fault path copies a
shared page, or restores write permission when only one owner remains.
Permission updates finish synchronous SMP shootdown before releasing old
storage. Allocation failure leaves the original mapping usable. Callers
must quiesce source writers when they need an atomic whole-process snapshot.
This interface does not create a task group or implement POSIX ``fork``;
NuttX's ``up_addrenv_clone`` keeps its existing descriptor-sharing semantics.

Page exceptions run on the interrupted thread's kernel stack, before IRQ
context is entered. Allocation requires interrupts enabled; faults in IRQ
handlers or IRQ-masked kernel code remain fatal. User permission violations
continue through the normal process-termination path.

User processes and system calls
===============================

``CONFIG_BUILD_KERNEL`` enables PLV3 execution with independent address
environments. ``loongarch_addrenv.c`` implements the NuttX environment
lifecycle, page lookup, heap page allocation and permission changes.
NuttX owns environment reference counts, including references held by
CPUs. Final destruction releases the owned backing pages and private
page tables; immutable empty tables remain shared.

The supplied kernel configurations reserve these virtual regions:

===================== ======================== ===========================
Region                Base                     Size
===================== ======================== ===========================
Text                  ``0x40000000``           1 MiB
Reserved header/data  ``0x40200000``           1 MiB including header
Heap/user stacks      ``0x40400000``           1 MiB
===================== ======================== ===========================

The first data page contains the NuttX address-environment header, so
application data starts at ``0x40201000``. ELF loading initially permits
text writes, then applies RX permissions. Data, heaps and user stacks are
RW and nonexecutable. The configured regions must be page aligned,
nonoverlapping and below the lower canonical address limit.

Each user thread also has a separate kernel stack. Exception entry checks
the saved privilege level and switches from an untrusted user SP through
CSR SAVE3 before storing registers. Ordinary IRQ handlers use the per-CPU
interrupt stack. User syscalls execute on the thread kernel stack with
interrupts enabled, allowing blocking and CPU migration.

``loongarch_syscall.c`` validates the syscall number before accessing
NuttX's generated stub table. Reserved context-switch and assertion
operations require PLV0. User signal delivery preserves a kernel
continuation, builds a user register frame and validates the destination
signal stack. ``arch_signal.S`` calls the user handler and returns through
the reserved signal-return operation. Task and pthread entry are provided
by ``loongarch_user.c``.

``crt0.c`` installs the signal trampoline and calls ``main(argc, argv)``;
normal return passes through libc ``exit``. The shared linker script,
``arch/loongarch/src/common/loongarch_user.ld``, describes a statically
linked LP64D ELF executable at the configured addresses. It merges a
statically resolved GOT into data because this loader otherwise interprets
a separate ``.got`` section as a PIC image. Make's ``export`` target exports
``crt0.o`` and a preprocessed ``scripts/gnu-elf.ld`` for user applications.
The QEMU regression links the same startup against NuttX libc, libmm and
the generated syscall proxies.

Current scope and limitations:

* ELF execution is tested with statically linked ``ET_EXEC`` files using
  the version-one LP64D ABI. Only ``R_LARCH_NONE``, ``R_LARCH_32`` and
  ``R_LARCH_64`` relocations are implemented; other relocation types fail
  explicitly. Dynamic linking and arbitrary relocatable modules are not
  supported by this implementation.
* A user CPU fault terminates that process through ``_exit(SIGSEGV)`` on
  its kernel stack. The test checks the resulting exit status; this is not
  POSIX signal-termination status or recoverable fault-signal delivery.
* ``CONFIG_LOONGARCH_USER_ACCESS`` defaults to strict syscall policies.
  Supported buffers, fixed structures, strings and exec vectors are copied
  into bounded kernel-owned storage before dispatch. Output uses checked
  physical aliases and resolves COW while holding the VM mutex. Unknown
  interfaces return ENOSYS; there is no unchecked fallback in strict mode.
  ``CONFIG_LOONGARCH_USER_COPY_MAX`` bounds staging bytes (64 KiB by default),
  paths use PATH_MAX, and exec vectors are limited to 32 strings each.
* Unadapted interfaces include device ioctls, asynchronous I/O, retained
  buffers, general nested objects, user-supplied pthread stacks and blocking
  user synchronization queues. The 13.1 libc handles uncontended semaphore
  and mutex operations in userspace. Checked slow-path counting operations
  never follow user wait-list pointers; mutex slow paths and contention
  requiring queues return ENOSYS. Disabling strict checking bypasses these
  argument policies.
* This architecture-only validation does not redesign NuttX's user heap,
  TLS or kernel services that internally use user-resident allocator and
  synchronization metadata. It is not a complete malicious-process security
  boundary. Staging storage is attached to the architecture thread context
  and released on return or kernel-stack destruction, including exec/exit.
* Kernel-build process ``fork``, shared-memory page ownership and CPU hotplug
  are not implemented. Constructor startup
  hooks are present; C++ application and compiler TLS support have not been
  validated in the user-process configuration.
* QEMU results do not validate noncoherent caches. Additional cache
  maintenance may be needed for another physical platform.

C library assembly
==================

``libs/libc/machine/loongarch/gnu`` contains the LP64D setjmp/longjmp and
signal-return implementations, plus scalar memory and string routines.
``CONFIG_LOONGARCH_STRING_FUNCTION`` selects memcpy, memmove, memset,
memcmp, memchr, strlen, strnlen, strcmp, strncmp, strcpy, strchr, strchrnul
and strrchr. Individual ``CONFIG_LOONGARCH_<FUNCTION>`` options can select
one replacement without the others. Both Make and CMake honor the common
``LIBC_BUILD_*`` guards and ``ARCH_LIBCFUN`` symbol naming.

Copy and fill use aligned doubleword operations when possible. Other paths
use byte accesses, including overlapping memmove and string searches, to
avoid unaligned faults and accesses past bounds or terminators. They use
caller-saved integer registers and require neither LSX nor LASX. These are
functional scalar implementations; no throughput claim is made.

``CONFIG_LOONGARCH_LIBC_TEST`` adds an optional startup regression to the
QEMU user-test configuration. It invokes actual linked functions through
volatile pointers to prevent compiler builtin substitution, checks every
source/destination alignment modulo 16, both overlap directions, short and
word-boundary lengths, unsigned comparisons and character truncation. It
also places ranges and NUL terminators immediately before unmapped pages.
The full function suite and following userspace regression passed on one
and four QEMU CPUs with software refill and hardware PTW. A Make four-CPU
image also passed with PTW.
Success prints ``LoongArch libc assembly tests: PASS``. The subsequent user
ELF links and exercises the same routines at PLV3.

Interrupts and SMP
==================

``ECFG.VS=5`` selects separate 128-byte vector slots for each exception
and each CPU interrupt source. Slots pass their IRQ number directly to
the common context-saving code; they do not scan ESTAT to choose a cause.
Device interrupt controllers are still demultiplexed below their CPU input.
TLB refill has its own physical entry and dedicated scratch CSRs.

Each CPU has a separate idle stack, interrupt stack, and interrupt-context
flag. C interrupt handlers execute on the interrupt stack so that a task
may migrate after the scheduler releases it. Nested interrupts are not
supported. Secondary CPUs use IOCSR mailbox zero and startup IPIs, followed
by per-CPU vector/MMU initialization. Scheduling and remote calls share
the NuttX SMP IPI handler. CPU IDs must be contiguous from zero; CPU
hotplug is not implemented. The single-core LS2K0300 does not offer SMP;
use QEMU to exercise multiple CPUs.

Building and testing with QEMU
=============================

Use a ``loongarch64-linux-gnu-`` toolchain and an apps checkout compatible
with this NuttX revision: use the official ``nuttx-13.1.0`` tag in both
repositories.
``CROSSDEV`` can override the Make toolchain prefix. CMake accepts
``TOOLCHAIN_PREFIX``.

Available configurations under ``boards/loongarch/qemu/qemu-loongarch``:

============== ==========================================================
Configuration  Purpose
============== ==========================================================
``nsh``        Single-core interactive NSH
``smp``        Four-core interactive NSH
``apps``       Single-core NSH with libc, heap, pipe and OS tests
``smp-apps``   Four-core version of the apps regression configuration
``mmu``        Single-core MMU and context regression tests
``smp-mmu``    Four-core MMU, context and TLB shootdown regression tests
``kernel``     Single-core ELF, syscall and user-permission regression
``smp-kernel`` Four-core ELF, syscall and process-migration regression
============== ==========================================================

For example, with apps in ``../apps``::

  ./tools/configure.sh -a ../apps qemu-loongarch:smp-mmu
  make -j8
  python3 tools/loongarch_qemu_test.py --cpus 4

The configure script requires the usual NuttX Kconfig host utilities.
Alternatively, use CMake from a clean source tree::

  cmake -S . -B build -GNinja -DBOARD_CONFIG=qemu-loongarch:mmu \
    -DNUTTX_APPS_DIR=../apps
  cmake --build build
  python3 tools/loongarch_qemu_test.py --kernel build/nuttx

Interactive execution::

  qemu-system-loongarch64 -M virt -cpu la464 -m 256M -smp 4 \
    -nographic -monitor none -kernel nuttx

The ELF image loads at physical address ``0x00200000`` and enters the
cached window before starting C code. The configuration uses 128 MiB of
RAM starting at that address. UART0 is the QEMU 16550 device at
``0x1fe001e0``; its PCH-PIC input 2 is routed through EIOINTC to CPU0.
Only CPU0 advances the global system tick.

To test hardware PTW, use a QEMU build that implements ``la464,ptw=on``::

  python3 tools/loongarch_qemu_test.py --cpus 4 --ptw \
    --qemu /path/to/ptw-capable/qemu-system-loongarch64

The runner checks the reported refill mode, not just the PASS marker.
QEMU 10.1.2 used here does not expose the PTW property; the separately
installed QEMU 10.2.0 build does. A version number alone does not establish
that a particular build implements this property.

The tests exercise even/odd TLB entries, cross-directory mappings,
overlap rejection, invalid arguments, allocation failure rollback, page
reclamation, remapping after every CPU has cached a translation, setjmp,
and pinned threads retaining integer and floating-point values across
blocking calls and signal delivery. Successful tests return to the idle
task after printing
``LoongArch MMU/SMP tests: PASS``.

For negative tests, set ``CONFIG_LOONGARCH_MMU_TEST_FAULT`` to 1 (write a
read-only page) or 2 (read an unmapped page), rebuild, then pass
``--fault write`` or ``--fault read`` to the runner. An exception and panic
are expected in these configurations; they must not be used as normal
boot configurations.

User-process regression
=======================

For a four-CPU kernel build::

  cmake -S . -B build-kernel -GNinja \
    -DBOARD_CONFIG=qemu-loongarch:smp-kernel -DNUTTX_APPS_DIR=../apps
  cmake --build build-kernel
  python3 tools/loongarch_qemu_test.py --kernel build-kernel/nuttx \
    --userspace --cpus 4 --timeout 60

Add ``--ptw`` to verify hardware walking. The ``kernel`` configuration
uses the same test on one CPU; omit ``--cpus 4`` in that case. Make builds
embed the same user ELF and can use the same runner.

Both kernel regression configurations set ``CONFIG_LOONGARCH_ASID_BITS=2``
and enable ``CONFIG_LOONGARCH_ASID_TEST``. Before starting user processes,
the test creates four private roots for the three available nonzero ASIDs.
On every CPU it checks TLB retention using ``TLBSRCH``, descriptor sharing,
and multiple generations of ASID reuse at equal virtual addresses. All
roots are populated before these checks, so mapping updates cannot mask a
missing rollover flush. It prints
``LoongArch ASID retention/rollover tests: PASS`` on success. For normal
applications, disable the ASID test and restore the default width of ten.

Use a QEMU containing upstream commit
`db2325f79481 <https://github.com/qemu/qemu/commit/db2325f79481fab87211e5a287580d753f582cb8>`_
for execution-permission tests. Older builds can recurse inside the emulator
when fetching BADI for an NX exception, before NuttX receives the exception.
Both the locally installed 10.1.2 and 10.2.0 builds reproduced this hang;
the local 11.1.50 build completed the test in both refill modes. This is
an emulator requirement, not a reason to disable NX in the page tables.

The supervisor installs the embedded executable in tmpfs and checks:

* Concurrent processes see private BSS at equal virtual addresses.
* Blocking syscalls preserve integer and floating-point registers. In an
  SMP build, every process changes affinity, blocks and checks that it
  resumed on the requested CPU. Blocking is intentional: this scheduler
  revision may defer migration after an affinity change until rescheduling.
* Invalid and reserved syscall numbers are rejected; user signals return
  through the trusted continuation.
* User libc allocates and frees memory, creates and joins a pthread, and
  exits through the normal program startup path. A PLV3 ``execve`` replaces
  the current image and runs the new executable successfully.
* Direct kernel reads/writes, text writes, data execution and an exception
  with SP zero terminate the offending process while the supervisor lives.
* Invalid syscall buffers, wrapping ranges, kernel pathnames, nested exec
  pointers and nonexecutable pthread callbacks are rejected. An unadapted
  ioctl returns ENOSYS without reaching the driver.
* Heap allocations grow beyond the resident bootstrap. Explicit reservations
  verify zero reads, private writes and syscall output into untouched pages.
* Twenty-four further process lifetimes exercise release and reuse of roots.
  With demand paging disabled their total eager heaps exceed the page pool.

``CONFIG_LOONGARCH_VM_TEST`` additionally checks actual demand-zero and COW
faults, parent/child isolation, sole-owner reuse, copyout into COW pages,
read-only and cross-page range rejection, and allocation failure with an
exhausted page pool. Sixty-four snapshot/copyout/destruction cycles exercise
backing-page ownership. It reports
``LoongArch demand-zero/COW/user-copy tests: PASS``.

Success is reported as
``LoongArch userspace isolation/syscall tests: PASS``.

13.1.0 upgrade and validation
============================

The kernel and apps are based on their official ``nuttx-13.1.0`` tags.
The common kernel code matches that release, apart from LoongArch
architecture/board registration, ELF machine identification and the libc
Make integration. Upgrade adaptations are contained in the LoongArch port.
Both repositories retain ``backup/loongarch-pre-13.1.0`` as a local rollback
reference.

The port uses the combined scheduler-switch notification and callback-based
signal delivery. Stack scans accept the new bounded-size argument. The
flat stack-copy primitive now advertises ``vfork`` rather than the 13.1
``fork`` capability, which requires an independent process address space.
User entry refreshes TLS process/thread IDs after exec exchanges IDs, using
checked user copies. Strict syscall handling recognizes the new semaphore
slow-path ABI and rejects mutex holder words as counters.

GCC 15.2.0 and the local QEMU 11.1.50 build were used for the following
upgrade checks:

* CMake single-CPU and four-CPU user regressions with software refill and
  hardware PTW: ASID rollover, all 13 libc assembly functions, demand-zero
  pages, COW, checked user copies, syscalls, exec, signals, migration, fault
  isolation and repeated process cleanup.
* Four-CPU flat MMU/context regressions with both refill modes.
* Make four-CPU kernel regression with hardware PTW and libc assembly.
* LS2K0300 NSH compilation and link; this is not peripheral hardware testing.

The ``apps`` and ``smp-apps`` configurations passed ``hello``, ``atomic``,
``fmemopen_test``, ``open_memstream_test``, ``stdbit_test``, ``heap``, ``pipe``
and ``ostest`` on one and four CPUs. Run these commands from NSH and check
each exit status with
``echo $?``. The OS tests exercise vfork, FPU context preservation, threads,
signals, timers, synchronization and message queues. ``fopencookie_test``
also passed in a single-CPU Make build; the upstream apps release does not
provide its CMake build file. These selected applications do not represent
a complete apps-tree test. Network services, hardware-dependent programs,
external-package applications and other configurations require additional
coverage.

Extended application checks
---------------------------

The broader application inventory, additional profiles and failure analyses
are recorded separately:

.. toctree::
   :maxdepth: 1

   apps-testing

The ``apps-extended`` and ``smp-apps-extended`` configurations add a
repeatable flat-build application suite. Build with CMake, for example::

  cmake -S . -B build-apps -GNinja \
    -DBOARD_CONFIG=qemu-loongarch:apps-extended \
    -DNUTTX_APPS_DIR=../nuttx-apps
  cmake --build build-apps -j8
  python3 tools/loongarch_apps_test.py \
    --kernel build-apps/nuttx --cpus 1 --ptw \
    --suite boards/loongarch/qemu/qemu-loongarch/tests/apps.json \
    --output /tmp/loongarch-apps-results

Use ``smp-apps-extended`` and ``--cpus 4`` for SMP. Set ``--qemu`` to the
compatible QEMU executable when it is not on PATH. The runner starts a new
guest for each case, prepares its filesystem, checks the NSH exit status
and output, and bounds execution time. The output directory must be new.
It retains serial logs (including a bounded drain after a guest assertion),
image SHA-256, the suite and the adjacent kernel
``.config`` when present. A failing case does not prevent subsequent cases
from running. Use ``--case NAME`` to repeat an individual case.

On NuttX/apps 13.1.0, GCC 15.2.0 and the local QEMU 11.1.50 build, the
following 13 cases passed on both one and four CPUs:

* ``arch_libctest``, including the selected LoongArch assembly routines;
* ``scanftest`` (164 checks) and ``wcstombs``;
* ``popen_test`` (seven checks);
* ``ramtest`` with byte, halfword and word accesses to 64 KiB;
* four-thread ``getprime``;
* signal and pthread variants of ``timerjitter`` (100 samples each);
* CRC tests and three iterations of ``fstest`` on tmpfs;
* ``helloxx``, including dynamic, stack and static object construction.

The SMP barrier application also passed with eight threads on four CPUs.
Run it using the same SMP image and ``tests/smp.json`` as the suite.

The ``apps-testsuites`` and ``smp-apps-testsuites`` configurations require
Make because some selected upstream applications lack complete CMake
integration. Configure and build them with the normal Make workflow, then
run ``tools/loongarch_apps_test.py`` with ``tests/testsuites.json``. These
configurations enable a software loopback interface, Unix sockets, a worker
queue and a 64-character filename limit for the generated test names.
They use tmpfs at ``/data``. No external network device is required.

The following additional Make tests passed on one and four CPUs:

* IOB operations and the pthread mutex performance application;
* CMocka memory management (eight cases), pthread (eight cases), mutex
  (two enabled cases), scheduling (16 cases), and timers (seven cases);
* TCP IPv4 loopback connection and Unix socket descriptor passing.

Known failures remain visible in the manifest and produce a failed runner
result. They must not be treated as a fully passing testsuites run:

* NUTS: 54 of 55 checks pass. ``overwrite_out_of_bounds`` expects 16 from
  ``circbuf_overwrite``; the current common implementation returns the
  eight overwritten bytes. List and enabled device checks pass.
* Filesystem: 45 of 46 checks pass. ``fs_stat01`` requires an mtime close
  to the current clock. ``tmpfs_stat_common`` clears the stat structure
  and never supplies timestamps, leaving mtime zero. A diagnostic run of
  the unmodified suite on an attribute-enabled littlefs RAM disk passed
  46 of 46 on both one and four CPUs. This comparison does not replace
  the original tmpfs failure or establish that tmpfs was fixed.
* Epoll: two of three checks pass. ``epoll03`` requires nonblocking Unix
  ``connect`` to return ``EINPROGRESS``; the common local-socket connection
  path can complete immediately. A GDB breakpoint at the unchanged
  assertion confirmed ``ret == 0`` (and argument register ``a0 == 0``).
  The failure occurs before any epoll wait; it does not demonstrate an
  interrupt, scheduler or epoll-readiness failure.
* With ``NAME_MAX=64``, the syscall suite passes its 83 CMocka assertions,
  but prints five socket errno mismatches without failing the assertions.
  The runner rejects those diagnostics even when the application returns
  zero. Socket type 75 is rejected with ``EINVAL`` by the common socket
  flag check. Unsupported INET type/protocol combinations return
  ``EPROTONOSUPPORT`` from ``inet_setup``. The test expects
  ``EAFNOSUPPORT`` for these cases. The initial 32-character configuration
  failed six cases whose
  generated filenames exceeded that limit.

These diagnoses do not authorize changing shared code or test assertions.
The original tests remain unchanged, and no LoongArch implementation
change has been justified by these four failures. Diagnostic filesystem
comparisons are not substituted for the original regression environment.

Upstream build limitations encountered, without changes to common code:

* CMake IOB lacks its internal ``mm/iob`` include directory; NUTS lacks
  the CMocka header dependency; the mutex performance application uses
  ``PROGNMAE`` instead of ``PROGNAME``. These applications were tested with
  Make. ``fopencookie_test`` also needs Make in this release.
* ``fdsantest_simple.c`` includes ``cmocka.h`` without first declaring
  ``jmp_buf``. It was excluded after the compile failure.
* ``nettest/others/test_others_bufpool.c`` assumes a fixed relative kernel
  path. That optional group was excluded; TCP and local tests were enabled.

These are selected flat-build application tests. In particular,
``cmocka_syscall_test`` calls the flat API and does not validate PLV3 syscall
pointer checking. C++ coverage here is limited to the basic runtime and
constructors, without STL, exceptions or RTTI. Hardware-dependent apps,
external services/packages and the remaining apps configurations are not
covered by this matrix. The separate user-process regressions above cover
the strict syscall entry, address spaces, demand paging and COW.

Additional application and I/O coverage
--------------------------------------

The board's ``tests/io.config`` fragment extends ``apps-testsuites`` or
``smp-apps-testsuites`` for another set of actual apps programs. It enables
AIO, cancellation points and cleanup handlers, pthread keys, recursive
mutexes, priority inheritance, round-robin scheduling, shared memory and
ROMFS. It does not change application sources or assertions. In an isolated
Make build, append the fragment to the configured ``.config``, run
``make olddefconfig`` and rebuild. The host needs ``genromfs`` for the ROMFS
example. Run ``tools/loongarch_apps_test.py`` with ``tests/io.json`` and the
matching CPU count.

With GCC 15.2.0, QEMU runs on one and four CPUs passed all six commands
in this manifest: ``posix_stdio``, ``shm_test``, ``romfs``, ``cctype``, ``helloxx``
and the extended ``ostest``. The C++ character example is a completion/output
smoke check, not an exhaustive validation of its character classification
table. The OS test also exercises FPU preservation, robust mutexes, message
queues, nested signals, POSIX timers, setjmp/longjmp and cross-CPU calls.
The four-CPU OS test completed in approximately 257 seconds; its timeout
is 420 seconds to accommodate the work-queue and timer tests.

Two other apps in the fragment run indefinitely and are not part of the
finite completion manifest:

* ``netloop``: 35-second observations on one and four CPUs reached at least five
  matching TCP echo exchanges without error diagnostics. This is bounded
  observation, not proof that the application completes or remains stable
  indefinitely.
* ``poll``: the 35-second runs on one and four CPUs stopped making progress
  after FIFO creation. On four CPUs, ``ps`` showed its task waiting on a
  semaphore while NSH and
  the CPUs remained responsive. ``examples/poll/poll_main.c`` opens both
  FIFOs with blocking ``O_WRONLY`` before creating the reader threads;
  ``pipecommon_open`` waits for a reader. The application therefore never
  reaches its listener-thread creation. No source or assertion was changed
  to bypass this failure.

The optional ``memtester`` application was tested separately. Its downloaded
4.5.1 source fails to compile with the GCC 15 default C23 language mode:
old-style empty parameter lists in function pointers and declarations now
mean no parameters. Both incompatible function-pointer initializations and
calls with excess arguments are rejected. The same unmodified source built
with GCC 14.1.0 (default GNU C17) and passed ``memtester 1M 1`` on one and
four CPUs, including the narrow-write tests. This compiler comparison does
not resolve or hide the GCC 15 build failure. The downloaded archive used
for the comparison has SHA-256
``03f8b53523d91156bb42607de3781fc88030a72cc1dada1154878b8b0bb0a22a``;
the upstream build rule downloads a moving branch rather than a fixed tag.

No LoongArch implementation change was justified by these additional
results. These flat-build checks still do not establish full apps-tree or
PLV3 application coverage.

Earlier baseline validation
---------------------------

With GCC 15.2.0 and NuttX apps 12.9.0, QEMU passed the single-CPU and
four-CPU MMU/context tests with both software refill and hardware PTW.
The four-CPU negative tests reached exception 4 for a read-only write and
exception 1 for an unmapped read in both modes. Directory-boundary tests
cover 2 MiB, 1 GiB, and 512 GiB boundaries.

The four-CPU NSH configuration passed interactive ``uname``, ``hello``,
``ps``, ``free``, ``sleep``, and ``echo`` checks. ``ps`` showed all four
idle tasks, and ``free`` reported the separate page pool. Make and CMake
QEMU builds were checked. The LS2K0300 NSH configuration also compiled
and linked with CMake. These results do not establish hardware peripheral
correctness on an LS2K0300 board.

The user-process regression passed on one and four CPUs with software
refill and hardware PTW using the local QEMU 11.1.50 build. The Make
``smp-kernel`` image was also exercised with PTW. Make export produced a
startup object, libraries and linker script that independently linked the
user regression executable. The architecture C files and headers, board
C files and libc C implementations pass ``tools/nxstyle``.

ASID retention and forced rollover passed on one and four CPUs with both
software refill and hardware PTW using a two-bit ASID limit. The same
images passed the complete user-process regression, including migration
and repeated environment destruction. A Make four-CPU image passed with
PTW. The default ten-bit limit also passed the four-CPU user regression in
both modes. A flat four-CPU MMU image continued to pass with PTW.

Strict syscall argument validation, demand-zero heaps and COW passed the
single-CPU and four-CPU user-process regressions with software refill and
hardware PTW. The VM checks include exhausted-page-pool failures and
repeated COW snapshot/copyout/destruction. These results use the compatible
apps 12.9.0 checkout; broader testing of the supplied apps branch is tracked
separately and is not implied by these architecture regressions.

LS2K0300 migration
==================

The ``hummingbird-ls2k0300:nsh`` configuration retains the imported
peripheral drivers. ``CONFIG_LS2K0300_UBOOT_BOOT`` selects
``ld-uboot.script``; direct boot uses ``ld.script``. SoC setup and peripheral
behavior require physical-board validation. QEMU ``virt`` does not emulate
the LS2K0300 and cannot validate those drivers.

The migration removes the duplicate exception/TLB stubs from chip startup,
fixes CSR write operand constraints, saves TP in the context, corrects TLS
byte counts, and gives heap/idle stacks a consistent layout. Architectural
constant-timer code is shared in ``loongarch_timer.c``. Board pin
configuration is named ``ls2k0300_pinmux.c/.h``; assembly context macros
are in ``loongarch_context.h``. Syscall handling is named
``loongarch_syscall.c`` and CPU IRQ dispatch is in ``loongarch_irq.c``.
``loongarch_forkentry.S`` has a distinct basename from ``loongarch_fork.c``
to avoid Make object-name collisions.

References
==========

* `LoongArch reference manual, Volume 1
  <https://loongson.github.io/LoongArch-Documentation/LoongArch-Vol1-EN.html>`_
* `QEMU LoongArch virt platform
  <https://www.qemu.org/docs/master/system/loongarch/virt.html>`_
