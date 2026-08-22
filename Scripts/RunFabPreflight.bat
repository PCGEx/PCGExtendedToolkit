@echo off
cd /d "%~dp0"

set "SCRIPT_DIR=%~dp0"
if exist "%SCRIPT_DIR%fab-preflight.py" goto :find_runtime

set "SCRIPT_DIR=%~dp0Plugins\PCGExtendedToolkit\Scripts\"
if exist "%SCRIPT_DIR%fab-preflight.py" goto :find_runtime

echo ERROR: Could not find fab-preflight.py.
echo Run this from either:
echo   - Project root (containing Plugins/PCGExtendedToolkit/)
echo   - Plugins/PCGExtendedToolkit/Scripts/
goto :end

:find_runtime
where python >nul 2>&1
if %errorlevel% equ 0 goto :run_python

where python3 >nul 2>&1
if %errorlevel% equ 0 goto :run_python3

echo.
echo ERROR: Python not found. Install it from https://python.org
goto :end

:run_python
python "%SCRIPT_DIR%fab-preflight.py" --selftest
python "%SCRIPT_DIR%fab-preflight.py" %*
goto :finish

:run_python3
python3 "%SCRIPT_DIR%fab-preflight.py" --selftest
python3 "%SCRIPT_DIR%fab-preflight.py" %*
goto :finish

:finish
if %errorlevel% neq 0 (
    echo.
    echo Pre-flight found blocking issues -- fix them before submitting to FAB.
) else (
    echo.
    echo Clean.
)

:end
echo.
pause
