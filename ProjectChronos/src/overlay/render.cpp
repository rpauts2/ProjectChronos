#include "overlay.h"
#include "core/types.h"
#include "imgui.h"
#include <cmath>

static ImDrawList* DL() { return ImGui::GetBackgroundDrawList(); }
static ImU32 C(float* col) { return ImGui::ColorConvertFloat4ToU32(ImVec4(col[0],col[1],col[2],col[3])); }

void Overlay::RenderAimbotFov(GameState* state) {
    auto* local = state->GetLocal();
    if (!local) return;

    int cx = screenW / 2;
    int cy = screenH / 2;
    auto dl = DL();

    if (settings.crosshairFovCircle) {
        float fovR = settings.fovRadius > 0 ? settings.fovRadius : 150.0f;
        ImU32 col = ImGui::GetColorU32(ImVec4(
            settings.crosshairFovColor[0], settings.crosshairFovColor[1],
            settings.crosshairFovColor[2], 0.35f));
        dl->AddCircle(ImVec2((float)cx, (float)cy), fovR, col, 64, 1.0f);
    }

    if (settings.crosshairLines) {
        int s = settings.crosshairSize;
        int g = settings.crosshairGap;
        float t = settings.crosshairThickness;
        ImU32 col = C(settings.crosshairColor);
        dl->AddLine(ImVec2((float)cx, (float)(cy - g)), ImVec2((float)cx, (float)(cy - g - s)), col, t);
        dl->AddLine(ImVec2((float)cx, (float)(cy + g)), ImVec2((float)cx, (float)(cy + g + s)), col, t);
        dl->AddLine(ImVec2((float)(cx - g), (float)cy), ImVec2((float)(cx - g - s), (float)cy), col, t);
        dl->AddLine(ImVec2((float)(cx + g), (float)cy), ImVec2((float)(cx + g + s), (float)cy), col, t);
    }

    if (settings.crosshairDot)
        dl->AddCircleFilled(ImVec2((float)cx, (float)cy), 2.0f, C(settings.crosshairColor), 12);
}

void Overlay::RenderWatermark() {
    if (!settings.showWatermark) return;

    auto dl = DL();
    float time = (float)ImGui::GetTime();

    // Get FPS
    float fps = ImGui::GetIO().Framerate;
    char fpsBuf[16]; snprintf(fpsBuf, sizeof(fpsBuf), "%.0f FPS", fps);

    // Build watermark text
    std::string text = "CHRONOS v12  |  ";
    text += fpsBuf;

    ImVec2 sz = ImGui::CalcTextSize(text.c_str());
    float padX = 12.0f, padY = 6.0f;
    float boxW = sz.x + padX * 2;
    float boxH = sz.y + padY * 2;
    float bx = (float)screenW - boxW - 15.0f;
    float by = 12.0f;

    // Pulse effect
    float pulse = 0.85f + 0.15f * sinf(time * 2.5f);

    // Layer 1: Outer frosted glass
    ImU32 outerBg = IM_COL32(8, 12, 30, (int)(180 * pulse));
    dl->AddRectFilled(ImVec2(bx - 2, by - 2), ImVec2(bx + boxW + 2, by + boxH + 2), outerBg, 10.f);

    // Layer 2: Inner dark glass
    ImU32 bg = IM_COL32(4, 8, 20, (int)(220 * pulse));
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + boxW, by + boxH), bg, 8.f);

    // Layer 3: Frosted stripes
    ImU32 frost = IM_COL32(255, 255, 255, (int)(4 * pulse));
    for (float sy = by + 2; sy < by + boxH - 2; sy += 3) {
        dl->AddLine(ImVec2(bx + 3, sy), ImVec2(bx + boxW - 3, sy), frost, 0.5f);
    }

    // Layer 4: Top highlight
    ImU32 highlight = IM_COL32(255, 255, 255, (int)(20 * pulse));
    dl->AddLine(ImVec2(bx + 8, by + 1), ImVec2(bx + boxW - 8, by + 1), highlight, 0.5f);

    // Layer 5: Border
    ImU32 border = IM_COL32(60, 80, 120, (int)(150 * pulse));
    dl->AddRect(ImVec2(bx, by), ImVec2(bx + boxW, by + boxH), border, 8.f, 0, 1.f);

    // Accent left bar
    float accentPulse = 0.7f + 0.3f * sinf(time * 4.0f);
    ImU32 accentBar = IM_COL32(138, 43, 226, (int)(200 * accentPulse));
    dl->AddRectFilled(ImVec2(bx, by + 4), ImVec2(bx + 3, by + boxH - 4), accentBar, 1.5f);

    // Text
    float tc[4] = { settings.watermarkColor[0], settings.watermarkColor[1], settings.watermarkColor[2], settings.watermarkColor[3] * pulse };
    DrawTextOutlined(text.c_str(), (int)(bx + padX), (int)(by + padY), tc, false);
}
