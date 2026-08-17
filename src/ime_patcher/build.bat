@echo off
setlocal
cd /d "%~dp0"

echo ================================
echo   Building ime_patcher.exe (x86)
echo ================================
echo.

rem Locate Visual Studio vcvars32.bat
set "VSPATH="
if exist "D:\Software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" set "VSPATH=D:\Software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
if not defined VSPATH if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" set "VSPATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
if not defined VSPATH if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat" set "VSPATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat"
if not defined VSPATH if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars32.bat" set "VSPATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars32.bat"
if not defined VSPATH if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" set "VSPATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"

if not defined VSPATH goto :novs
echo [INFO] Using: %VSPATH%
echo.

call "%VSPATH%" > NUL 2>&1
cl /nologo /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /utf-8 /Fe:ime_patcher.exe ime_patcher.c /link /MACHINE:X86 /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /Brepro user32.lib comdlg32.lib advapi32.lib
if errorlevel 1 (
    echo [ERROR] Compile failed.
    exit /b 1
)

if exist "%~dp0ime_patcher.manifest" (
    mt.exe -manifest "%~dp0ime_patcher.manifest" -outputresource:ime_patcher.exe;#1
    if errorlevel 1 (
        echo [WARN] Manifest embed failed.
    ) else (
        echo [PASS] asInvoker manifest embedded.
    )
)

echo [PASS] ime_patcher.exe built.
echo.
echo Usage:
echo   Double-click ime_patcher.exe          - GUI: pick isaac-ng.exe to patch
echo   ime_patcher.exe "path\to\isaac-ng.exe" - apply patch
echo   ime_patcher.exe --restore "path\to\isaac-ng.exe" - undo patch (uses .imefix.bak)
echo.
endlocal
exit /b 0

:novs
echo [ERROR] Visual Studio 2022 not found. Install VS 2022 with "Desktop development with C++".
endlocal
exit /b 1
