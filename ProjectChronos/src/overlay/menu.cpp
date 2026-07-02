#include "overlay.h"
#include "core/types.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "../utils/logging.h"

extern Overlay* g_overlay;

void Overlay::RenderImGui(Ragebot* ragebot) {
    if (!menuOpen) return;

    ImGui::SetNextWindowSize(ImVec2(520, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGui::Begin("Project Chronos", &menuOpen, ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("ESP")) {
            ImGui::Checkbox("Box ESP", &settings.showBox);
            ImGui::SameLine();
            ImGui::Checkbox("Name", &settings.showName);
            ImGui::SameLine();
            ImGui::Checkbox("Health", &settings.showHealth);
            ImGui::Checkbox("Weapon", &settings.showWeapon);
            ImGui::SameLine();
            ImGui::Checkbox("Skeleton", &settings.showSkeleton);
            ImGui::SameLine();
            ImGui::Checkbox("Radar", &settings.showRadar);

            ImGui::Separator();

            const char* boxStyles[] = { "Full", "Corner", "Filled" };
            ImGui::Combo("Box Style", &settings.boxStyle, boxStyles, IM_ARRAYSIZE(boxStyles));

            ImGui::SliderFloat("Box Width", &settings.boxWidthRatio, 0.3f, 1.0f, "%.2f");

            float col[4] = { settings.boxColor[0], settings.boxColor[1], settings.boxColor[2], settings.boxColor[3] };
            ImGui::ColorEdit4("Box Color", col);
            for (int i = 0; i < 4; i++) settings.boxColor[i] = col[i];

            ImGui::Checkbox("Nade Helper", &settings.showNadeUI);
            ImGui::Checkbox("Crosshair", &settings.showCrosshair);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Rage")) {
            if (ragebot) {
                auto& s = ragebot->settings;
                ImGui::Checkbox("Enabled##rage", &s.enabled);
                ImGui::SameLine();
                ImGui::Checkbox("Autofire", &s.autofire);
                ImGui::SameLine();
                ImGui::Checkbox("Auto Scope", &s.autoScope);
                ImGui::SameLine();
                ImGui::Checkbox("Auto Stop", &s.autoStop);

                ImGui::Separator();

                ImGui::SliderFloat("FOV", &s.maxFov, 0, 180, "%.0f");
                ImGui::SliderFloat("Max Distance", &s.maxDistance, 500, 8192, "%.0f");
                ImGui::SliderFloat("Min Damage", &s.minDamage, 1, 100, "%.0f");
                ImGui::SliderFloat("Min Hitchance %", &s.minHitchance, 1, 100, "%.0f");
                ImGui::SliderFloat("Smoothing", &s.smoothing, 1, 20, "%.0f");

                ImGui::Separator();

                const char* resolverModes[] = { "None", "LBY", "Freestanding", "Bruteforce", "Backtrack" };
                ImGui::Combo("Resolver", &s.resolverMode, resolverModes, IM_ARRAYSIZE(resolverModes));

                ImGui::Checkbox("Visible Only", &s.visibleOnly);
                ImGui::SameLine();
                ImGui::Checkbox("Wall Only", &s.wallOnly);
                ImGui::SameLine();
                ImGui::Checkbox("Silent Aim", &s.silentAim);
            } else {
                ImGui::Text("Ragebot not initialized");
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Exploits")) {
            ImGui::SliderInt("Level", &settings.exploitLevel, 0, 5);
            ImGui::Separator();

            const char* aimModes[] = { "Off", "Silent", "Legit", "Rage" };
            ImGui::Combo("Aim Mode", &settings.aimMode, aimModes, IM_ARRAYSIZE(aimModes));

            ImGui::Checkbox("Ghost", &settings.showGhost);
            ImGui::Checkbox("Crosshair", &settings.showCrosshair);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Info")) {
            ImGui::Text("INS = Menu");
            ImGui::Text("END = Exit");
            ImGui::Separator();
            ImGui::Text("Build: v9");
            ImGui::Text("Status: %s", "Running");
            if (ragebot) {
                ImGui::Separator();
                ImGui::Text("Target: %d", ragebot->GetCurrentTarget());
                ImGui::Text("Hitchance: %.1f%%", ragebot->GetCurrentHitchance());
                ImGui::Text("Aiming: %s", ragebot->HasAimTarget() ? "Yes" : "No");
                ImGui::Text("Fire: %s", ragebot->ShouldFire() ? "Yes" : "No");
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
