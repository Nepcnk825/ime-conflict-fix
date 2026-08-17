@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ================================
echo   Building ime_loader.dll (x86)
echo ================================
echo.

:: Try to locate Visual Studio vcvars32.bat
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
cl /nologo /O2 /MT /GS- /utf-8 /LD /Fe:ime_loader.dll ime_loader.c /link /SUBSYSTEM:WINDOWS /MACHINE:X86 /Brepro user32.lib /EXPORT:IME_Init
if errorlevel 1 (
    echo [ERROR] Compile failed.
    exit /b 1
)

echo [PASS] ime_loader.dll built.
echo.
echo Files:
echo   ime_loader.dll  - loader/updater (exe imports this)
echo   ime_fix.dll     - functional DLL (loaded by ime_loader at runtime)
echo.
echo Note: ime_fix.dll must be built separately (src\ime_fix\build.bat),
echo and shipped as ime_fix.bin in the workshop mod folder.
endlocal
exit /b 0

:novs
echo [ERROR] Visual Studio 2022 not found. Install VS 2022 with "Desktop development with C++".
endlocal
exit /b 1
