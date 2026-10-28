=============================
LoongArch application testing
=============================

Scope and evidence
==================

The application inventory for apps commit
``57abaf06dd56b38372ef1b37d0d9da34fdcb4f3f`` contains 492 tracked
directories declaring application build targets. This is not a count of
commands: some directories build several programs, and some contain
loadable-module tests. The current ledger records 144 directories with runtime
attempts or observations, 12 blocked during builds or configuration, and 336 not yet run. A successful
command is not proof that every feature
of its application works.

The :download:`coverage inventory <apps-coverage.csv>` records every directory,
including those not run. ``EXECUTED_OR_ATTEMPTED`` includes failed prerequisites
and bounded observations; it does not mean all tests in that directory passed.
``BUILD_BLOCKED`` describes the attempted configuration, not every possible
configuration. ``NOT_RUN`` is pending coverage, not a successful result or a
proven hardware limitation. Dependency strings are collected from Kconfig
files, including subordinate options; they are not one combined dependency
expression.

Serial logs, reports, image hashes, full configurations and diagnostic scripts
from this run are retained in ``/tmp/loongarch-all-apps`` and archived in
``/home/airxs/user/aicode/kernel/loongarch-apps-results-20261028``. Earlier regression
reports referenced by the inventory retain their own paths. Original failed
runs are preserved alongside subsequent runs. The tests use QEMU ``virt``,
256 MiB guest memory, ``la464,ptw=on``, and one or four CPUs. These are flat-build
application checks; they do not replace the PLV3 syscall, COW and demand-paging
regressions described in the platform documentation.

No common kernel source, upstream application source or upstream assertion
was changed to obtain these results. Runtime stack sizes, filesystem setup
and independent build profiles provide the resources the programs require.
Programs with colliding global symbols are built separately.

Reproducing the suites
======================

Use an isolated kernel/apps checkout for each concurrent build. Make and
CMake must not share an apps build directory: generated builtin registries
can otherwise contaminate another image. In a clean Make checkout::

  tools/configure.sh -a ../nuttx-apps qemu-loongarch:apps-software
  make -j8
  python3 tools/loongarch_apps_test.py --kernel nuttx --cpus 1 --ptw \
    --suite boards/loongarch/qemu/qemu-loongarch/tests/software.json \
    --output /tmp/apps-software-results

Pass ``--qemu`` when the compatible QEMU executable is not on PATH. Select
``smp-apps-software`` and ``--cpus 4`` for SMP. Clean the build when changing
the selected applications: an incremental archive can retain disabled
objects and silently resolve a symbol to a different program.

The board provides these additional profiles and manifests:

.. list-table:: Application profiles
   :header-rows: 1
   :widths: 27 20 53

   * - Profile
     - Manifest
     - Purpose
   * - ``apps-software``, ``smp-apps-software``
     - ``software.json``
     - 50 finite software, interpreter, filesystem and loopback cases.
       Tested with GCC 15.2.0.
   * - ``smp-apps-crypto``
     - ``crypto.json``
     - Eleven TinyCrypt programs, LibTomMath and LibTomCrypt. Some TinyCrypt
       commands report successful checks but fail their exit-status check.
   * - ``apps-packages``
     - ``packages.json``
     - IOzone, WebAssembly and JavaScript interpreters, Mbed TLS self-test,
       Embedlog, CoreMark-Pro and libuv. Use GCC 14.1.0: the unmodified IOzone
       and CoreMark-Pro sources reject GCC 15's default C23 mode.
   * - ``apps-cxx``, ``smp-apps-cxx``
     - ``cxx.json``
     - 35 cases covering libc++ 17.0.6, libc++abi, C++20 containers, streams,
       threads and RTTI, RAM MTD, FAT UTF-8, NXFFS, FDT tools and Mbed TLS.
       Exceptions are disabled; the exception program remains a failed case.
       Tested with GCC 14.1.0.

