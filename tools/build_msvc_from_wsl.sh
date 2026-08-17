#!/usr/bin/env bash
# Build the MSVC /Brepro release binaries from inside WSL.
#
# WSL mounts Windows drives as read-only 9p here, so Linux tools cannot write
# directly to C:\. Instead this script drives the Windows side through
# interop:
#   1. robocopy WSL source -> Windows NTFS build directory
#   2. cmd.exe runs build_all.bat (MSVC vcvars32 + cl + mt)
#   3. optionally runs package_workshop.py and prints certutil hashes
#
# Usage:
#   tools/build_msvc_from_wsl.sh                 # sync + build + hash
#   WIN_BUILD_DIR='C:\dev\ime-conflict-fix' \
#     tools/build_msvc_from_wsl.sh               # use another Windows dir
#   PACKAGE=1 tools/build_msvc_from_wsl.sh       # also run package_workshop.py
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIN_BUILD_DIR="${WIN_BUILD_DIR:-C:\\dev\\ime-conflict-fix}"
PACKAGE="${PACKAGE:-0}"

if ! command -v cmd.exe >/dev/null 2>&1; then
  echo "ERROR: cmd.exe interop is not available." >&2
  exit 1
fi

case "$WIN_BUILD_DIR" in
  *" "*)
    echo "ERROR: WIN_BUILD_DIR must not contain spaces (cmd.exe path handling)." >&2
    exit 1
    ;;
esac

SRC_WIN="$(wslpath -w "$REPO_ROOT")"

echo "[wsl-msvc] source : $SRC_WIN"
echo "[wsl-msvc] dest   : $WIN_BUILD_DIR"

echo "[wsl-msvc] syncing source to Windows..."
cmd.exe /d /s /c "robocopy $SRC_WIN $WIN_BUILD_DIR /MIR /XD .git .dev build workshop_upload __pycache__ /XF *.dll *.exe *.bin *.obj *.lib *.exp & if errorlevel 8 exit /b 1"

echo "[wsl-msvc] building with MSVC..."
cmd.exe /d /s /c "cd /d $WIN_BUILD_DIR & build_all.bat"

if [ "$PACKAGE" = "1" ]; then
  echo "[wsl-msvc] packaging workshop upload..."
  cmd.exe /d /s /c "cd /d $WIN_BUILD_DIR & python tools\\package_workshop.py"
fi

echo "[wsl-msvc] hashes (MD5 then SHA256):"
cmd.exe /d /s /c "cd /d $WIN_BUILD_DIR & certutil -hashfile src\\ime_fix\\ime_fix.dll MD5 & certutil -hashfile src\\ime_loader\\ime_loader.dll MD5 & certutil -hashfile src\\ime_patcher\\ime_patcher.exe MD5 & certutil -hashfile src\\ime_fix\\ime_fix.dll SHA256 & certutil -hashfile src\\ime_loader\\ime_loader.dll SHA256 & certutil -hashfile src\\ime_patcher\\ime_patcher.exe SHA256"
