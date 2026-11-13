#!/usr/bin/env python3
# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause
"""Run a freshly packaged open-source FVP image with persistent UART logs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--bl31", required=True, type=Path)
    parser.add_argument("--mode", choices=("linux", "unittests"), required=True)
    parser.add_argument("--linux", type=Path)
    parser.add_argument("--ramdisk", type=Path)
    parser.add_argument("--quality", default="debug")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    hyp = Path(__file__).resolve().parents[2]
    featureset = "unittests" if args.mode == "unittests" else "gunyah-baseline"
    out = hyp / "build-fvp/hyp" / args.quality / "fvp-aem" / featureset
    config = hyp / "tools/fvp_aem/fvp_aem_config_v8_9.txt"
    images = [(args.bl31.resolve(), "0x4010000"),
              (out / "hypvm.bin", "0x88000000")]
    if args.mode == "linux":
        if args.linux is None or args.ramdisk is None:
            parser.error("linux mode requires --linux and --ramdisk")
        images += [(args.linux.resolve(), "0xa0000000"),
                   (args.ramdisk.resolve(), "0xa6000000"),
                   (hyp / "build-fvp/resource-manager/fvp-aem" / args.quality /
                    "dtb/fvp-aem-simple.dtb", "0xa4000000")]
    for path in [args.model, config] + [p for p, _ in images]:
        if not path.is_file():
            parser.error(f"missing input: {path}")
    logs = hyp / "build-fvp/logs" / (args.mode + "-" + time.strftime("%Y%m%d-%H%M%S"))
    logs.mkdir(parents=True, exist_ok=False)
    command = [str(args.model.resolve()), "--stat", "-f", str(config)]
    for path, address in images:
        command += ["--data", f"cluster0.cpu0={path}@{address}"]
    for uart in range(4):
        command += ["-C", f"bp.pl011_uart{uart}.out_file={logs}/uart{uart}.log",
                    "-C", f"bp.pl011_uart{uart}.unbuffered_output=1",
                    "-C", f"bp.terminal_{uart}.start_telnet=0"]
    command += ["-C", "bp.vis.disable_visualisation=1"]
    metadata = {"command": command, "inputs": {
        str(p): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in [config] + [p for p, _ in images]}, "revisions": {}}
    provenance = out / "build-inputs.txt"
    if provenance.exists():
        metadata["build_inputs"] = provenance.read_text()
    for repo in (hyp, hyp.parent / "resource-manager", hyp.parent / "musl-c-runtime"):
        metadata["revisions"][str(repo)] = subprocess.check_output(
            ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    (logs / "inputs.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Logs: {logs}", flush=True)
    success = "Tests completed successfully" if args.mode == "unittests" else "Welcome to Buildroot"
    failure = re.compile(r"Assert failed|Tests are failing|Panic:|Unhandled exception")
    passed = False
    with (logs / "fvp.stdout").open("w") as stdout:
        process = subprocess.Popen(command, stdout=stdout, stderr=subprocess.STDOUT,
                                   cwd=logs)
        try:
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                # TF-A uses UART0; this checkout's HYP UART at 0x1c0a0000 is UART1.
                text0 = "\n".join(p.read_text(errors="replace") for p in
                                  (logs / "uart0.log", logs / "uart1.log") if p.exists())
                watched = logs / ("uart1.log" if args.mode == "unittests" else "uart2.log")
                text = watched.read_text(errors="replace") if watched.exists() else ""
                if failure.search(text0):
                    print("FAIL: hypervisor/RM failure in UART0/UART1", flush=True)
                    break
                complete = success in text
                if args.mode == "unittests":
                    completed_cpus = set(re.findall(
                        r"Tests completed successfully on CPU (\d+)", text))
                    complete = completed_cpus == {str(cpu) for cpu in range(8)}
                if args.mode == "linux" and re.search(r"Kernel panic|Oops:|BUG:", text):
                    print("FAIL: Linux error in UART2", flush=True)
                    break
                if complete:
                    revision = metadata["revisions"][str(hyp)][:9]
                    if revision not in text0:
                        print("FAIL: boot banner does not match the source revision", flush=True)
                        break
                    passed = True
                    break
                if process.poll() is not None:
                    print(f"FAIL: model exited with status {process.returncode}", flush=True)
                    break
                time.sleep(1)
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    print("PASS" if passed else "FAIL (inspect UART logs and fvp.stdout)", flush=True)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
