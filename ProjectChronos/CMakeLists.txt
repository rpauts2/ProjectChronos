cmake_minimum_required(VERSION 3.20)
project(ProjectChronos VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== ImGui (bundled) ====================

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/ImGui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

add_library(ImGui STATIC ${IMGUI_SOURCES})
target_include_directories(ImGui PUBLIC ${IMGUI_DIR})
target_link_libraries(ImGui PRIVATE d3d11.lib dxgi.lib)

# ==================== Executable ====================

add_executable(chronos
    src/main.cpp

    src/core/memory_reader.cpp
    src/core/state_engine.cpp
    src/core/offset_manager.cpp
    src/core/config.cpp

    src/exploits/exploit_selector.cpp
    src/exploits/executor.cpp
    src/exploits/packet_engine.cpp
    src/exploits/input_history.cpp
    src/exploits/prediction.cpp

    src/overlay/overlay.cpp
    src/overlay/imgui_setup.cpp
    src/overlay/menu.cpp
    src/overlay/render.cpp

    src/safety/adaptive_difficulty.cpp
    src/safety/failure_response.cpp
    src/safety/obfuscation.cpp
    src/safety/vac_shield.cpp

    src/nade/nade_engine.cpp

    src/aimbot/ragebot.cpp
    src/aimbot/resolver.cpp
    src/aimbot/autowall.cpp
    src/aimbot/quantum_aim.cpp

    src/utils/math.cpp
    src/utils/logging.cpp
)

target_include_directories(chronos PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${CMAKE_SOURCE_DIR}/external/WinDivert
)

target_link_libraries(chronos
    ImGui
    ${CMAKE_SOURCE_DIR}/external/WinDivert/WinDivert.lib
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    winmm.lib
    ws2_32.lib
)