#include "overlay.h"
#include "imgui_setup.h"
#include "imgui.h"
#include "safety/obfuscation.h"
#include "utils/logging.h"
#include <dwmapi.h>

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#include <string>
#include <algorithm>
#include <cmath>
#include <tlhelp32.h>

Overlay* g_overlay = nullptr;

#pragma comment(lib, "dwmapi.lib")

static bool IsValidBonePos(const Vector3& v) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
        return false;
    if (fabsf(v.x) > 100000.f || fabsf(v.y) > 100000.f || fabsf(v.z) > 100000.f)
        return false;
    if (fabsf(v.x) < 0.001f && fabsf(v.y) < 0.001f && fabsf(v.z) < 0.001f)
        return false;
    return true;
}

void Overlay::RandomizeClassName() {
    NameGen::RandomString(windowClass, 12);
}

void Overlay::ToggleMenu() {
    menuOpen = !menuOpen;
    UpdateInputState();
}

void Overlay::UpdateInputState() {
    if (!hwnd) return;
    if (menuOpen) {
        SetWindowLongW(hwnd, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    } else {
        SetWindowLongW(hwnd, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    }
}

bool Overlay::FindTargetWindow() {
    DWORD cs2Pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"cs2.exe") == 0) {
                    cs2Pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    if (!cs2Pid) return false;

    struct EnumData { DWORD pid; HWND hwnd; };
    EnumData ed = { cs2Pid, nullptr };

    EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* ed = (EnumData*)lparam;
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == ed->pid) {
            wchar_t title[128];
            GetWindowTextW(hwnd, title, 128);
            if (wcsstr(title, L"Counter-Strike") || wcsstr(title, L"CS2")) {
                if (IsWindowVisible(hwnd)) {
                    ed->hwnd = hwnd;
                    return FALSE;
                }
            }
        }
        return TRUE;
    }, (LPARAM)&ed);

    targetHwnd = ed.hwnd;
    return targetHwnd != nullptr;
}

bool Overlay::Create() {
    RandomizeClassName();

    if (!FindTargetWindow()) {
        for (int i = 0; i < 200; i++) {
            Sleep(50);
            if (FindTargetWindow()) break;
        }
        if (!targetHwnd) return false;
    }

    RECT rect;
    GetClientRect(targetHwnd, &rect);
    screenW = rect.right - rect.left;
    screenH = rect.bottom - rect.top;

    // Create D3D11 device (shared between DComp and classic paths)
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, nullptr, 0, D3D11_SDK_VERSION, &device, &fl, &context)))
        return false;

    g_overlay = this;

    // Use classic window for both rendering AND input (DComp has input issues)
    if (InitClassicWindow() && InitSwapChain()) {
        if (!FinishCreate()) { Cleanup(); return false; }
        dcompMode = false;
        initialized = true;
        return true;
    }

    // Fallback: DirectComposition (renders directly on CS2, but input is limited)
    bool dcompOk = InitDComp();
    if (dcompOk) {
        if (!FinishCreate()) { Cleanup(); return false; }
        dcompMode = true;
        initialized = true;
        return true;
    }

    Cleanup();
    return false;
}

bool Overlay::InitClassicWindow() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (m == WM_DESTROY) PostQuitMessage(0);
        if (m == WM_PAINT) { ValidateRect(h, nullptr); return 0; }
        if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1;
        return DefWindowProcW(h, m, w, l);
    };
    wc.hInstance = GetModuleHandle(nullptr);
    wchar_t wcName[16];
    for (int i = 0; i < 12; i++) wcName[i] = (unsigned char)windowClass[i];
    wcName[12] = 0;
    wc.lpszClassName = wcName;

    if (!RegisterClassExW(&wc)) return false;
    classRegistered = true;

    hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wcName, L"", WS_POPUP,
        overlayX, overlayY, screenW, screenH,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    if (!hwnd) return false;

    SetLayeredWindowAttributes(hwnd, RGB(0,0,0), 0, LWA_COLORKEY);
    SetWindowPos(hwnd, HWND_TOPMOST, overlayX, overlayY, screenW, screenH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    // Allow mouse to pass through when menu is closed
    menuOpen = false;
    UpdateInputState();
    return true;
}

