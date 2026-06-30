@echo off
title BAFlare Cleanup
color 0A

echo ==========================================
echo      BAFlare Effect Cleanup Script
echo ==========================================
echo.

echo [Step 1/6] Attempting to terminate process BAFlare.exe...
taskkill /f /im "BAFlare.exe" >nul 2>&1
if %errorlevel% equ 0 (
    echo [SUCCESS] Process terminated.
) else (
    echo [INFO] No running process found, continuing cleanup...
)
echo.

echo [Step 2/6] Attempting to terminate process BAFlareSettings.exe...
taskkill /f /im "BAFlareSettings.exe" >nul 2>&1
if %errorlevel% equ 0 (
    echo [SUCCESS] Process terminated.
) else (
    echo [INFO] No running process found, continuing cleanup...
)
echo.


echo [Step 3/6] Cleaning registry entries...

reg delete "HKCU\Software\SparkCursorEffect" /f >nul 2>&1
if %errorlevel% equ 0 (
    echo [SUCCESS] Deleted registry key: HKCU\Software\SparkCursorEffect
) else (
    echo [SKIPPED] Registry key HKCU\Software\SparkCursorEffect does not exist or already removed
)

reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "BAFlare" /f >nul 2>&1
if %errorlevel% equ 0 (
    echo [SUCCESS] Deleted startup entry: HKCU\...\Run\BAFlare
) else (
    echo [SKIPPED] Startup entry does not exist or already removed
)
echo.

echo [Step 4/6] Cleaning up files...
if exist "BAFlare.exe" (
    del /f /q "BAFlare.exe"
    echo [SUCCESS] File BAFlare.exe deleted.
) else (
    echo [SKIPPED] BAFlare.exe not found in current directory.
)

echo [Step 5/6] Cleaning up files...
if exist "BAFlareSettings.exe" (
    del /f /q "BAFlareSettings.exe"
    echo [SUCCESS] File BAFlareSettings.exe deleted.
) else (
    echo [SKIPPED] BAFlareSettings.exe not found in current directory.
)


echo [Step 6/6] Cleaning up files...
if exist "SDL2.dll" (
    del /f /q "SDL2.dll"
    echo [SUCCESS] File SDL2.dll deleted.
) else (
    echo [SKIPPED] SDL2.dll not found in current directory.
)


echo.
echo ==========================================
echo          Cleanup Complete!
echo ==========================================
echo.


msg %username% Done.



start "" cmd /c "timeout /t 1 >nul & del "%~f0""
exit
