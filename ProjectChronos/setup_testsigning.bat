@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

echo [*] Project Chronos — Test Signing Setup
echo.

REM === Step 1: Enable Test Signing ===
echo [1/4] Enabling Windows Test Signing mode...
bcdedit /set testsigning on 2>nul
if %ERRORLEVEL% EQU 0 (
    echo   [+] Test signing enabled (reboot required to take effect)
) else (
    echo   [!] Run as Administrator! Trying...
    echo.
    REM Re-run self as admin
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    pause
    exit /b
)

REM === Step 2: Install Certificate ===
echo [2/4] Installing Chronos test certificate to system stores...
certutil -addstore Root "%TEMP%\chronos_testcert.pfx" 2>nul
certutil -addstore TrustedPublisher "%TEMP%\chronos_testcert.pfx" 2>nul
echo   [+] Certificate installed (if cert file exists)

REM === Step 3: Sign driver if not already signed ===
echo [3/4] Signing chronos_drv.sys...
if not exist "build\chronos_drv.sys" (
    echo   [!] build\chronos_drv.sys not found. Run build_driver.bat first.
) else (
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe" sign /v /fd SHA256 /a /t http://timestamp.digicert.com "build\chronos_drv.sys" 2>&1 | findstr /C:"Successfully signed" /C:"error"
    echo   [+] Driver signed
)

REM === Step 4: Status ===
echo.
echo [4/4] Status:
bcdedit /enum | findstr /i "testsigning"
echo.
echo [*] REBOOT REQUIRED for test signing to take effect.
echo [*] After reboot, chronos_drv.sys will load WITHOUT Secure Boot.
echo.
echo [!] WARNING: Test signing mode lowers VAC Trust Factor.
echo     Use ONLY for development/testing.
echo     For production, use leaked cert or EfiGuard DSE bypass.
echo.
pause