bool Overlay::InitSwapChain() {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.Width = screenW;
    scd.BufferDesc.Height = screenH;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory* factory = nullptr;
    HRESULT hr = device->QueryInterface(&dxgiDevice);
    if (SUCCEEDED(hr)) hr = dxgiDevice->GetAdapter(&adapter);
    if (SUCCEEDED(hr)) hr = adapter->GetParent(__uuidof(IDXGIFactory), (void**)&factory);
    if (SUCCEEDED(hr)) hr = factory->CreateSwapChain(device, &scd, &swapChain);
    if (dxgiDevice) dxgiDevice->Release();
    if (adapter) adapter->Release();
    if (factory) factory->Release();
    if (FAILED(hr)) return false;

    ID3D11Texture2D* bb = nullptr;
    swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    device->CreateRenderTargetView(bb, nullptr, &rtView);
    bb->Release();
    return true;
}

bool Overlay::InitDComp() {
    HRESULT hr;
    hr = DCompositionCreateDevice(nullptr, __uuidof(IDCompositionDevice),
                                   (void**)&dcompDevice);
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = screenW;
    td.Height = screenH;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    hr = device->CreateTexture2D(&td, nullptr, &dcompTexture);
    if (FAILED(hr)) return false;

    IDXGISurface1* dxgiSurface = nullptr;
    hr = dcompTexture->QueryInterface(&dxgiSurface);
    if (FAILED(hr)) return false;

    hr = dcompDevice->CreateVisual(&dcompVisual);
    if (SUCCEEDED(hr)) {
        dcompVisual->SetContent(dxgiSurface);
        hr = dcompDevice->CreateTargetForHwnd(targetHwnd, TRUE, &dcompTarget);
        if (SUCCEEDED(hr)) {
            dcompTarget->SetRoot(dcompVisual);
            hr = dcompDevice->Commit();
        }
    }

    dxgiSurface->Release();
    return SUCCEEDED(hr);
}

bool Overlay::FinishCreate() {
    if (dcompMode) {
        D3D11_RENDER_TARGET_VIEW_DESC rtd = {};
        rtd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(dcompTexture, &rtd, &rtView);
    }

    // ImGui: for DComp use CS2's HWND (subclassed for input), for classic use overlay HWND
    HWND imguiHwnd = dcompMode ? targetHwnd : hwnd;
    if (!imguiHwnd)
        imguiHwnd = targetHwnd;
    ImGui_Init(imguiHwnd, device, context);

    return true;
}

void Overlay::CleanupDComp() {
    if (dcompVisual) { dcompVisual->Release(); dcompVisual = nullptr; }
    if (dcompTarget) { dcompTarget->Release(); dcompTarget = nullptr; }
    if (dcompDevice) { dcompDevice->Release(); dcompDevice = nullptr; }
    if (dcompTexture) { dcompTexture->Release(); dcompTexture = nullptr; }
}

void Overlay::Cleanup() {
    initialized = false;
    g_overlay = nullptr;
    ImGui_Shutdown();

    if (rtView) { rtView->Release(); rtView = nullptr; }
    if (context) { context->Release(); context = nullptr; }
    if (swapChain) { swapChain->Release(); swapChain = nullptr; }

    CleanupDComp();

    if (device) { device->Release(); device = nullptr; }

    if (hwnd && !dcompMode) { DestroyWindow(hwnd); hwnd = nullptr; }
    if (classRegistered) {
        wchar_t name[16];
        for (int i = 0; i < 12; i++) name[i] = windowClass[i];
        name[12] = 0;
        UnregisterClassW(name, GetModuleHandle(nullptr));
        classRegistered = false;
    }
}

