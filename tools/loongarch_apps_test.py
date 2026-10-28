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

"""Run isolated NSH application cases on LoongArch QEMU and retain evidence."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import shlex
import subprocess
import time


class Guest:
    """Own a guest for one case so crashes cannot contaminate later cases."""

    def __init__(self, command, log):
        self.process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.log = log
        self.pending = ""

    def prompt(self, timeout, completion=None):
        """Read through a prompt or an explicitly required completion marker."""
        deadline = time.monotonic() + timeout
        while True:
            if completion is not None:
                match = re.search(completion, self.pending)
                if match:
                    output = self.pending[:match.end()]
                    self.pending = self.pending[match.end():]
                    return output.replace("\r", "")
            else:
                # Configuration dumps can contain the prompt as a quoted
                # value. Accept a prompt at the start of a line, or followed
                # by NSH's line-editor erase sequence when an application
                # exits without printing a final newline.

                match = re.search(r"(?m)^nsh> |nsh> (?=\x1b\[K)",
                                  self.pending)
                if match:
                    output = self.pending[:match.start()]
                    self.pending = self.pending[match.end():]
                    return output.replace("\r", "")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("NSH prompt timed out")
            for key, _ in self.selector.select(min(remaining, 0.5)):
                chunk = os.read(key.fd, 65536)
                if not chunk:
                    raise RuntimeError("QEMU exited before returning to NSH")
                self.log.write(chunk)
                self.log.flush()
                self.pending += chunk.decode("utf-8", errors="replace")
                # Some application suites report a failed child assertion and
                # continue with the next case.  Only a kernel assertion or an
                # architecture exception justifies stopping the whole guest.

                if (re.search(r"Assertion failed[^\n]*at file:", self.pending)
                        or "EXCEPTION:" in self.pending):
                    self.drain(2)
                    raise RuntimeError("Guest assertion or exception")

    def drain(self, timeout):
        """Retain the crash dump before stopping an unresponsive guest."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for key, _ in self.selector.select(
                    max(0, deadline - time.monotonic())):
                chunk = os.read(key.fd, 65536)
                if not chunk:
                    return

                self.log.write(chunk)
                self.log.flush()

    def command(self, command, timeout, completion=None):
        """Run a command and obtain its actual NSH exit status separately."""
        self.process.stdin.write((command + "\n").encode())
        self.process.stdin.flush()
        output = self.prompt(timeout, completion)
        self.process.stdin.write(b"echo __LA_STATUS__$?\n")
        self.process.stdin.flush()
        status_output = self.prompt(10)
        match = re.search(r"(?m)^__LA_STATUS__(\d+)$", status_output)
        if match is None:
            raise RuntimeError("Missing NSH exit status")
        return int(match.group(1)), output

    def close(self):
        """Stop only this owned QEMU process, including a wedged guest."""
        self.process.terminate()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.selector.close()
        self.process.stdin.close()
        self.process.stdout.close()


