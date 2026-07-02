@echo off
setlocal enabledelayedexpansion

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

set WDK_ROOT=C:\Program Files (x86)\Windows Kits\10
set WDK_VER=10.0.28000.0
set ROOT=%~dp0
set OUT=%ROOT%build

if not exist "%OUT%" mkdir "%OUT%"

REM Remove old .sys if locked (previous crash)
if exist "%OUT%\chronos_drv.sys" (
    del /f /q "%OUT%\chronos_drv.sys" 2>nul
)

set INCLUDE=%WDK_ROOT%\Include\%WDK_VER%\km;%WDK_ROOT%\Include\%WDK_VER%\km\crt;%WDK_ROOT%\Include\%WDK_VER%\shared;%ROOT%\driver;%INCLUDE%
set LIB=%WDK_ROOT%\Lib\%WDK_VER%\km\x64;%LIB%

cl /nologo /c /O1 /GS- /kernel /D"_AMD64_" /D"_M_AMD64" /D"_WIN64" /D"NT" ^
    /I"%WDK_ROOT%\Include\%WDK_VER%\km" ^
    /I"%WDK_ROOT%\Include\%WDK_VER%\km\crt" ^
    /I"%WDK_ROOT%\Include\%WDK_VER%\shared" ^
    /I"%ROOT%\driver" ^
    /Fo"%OUT%\chronos_drv.obj" ^
    "%ROOT%\driver\driver.cpp" 2>&1

if %ERRORLEVEL% NEQ 0 goto :error

link /nologo /driver /kernel /pdb:"%OUT%\chronos_drv.pdb" ^
    /out:"%OUT%\chronos_drv.sys" ^
    "%OUT%\chronos_drv.obj" ^
    "%WDK_ROOT%\Lib\%WDK_VER%\km\x64\ntoskrnl.lib" ^
    /entry:DriverEntry /subsystem:native /machine:x64 2>&1

if %ERRORLEVEL% EQU 0 (
    echo.
    echo [+] chronos_drv.sys built successfully
    echo     %OUT%\chronos_drv.sys
) else (
    echo [-] Link failed
)
goto :end

:error
echo [-] Compilation failed with error %ERRORLEVEL%

:end
endlocal
