#!/usr/bin/env python3
"""Configure, build, and run every draw canvas test bundle."""

from __future__ import annotations

import argparse
import datetime as dt
import os
from pathlib import Path
import shlex
import subprocess
import sys
from typing import Mapping, Sequence, TextIO


PROJECT_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD_DIR = PROJECT_ROOT / "build_tests"
DEFAULT_REPORT_NAME = "test_report.txt"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Configure build_tests with sanitizers, build all draw canvas "
            "test bundles, run CTest, and write a text report."
        )
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIR,
        help="test build directory (default: %(default)s)",
    )
    parser.add_argument(
        "--report",
        type=Path,
        help="report path (default: <build-dir>/test_report.txt)",
    )
    parser.add_argument(
        "--build-type",
        default="Debug",
        help="CMake build type (default: %(default)s)",
    )
    return parser.parse_args()


def sanitizer_environment() -> dict[str, str]:
    environment = os.environ.copy()
    environment.setdefault(
        "ASAN_OPTIONS",
        "abort_on_error=1:halt_on_error=1:detect_leaks=0",
    )
    environment.setdefault(
        "UBSAN_OPTIONS",
        "halt_on_error=1:print_stacktrace=1",
    )
    environment["CTEST_OUTPUT_ON_FAILURE"] = "1"
    return environment


def display_command(command: Sequence[str]) -> str:
    return shlex.join(command)


def run_stage(
    name: str,
    command: Sequence[str],
    environment: Mapping[str, str],
    report: TextIO,
) -> int:
    heading = f"\n[{name}]\n$ {display_command(command)}\n"
    print(heading, end="")
    report.write(heading)
    report.flush()

    process = subprocess.Popen(
        command,
        cwd=PROJECT_ROOT,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    assert process.stdout is not None
    for line in process.stdout:
        print(line, end="")
        report.write(line)
    return_code = process.wait()

    result = f"[{name} exit code: {return_code}]\n"
    print(result, end="")
    report.write(result)
    report.flush()
    return return_code


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.expanduser().resolve()
    report_path = (
        args.report.expanduser().resolve()
        if args.report is not None
        else build_dir / DEFAULT_REPORT_NAME
    )
    build_dir.mkdir(parents=True, exist_ok=True)
    report_path.parent.mkdir(parents=True, exist_ok=True)

    environment = sanitizer_environment()
    stages = (
        (
            "configure",
            (
                "cmake",
                "-S",
                str(PROJECT_ROOT),
                "-B",
                str(build_dir),
                "-DBUILD_TESTING=ON",
                "-DDRAW_CANVAS_TEST_SANITIZERS=ON",
                f"-DCMAKE_BUILD_TYPE={args.build_type}",
            ),
        ),
        (
            "build",
            (
                "cmake",
                "--build",
                str(build_dir),
                "--target",
                "draw_canvas_tests",
                "--parallel",
            ),
        ),
        (
            "test",
            (
                "ctest",
                "--test-dir",
                str(build_dir),
                "--output-on-failure",
            ),
        ),
    )

    started = dt.datetime.now(dt.timezone.utc)
    with report_path.open("w", encoding="utf-8") as report:
        report.write("Draw canvas test report\n")
        report.write(f"Started (UTC): {started.isoformat()}\n")
        report.write(f"Project root: {PROJECT_ROOT}\n")
        report.write(f"Build directory: {build_dir}\n")
        report.write("Sanitizers: address,undefined\n")
        report.write(f"ASAN_OPTIONS: {environment['ASAN_OPTIONS']}\n")
        report.write(f"UBSAN_OPTIONS: {environment['UBSAN_OPTIONS']}\n")

        final_code = 0
        for name, command in stages:
            final_code = run_stage(name, command, environment, report)
            if final_code != 0:
                break

        finished = dt.datetime.now(dt.timezone.utc)
        report.write(f"\nFinished (UTC): {finished.isoformat()}\n")
        report.write(f"Duration: {finished - started}\n")
        report.write(
            "Overall result: "
            + ("PASS\n" if final_code == 0 else "FAIL\n")
        )

    print(f"\nTest report: {report_path}")
    return final_code


if __name__ == "__main__":
    sys.exit(main())