The ``apps-elf``/``smp-apps-elf`` profiles use ``elf.json``. The
``apps-system``/``smp-apps-system`` profiles use ``system.json``. The
``apps-crypto-packages`` profile uses ``sodium.json``, ``wolfssl.json`` and
``drivers.json``. The ``apps-storage-crypto`` profile uses
``storage-crypto.json`` for cryptodev, RAM MTD, SMARTFS and bounded load checks.
These profiles were tested with GCC 14.1.0.

For fixture-based suites, pass ``--apps`` pointing to the apps checkout used
for the build. The runner transfers the original downloaded fixture files
through NSH, records their SHA-256 digests, and leaves their contents unchanged.
For example, use ``--apps ../nuttx-apps --suite
boards/loongarch/qemu/qemu-loongarch/tests/sodium.json``. Fixtures require a
writable ``/tmp`` mount and the ``hex2bin`` builtin, supplied by these profiles.
The runner retains the kernel image hash, full configuration and each case's
raw serial output; a declared upstream skip remains ``SKIP`` rather than a pass.

The package profile also builds the CoreMark-Pro JPEG program. Its successful
diagnostic run supplies the original ``benchmarks/libbmp/Rose256.bmp`` and
``benchmarks/libjpeg/Rose256.jpg`` files from the downloaded CoreMark-Pro
source in a writable guest directory. The local ``suite-i2.json`` retains
the complete serial upload and invocation. The permanent package manifest
does not embed these third-party binary fixtures.

Some external-package Make rules need additional preparation. MicroQuickJS
tries to build its host generator before unpacking the source and expects a
different archive directory name. The test supplied the unmodified source
cache before building. SQLite's repeated context rule can unpack over an
existing source directory. libc++ archive timestamps can trigger a second
application of already-applied patches. These build-system limitations were
not fixed in public code. ETL also has an incorrect profile-copy destination
and repeatedly downloads its deleted archive; the isolated cache supplies the
unchanged GCC profile and retains the archive. Libsodium names its Make archive
target with literal quotes, requiring a matching cache alias to avoid repeated
unpack/patch attempts. These preparations do not imply a successful
unattended build from an empty download cache.

Architecture corrections
========================

Linux-targeted LoongArch GCC drivers predefine Linux and Unix operating-system
macros even when building NuttX. Portable libraries can consequently compile
the wrong platform branch. The LoongArch Make and CMake toolchains now remove
``__linux__``, ``__linux``, ``linux``, ``__gnu_linux__``, ``__unix__``,
``__unix`` and ``unix``. LoongArch CPU macros and ``__NuttX__`` remain available.

The correction removes libuv's erroneous ``syscall(SYS_close, ...)`` path
and SQLite's Linux ``mremap`` path. SQLite still does not build because its
application configuration header separately enables unavailable
``pread64``/``pwrite64`` interfaces. This remaining failure is not an
architecture macro problem.

Macro checks passed for GCC 14.1.0 and 15.2.0 in C, C++ and preprocessed
assembly modes. After the correction, CMake images passed all 13 cases in
``apps.json`` on both one and four CPUs. The package and C++ Make images also
use the corrected toolchain.

The ELF link options now use ``libs/libc/elf/gnu-elf.ld``, the path in NuttX
13.1. Standalone applications link an architecture startup object; flat builds
avoid installing a PLV3 signal trampoline. C++ ELF objects use finalizer arrays
and the startup code handles configurations without ``atexit`` slots.

The LoongArch relocator now handles normal-code-model branches, PC-relative
addresses and unwind-record arithmetic, checking displacement overflow before
writing instructions. ``R_LARCH_32_PCREL`` is corrected from 57 to 99 according
to the `LoongArch ELF ABI <https://github.com/loongson/la-abi-specs/blob/release/laelf.adoc>`_.
The host sanitizer run passed 140 encoding, overflow and unaligned-data checks.
GOT, TLS, legacy stack-machine relocations and shared-object load-base
relocations remain unsupported; these results do not establish support for
arbitrary toolchain output. Make module and dynamic-library samples pass on one and four CPUs.
The CMake basic suite passes all 13 cases on one and four CPUs after the startup
change; this does not validate CMake loadable applications.

