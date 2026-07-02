#include "imgui_setup.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool ImGui_Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // Don't save settings
    io.LogFilename = nullptr;

    // Disable ImGui window handling — we use our own
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.MouseDrawCursor = false;

    if (!ImGui_ImplWin32_Init(hwnd)) return false;
    if (!ImGui_ImplDX11_Init(device, context)) return false;

    return true;
}

void ImGui_Shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void ImGui_NewFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGui_EndFrame() {
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void ImGui_DrawText(const char* text, float x, float y, float r, float g, float b, float a, bool centered) {
    auto* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList || !text) return;

    ImU32 color = ImGui::ColorConvertFloat4ToU32({r, g, b, a});

    if (centered) {
        float tw = ImGui::CalcTextSize(text).x;
        drawList->AddText(ImVec2(x - tw / 2, y), color, text);
    } else {
        drawList->AddText(ImVec2(x, y), color, text);
    }
}