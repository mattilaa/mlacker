#!/usr/bin/env python3
"""Validate the source version and resolve a deterministic CI/release version."""

import argparse
from pathlib import Path
import re
import sys

NUMBER = r"(?:0|[1-9][0-9]*)"
BASE = rf"{NUMBER}\.{NUMBER}\.{NUMBER}"


def source_version(root):
    version = (root / "version.txt").read_text().strip()
    if not re.fullmatch(BASE, version):
        raise ValueError("version.txt must contain MAJOR.MINOR.PATCH (no leading zeros)")
    manifest = (root / "mlang.toml").read_text()
    package = re.search(r"(?ms)^\[package\]\s*\n(.*?)(?=^\[|\Z)", manifest)
    metadata = re.search(r'^version\s*=\s*"([^"]+)"\s*$', package[1], re.M) if package else None
    if not metadata or metadata[1] != version:
        raise ValueError(f"mlang.toml [package].version must match version.txt ({version})")
    return version


def resolve(root, release_tag=None, build_number=None):
    base = source_version(root)
    if release_tag:
        if build_number is not None:
            raise ValueError("a release tag and a separate build number cannot be combined")
        if not re.fullmatch(rf"v{BASE}(?:\.{NUMBER})?", release_tag):
            raise ValueError("release tag must use vMAJOR.MINOR.PATCH[.BUILD] (no leading zeros)")
        version = release_tag[1:]
        if ".".join(version.split(".")[:3]) != base:
            raise ValueError(f"release tag {release_tag} does not match version.txt ({base})")
    elif build_number is not None:
        if not re.fullmatch(NUMBER, build_number):
            raise ValueError("build number must be a nonnegative integer without leading zeros")
        version = f"{base}.{build_number}"
    else:
        version = base
    return version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-tag")
    parser.add_argument("--build-number")
    parser.add_argument("--github", action="store_true", help="write GitHub Actions outputs and environment")
    args = parser.parse_args()
    try:
        version = resolve(Path(__file__).resolve().parents[1], args.release_tag, args.build_number)
        print(f"mlacker {version}")
        if args.github:
            import os
            with open(os.environ["GITHUB_OUTPUT"], "a") as output:
                output.write(f"version={version}\ntag=v{version}\n")
            with open(os.environ["GITHUB_ENV"], "a") as environment:
                environment.write(f"MLACKER_RELEASE_VERSION={version}\n")
            with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
                summary.write(f"mlacker {version}\n")
    except (ValueError, OSError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
