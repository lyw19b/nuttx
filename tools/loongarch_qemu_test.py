#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.

"""Run a qemu-loongarch:mmu or smp-mmu image and check serial test results."""

import argparse
import os
from pathlib import Path
import selectors
import subprocess
import sys
import time


def run(args):
    cpu = "la464,ptw=on" if args.ptw else "la464"
    command = [
        args.qemu, "-M", "virt", "-cpu", cpu, "-m", "256M",
        "-smp", str(args.cpus), "-nographic", "-monitor", "none",
        "-kernel", str(args.kernel.resolve()),
    ]
    expected_mode = "hardware PTW" if args.ptw else "software refill"
    banner = f"LoongArch MMU: {expected_mode}, {args.cpus} CPU(s)"
    markers = {
        "none": "LoongArch MMU/SMP tests: PASS",
        "write": "EXCEPTION: TLB modified fault. ExCode: 4",
        "read": "EXCEPTION: Page invalid on load. ExCode: 1",
    }
    output = ""
    deadline = time.monotonic() + args.timeout
    process = subprocess.Popen(command, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            while time.monotonic() < deadline:
                events = selector.select(min(0.5, max(0, deadline - time.monotonic())))
                for key, _ in events:
                    chunk = os.read(key.fileobj.fileno(), 65536)
                    if not chunk:
                        raise RuntimeError("QEMU exited before the expected result")
                    text = chunk.decode("utf-8", errors="replace")
                    output += text
                    sys.stdout.write(text)
                    sys.stdout.flush()
                    if banner in output and markers[args.fault] in output:
                        return
                    if args.fault == "none" and (
                        "EXCEPTION:" in output or "Assertion failed" in output
                    ):
                        raise RuntimeError("Unexpected guest exception or assertion")
            raise RuntimeError("Timed out waiting for the expected test result")
    finally:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        process.stdout.close()
        if args.log:
            args.log.write_text(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, default=Path("nuttx"))
    parser.add_argument("--qemu", default="qemu-system-loongarch64")
    parser.add_argument("--cpus", type=int, choices=range(1, 33), default=1)
    parser.add_argument("--ptw", action="store_true")
    parser.add_argument("--fault", choices=("none", "write", "read"), default="none")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, RuntimeError) as error:
        parser.exit(1, f"\nTest failed: {error}\n")


if __name__ == "__main__":
    main()
