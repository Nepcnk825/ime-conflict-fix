@echo off
setlocal enabledelayedexpansion

echo ================================
echo   Building ime_patcher.exe (x86)
echo ================================
echo.

:: Try to locate Visual Studio vcvars32.bat
set "VSPATH="

:: Check user's non-standard VS install on D: (this machine)
if exist "D:\Software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" (
    set "VSPATH=D:\Software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
)

:: Check VS 2022 Community (default location)
if not defined VSPATH (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" (
        set "VSPATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
    )
)

:: Check VS 2022 Professional
if not defined VSPATH (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat" (
        set "VSPATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat"
    )
)

:: Check VS 2022 Enterprise
if not defined VSPATH (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars32.bat" (
        set "VSPATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars32.bat"
    )
)

:: Check BuildTools
if not defined VSPATH (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" (
        set "VSPATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
    )
)

if not defined VSPATH (
    echo [ERROR] Visual Studio 2022 not found. Install VS 2022 with "Desktop development with C++".
    exit /b 1
)

echo [INFO] Using: !VSPATH!
echo.

:: Compile (GUI subsystem, x86, static CRT - no runtime dependency, no console window)
:: /utf-8 : source is UTF-8; msg_box converts to UTF-16 so Chinese shows correctly
call "!VSPATH!" > NUL 2>&1
cl /nologo /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /utf-8 /Fe:ime_patcher.exe ime_patcher.c /link /MACHINE:X86 /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup user32.lib comdlg32.lib advapi32.lib
if errorlevel 1 (
    echo [ERROR] Compile failed.
    exit /b 1
)

:: Embed asInvoker manifest (no UAC prompt)
if exist "%~dp0ime_patcher.manifest" (
    mt.exe -manifest "%~dp0ime_patcher.manifest" -outputresource:ime_patcher.exe;#1
    if errorlevel 1 (
        echo [WARN] Manifest embed failed - exe may show a UAC prompt.
    ) else (
        echo [PASS] asInvoker manifest embedded (no UAC prompt).
    )
)

echo [PASS] ime_patcher.exe built.
echo.
echo Usage:
echo   Double-click ime_patcher.exe          - GUI: pick isaac-ng.exe to patch
echo   ime_patcher.exe "path\to\isaac-ng.exe" - apply patch
echo   ime_patcher.exe --restore "path\to\isaac-ng.exe" - undo patch (uses .imefix.bak)
echo.
echo Run install.bat in the project root for full install/uninstall.
endlocal
exit /b 0