void Overlay::Render(GameState* state, ExploitSelector* selector, Ragebot* ragebot) {
    if (!initialized || !state) { LogMessage("Overlay::Render: not initialized or null state"); return; }

    if (dcompMode) {
        RECT r;
        GetClientRect(targetHwnd, &r);
        screenW = r.right - r.left;
        screenH = r.bottom - r.top;
    } else if (targetHwnd) {
        RECT r;
        GetClientRect(targetHwnd, &r);
        int w = r.right - r.left, h = r.bottom - r.top;
        POINT pt = {0, 0};
        ClientToScreen(targetHwnd, &pt);
        if (w != screenW || h != screenH || pt.x != overlayX || pt.y != overlayY) {
            screenW = w; screenH = h;
            overlayX = pt.x; overlayY = pt.y;
            SetWindowPos(hwnd, HWND_TOPMOST, pt.x, pt.y, w, h, SWP_NOACTIVATE);
        }
    }

    ImGui_NewFrame();
    BeginDraw();

    if (settings.showBox || settings.showName || settings.showHealth ||
        settings.showWeapon)
        RenderESP(state);

    if (settings.showSkeleton) {
        for (int i = 0; i < 64; i++) {
            auto& p = state->players[i];
            if (p.IsValid() && p.IsEnemy(state->localTeam))
                RenderSkeleton(state, i);
        }
    }

    if (settings.showRadar) RenderRadar(state);
    if (settings.showNadeUI) RenderNadeUI(state);
    if (settings.showCrosshair) RenderAimbotFov(state);
    RenderStatus(state, selector, ragebot);
    RenderWatermark();

    if (menuOpen) {
        RenderImGui(ragebot);
    }

    EndDraw();
    ImGui_EndFrame();

    if (dcompMode) {
        dcompDevice->Commit();
    } else if (swapChain) {
        swapChain->Present(0, 0);
    }
}

// ==================== DRAWING PRIMITIVES ====================

static ImDrawList* DL() { return ImGui::GetBackgroundDrawList(); }
static ImU32 C(float* col) { return ImGui::ColorConvertFloat4ToU32(ImVec4(col[0],col[1],col[2],col[3])); }

void Overlay::BeginDraw() {
    context->OMSetRenderTargets(1, &rtView, nullptr);
    float clear[] = {0, 0, 0, 0};
    context->ClearRenderTargetView(rtView, clear);
}

void Overlay::EndDraw() {}

void Overlay::DrawLine(int x1, int y1, int x2, int y2, float* color, float thickness) {
    DL()->AddLine(ImVec2((float)x1,(float)y1), ImVec2((float)x2,(float)y2), C(color), thickness);
}

void Overlay::DrawBox(int x, int y, int w, int h, float* color, float thickness) {
    DL()->AddRect(ImVec2((float)x,(float)y), ImVec2((float)(x+w),(float)(y+h)), C(color), 0.f, 0, thickness);
}

void Overlay::DrawRect(int x, int y, int w, int h, float* color, bool filled) {
    if (filled)
        DL()->AddRectFilled(ImVec2((float)x,(float)y), ImVec2((float)(x+w),(float)(y+h)), C(color));
    else
        DL()->AddRect(ImVec2((float)x,(float)y), ImVec2((float)(x+w),(float)(y+h)), C(color), 0.f, 0, 1.f);
}

void Overlay::DrawCornerBox(int x, int y, int w, int h, float* color, float thickness) {
    float fx=(float)x, fy=(float)y, fw=(float)w, fh=(float)h;
    float lx=fw*0.22f, ly=fh*0.22f;
    ImU32 c = C(color); auto dl = DL();
    dl->AddLine(ImVec2(fx,fy), ImVec2(fx+lx,fy), c, thickness);
    dl->AddLine(ImVec2(fx,fy), ImVec2(fx,fy+ly), c, thickness);
    dl->AddLine(ImVec2(fx+fw,fy), ImVec2(fx+fw-lx,fy), c, thickness);
    dl->AddLine(ImVec2(fx+fw,fy), ImVec2(fx+fw,fy+ly), c, thickness);
    dl->AddLine(ImVec2(fx,fy+fh), ImVec2(fx+lx,fy+fh), c, thickness);
    dl->AddLine(ImVec2(fx,fy+fh), ImVec2(fx,fy+fh-ly), c, thickness);
    dl->AddLine(ImVec2(fx+fw,fy+fh), ImVec2(fx+fw-lx,fy+fh), c, thickness);
    dl->AddLine(ImVec2(fx+fw,fy+fh), ImVec2(fx+fw,fy+fh-ly), c, thickness);
}

