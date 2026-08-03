@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0_hyrule_update\Apply-HyruleCoopUpdate.ps1" -InstallDir "%~dp0"
set "UPDATE_EXIT=%ERRORLEVEL%"
echo.
if not "%UPDATE_EXIT%"=="0" (
    echo Hyrule Co-op update failed. No personal game files were intentionally changed.
) else (
    echo Hyrule Co-op update completed.
)
pause
exit /b %UPDATE_EXIT%
