@echo off
echo === Chronos Testing Teardown ===
echo This will undo the testing configuration.

echo [1/2] Re-enabling VDBL...
reg add HKLM\SYSTEM\CurrentControlSet\Control\CI\Config /v VulnerableDriverBlocklistEnable /t REG_DWORD /d 1 /f
if %errorlevel% equ 0 (echo OK) else (echo FAILED)

echo [2/2] Disabling Test Signing...
bcdedit /set testsigning off
if %errorlevel% equ 0 (echo OK) else (echo FAILED - try as Administrator)

echo.
echo REBOOT REQUIRED to restore normal security.
pause
