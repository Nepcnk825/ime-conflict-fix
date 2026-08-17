@echo off
setlocal enabledelayedexpansion

echo ================================
echo   Building ime_fix.dll (x86)
echo ================================
echo.

:: Try to locate Visual Studio vcvars32.bat
set "VSPATH="

:: Check user's non-standard VS install on D: (first ? this machine)
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

:: Fallback: assume cl.exe is already in PATH
if not defined VSPATH (
    echo [WARN] Could not find Visual Studio 2022 vcvars32.bat
    echo        Attempting to use cl.exe from PATH...
    echo.
    goto :compile
)

echo [INFO] Using: !VSPATH!
echo.
call "!VSPATH!" || (
    echo [ERROR] Failed to set up Visual Studio environment.
    exit /b 1
)

:compile
echo [INFO] Compiling ime_fix sources ...
echo.

cl.exe /nologo /O2 /MT /GS- /utf-8 /LD /Fe:ime_fix.dll dllmain.c /link /SUBSYSTEM:WINDOWS /MACHINE:X86 /Brepro user32.lib kernel32.lib imm32.lib winmm.lib /EXPORT:IME_Init

if %ERRORLEVEL% neq 0 (
    echo.
    echo [FAIL] Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

echo.
echo [PASS] Build succeeded!

:: Check if ime_fix.dll exists and its size
if exist "ime_fix.dll" (
    for %%A in ("ime_fix.dll") do (
        set "SIZE=%%~zA"
        set /a SIZEBYTES=!SIZE!
        echo [INFO] ime_fix.dll size: !SIZE! bytes

        if !SIZEBYTES! gtr 51200 (
            echo [WARN] DLL is larger than 50KB (!SIZE! bytes^). This may indicate extra dependencies.
        ) else (
            echo [OK]   DLL size is within expected range (under 50KB^)
        )
    )
) else (
    echo [ERROR] ime_fix.dll not found after build!
    exit /b 1
)

echo.
echo ================================
echo   Build Complete
echo ================================
echo.
echo Place ime_fix.dll in the Isaac game directory (next to isaac-ng.exe):
echo   steamapps\common\The Binding of Isaac Rebirth\
echo.
echo Verify IME API imports with:
echo   dumpbin /imports ime_fix.dll
echo.

endlocal
exit /b 0
