@echo off
echo === Chronos Testing Setup ===
echo This script will:
echo 1. Disable VDBL (Vulnerable Driver Blocklist)
echo 2. Enable Windows Test Signing mode
echo 3. Tell you to reboot
echo.

echo [1/3] Disabling VDBL...
reg add HKLM\SYSTEM\CurrentControlSet\Control\CI\Config /v VulnerableDriverBlocklistEnable /t REG_DWORD /d 0 /f
if %errorlevel% equ 0 (echo OK) else (echo FAILED)

echo [2/3] Enabling Test Signing mode...
bcdedit /set testsigning on
if %errorlevel% equ 0 (echo OK) else (echo FAILED - try running as Administrator)

echo [3/3] Showing current test signing status:
bcdedit /enum {current} | findstr testsigning

echo.
echo ========================================
echo REBOOT REQUIRED for changes to take effect.
echo After reboot, run: chronos.exe
echo To disable test signing later: bcdedit /set testsigning off
echo ========================================
pause
