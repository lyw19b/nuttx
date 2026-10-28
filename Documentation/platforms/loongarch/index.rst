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
NuttX interfaces from this repository's revision.

Memory management
=================

``CONFIG_ARCH_USE_MMU`` enables a shared, four-level page table with 4 KiB
pages and 512 entries per level. Normal kernel RAM and MMIO remain in
PLV0-only cached and uncached direct mapping windows. Dynamic mappings
occupy the lower canonical address range. This is a flat NuttX build with
paged mappings; per-process ``ARCH_ADDRENV``, demand paging, kernel builds,
and user-mode system calls are not implemented or advertised.

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
before detached tables can be reused. ASID zero is used for the shared
address space. Per-process ASID allocation is not provided.

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
with this NuttX revision (NuttX apps 12.9.0 was used for validation).
``CROSSDEV`` can override the Make toolchain prefix. CMake accepts
``TOOLCHAIN_PREFIX``.

Available configurations under ``boards/loongarch/qemu/qemu-loongarch``:

============ ==========================================================
Configuration Purpose
============ ==========================================================
``nsh``      Single-core interactive NSH
``smp``      Four-core interactive NSH
``mmu``      Single-core MMU and context regression tests
``smp-mmu``  Four-core MMU, context and TLB shootdown regression tests
============ ==========================================================

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

Validation performed
====================

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
constant-timer code is shared in ``loongarch_timerisr.c``. Board pin
configuration is named ``ls2k0300_pinmux.c/.h``; assembly context macros
are in ``loongarch_context.h``. ``loongarch_forkentry.S`` has a distinct
basename from ``loongarch_fork.c`` to avoid Make object-name collisions.

References
==========

* `LoongArch reference manual, Volume 1
  <https://loongson.github.io/LoongArch-Documentation/LoongArch-Vol1-EN.html>`_
* `QEMU LoongArch virt platform
  <https://www.qemu.org/docs/master/system/loongarch/virt.html>`_
