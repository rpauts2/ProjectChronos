#include "overlay.h"
#include "core/types.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "../utils/logging.h"
#include <cmath>

extern Overlay* g_overlay;

// ═══════════════════════════════════════════════════════════════
// CHRONOS UI FRAMEWORK — Dark Industrial Tech Design
// ═══════════════════════════════════════════════════════════════

static ImU32 U32(float r, float g, float b, float a = 1) {
    return IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), (int)(a*255));
}

static ImVec4 V4(float r, float g, float b, float a = 1) {
    return ImVec4(r, g, b, a);
}

// ── Palette ──
namespace Pal {
    static const ImVec4 bg       = V4(0.04f, 0.04f, 0.07f, 0.96f);
    static const ImVec4 sidebar  = V4(0.06f, 0.06f, 0.10f, 1.f);
    static const ImVec4 card     = V4(0.07f, 0.07f, 0.12f, 0.85f);
    static const ImVec4 accent   = V4(0.50f, 0.22f, 0.95f, 1.f);
    static const ImVec4 accentGl = V4(0.50f, 0.22f, 0.95f, 0.20f);
    static const ImVec4 cyan     = V4(0.15f, 0.85f, 0.95f, 1.f);
    static const ImVec4 text     = V4(0.90f, 0.90f, 0.96f, 1.f);
    static const ImVec4 dim      = V4(0.45f, 0.45f, 0.58f, 1.f);
    static const ImVec4 border   = V4(0.15f, 0.15f, 0.24f, 0.60f);
    static const ImVec4 danger   = V4(0.95f, 0.20f, 0.20f, 1.f);
    static const ImVec4 green    = V4(0.20f, 0.90f, 0.40f, 1.f);
}

// ═══════════════════════════════════════════════════════════════
// ANIMATED TOGGLE SWITCH — Smooth ease-out cubic
// ═══════════════════════════════════════════════════════════════

struct ToggleAnim { float v = 0.f; };
static ToggleAnim g_togAnims[128];
static int g_togIdx = 0;

static bool Toggle(const char* label, bool* val) {
    int idx = g_togIdx++;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    float sw = 38.f, sh = 20.f;
    float ky = p.y + (h - sh) * 0.5f;

    // Animate
    float target = *val ? 1.f : 0.f;
    g_togAnims[idx].v += (target - g_togAnims[idx].v) * std::min(1.f, ImGui::GetIO().DeltaTime * 14.f);
    float t = 1.f - powf(1.f - g_togAnims[idx].v, 3.f); // ease-out cubic

    // Track
    ImVec4 bgOff(0.12f, 0.12f, 0.18f, 1);
    ImVec4 bgOn = Pal::accent;
    ImVec4 bgM(bgOff.x*(1-t)+bgOn.x*t, bgOff.y*(1-t)+bgOn.y*t, bgOff.z*(1-t)+bgOn.z*t, 1);
    dl->AddRectFilled(ImVec2(p.x, ky), ImVec2(p.x+sw, ky+sh), ImGui::ColorConvertFloat4ToU32(bgM), sh*0.5f);

    // Glow when on
    if (t > 0.01f) {
        ImU32 glow = U32(Pal::accent.x, Pal::accent.y, Pal::accent.z, 0.15f * t);
        dl->AddRectFilled(ImVec2(p.x-1, ky-1), ImVec2(p.x+sw+1, ky+sh+1), glow, sh*0.55f);
    }

    // Knob
    float ks = sh - 4.f;
    float kx = p.x + 2.f + (sw - ks - 4.f) * t;
    ImU32 knobCol = t > 0.5f ? U32(1,1,1,1) : U32(0.55f,0.55f,0.65f,1);
    dl->AddCircleFilled(ImVec2(kx + ks*0.5f, ky + ks*0.5f + 1), ks*0.5f, knobCol, 16);

    // Hit area
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 2.f));
    ImGui::InvisibleButton(label, ImVec2(sw + ImGui::CalcTextSize(label).x + 12.f, h + 4.f));
    bool clicked = ImGui::IsItemClicked();
    if (clicked) *val = !*val;

    // Label
    ImGui::SetCursorScreenPos(ImVec2(p.x + sw + 8.f, p.y));
    ImGui::TextUnformatted(label);
    return clicked;
}

// ═══════════════════════════════════════════════════════════════
// SLIDER — Themed with accent grab
// ═══════════════════════════════════════════════════════════════

static bool SliderF(const char* label, float* val, float min, float max, const char* fmt = "%.1f") {
    ImGui::SetNextItemWidth(180);
    return ImGui::SliderFloat(label, val, min, max, fmt);
}

static bool SliderI(const char* label, int* val, int min, int max) {
    ImGui::SetNextItemWidth(180);
    return ImGui::SliderInt(label, val, min, max);
}

// ═══════════════════════════════════════════════════════════════
// COMBO BOX — Themed
// ═══════════════════════════════════════════════════════════════