def upload_fixture(guest, source, target):
    """Copy an unchanged upstream fixture through NSH and record its digest."""
    data = source.read_bytes()
    commands = []
    if not data:
        commands.append(f"cp /dev/null {shlex.quote(target)}")
    else:
        # Sixteen-byte Intel HEX records fit the default 80-column NSH line.
        # Extended address records also allow fixtures larger than 64 KiB.

        records = []
        for offset in range(0, len(data), 16):
            if offset and offset % 65536 == 0:
                records.append(bytes([2, 0, 0, 4])
                               + (offset >> 16).to_bytes(2, "big"))
            chunk = data[offset:offset + 16]
            records.append(bytes([len(chunk), (offset >> 8) & 255,
                                  offset & 255, 0]) + chunk)
        records.append(bytes([0, 0, 0, 1]))
        for index, record in enumerate(records):
            record += bytes([-sum(record) & 255])
            redirect = ">>" if index else ">"
            commands.append(f'echo ":{record.hex().upper()}" '
                            f'{redirect} /tmp/la.hex')
        commands.append(f"hex2bin /tmp/la.hex {shlex.quote(target)}")

    for command in commands:
        status, _ = guest.command(command, 30)
        if status:
            raise RuntimeError(f"Fixture copy failed ({status}): {command}")
    return {"source": str(source), "target": target, "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def run(args):
    """Execute selected cases and write a report even when a case fails."""
    suite = json.loads(args.suite.read_text())
    cases = [case for case in suite["cases"]
             if not args.case or case["name"] in args.case]
    if not cases or (args.case and set(args.case) - {c["name"] for c in cases}):
        raise ValueError("No matching case, or an unknown --case name")

    # Reject malformed expectations before starting any guest.  A manifest
    # error must not interrupt the suite after earlier cases have run.

    for case in cases:
        for fixture in suite.get("fixtures", []) + case.get("fixtures", []):
            source = (args.apps / fixture["source"]).resolve()
            if not source.is_relative_to(args.apps.resolve()):
                raise ValueError(f"Fixture is outside the apps tree: {source}")
            if not source.is_file():
                raise FileNotFoundError(source)
        if "completion" in case:
            re.compile(case["completion"])
        if "skip_pattern" in case:
            re.compile(case["skip_pattern"])
        for field in ("expect", "reject"):
            for pattern in case.get(field, []):
                re.compile(pattern)

    # Keep previous evidence intact, including logs from failed runs.

    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "suite.json").write_text(json.dumps(suite, indent=2) + "\n")
    config = args.kernel.resolve().parent / ".config"
    if config.is_file():
        (args.output / "kernel.config").write_bytes(config.read_bytes())
    command = [args.qemu, "-M", "virt", "-cpu",
               "la464,ptw=on" if args.ptw else "la464", "-m", "256M",
               "-smp", str(args.cpus), "-nographic", "-monitor", "none",
               "-kernel", str(args.kernel.resolve())]
    report = {
        "kernel": str(args.kernel.resolve()),
        "sha256": hashlib.sha256(args.kernel.read_bytes()).hexdigest(),
        "command": command, "cases": [],
    }
    for index, case in enumerate(cases):
        start = time.monotonic()
        result = {"name": case["name"], "command": case["command"],
                  "status": "FAIL", "log": f"{index:03d}.log"}
        guest = None
        print(f"RUN {case['name']}: {case['command']}", flush=True)
        with (args.output / result["log"]).open("wb") as log:
            try:
                guest = Guest(command, log)
                guest.prompt(30)
                for setup in suite.get("setup", []) + case.get("setup", []):
                    status, _ = guest.command(setup, 30)
                    if status:
                        raise RuntimeError(f"Setup failed ({status}): {setup}")
                for fixture in (suite.get("fixtures", [])
                                + case.get("fixtures", [])):
                    evidence = upload_fixture(
                        guest, (args.apps / fixture["source"]).resolve(),
                        fixture["target"])
                    result.setdefault("fixtures", []).append(evidence)
                status, output = guest.command(case["command"],
                                               case.get("timeout", 60),
                                               case.get("completion"))
                result["exit_status"] = status
                skipped = (status in case.get("skip_exit_status", [])
                           and "skip_pattern" in case
                           and re.search(case["skip_pattern"], output))
                if not skipped and status != case.get("exit_status", 0):
                    raise RuntimeError(f"Unexpected exit status {status}")
                for pattern in case.get("expect", []):
                    if re.search(pattern, output) is None:
                        raise RuntimeError(f"Missing output pattern: {pattern}")
                for pattern in case.get("reject", []):
                    if re.search(pattern, output):
                        raise RuntimeError(f"Failure output pattern: {pattern}")
                result["status"] = "SKIP" if skipped else "PASS"
                if skipped:
                    result["reason"] = skipped.group(0)
            except (OSError, RuntimeError, TimeoutError) as error:
                result["reason"] = str(error)
            finally:
                if guest is not None:
                    guest.close()
        result["seconds"] = round(time.monotonic() - start, 2)
        report["cases"].append(result)
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"{result['status']} {case['name']} {result.get('reason', '')}",
              flush=True)
    return int(any(case["status"] == "FAIL" for case in report["cases"]))


def main():
    """Parse the image, test manifest and guest execution options."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-loongarch64")
    parser.add_argument("--suite", type=Path, required=True)
    parser.add_argument("--apps", type=Path, default=Path("../nuttx-apps"),
                        help="apps source tree containing upstream fixtures")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cpus", type=int, choices=range(1, 33), default=1)
    parser.add_argument("--ptw", action="store_true")
    parser.add_argument("--case", action="append")
    args = parser.parse_args()
    raise SystemExit(run(args))


if __name__ == "__main__":
    main()