void Overlay::DrawCircle(int cx, int cy, int r, float* color, int segments) {
    DL()->AddCircle(ImVec2((float)cx,(float)cy), (float)r, C(color), segments, 1.f);
}

void Overlay::DrawText(const char* text, int x, int y, float* color, bool centered) {
    if (text && text[0])
        ImGui_DrawText(text, (float)x, (float)y, color[0], color[1], color[2], color[3], centered);
}

void Overlay::DrawHealthBar(int x, int y, int w, int h, int health, int maxHealth) {
    float ratio = (std::max)(0.0f, (std::min)(1.0f, (float)health / (float)maxHealth));
    float bg[4] = {0.2f, 0.2f, 0.2f, 0.8f};
    DrawRect(x, y, w, h, bg, true);
    float hc[4];
    if (ratio > 0.5f) { hc[0]=0; hc[1]=1; hc[2]=0; hc[3]=1; }
    else if (ratio > 0.25f) { hc[0]=1; hc[1]=0.8f; hc[2]=0; hc[3]=1; }
    else { hc[0]=1; hc[1]=0; hc[2]=0; hc[3]=1; }
    int fh = (int)(h * ratio);
    DrawRect(x, y + h - fh, w, fh, hc, true);
}

// ==================== ESP / RADAR / MENU ====================

bool Overlay::GetPlayerBox(GameState* state, int idx, int& outX, int& outY, int& outW, int& outH) {
    auto& p = state->players[idx];
    float* vm = state->viewMatrix;

    Vector3 head3D = IsValidBonePos(p.bonePos[6]) ? p.bonePos[6] : p.GetHeadPos();
    Vector3 feet3D = p.origin;

    head3D.z += 7.0f;

    Vector2 headScreen, feetScreen;
    if (!WorldToScreen(head3D, headScreen, vm)) return false;
    if (!WorldToScreen(feet3D, feetScreen, vm)) return false;

    float outHf = feetScreen.y - headScreen.y;
    if (outHf < 5.0f || outHf > (float)screenH * 1.5f) return false;

    float outWf = outHf * settings.boxWidthRatio;

    outX = (int)(headScreen.x - outWf * 0.5f);
    outY = (int)headScreen.y;
    outW = (int)outWf;
    outH = (int)outHf;

    return outW >= 4;
}

void Overlay::DrawBoneLine(Vector3 from, Vector3 to, float* vm, float* color) {
    Vector2 sFrom, sTo;
    if (!WorldToScreen(from, sFrom, vm)) return;
    if (!WorldToScreen(to, sTo, vm)) return;
    DrawLine((int)sFrom.x, (int)sFrom.y, (int)sTo.x, (int)sTo.y, color);
}