static bool Combo(const char* label, int* current, const char* items[], int count) {
    ImGui::SetNextItemWidth(180);
    return ImGui::Combo(label, current, items, count);
}

// ═══════════════════════════════════════════════════════════════
// COLOR EDITOR — Mini style
// ═══════════════════════════════════════════════════════════════

static void ColorPick(const char* label, float* col) {
    ImGui::SetNextItemWidth(60);
    ImGui::ColorEdit4(label, col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::SameLine();
    ImGui::Text("%s", label);
}

// ═══════════════════════════════════════════════════════════════
// SECTION HEADER — With accent line
// ═══════════════════════════════════════════════════════════════

static void Header(const char* title) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddText(ImVec2(p.x, p.y + 1), U32(Pal::text.x, Pal::text.y, Pal::text.z, 1), title);
    ImVec2 ts = ImGui::CalcTextSize(title);
    dl->AddRectFilled(ImVec2(p.x, p.y + 17), ImVec2(p.x + ts.x + 6, p.y + 19),
                       U32(Pal::accent.x, Pal::accent.y, Pal::accent.z, 0.8f));
    ImGui::Dummy(ImVec2(0, 24));
}

// ═══════════════════════════════════════════════════════════════
// SUB HEADER — Smaller, dimmer
// ═══════════════════════════════════════════════════════════════

static void SubHeader(const char* title) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddText(ImVec2(p.x, p.y), U32(Pal::dim.x, Pal::dim.y, Pal::dim.z, 1), title);
    ImGui::Dummy(ImVec2(0, 16));
}

// ═══════════════════════════════════════════════════════════════
// SIDEBAR NAVIGATION — Pure drawlist (no child window)
// ═══════════════════════════════════════════════════════════════

static int g_tab = 0;

static void SidebarItem(ImDrawList* dl, ImVec2 base, const char* icon, const char* label, int id, float w) {
    float y = base.y + 44.f + id * 52.f;
    float h = 52.f;
    ImVec2 mn(base.x, y), mx(base.x + w, y + h);
    bool active = (g_tab == id);

    // Background
    if (active) {
        dl->AddRectFilled(mn, mx, U32(0.14f, 0.09f, 0.28f, 1), 0);
    } else {
        ImVec2 mp = ImGui::GetMousePos();
        if (mp.x >= mn.x && mp.x <= mx.x && mp.y >= mn.y && mp.y <= mx.y) {
            dl->AddRectFilled(mn, mx, U32(0.10f, 0.10f, 0.16f, 1), 0);
            if (ImGui::IsMouseClicked(0)) g_tab = id;
        }
    }

    // Active indicator — left bar with glow
    if (active) {
        dl->AddRectFilled(ImVec2(base.x, y + 6), ImVec2(base.x + 3, y + h - 6),
                           U32(Pal::accent.x, Pal::accent.y, Pal::accent.z, 1), 1.5f);
        dl->AddRectFilled(ImVec2(base.x + 1, y + 8), ImVec2(base.x + 2, y + h - 8),
                           U32(Pal::accent.x, Pal::accent.y, Pal::accent.z, 0.3f), 1.f);
    }

    // Icon circle
    ImVec2 ic(base.x + w * 0.5f, y + 18.f);
    ImU32 icBg = active ? U32(Pal::accent.x, Pal::accent.y, Pal::accent.z, 0.25f) : U32(0.13f, 0.13f, 0.22f, 1);
    dl->AddCircleFilled(ic, 13.f, icBg, 20);
    ImU32 icCol = active ? U32(Pal::accent.x, Pal::accent.y, Pal::accent.z, 1) : U32(0.65f, 0.65f, 0.80f, 1);
    ImVec2 ts = ImGui::CalcTextSize(icon);
    dl->AddText(ImVec2(ic.x - ts.x*0.5f, ic.y - ts.y*0.5f), icCol, icon);

    // Label
    ImVec2 lp(base.x + w * 0.5f, y + 35.f);
    ImVec2 ls = ImGui::CalcTextSize(label);
    ImU32 lc = active ? U32(Pal::text.x, Pal::text.y, Pal::text.z, 1) : U32(Pal::dim.x, Pal::dim.y, Pal::dim.z, 1);
    dl->AddText(ImVec2(lp.x - ls.x*0.5f, lp.y), lc, label);
}

// ═══════════════════════════════════════════════════════════════
// MAIN MENU RENDER
// ═══════════════════════════════════════════════════════════════