Results and diagnoses
=====================

The software manifest completed with 43 of 50 passing on one CPU and 44 of
50 on four CPUs. Full reruns after the architecture macro correction produced
the same results. Separate runs passed CoreMark,
TinyMemBench (about 360 seconds), SuperPI, the 256-datagram UDP exchange
(about 517 seconds), and single-CPU iperf. LibTomMath passed 42 tests.
LibTomCrypt passed ten groups and reported eleven unsupported groups with
the selected multiprecision configuration; these eleven groups are not
counted as cryptographic validation.

With the package configuration's larger pthread stack, the original JPEG
fixture passed, as did the other ten finite package cases. The full libuv
suite advertised 416 cases. Its initial run stalled after ten results because
default signal actions were disabled, preventing SIGTERM from stopping helper
processes. After enabling those actions, two stack overflows exposed an 8 KiB
worker buffer and a 65537-byte test-local array. The profile now supplies
64 KiB worker stacks and 256 KiB test stacks.

The full run then stopped after 148 reported cases in the pipe IPC test.
Running all 416 cases in separate guests, with the original fixture files,
produced 299 passes and 117 failures. The individual manifest is ``libuv.json``
and uses the ``apps-packages`` profile. Original assertions, timeouts and
expected values remain unchanged. Individual completion is not a successful
full-suite run.

GDB caught the first pipe IPC assertion in ``nxsem_wait_slow``: common Unix
socket cleanup recursively acquires ``g_local_lock`` through ``local_release``.
The later stack exhaustion is a consequence of assertion handling, not proof
that this test needs another stack increase. The UDP echo helper also accesses
``req->bufs`` after the library clears it before the callback. Neither common
code nor tests were changed. Other failures include unsupported file/socket
operations and ``uv_exepath`` returning ``UV_ENOTSUP`` directly in the NuttX
backend; the latter blocks many spawn tests before they create a child.

Libsodium 1.0.18 passes all 77 built tests. Each original ``cmptest.h``
compares the generated result byte-for-byte against the original ``.exp`` file.
The apps-provided version-header patch reports a newer version string; that
string is not the version of the downloaded source. The final crypto-package
image also passes the RT-Tests 100-iteration timer run and the driver framework's
simple assertions, POSIX timer and RAM block-device cases. The driver framework
result does not validate peripherals that were not enabled.

wolfCrypt self-tests and the TLS client/server sample pass after setting the
guest clock to 2026-10-28, inside the embedded certificates' 2026-2029 validity
period. The default 2018 clock correctly fails certificate loading. The TLS
sample exchanges data through memory callbacks, not through a network driver.
The application's existing settings use its test RNG; these are functional
tests, not validation of a production entropy source. The Make integration
clones moving branches despite its version option. The recorded revisions are
``685e2abfa0f152f4c85c08ab9b346f32a1752e36`` for wolfSSL and
``321f4578a1c44e50da0e87c7320ea9b306c8472d`` for wolfssl-examples. Public HTTPS
clones populated the isolated cache because the Make recipe requests SSH.

The permanent C++ manifest passes 34 of 35 cases on one CPU and 33 of 35 on
four CPUs. The exception command returns one because exception support is
disabled. The additional SMP failure is Mbed TLS's benchmark: an alarm
delivered from the timer interrupt to a task running on another CPU enters
the common signal dispatcher and calls synchronous ``nxsched_smp_call``.
That function asserts that it is not running in interrupt context. GDB
confirmed ``sig_handler`` as the callback and ``nxsig_tcbdispatch`` as the
caller. The single-CPU benchmark passes with a 64 KiB stack. No affinity
workaround replaces the failing SMP run.