void Overlay::RenderESP(GameState* state) {
    auto* local = state->GetLocal();
    if (!local) return;

    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;

        int bx, by, bw, bh;
        if (!GetPlayerBox(state, i, bx, by, bw, bh)) continue;

        // === BOX ===
        if (settings.showBox) {
            float outline[4] = {0, 0, 0, 1};
            float boxCol[4] = { settings.boxColor[0], settings.boxColor[1], settings.boxColor[2], settings.boxColor[3] };
            // Draw outline
            if (settings.boxStyle == 0) {
                DrawBox(bx - 1, by - 1, bw + 2, bh + 2, outline, 2);
                DrawBox(bx, by, bw, bh, boxCol, 1);
            } else if (settings.boxStyle == 1) {
                DrawCornerBox(bx - 1, by - 1, bw + 2, bh + 2, outline, 2);
                DrawCornerBox(bx, by, bw, bh, boxCol, 1.5f);
            } else if (settings.boxStyle == 2) {
                float fillCol[4] = { boxCol[0], boxCol[1], boxCol[2], boxCol[3] * 0.3f };
                DrawRect(bx, by, bw, bh, fillCol, true);
                DrawBox(bx, by, bw, bh, boxCol, 1);
            }
        }

        // === HEALTH BAR (left side of box) ===
        if (settings.showHealth) {
            DrawHealthBar(bx - 6, by, 4, bh, p.health, 100);
        }

        // === NAME (above box) ===
        if (settings.showName && p.name[0]) {
            float white[4] = {1, 1, 1, 1};
            float shadow[4] = {0, 0, 0, 0.7f};
            DrawText(p.name, bx + bw/2 + 1, by - 13, shadow, true);
            DrawText(p.name, bx + bw/2, by - 14, white, true);
        }

        // === WEAPON (below box) ===
        if (settings.showWeapon) {
            float yellow[4] = {1, 1, 0, 0.9f};
            float shadow[4] = {0, 0, 0, 0.7f};
            const char* weaponName = "?";
            switch (p.weaponId) {
                case 1: weaponName = "Deagle"; break;
                case 2: weaponName = "Duals"; break;
                case 3: weaponName = "5-7"; break;
                case 4: weaponName = "Glock"; break;
                case 7: weaponName = "AK-47"; break;
                case 8: weaponName = "AUG"; break;
                case 9: weaponName = "AWP"; break;
                case 10: weaponName = "FAMAS"; break;
                case 11: weaponName = "G3SG1"; break;
                case 13: weaponName = "Galil"; break;
                case 14: weaponName = "M249"; break;
                case 16: weaponName = "M4A4"; break;
                case 17: weaponName = "MAC-10"; break;
                case 19: weaponName = "P90"; break;
                case 23: weaponName = "MP5-SD"; break;
                case 24: weaponName = "UMP-45"; break;
                case 25: weaponName = "XM1014"; break;
                case 26: weaponName = "Bizon"; break;
                case 27: weaponName = "MAG-7"; break;
                case 28: weaponName = "Negev"; break;
                case 29: weaponName = "Sawed-Off"; break;
                case 30: weaponName = "Tec-9"; break;
                case 32: weaponName = "P2000"; break;
                case 33: weaponName = "MP7"; break;
                case 34: weaponName = "MP9"; break;
                case 35: weaponName = "Nova"; break;
                case 36: weaponName = "P250"; break;
                case 38: weaponName = "SCAR-20"; break;
                case 39: weaponName = "SG 553"; break;
                case 40: weaponName = "SSG 08"; break;
                case 60: weaponName = "M4A1-S"; break;
                case 61: weaponName = "USP-S"; break;
                case 63: weaponName = "CZ75"; break;
                case 64: weaponName = "R8 Revolver"; break;
                case 80: weaponName = "Zeus"; break;
                case 81: weaponName = "Flash"; break;
                case 82: weaponName = "HE"; break;
                case 83: weaponName = "Smoke"; break;
                case 84: weaponName = "Decoy"; break;
                case 86: weaponName = "Molotov"; break;
                case 87: weaponName = "Incendiary"; break;
                case 88: weaponName = "C4"; break;
            }
            DrawText(weaponName, bx + bw/2 + 1, by + bh + 3, shadow, true);
            DrawText(weaponName, bx + bw/2, by + bh + 2, yellow, true);
        }
    }
}

