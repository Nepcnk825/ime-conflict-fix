#!/usr/bin/env python3
"""Package the Isaac Workshop upload folder for IME Conflict Fix.

This script only generates a staging directory; it never uploads anything.

Inputs (canonical sources):
  main.lua                         repo root
  metadata.xml                     repo root (full BBCode description)
  src/ime_fix/ime_fix.dll          -> ime_fix.bin
  src/ime_patcher/ime_patcher.exe  -> ime_loader.zip
  src/ime_loader/ime_loader.dll    -> ime_loader.zip

Output:
  workshop_upload/mod/             staging directory for ModUploader
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import struct
import sys
import zipfile
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8")
except (AttributeError, OSError):
    pass

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "workshop_upload" / "mod"

MOD_DIR_TAG = "ime-conflict-fix"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def md5_file(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def is_pe32_x86(data: bytes, name: str) -> bool:
    if len(data) < 0x40 or data[:2] != b"MZ":
        print(f"FAIL: {name} is not a PE file")
        return False
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_off + 24 > len(data) or data[pe_off : pe_off + 4] != b"PE\x00\x00":
        print(f"FAIL: {name} has no PE header")
        return False
    machine = struct.unpack_from("<H", data, pe_off + 4)[0]
    magic = struct.unpack_from("<H", data, pe_off + 24)[0]
    if machine != 0x14C or magic != 0x10B:
        print(
            f"FAIL: {name} is not PE32 x86 "
            f"(machine=0x{machine:X}, optional-magic=0x{magic:X})"
        )
        return False
    return True


def copy_text(src: Path, dst: Path) -> None:
    dst.write_text(src.read_text(encoding="utf-8-sig"), encoding="utf-8")


def verify_built_version(header: Path, define: str, binary: Path, pattern: bytes, label: str) -> bool:
    """Ensure the checked-in binary still matches the current source version."""
    if not binary.is_file():
        print(f"FAIL: {label} binary not found: {binary}")
        return False
    m = re.search(rf'#define\s+{define}\s+"([^"]+)"', header.read_text(encoding="utf-8"))
    if not m:
        print(f"FAIL: {define} not found in {header}")
        return False
    expected = m.group(1).encode("ascii")
    data = binary.read_bytes()
    if pattern.replace(b"VERSION", expected) not in data:
        print(
            f"FAIL: {label} is stale: {header} says {define}={expected.decode()}, "
            f"but the binary does not contain the expected version string."
        )
        print("       Rebuild on Windows with build_all.bat before packaging.")
        return False
    return True


def package() -> int:
    src_main = ROOT / "main.lua"
    src_meta = ROOT / "metadata.xml"
    src_fix = ROOT / "src" / "ime_fix" / "ime_fix.dll"
    src_loader = ROOT / "src" / "ime_loader" / "ime_loader.dll"
    src_patcher = ROOT / "src" / "ime_patcher" / "ime_patcher.exe"

    missing = [str(p) for p in (src_main, src_meta, src_fix, src_loader, src_patcher) if not p.is_file()]
    if missing:
        print("Missing inputs:")
        for item in missing:
            print("  -", item)
        print("Run build_all.bat first.")
        return 2

    bad = []
    for path in (src_fix, src_loader, src_patcher):
        if not is_pe32_x86(path.read_bytes(), path.name):
            bad.append(path.name)
    if bad:
        return 3

    if not verify_built_version(
        ROOT / "src" / "ime_fix" / "ime_fix.h",
        "IME_FIX_STRING",
        src_fix,
        b"ime_fix.dll vVERSION loaded",
        "ime_fix.dll",
    ):
        return 4

    if not verify_built_version(
        ROOT / "src" / "ime_loader" / "ime_loader.h",
        "IME_LOADER_STRING",
        src_loader,
        b"ime_loader.dll vVERSION loaded",
        "ime_loader.dll",
    ):
        return 4

    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    OUT_DIR.mkdir(parents=True)

    # 1. Lua + metadata are copied from the repo root, the single source of truth.
    copy_text(src_main, OUT_DIR / "main.lua")
    copy_text(src_meta, OUT_DIR / "metadata.xml")

    # No settings.ini is shipped in the mod folder. The in-game setting is
    # persisted by Mod Config Menu via Isaac.SaveModData; main.lua emits the
    # resulting value as [IME_LOCK_LAYOUT:<0/1>] in log.txt, which the DLL
    # reads at runtime.

    # 2. Functional DLL ships as .bin so ModUploader accepts it.
    shutil.copyfile(src_fix, OUT_DIR / "ime_fix.bin")

    # 3. One-time patch tools ship inside a zip (ModUploader bans .exe/.dll).
    zip_path = OUT_DIR / "ime_loader.zip"
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        # Fixed timestamps keep the generated zip byte-reproducible.
        for src, arc in ((src_patcher, "ime_patcher.exe"), (src_loader, "ime_loader.dll")):
            info = zipfile.ZipInfo(arc, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o600 << 16
            zf.writestr(info, src.read_bytes())

    # On Windows/NTFS, files copied through a browser or \\wsl.localhost may
    # carry a "Zone.Identifier" alternate data stream (Mark of the Web).
    # Strip it from every generated file so the upload package is clean.
    if hasattr(os, "name") and os.name == "nt":
        for path in OUT_DIR.iterdir():
            if path.is_file():
                try:
                    os.remove(str(path) + ":Zone.Identifier")
                except OSError:
                    pass

    print("Workshop staging ready:", OUT_DIR)
    print(f"{'file':24} {'size':>9}  MD5              SHA256")
    for path in sorted(OUT_DIR.iterdir()):
        if path.is_file():
            size = path.stat().st_size
            print(
                f"{path.name:24} {size:9}  "
                f"{md5_file(path)}  {sha256_file(path)}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(package())