The ``apps-elf`` profile and ``elf.json`` exercise three parent commands.
``module`` completes character-driver installation, read/write and removal;
``sotest`` resolves dynamic symbols and invokes its callbacks. The parent
``elf`` command remains failed. Empty constructor/finalizer sections retain
small link-time addresses because the common loader skips zero-sized sections,
which makes their PC-relative relocations out of range. Several ELF children
fail to load. The builtin ``hello`` can be selected as a fallback and must not
be counted as a successful ELF load. The loaded C++ constructor/destructor,
mutex and child-task examples do run. The runner waits for ``End-of-Test`` to
retain their output even if NSH prints an early prompt.

The ``apps-system`` and ``smp-apps-system`` profiles use ``system.json``.
All eleven cases pass on one and four CPUs: cache API smoke test, ETL,
file seeks/writes on tmpfs and SMARTFS, nxdiag, stack monitor, IPv4/IPv6 filter
rules, empty connection-table dumps and thirteen NVS storage checks. The cache
test uses the default address offset; it does not validate uncached aliases or
physical hardware cache coherency. NVS uses a file-backed MTD that is explicitly
erased before testing. The initial all-zero fixture correctly failed as an
invalid filesystem and its log remains available. SMARTFS uses its own formatted
loop device; its result does not replace an earlier tmpfs failure.

The storage/cryptodev batch records fifteen passing and five failing cases
on one CPU. The passing cryptodev programs cover 3DES, AES-CBC/CTR/XTS,
MD5/SHA hashes, ChaCha20, ChaCha20-Poly1305, CRC32, ECDSA, the DH sample and
PBKDF2 (including the original 16777216-iteration vectors). The DH sample uses
the common backend's Curve25519 operation; it does not certify arbitrary DH
parameters. Four background-program smoke cases observe four-thread memory
stress for five seconds, resource monitoring for three seconds, CPU load for
three seconds, and a FIFO producer/consumer for three seconds. Their shell
wait or inspection command succeeds; the background applications do not finish
normally before QEMU is stopped. Resource monitoring runs with CPU-load
accounting disabled, so its CPU percentage is not validated.

This batch retains the following failures:

* HMAC's last two SHA-224 expected vectors differ from independent host
  calculations. The QEMU results equal Python's HMAC/SHA-224 output for the
  original 131-byte keys and 54-/152-byte messages. The original program still
  reports mismatches and remains failed, despite its zero exit status.
* AES-CMAC fails its 16-byte vector. The common ``swcr_authenc`` path subtracts
  ``exf->blocksize`` from the message length when no IV is supplied, even though
  CMAC's IV length is zero. Its one-byte block size changes the length to 15.
  Independent host CMAC of the first 15 bytes exactly reproduces the observed
  ``f212d4c2154c8766de60c18c98fa0c93`` result.
* RSA does not complete within 120 seconds. GDB samples it inside
  ``pow_mod_faster``/``bignum_divmod``/``bignum_mul`` in common crypto code.
  The implementation uses 512 one-byte limbs and repeatedly scans whole arrays
  inside its multiplication loops. This is a timeout with evidence of active
  computation, not a proven architecture failure or a completed RSA test.
* ``mtdrwb`` passes ``false`` to ``bchdev_register``. Its third argument is now
  an open-mode integer, making this ``O_RDONLY``. The BCH write therefore
  returns ``EACCES``. The obsolete application call is unchanged.
* SMARTFS initially rejected generated names with ``ENAMETOOLONG`` and entered
  an unbounded unlink retry loop after later ``ENOENT`` failures. Those guests
  were explicitly stopped and their failure logs retained. Setting the test
  filesystem's name capacity to 32 allows all 100 original fill/delete loops
  to finish. There are still 99 ``EIO`` diagnostics at capacity and one
  ``EEXIST`` from a generated-name collision, so the run remains failed even
  though it exits with zero and restores its reported initial memory usage.
  GDB catches ``smart_allocsector`` returning ``-EIO`` with ten free physical
  sectors but no free ordinary logical sector in the 12--127 range. The common
  allocator's logical-sector exhaustion path reports ``EIO`` rather than
  ``ENOSPC``; this is not an emulated flash read/write failure.