void Overlay::RenderRadar(GameState* state) {
    int rx = screenW - 220, ry = 20, rs = 200;
    int cx = rx + rs/2, cy = ry + rs/2;
    auto* local = state->GetLocal();
    if (!local) return;

    float bg[4] = {0, 0, 0, 0.5f};
    DrawRect(rx, ry, rs, rs, bg, true);
    float border[4] = {0.5f, 0.5f, 0.5f, 1};
    DrawBox(rx, ry, rs, rs, border);
    float lc[4] = {0, 1, 0, 1};
    DrawCircle(cx, cy, 3, lc);

    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;
        Vector3 d = p.origin - local->origin;
        int mx = cx + (int)(d.y * 0.5f);
        int my = cy - (int)(d.x * 0.5f);
        if (mx >= rx && mx <= rx+rs && my >= ry && my <= ry+rs) {
            float ec[4] = {1, 0, 0, 1};
            DrawCircle(mx, my, 2, ec);
        }
    }
}

void Overlay::RenderNadeUI(GameState*) {}

void Overlay::RenderMenu(Ragebot* ragebot) {
    float bg[4] = {0.1f, 0.1f, 0.15f, 0.9f};
    DrawRect(screenW/2 - 250, screenH/2 - 200, 500, 400, bg, true);
    float title[4] = {0, 0.8f, 1, 1};
    DrawText("PROJECT CHRONOS", screenW/2, screenH/2 - 190, title, true);
    int y = screenH/2 - 160, x = screenW/2 - 200;
    float green[4] = {0, 1, 0, 1}, grey[4] = {0.5f, 0.5f, 0.5f, 1};

    auto toggle = [&](const char* n, bool& v, int& yy) {
        DrawText((std::string("  [") + (v?"ON":"OFF") + "] " + n).c_str(), x, yy, v?green:grey, false);
        yy += 20;
    };
    toggle("ESP Box", settings.showBox, y);
    toggle("ESP Name", settings.showName, y);
    toggle("ESP Health", settings.showHealth, y);
    toggle("ESP Weapon", settings.showWeapon, y);
    toggle("Radar", settings.showRadar, y);
    toggle("Skeleton", settings.showSkeleton, y);
    y += 10;
    float inf[4] = {0, 0.8f, 1, 1};
    DrawText(("Level: " + std::to_string(settings.exploitLevel)).c_str(), x, y, inf, false);
    y += 20;
    if (ragebot && ragebot->HasAimTarget()) {
        DrawText(("Target: " + std::to_string(ragebot->GetCurrentTarget()) + " HC:" + std::to_string((int)ragebot->GetCurrentHitchance())).c_str(), x, y, green, false);
        y += 20;
    }
    DrawText("INS=Menu END=Exit", x, y + 30, grey, false);
}

void Overlay::RenderStatus(GameState* state, ExploitSelector*, Ragebot* ragebot) {
    if (!state) return;
    float green[4] = {0, 1, 0, 1}, grey[4] = {0.5f, 0.5f, 0.5f, 1};
    std::string s = state->GetLocal() ? "Active" : "Waiting...";
    DrawText(s.c_str(), 10, 10, green, false);
    DrawText(("Players: " + std::to_string(state->playerCount)).c_str(), 10, 30, grey, false);
}

// ==================== WORLD TO SCREEN ====================

bool Overlay::WorldToScreen(Vector3 wp, Vector2& sp, float* m) {
    if (!m) { sp.x = (float)screenW / 2; sp.y = (float)screenH / 2; return true; }

    float clipW = wp.x * m[12] + wp.y * m[13] + wp.z * m[14] + m[15];
    if (clipW < 0.001f) return false;

    float clipX = wp.x * m[0] + wp.y * m[1] + wp.z * m[2] + m[3];
    float clipY = wp.x * m[4] + wp.y * m[5] + wp.z * m[6] + m[7];

    float ndcX = clipX / clipW;
    float ndcY = clipY / clipW;

    if (fabsf(ndcX) > 2.0f || fabsf(ndcY) > 2.0f) return false;

    sp.x = (screenW / 2.0f * ndcX) + (screenW / 2.0f);
    sp.y = -(screenH / 2.0f * ndcY) + (screenH / 2.0f);

    return true;
}


