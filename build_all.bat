@echo off
setlocal
set "ROOT=%~dp0"

echo ================================
echo   Building IME Conflict Fix (x86)
echo ================================
echo.

call "%ROOT%src\ime_fix\build.bat"
if errorlevel 1 exit /b 1

call "%ROOT%src\ime_loader\build.bat"
if errorlevel 1 exit /b 1

call "%ROOT%src\ime_patcher\build.bat"
if errorlevel 1 exit /b 1

echo.
echo [PASS] All three components built.
echo.
echo ime_fix.dll must be renamed to ime_fix.bin inside the mod folder.
endlocal
exit /b 0