The separate ``testing/crypto/passwd`` target is blocked in the attempted Make
configuration: its parent has no Makefile, so preconfig does not generate that
parent's Kconfig and ``TESTING_PBKDF2`` is absent. Its CMake integration has not
been tested. Public build files, crypto code, filesystem code and test vectors
remain unchanged. The runner handles NSH prompts after output without a final
newline; its nxdiag configuration-dump regression also passes.

Additional findings, with original failures retained:

* TACLeBench and Duktape need more than their default 8 KiB task stacks.
  Both pass with 64 KiB. Zip needs more than 32 KiB because its initialization
  contains a 64 KiB local buffer; the profile supplies 128 KiB.
* CoreMark-Pro creates workers with default pthread attributes. GDB showed
  only 1872 usable stack bytes and a corrupted TCB during JPEG Huffman-table
  generation. A 64 KiB pthread stack passes the same input and checks.
* TinyCrypt HMAC-PRNG has a 512 KiB local array. The crypto profile supplies
  a 1 MiB task stack and writable storage. Seven TinyCrypt programs still
  return nonzero after printing success: their renamed ``main`` functions
  fall off the end without returning a value. Spinlock-bench and Dhrystone
  have similarly invalid or missing return values. These remain failed
  commands, not successful exit-status tests.
* Argtable3 completes 168 checks: 135 pass and 33 fail. It needs a larger
  stack merely to print failures; its failure-report buffer alone is 8 KiB.
  Removing Linux macros does not resolve the assertions.
* Usrsocktest passes 15 of 17 groups. Its no-daemon cases expect socket
  creation to fail, while the common socket layer falls back to native TCP.
* Unix datagram tests expect a named sender address from an unbound client.
  IPv4 loopback ping receives an echo request instead of an echo reply.
  IPv6 loopback ping passes. Netlink-route still reports errors. The
  single-CPU netcat transfer and four-CPU iperf completion also remain
  failures; throughput output alone is not a completed test.
* Mkgpt writes its table but retains its initial failure return value.
  The write command remains failed even when subsequent data is readable.
* NXFFS's default generated names exceed the earlier 64-character VFS
  limit. With ``NAME_MAX=255`` and ``PATH_MAX=512``, the unchanged test
  passes three fill/verify/delete loops. FAT UTF-8, MTD partitions and the
  1048-check configdata run also pass.
* ``adjtime`` cannot be enabled without an architecture, RTC or PTP clock
  adjustment provider. The LoongArch profile currently provides none.
* Stressapptest's public Make integration does not generate its required
  ``stressapptest_config.h``. JimTcl/QuickJS need math functions missing
  from the selected libm; cJSON's multiple statically linked tests collide;
  TinyCBOR's downloaded allocator declarations do not match its integration.

The downloaded WAMR 2.1.0 integration rejects the LoongArch build target.
The test-tlb benchmark likewise lacks LoongArch frequency-measurement code.
These external runtime/application ports were not added to common files.

Other static-link collisions include TinyCrypt's incompatible ``hex2bin``,
SuperPI's ``mp_cmp`` versus LibTomMath, kernel crypto versus LibTomCrypt,
CoreMark versus CoreMark-Pro, IOzone versus syscall tests, wolfSSL
``mp_rand`` versus LibTomMath, and Mbed TLS benchmark ``buf`` versus libuv tests. Separate configurations avoid linking
these incompatible definitions together without changing their sources.

The older NUTS, tmpfs timestamp, epoll and syscall errno failures remain
documented in the platform page. This matrix is not full apps-tree coverage.
Unrun applications remain listed individually, including software requiring
further configurations as well as applications needing peripherals,
external services, language toolchains or other architectures.
