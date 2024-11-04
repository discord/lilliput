#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path
from typing import Dict, List, NamedTuple, Tuple


class BuildInfo(NamedTuple):
    commit_sha: str
    platform: str
    files: Dict[str, str]  # relative path -> sha256


def calculate_checksum(file_path: Path) -> str:
    """Calculate SHA-256 checksum of a file."""
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        # Read in 1MB chunks to handle large files efficiently
        for byte_block in iter(lambda: f.read(4096 * 256), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()


def scan_deps(deps_dir: Path) -> Dict[str, str]:
    """Scan directory for dependency files and calculate their checksums."""
    checksums = {}
    for file_path in deps_dir.rglob("*"):
        if not file_path.is_file():
            continue

        # Only process shared libraries, static libraries, and headers
        if not (
            file_path.suffix in [".so", ".dylib", ".a", ".h"]
            or (
                file_path.suffix.startswith(".so.")
                or file_path.suffix.startswith(".dylib.")
            )
        ):
            continue

        # Get path relative to deps_dir
        rel_path = str(file_path.relative_to(deps_dir))
        try:
            checksums[rel_path] = calculate_checksum(file_path)
        except (IOError, OSError) as e:
            print(f"Error processing {rel_path}: {e}", file=sys.stderr)
            continue

    return checksums


def generate_build_info(deps_dir: Path, platform: str, commit_sha: str) -> BuildInfo:
    """Generate build info for the given deps directory."""
    checksums = scan_deps(deps_dir)
    return BuildInfo(commit_sha=commit_sha, platform=platform, files=checksums)


def verify_deps(deps_dir: Path, build_info: BuildInfo) -> Tuple[bool, List[str]]:
    """Verify deps directory against build info."""
    mismatches = []
    valid = True

    # Get current state of deps directory
    current_checksums = scan_deps(deps_dir)

    print(f"Found {len(current_checksums)} files to verify")

    # Check for missing or mismatched files
    for rel_path, expected_checksum in build_info.files.items():
        if rel_path not in current_checksums:
            mismatches.append(f"{rel_path}: file not found in deps directory")
            valid = False
            continue

        actual_checksum = current_checksums[rel_path]
        if actual_checksum != expected_checksum:
            mismatches.append(
                f"{rel_path}: checksum mismatch\n"
                f"  expected: {expected_checksum}\n"
                f"  got:      {actual_checksum}"
            )
            valid = False
        else:
            print(f"Verified: {rel_path}")

    # Check for extra files
    for rel_path in current_checksums:
        if rel_path not in build_info.files:
            mismatches.append(f"{rel_path}: extra file in deps directory")
            valid = False

    return valid, mismatches


def main():
    parser = argparse.ArgumentParser(description="Verify Lilliput dependencies")

    # Create subparsers first
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Generate command
    generate_parser = subparsers.add_parser(
        "generate", help="Generate build info for dependencies"
    )
    generate_parser.add_argument(
        "--deps-dir", required=True, type=Path, help="Directory containing dependencies"
    )
    generate_parser.add_argument(
        "--platform",
        required=True,
        choices=["linux", "macos"],
        help="Platform identifier",
    )
    generate_parser.add_argument(
        "--commit", required=True, help="Commit SHA that produced the build"
    )
    generate_parser.add_argument(
        "--output", type=Path, help="Output file (default: <deps-dir>/build-info.json)"
    )

    # Verify command
    verify_parser = subparsers.add_parser(
        "verify", help="Verify deps against build info"
    )
    verify_parser.add_argument(
        "--deps-dir", required=True, type=Path, help="Directory containing dependencies"
    )
    verify_parser.add_argument(
        "--build-info", required=True, type=Path, help="Path to build info JSON file"
    )

    args = parser.parse_args()

    if not os.path.exists(args.deps_dir):
        print(f"Error: deps directory not found: {args.deps_dir}", file=sys.stderr)
        sys.exit(1)

    if args.command == "generate":
        build_info = generate_build_info(args.deps_dir, args.platform, args.commit)

        output_file = args.output or args.deps_dir / "build-info.json"

        try:
            with open(output_file, "w") as f:
                json.dump(build_info._asdict(), f, indent=4)
            print(f"Build info generated successfully: {output_file}")
        except (IOError, OSError) as e:
            print(f"Error writing build info: {e}", file=sys.stderr)
            sys.exit(1)

    elif args.command == "verify":
        try:
            with open(args.build_info) as f:
                build_info_dict = json.load(f)
            build_info = BuildInfo(**build_info_dict)
        except (IOError, OSError, json.JSONDecodeError) as e:
            print(f"Error reading build info: {e}", file=sys.stderr)
            sys.exit(1)

        print(f"Verifying deps against build from commit {build_info.commit_sha}")
        valid, mismatches = verify_deps(args.deps_dir, build_info)

        if not valid:
            print("\nVerification failed:")
            for mismatch in mismatches:
                print(f"  {mismatch}")
            sys.exit(1)

        print("\nAll dependencies verified successfully")


if __name__ == "__main__":
    main()
