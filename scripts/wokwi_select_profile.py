#!/usr/bin/env python3
"""
Select active Wokwi profile by copying profile files to project root.

Usage:
  python3 scripts/wokwi_select_profile.py flowio
  python3 scripts/wokwi_select_profile.py supervisor
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path


PROFILE_FILES = ("diagram.json", "wokwi.toml")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Select active Wokwi profile")
    parser.add_argument(
        "profile",
        choices=("flowio", "supervisor"),
        help="Profile name under wokwi/<profile>/",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_dir = Path(__file__).resolve().parent.parent
    profile_dir = project_dir / "wokwi" / args.profile

    if not profile_dir.exists():
        print(f"[wokwi-profile] missing profile directory: {profile_dir}", file=sys.stderr)
        return 2

    for name in PROFILE_FILES:
        src = profile_dir / name
        dst = project_dir / name
        if not src.exists():
            print(f"[wokwi-profile] missing source file: {src}", file=sys.stderr)
            return 2
        shutil.copy2(src, dst)

    print(f"[wokwi-profile] active profile set to '{args.profile}'")
    print(f"[wokwi-profile] updated: {project_dir / 'diagram.json'}")
    print(f"[wokwi-profile] updated: {project_dir / 'wokwi.toml'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
