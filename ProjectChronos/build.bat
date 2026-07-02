@echo off
setlocal enabledelayedexpansion

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

set ROOT=%~dp0
set SRC=%ROOT%src
set IMGUI=%ROOT%external\ImGui
set WD=%ROOT%external\WinDivert
set OUT=%ROOT%build

if not exist "%OUT%" mkdir "%OUT%"

rc /nologo /fo "%OUT%\chronos_drv.res" "%SRC%\chronos_drv.rc" 2>&1
if %ERRORLEVEL% NEQ 0 echo [WARN] RC failed — continuing without embedded driver
cl /std:c++20 /EHsc /DUSE_IMGUI=1 /Fe:"%OUT%\chronos.exe" /I"%SRC%" /I"%IMGUI%" /I"%WD%" ^
    "%SRC%\main.cpp" ^
    "%ROOT%\kdmapper\kdmapper.cpp" ^
    "%SRC%\core\memory_reader.cpp" "%SRC%\core\state_engine.cpp" "%SRC%\core\offset_manager.cpp" "%SRC%\core\config.cpp" ^
    "%SRC%\exploits\exploit_selector.cpp" "%SRC%\exploits\executor.cpp" "%SRC%\exploits\packet_engine.cpp" "%SRC%\exploits\input_history.cpp" "%SRC%\exploits\prediction.cpp" ^
    "%SRC%\overlay\overlay.cpp" "%SRC%\overlay\imgui_setup.cpp" "%SRC%\overlay\menu.cpp" "%SRC%\overlay\render.cpp" ^
    "%SRC%\safety\adaptive_difficulty.cpp" "%SRC%\safety\failure_response.cpp" "%SRC%\safety\obfuscation.cpp" "%SRC%\safety\vac_shield.cpp" "%SRC%\safety\hwid_spoofer.cpp" "%SRC%\safety\antidump.cpp" ^
    "%SRC%\nade\nade_engine.cpp" ^
    "%SRC%\aimbot\ragebot.cpp" "%SRC%\aimbot\resolver.cpp" "%SRC%\aimbot\autowall.cpp" "%SRC%\aimbot\quantum_aim.cpp" ^
    "%SRC%\utils\math.cpp" "%SRC%\utils\logging.cpp" ^
    "%IMGUI%\imgui.cpp" "%IMGUI%\imgui_draw.cpp" "%IMGUI%\imgui_tables.cpp" "%IMGUI%\imgui_widgets.cpp" ^
    "%IMGUI%\backends\imgui_impl_win32.cpp" "%IMGUI%\backends\imgui_impl_dx11.cpp" ^
    "%WD%\WinDivert.lib" ^
    "%OUT%\chronos_drv.res" 2>nul ^
    d3d11.lib dxgi.lib winmm.lib ws2_32.lib

echo Exit code: %ERRORLEVEL%
pause