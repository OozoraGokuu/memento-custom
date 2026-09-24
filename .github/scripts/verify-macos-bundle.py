#!/usr/bin/env python3
"""Reject incomplete, wrong-architecture, or over-target macOS bundles."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


def command(*arguments: str) -> str:
    result = subprocess.run(
        arguments,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if result.returncode:
        raise RuntimeError(
            f"{' '.join(arguments)} failed ({result.returncode}):\n{result.stdout}"
        )
    return result.stdout


def version_tuple(value: str) -> tuple[int, ...]:
    return tuple(int(part) for part in value.split("."))


def expand_loader_path(value: str, binary: Path, executable_dir: Path) -> Path | None:
    if value.startswith("@loader_path/"):
        return (binary.parent / value.removeprefix("@loader_path/")).resolve()
    if value.startswith("@executable_path/"):
        return (executable_dir / value.removeprefix("@executable_path/")).resolve()
    if value.startswith("/"):
        return Path(value).resolve()
    return None


def load_rpaths(binary: Path, executable_dir: Path) -> list[Path]:
    output = command("otool", "-l", str(binary))
    paths: list[Path] = []
    expect_path = False
    for line in output.splitlines():
        stripped = line.strip()
        if stripped == "cmd LC_RPATH":
            expect_path = True
            continue
        if expect_path and stripped.startswith("path "):
            value = stripped.split()[1]
            expanded = expand_loader_path(value, binary, executable_dir)
            if expanded is not None:
                paths.append(expanded)
            expect_path = False
    return paths


def dependency_exists(
    dependency: str,
    binary: Path,
    bundle: Path,
    executable_dir: Path,
) -> bool:
    if dependency.startswith("/System/Library/") or dependency.startswith("/usr/lib/"):
        return True

    expanded = expand_loader_path(dependency, binary, executable_dir)
    if expanded is not None:
        return expanded.exists() and expanded.is_relative_to(bundle)

    if dependency.startswith("@rpath/"):
        suffix = dependency.removeprefix("@rpath/")
        candidates = [
            path / suffix for path in load_rpaths(binary, executable_dir)
        ]
        candidates.append(bundle / "Contents" / "Frameworks" / suffix)
        return any(
            candidate.exists() and candidate.resolve().is_relative_to(bundle)
            for candidate in candidates
        )

    return False


def build_versions(binary: Path) -> list[tuple[str, str]]:
    try:
        output = command("xcrun", "vtool", "-show-build", str(binary))
    except RuntimeError:
        output = ""
    records: list[tuple[str, str]] = []
    platform = ""
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("platform "):
            platform = stripped.split(maxsplit=1)[1]
        elif stripped.startswith("minos "):
            records.append((platform, stripped.split(maxsplit=1)[1]))
            platform = ""
    if records:
        return records

    # Compatibility fallback for binaries using LC_VERSION_MIN_MACOSX.
    output = command("otool", "-l", str(binary))
    versions: list[tuple[str, str]] = []
    waiting_for_version = False
    for line in output.splitlines():
        stripped = line.strip()
        if stripped == "cmd LC_VERSION_MIN_MACOSX":
            waiting_for_version = True
            continue
        if waiting_for_version and stripped.startswith("version "):
            versions.append(("MACOS", stripped.split()[1]))
            waiting_for_version = False
    return versions


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=Path)
    parser.add_argument("expected_arch", choices=("arm64", "x86_64"))
    parser.add_argument("maximum_minos")
    arguments = parser.parse_args()

    bundle = arguments.bundle.resolve()
    executable_dir = bundle / "Contents" / "MacOS"
    main_executable = executable_dir / "Memento"
    if not main_executable.is_file():
        raise SystemExit(f"Missing bundle executable: {main_executable}")

    failures: list[str] = []
    checked = 0
    maximum = version_tuple(arguments.maximum_minos)
    for candidate in bundle.rglob("*"):
        if not candidate.is_file() or candidate.is_symlink():
            continue
        file_description = command("file", "-b", str(candidate))
        if "Mach-O" not in file_description:
            continue
        checked += 1

        architectures = command("lipo", "-archs", str(candidate)).split()
        if arguments.expected_arch not in architectures:
            failures.append(
                f"{candidate}: missing {arguments.expected_arch} slice ({architectures})"
            )

        versions = build_versions(candidate)
        if not versions:
            failures.append(f"{candidate}: no macOS minimum-version load command")
        for platform, minimum in versions:
            if platform.upper() != "MACOS":
                failures.append(
                    f"{candidate}: unexpected Mach-O platform {platform or '<missing>'}"
                )
            if version_tuple(minimum) > maximum:
                failures.append(
                    f"{candidate}: requires macOS {minimum}, above "
                    f"{arguments.maximum_minos}"
                )

        dependency_output = command("otool", "-L", str(candidate)).splitlines()[1:]
        for line in dependency_output:
            dependency = line.strip().split(" ", 1)[0]
            if dependency and not dependency_exists(
                dependency, candidate, bundle, executable_dir
            ):
                failures.append(f"{candidate}: unresolved/external dependency {dependency}")

    if checked == 0:
        failures.append("bundle contains no Mach-O binaries")
    if not any("torrent" in path.name.lower() for path in bundle.rglob("*.dylib")):
        failures.append("bundle does not contain libtorrent")

    if failures:
        print("macOS bundle verification failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(
        f"Verified {checked} Mach-O files for {arguments.expected_arch}; "
        f"all require macOS {arguments.maximum_minos} or earlier."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