void Overlay::RenderImGui(AimController* aimController) {
    if (!menuOpen) return;
    g_togIdx = 0;

    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& s = ImGui::GetStyle();

    // ── Global style ──
    s.WindowRounding = 6.f;
    s.FrameRounding = 4.f;
    s.GrabRounding = 4.f;
    s.ChildRounding = 8.f;
    s.ChildBorderSize = 1.f;
    s.WindowBorderSize = 0.f;
    s.FrameBorderSize = 0.f;
    s.WindowPadding = ImVec2(0, 0);
    s.FramePadding = ImVec2(8, 4);
    s.ItemSpacing = ImVec2(8, 5);
    s.ScrollbarSize = 5.f;
    s.ScrollbarRounding = 3.f;
    s.Colors[ImGuiCol_WindowBg] = Pal::bg;
    s.Colors[ImGuiCol_ChildBg] = V4(0,0,0,0);
    s.Colors[ImGuiCol_Border] = Pal::border;
    s.Colors[ImGuiCol_Text] = Pal::text;
    s.Colors[ImGuiCol_Button] = V4(0.10f, 0.10f, 0.18f, 1);
    s.Colors[ImGuiCol_ButtonHovered] = V4(0.18f, 0.18f, 0.28f, 1);
    s.Colors[ImGuiCol_ButtonActive] = V4(0.25f, 0.25f, 0.40f, 1);
    s.Colors[ImGuiCol_FrameBg] = V4(0.07f, 0.07f, 0.12f, 1);
    s.Colors[ImGuiCol_FrameBgHovered] = V4(0.13f, 0.13f, 0.20f, 1);
    s.Colors[ImGuiCol_FrameBgActive] = V4(0.18f, 0.18f, 0.28f, 1);
    s.Colors[ImGuiCol_SliderGrab] = Pal::accent;
    s.Colors[ImGuiCol_SliderGrabActive] = V4(0.60f, 0.35f, 1.f, 1);
    s.Colors[ImGuiCol_CheckMark] = Pal::accent;
    s.Colors[ImGuiCol_ScrollbarBg] = V4(0.04f, 0.04f, 0.08f, 1);
    s.Colors[ImGuiCol_ScrollbarGrab] = V4(0.18f, 0.18f, 0.28f, 1);
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = V4(0.25f, 0.25f, 0.38f, 1);
    s.Colors[ImGuiCol_ScrollbarGrabActive] = V4(0.35f, 0.35f, 0.55f, 1);
    s.Colors[ImGuiCol_PopupBg] = V4(0.06f, 0.06f, 0.10f, 0.96f);

    // ── Window (no p_open — we control visibility via menuOpen + INSERT only) ──
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*0.5f - 380, io.DisplaySize.y*0.5f - 280), ImGuiCond_FirstUseEver);
    ImGui::Begin("##chrono", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing);

    float sideW = 68.f;
    float cx = sideW;
    float cw = ImGui::GetWindowWidth() - sideW;
    float wh = ImGui::GetWindowHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();

    // ── Top accent stripe ──
    dl->AddRectFilled(wp, ImVec2(wp.x + ImGui::GetWindowWidth(), wp.y + 2),
                       U32(Pal::accent.x, Pal::accent.y, Pal::accent.z, 1));
    // Bottom accent stripe (thinner)
    dl->AddRectFilled(ImVec2(wp.x, wp.y + wh - 1), ImVec2(wp.x + ImGui::GetWindowWidth(), wp.y + wh),
                       U32(Pal::accent.x, Pal::accent.y, Pal::accent.z, 0.4f));

    // ── Sidebar (pure drawlist, no child window) ──
    {
        ImVec2 sbBase(wp.x, wp.y + 2.f);

        // Sidebar background
        dl->AddRectFilled(sbBase, ImVec2(sbBase.x + sideW, sbBase.y + wh - 2),
                           U32(Pal::sidebar.x, Pal::sidebar.y, Pal::sidebar.z, Pal::sidebar.w));

        // Logo "C" with glow
        float logoX = sbBase.x + sideW * 0.5f;
        float logoY = sbBase.y + 20.f;
        dl->AddCircleFilled(ImVec2(logoX, logoY), 16.f,
                              U32(Pal::accent.x, Pal::accent.y, Pal::accent.z, 0.15f), 20);
        dl->AddCircleFilled(ImVec2(logoX, logoY), 12.f,
                              U32(Pal::accent.x, Pal::accent.y, Pal::accent.z, 0.25f), 20);
        ImVec2 lts = ImGui::CalcTextSize("C");
        dl->AddText(ImVec2(logoX - lts.x*0.5f, logoY - lts.y*0.5f),
                      U32(Pal::accent.x, Pal::accent.y, Pal::accent.z, 1), "C");

        // Sidebar items
        SidebarItem(dl, sbBase, "E", "ESP", 0, sideW);
        SidebarItem(dl, sbBase, "G", "Glow", 1, sideW);
        SidebarItem(dl, sbBase, "R", "Radar", 2, sideW);
        SidebarItem(dl, sbBase, "A", "Aim", 3, sideW);
        SidebarItem(dl, sbBase, "N", "Nade", 4, sideW);
        SidebarItem(dl, sbBase, "M", "Misc", 5, sideW);

        // Status at bottom
        dl->AddText(ImVec2(sbBase.x + 10, sbBase.y + wh - 50), U32(Pal::green.x, Pal::green.y, Pal::green.z, 0.8f), "v12");
        dl->AddText(ImVec2(sbBase.x + 10, sbBase.y + wh - 35), U32(Pal::dim.x, Pal::dim.y, Pal::dim.z, 0.6f), "RUN");
    }

    // ── Content ──
    ImGui::SetCursorPos(ImVec2(cx, 2));
    ImGui::BeginChild("##content", ImVec2(cw, wh - 10), false);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);

    // ═══════════════════════════════════════════════════════════
    // TAB 0: ESP
    // ═══════════════════════════════════════════════════════════
    if (g_tab == 0) {
        Header("Player ESP");
        ImGui::Columns(2, "##esp1", false);
        ImGui::SetColumnWidth(0, cw * 0.48f);

        SubHeader("Elements");
        Toggle("Box", &settings.showBox);
        Toggle("Name", &settings.showName);
        Toggle("Health", &settings.showHealth);
        Toggle("Weapon", &settings.showWeapon);
        Toggle("Weapon Icons", &settings.useWeaponIcons);
        Toggle("Skeleton", &settings.showSkeleton);
        Toggle("Armor", &settings.showArmor);
        Toggle("Head Dot", &settings.showHeadDot);
        Toggle("Chams", &settings.showChams);

        ImGui::NextColumn();
        Toggle("Flags", &settings.showFlags);
        Toggle("Distance", &settings.showDistance);
        Toggle("Snaplines", &settings.showSnaplines);
        Toggle("Aimline", &settings.showAimline);
        Toggle("Info Panel", &settings.showInfoPanel);
        Toggle("Health Color", &settings.healthBasedColor);
        Toggle("Ammo Bar", &settings.showAmmo);
        Toggle("Watermark", &settings.showWatermark);
        Toggle("OOV Arrows", &settings.showOOVIndicators);
        Toggle("Money", &settings.showMoney);
        Toggle("Bomb Info", &settings.showBombInfo);
        Toggle("Dead Skulls", &settings.showDeadSkulls);
        ImGui::Columns(1);

        ImGui::Dummy(ImVec2(0, 6));
        Header("Box");
        const char* boxTypes[] = { "None", "Corner", "Square", "3D", "Rounded", "Glow" };
        Combo("Type##box", &settings.boxStyle, boxTypes, IM_ARRAYSIZE(boxTypes));
        SliderF("Width##box", &settings.boxWidthRatio, 0.2f, 0.8f, "%.2f");
        SliderF("Thickness##box", &settings.boxThickness, 0.5f, 3.5f, "%.1f");

        ImGui::Dummy(ImVec2(0, 6));
        Header("Colors");
        ImGui::Columns(2, "##cols", false);
        ImGui::SetColumnWidth(0, cw * 0.48f);
        ColorPick("Box", settings.boxColor);
        ColorPick("Name", settings.nameColor);
        ColorPick("Weapon", settings.weaponColor);
        ColorPick("HeadDot", settings.headDotColor);
        ColorPick("Chams", settings.chamsColor);
        ImGui::NextColumn();
        ColorPick("Skeleton", settings.skeletonColor);
        ColorPick("Flags", settings.flagColor);
        ColorPick("Aimline", settings.aimlineColor);
        ColorPick("Watermark", settings.watermarkColor);
        ColorPick("Chams Hidden", settings.chamsHiddenColor);
        ImGui::Columns(1);

        ImGui::Dummy(ImVec2(0, 6));
        Header("Head Dot");
        Toggle("Glow##head", &settings.headDotGlow);
        SliderF("Size##head", &settings.headDotSize, 1.0f, 10.0f, "%.1f");
        SliderF("Glow Size##head", &settings.headDotGlowSize, 4.0f, 25.0f, "%.1f");

        ImGui::Dummy(ImVec2(0, 6));
        Header("Trajectory");
        Toggle("Show##traj", &settings.showTrajectory);
        ColorPick("Color##traj", settings.trajectoryColor);

        ImGui::Dummy(ImVec2(0, 6));
        Header("Health");
        const char* hStyles[] = { "Bar", "Text", "Both" };
        Combo("Style##hp", &settings.healthStyle, hStyles, IM_ARRAYSIZE(hStyles));
        const char* barPos[] = { "Left", "Right" };
        Combo("Position##hp", (int*)&settings.healthBarPos, barPos, 2);

        ImGui::Dummy(ImVec2(0, 6));
        Header("Snaplines");
        const char* slStyles[] = { "Bottom", "Center", "Crosshair" };
        Combo("Style##sl", &settings.snaplineStyle, slStyles, IM_ARRAYSIZE(slStyles));
        SliderF("Thickness##sl", &settings.snaplineThickness, 0.5f, 3.0f, "%.1f");
        ColorPick("Color##sl", settings.snaplineColor);

        ImGui::Dummy(ImVec2(0, 6));
        Header("Crosshair");
        Toggle("Show Crosshair", &settings.showCrosshair);
        Toggle("Lines", &settings.crosshairLines);
        Toggle("Dot", &settings.crosshairDot);
        Toggle("FOV Circle", &settings.crosshairFovCircle);
        Toggle("Recoil Crosshair", &settings.showRecoilCrosshair);
        SliderF("FOV Radius", &settings.fovRadius, 30.f, 400.f, "%.0f");
        SliderF("Thickness##ch", &settings.crosshairThickness, 0.5f, 3.0f, "%.1f");

        ImGui::Dummy(ImVec2(0, 6));
        Header("Visibility");
        Toggle("Visibility-Based", &settings.visibilityBasedStyle);
    }

    // ═══════════════════════════════════════════════════════════
    // TAB 1: GLOW
    // ═══════════════════════════════════════════════════════════
    if (g_tab == 1) {
        Header("Glow");
        Toggle("Enable Glow", &settings.showGlow);
        Toggle("Bloom", &settings.glowBloom);

        ImGui::Dummy(ImVec2(0, 6));
        SubHeader("Style");
        const char* glowStyles[] = { "Solid", "Outline", "Pulse", "Rainbow" };
        Combo("Style##glow", &settings.glowStyle, glowStyles, IM_ARRAYSIZE(glowStyles));

        ImGui::Dummy(ImVec2(0, 4));
        SliderF("Intensity##glow", &settings.glowAlpha, 0.05f, 1.0f, "%.2f");
        SliderF("Thickness##glow", &settings.glowThickness, 2.f, 12.f, "%.0f");
        ColorPick("Color##glow", settings.glowColor);

        ImGui::Dummy(ImVec2(0, 10));
        Header("Presets");
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 8));
        if (ImGui::Button("E-Sports", ImVec2(140, 32))) ApplyPreset(1);
        ImGui::SameLine();
        if (ImGui::Button("Chronos Pro", ImVec2(140, 32))) ApplyPreset(2);
        ImGui::SameLine();
        if (ImGui::Button("Lethal", ImVec2(140, 32))) ApplyPreset(3);
        ImGui::SameLine();
        if (ImGui::Button("Neverlose", ImVec2(140, 32))) ApplyPreset(4);
        ImGui::SameLine();
        if (ImGui::Button("Skeet", ImVec2(100, 32))) ApplyPreset(5);
        ImGui::PopStyleVar(2);
    }

    // ═══════════════════════════════════════════════════════════
    // TAB 2: RADAR
    // ═══════════════════════════════════════════════════════════
    if (g_tab == 2) {
        Header("Radar");
        Toggle("Show Radar", &settings.showRadar);
        Toggle("Rotate", &settings.radarRotate);
        Toggle("Show Team", &settings.radarShowTeam);

        ImGui::Dummy(ImVec2(0, 6));
        SubHeader("Appearance");
        const char* rStyles[] = { "Circle", "Square", "Minimal" };
        Combo("Style##rad", &settings.radarStyle, rStyles, IM_ARRAYSIZE(rStyles));
        SliderI("Size##rad", &settings.radarSize, 120, 400);
        SliderF("Zoom##rad", &settings.radarScale, 0.1f, 1.5f, "%.1f");
        const char* rPos[] = { "Top-Right", "Top-Left", "Bottom-Right", "Bottom-Left" };
        Combo("Position##rad", &settings.radarPosX, rPos, IM_ARRAYSIZE(rPos));

        ImGui::Dummy(ImVec2(0, 6));
        SubHeader("Colors");
        ColorPick("Background##rad", settings.radarBg);
        ColorPick("Border##rad", settings.radarBorder);
    }

    // ═══════════════════════════════════════════════════════════
    // TAB 3: AIM
    // ═══════════════════════════════════════════════════════════
    // TAB 3: AIM — FULLY CONSOLIDATED (Aim + Neural + Exploit)
    // ═══════════════════════════════════════════════════════════
    if (g_tab == 3) {
        if (aimController) {
            auto& qs = aimController->settings;

            Header("Aimbot");
            Toggle("Enable", &qs.enabled);
            Toggle("Silent Aim", &qs.silentAim);

            ImGui::Dummy(ImVec2(0, 3));
            SubHeader("Keybind");
            {
                static const char* keyNames[] = { "Always On", "Right Click", "Left Click", "ALT", "SHIFT", "CAPS", "Mouse 4", "Mouse 5" };
                static int keyIdx = 0;
                static bool inited = false;
                if (!inited) {
                    inited = true;
                    keyIdx = 0;
                    if (qs.aimKey == VK_RBUTTON) keyIdx = 1;
                    else if (qs.aimKey == VK_LBUTTON) keyIdx = 2;
                    else if (qs.aimKey == VK_MENU) keyIdx = 3;
                    else if (qs.aimKey == VK_SHIFT) keyIdx = 4;
                    else if (qs.aimKey == VK_CAPITAL) keyIdx = 5;
                    else if (qs.aimKey == VK_XBUTTON1) keyIdx = 6;
                    else if (qs.aimKey == VK_XBUTTON2) keyIdx = 7;
                }
                if (Combo("Aim Key", &keyIdx, keyNames, IM_ARRAYSIZE(keyNames))) {
                    switch (keyIdx) {
                        case 0: qs.aimKey = 0; break;
                        case 1: qs.aimKey = VK_RBUTTON; break;
                        case 2: qs.aimKey = VK_LBUTTON; break;
                        case 3: qs.aimKey = VK_MENU; break;
                        case 4: qs.aimKey = VK_SHIFT; break;
                        case 5: qs.aimKey = VK_CAPITAL; break;
                        case 6: qs.aimKey = VK_XBUTTON1; break;
                        case 7: qs.aimKey = VK_XBUTTON2; break;
                    }
                }
            }

            ImGui::Dummy(ImVec2(0, 3));
            SubHeader("Core");
            SliderF("FOV", &qs.fov, 10, 180, "%.0f");
            SliderF("Distance", &qs.maxDist, 500, 8192, "%.0f");
            SliderF("Min Hitchance", &qs.minHitchance, 5, 100, "%.0f");

            ImGui::Dummy(ImVec2(0, 3));
            SubHeader("Mouse Aim");
            SliderF("Sensitivity", &qs.mouseSensitivity, 0.1f, 5.0f, "%.2f");
            Toggle("Auto Fire", &qs.autoFire);
            SliderF("On Target (deg)", &qs.onTargetThreshold, 1.0f, 10.0f, "%.1f");
            {
                static const char* fireModes[] = { "Hold", "Burst (Click)" };
                Combo("Fire Mode", &qs.fireMode, fireModes, 2);
            }

            ImGui::Dummy(ImVec2(0, 3));
            SubHeader("Modules");
            Toggle("RCS", &qs.rcs);
            Toggle("Predictive", &qs.predictive);
            {
                bool resOn = qs.resolverMode > 0;
                if (Toggle("Resolver", &resOn)) qs.resolverMode = resOn ? 1 : 0;
            }

            ImGui::Dummy(ImVec2(0, 3));
            SubHeader("Smooth Aim");
            Toggle("Smooth Aim", &qs.smoothEnabled);
            SliderF("Speed", &qs.smoothSpeed, 1.0f, 50.0f, "%.1f");
            SliderF("Max Angle/Frame", &qs.smoothMaxAngle, 1.0f, 45.0f, "%.1f");
            {
                static const char* curveNames[] = { "Linear", "Ease In", "Ease Out", "Ease In/Out", "Circle", "Exponential" };
                Combo("Curve", &qs.smoothCurve, curveNames, 6);
            }

            ImGui::Dummy(ImVec2(0, 3));
            SubHeader("Backtrack");
            Toggle("Backtrack", &qs.backtrackEnabled);
            SliderF("Max Time", &qs.backtrackMaxTime, 0.05f, 0.2f, "%.2fs");

            ImGui::Dummy(ImVec2(0, 3));
            SubHeader("Triggerbot");
            Toggle("Triggerbot", &qs.triggerEnabled);
            SliderI("Delay (ms)", &qs.triggerDelay, 0, 200);

            ImGui::Dummy(ImVec2(0, 3));
            SubHeader("Rage Mode");
            Toggle("Rage Mode", &qs.rageMode);
            Toggle("Auto-Scope", &qs.autoScope);
            Toggle("Auto-Stop", &qs.autoStop);

            ImGui::Dummy(ImVec2(0, 3));
            SubHeader("Weapon Profiles");
            Toggle("Use Profiles", &qs.useWeaponProfiles);
            if (qs.useWeaponProfiles) {
                const char* profileNames[] = { "Rifle", "Pistol", "Sniper", "SMG", "Shotgun" };
                static int selectedProfile = 0;
                Combo("Profile", &selectedProfile, profileNames, 5);

                AimController::WeaponProfile* prof = nullptr;
                switch (selectedProfile) {
                    case 0: prof = &qs.rifleProfile; break;
                    case 1: prof = &qs.pistolProfile; break;
                    case 2: prof = &qs.sniperProfile; break;
                    case 3: prof = &qs.smgProfile; break;
                    case 4: prof = &qs.shotgunProfile; break;
                }
                if (prof) {
                    char label[64];
                    snprintf(label, sizeof(label), "FOV##%s", profileNames[selectedProfile]);
                    SliderF(label, &prof->fov, 1.0f, 180.0f, "%.0f");
                    snprintf(label, sizeof(label), "Smooth Speed##%s", profileNames[selectedProfile]);
                    SliderF(label, &prof->smoothSpeed, 1.0f, 50.0f, "%.0f");
                    snprintf(label, sizeof(label), "Max Angle##%s", profileNames[selectedProfile]);
                    SliderF(label, &prof->maxAnglePerFrame, 1.0f, 45.0f, "%.0f");
                    snprintf(label, sizeof(label), "Min HC##%s", profileNames[selectedProfile]);
                    SliderF(label, &prof->minHitchance, 1.0f, 100.0f, "%.0f");
                    snprintf(label, sizeof(label), "Max Dist##%s", profileNames[selectedProfile]);
                    SliderF(label, &prof->maxDist, 256.0f, 16384.0f, "%.0f");
                    snprintf(label, sizeof(label), "Fire Rate##%s", profileNames[selectedProfile]);
                    SliderF(label, &prof->fireRate, 0.01f, 2.0f, "%.2fs");
                    snprintf(label, sizeof(label), "Spread##%s", profileNames[selectedProfile]);
                    SliderF(label, &prof->spread, 0.01f, 0.50f, "%.2f");
                }
            }

            ImGui::Dummy(ImVec2(0, 3));
            SubHeader("Status");
            ImGui::Text("Target: %d  HC: %.0f%%", aimController->GetCurrentTarget(), aimController->GetCurrentHitchance());
            ImGui::TextColored(aimController->HasTarget() ? Pal::green : Pal::danger, "%s", aimController->HasTarget() ? "LOCKED" : "---");

        } else {
            ImGui::TextColored(Pal::danger, "AimController not initialized");
        }
    }

    // ═══════════════════════════════════════════════════════════
    // TAB 4: GRENADE HELPER
    // ═══════════════════════════════════════════════════════════
    if (g_tab == 4) {
        NadeEngine* nadeEng = static_cast<NadeEngine*>(nadeEnginePtr);

        Header("Grenade Helper");

        ImGui::Dummy(ImVec2(0, 3));
        SubHeader("Display");
        Toggle("Show Trajectory", &settings.showTrajectory);
        ColorPick("Trajectory Color", settings.trajectoryColor);

        ImGui::Dummy(ImVec2(0, 3));
        SubHeader("Keybinds");
        {
            static const char* keyNames[] = { "G", "H", "J", "K", "L", "Z", "X", "C", "V", "B", "N", "M", "T", "Y", "U", "I", "O", "P" };
            static int keyVals[] = { 'G', 'H', 'J', 'K', 'L', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', 'T', 'Y', 'U', 'I', 'O', 'P' };
            static int helperKeyIdx = 0;
            static int throwKeyIdx = 8; // V
            static bool inited = false;
            if (!inited) {
                inited = true;
                // Find current keys
                for (int i = 0; i < 18; i++) {
                    if (keyVals[i] == settings.nadeHelperKey) helperKeyIdx = i;
                    if (keyVals[i] == settings.nadeHelperThrowKey) throwKeyIdx = i;
                }
            }
            if (Combo("Show Spots Key", &helperKeyIdx, keyNames, 18)) {
                settings.nadeHelperKey = keyVals[helperKeyIdx];
                if (nadeEng) nadeEng->nadeHelperKey = keyVals[helperKeyIdx];
            }
            if (Combo("Throw Key", &throwKeyIdx, keyNames, 18)) {
                settings.nadeHelperThrowKey = keyVals[throwKeyIdx];
                if (nadeEng) nadeEng->throwKeyBind = keyVals[throwKeyIdx];
            }
        }

        SliderF("Spot Radius", &settings.nadeHelperRadius, 100.0f, 2000.0f, "%.0f");
        SliderF("Aim Speed", &settings.nadeHelperAimSpeed, 0.05f, 1.0f, "%.2f");

        ImGui::Dummy(ImVec2(0, 3));
        SubHeader("Controls");

        if (nadeEng) {
            auto& tc = nadeEng->GetTrajectoryCache();
            if (tc.valid && tc.points.size() > 0) {
                ImGui::TextColored(Pal::green, "Trajectory active");
                ImGui::Text("Points: %d", (int)tc.points.size());
                ImGui::Text("Flight: %.2fs", tc.flightTime);
            } else {
                ImGui::TextColored(Pal::dim, "No active trajectory");
            }

            if (nadeEng->IsThrowing()) {
                ImGui::TextColored(Pal::accent, "THROWING...");
            }

            ImGui::Dummy(ImVec2(0, 6));
            SubHeader("Nearby Spots");
            ImGui::Text("Press [%c] to show spots", (char)settings.nadeHelperKey);
            ImGui::Text("Press [%c] to throw", (char)settings.nadeHelperThrowKey);

            const char* nadeTypeNames[] = { "All", "HE", "Smoke", "Molly", "Flash" };
            static int nadeFilter = 0;
            Combo("Filter##nade", &nadeFilter, nadeTypeNames, 5);
        } else {
            ImGui::TextColored(Pal::dim, "NadeEngine not connected");
        }

        ImGui::Dummy(ImVec2(0, 10));
        Header("Auto-Tricks (Movement)");
        Toggle("Enable Tricks", &settings.autoTrickEnabled);
        {
            static const char* trickKeyNames[] = { "T", "Y", "U", "I", "O", "P", "G", "H", "J", "K", "L", "Z", "X", "C", "V", "B", "N", "M" };
            static int trickKeyVals[] = { 'T', 'Y', 'U', 'I', 'O', 'P', 'G', 'H', 'J', 'K', 'L', 'Z', 'X', 'C', 'V', 'B', 'N', 'M' };
            static int trickKeyIdx = 0;
            static bool inited = false;
            if (!inited) {
                inited = true;
                for (int i = 0; i < 18; i++) {
                    if (trickKeyVals[i] == settings.autoTrickKey) trickKeyIdx = i;
                }
            }
            if (Combo("Trick Key", &trickKeyIdx, trickKeyNames, 18)) {
                settings.autoTrickKey = trickKeyVals[trickKeyIdx];
            }
        }
        SliderF("Trick Radius", &settings.autoTrickRadius, 50.0f, 500.0f, "%.0f");
        Toggle("Show Indicators", &settings.autoTrickShowIndicators);

        if (nadeEng && nadeEng->IsTricking()) {
            ImGui::TextColored(Pal::accent, "EXECUTING TRICK...");
        }
    }

    // ═══════════════════════════════════════════════════════════
    // TAB 5: MISC
    // ═══════════════════════════════════════════════════════════
    if (g_tab == 5) {
        Header("Visual Extras");
        Toggle("Hit Marker", &settings.showHitMarker);
        ColorPick("Hit Color", settings.hitMarkerColor);
        SliderF("Hit Size", &settings.hitMarkerSize, 4.0f, 16.0f, "%.1f");

        ImGui::Dummy(ImVec2(0, 6));
        Header("Indicators");
        Toggle("Spectator List", &settings.showSpectators);
        Toggle("Velocity", &settings.showVelocity);
        ColorPick("Velocity Color", settings.velocityColor);
        Toggle("Scope Overlay", &settings.showScopeOverlay);
        ColorPick("Scope Line", settings.scopeLineColor);

        ImGui::Dummy(ImVec2(0, 6));
        Header("Movement");
        Toggle("Anti-Flash", &settings.antiFlashEnabled);
        Toggle("Third Person", &settings.thirdPerson);

        ImGui::Dummy(ImVec2(0, 6));
        Header("Anti-Aim");
        Toggle("Anti-Aim Pitch", &settings.antiAimPitchEnabled);
        {
            static const char* pitchStyles[] = { "Off", "Up", "Down", "Random" };
            Combo("Pitch Style", &settings.antiAimPitch, pitchStyles, 4);
        }
        {
            static const char* yawStyles[] = { "Off", "Left", "Right", "Back", "Random" };
            Combo("Yaw Style", &settings.antiAimYaw, yawStyles, 5);
        }
        Toggle("Edge Anti-Aim", &settings.edgeAntiAim);
        Toggle("Fake Duck", &settings.fakeDuckEnabled);

        ImGui::Dummy(ImVec2(0, 6));
        Header("Exploits");
        Toggle("Fake Latency", &settings.fakeLatencyEnabled);
        SliderF("Latency Amount", &settings.fakeLatencyAmount, 20.0f, 200.0f, "%.0fms");
        Toggle("Quick Switch", &settings.quickSwitchEnabled);
        Toggle("Quick Stop", &settings.quickStopEnabled);

        ImGui::Dummy(ImVec2(0, 6));
        Header("Combat Extras");
        Toggle("Knife Bot", &settings.knifeBotEnabled);
        SliderF("Knife Range", &settings.knifeBotRange, 40.0f, 200.0f, "%.0f");
        Toggle("Auto Defuse", &settings.autoDefuseEnabled);

        ImGui::Dummy(ImVec2(0, 6));
        Header("Clan Tag");
        Toggle("Animated Tag", &settings.clanTagEnabled);
        {
            static const char* tagStyles[] = { "Static", "Scroll", "Fade", "Rainbow" };
            Combo("Tag Style", &settings.clanTagStyle, tagStyles, 4);
        }

        ImGui::Dummy(ImVec2(0, 6));
        Header("World");
        Toggle("Dropped Weapons", &settings.showDroppedWeapons);
        ColorPick("Dropped Color", settings.droppedWeaponColor);
        Toggle("Sound ESP", &settings.showSoundESP);

        ImGui::Dummy(ImVec2(0, 6));
        Header("Skin Changer");
        Toggle("Enable Skins", &settings.skinChangerEnabled);
        ImGui::TextColored(Pal::dim, "Applies skins via kernel write.");
        ImGui::TextColored(Pal::dim, "AK Redline, AWP Dragon Lore,");
        ImGui::TextColored(Pal::dim, "Glock Fade, Deagle Blaze, etc.");
        if (settings.skinChangerEnabled) {
            ImGui::TextColored(Pal::accent, "Skins active - restart to apply");
        }
    }

    ImGui::EndChild();
    ImGui::End();
}
