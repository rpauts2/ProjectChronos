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
#include <cstring>
#include <tlhelp32.h>

Overlay* g_overlay = nullptr;

#pragma comment(lib, "dwmapi.lib")

static ImDrawList* DL() { return ImGui::GetBackgroundDrawList(); }
static ImU32 C(float* col) { return ImGui::ColorConvertFloat4ToU32(ImVec4(col[0],col[1],col[2],col[3])); }

static bool IsValidBonePos(const Vector3& v, const Vector3& origin = {}) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
        return false;
    if (fabsf(v.x) > 100000.f || fabsf(v.y) > 100000.f || fabsf(v.z) > 100000.f)
        return false;
    if (fabsf(v.x) < 0.001f && fabsf(v.y) < 0.001f && fabsf(v.z) < 0.001f)
        return false;
    if (origin.x != 0 || origin.y != 0 || origin.z != 0) {
        float dx = v.x - origin.x, dy = v.y - origin.y, dz = v.z - origin.z;
        if (dx*dx + dy*dy + dz*dz > 100000.f) return false;
    }
    return true;
}

void Overlay::RandomizeClassName() {
    NameGen::RandomString(windowClass, 12);
}

void Overlay::ToggleMenu() {
    menuOpen = !menuOpen;
    pendingStyleUpdate = true;
}

void Overlay::UpdateInputState() {
    if (!hwnd) return;
    if (menuOpen) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    } else {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    }
}

void Overlay::ApplyPendingStyle() {
    if (!pendingStyleUpdate) return;
    pendingStyleUpdate = false;
    UpdateInputState();
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

// ==================== EXPLOIT FLASH INDICATOR ====================

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

// Hit marker state (used by DrawHitMarker and TriggerHitMarker)
static float g_hitMarkerTime = 0;
static float g_hitMarkerDamage = 0;

void Overlay::PlayExploitSound() {
    if (!settings.exploitSound) return;
    MessageBeep(MB_OK);
}

void Overlay::TriggerExploitFlash(float r, float g, float b, float a, float dur) {
    exploitFlash.active = true;
    exploitFlash.startTime = (float)ImGui::GetTime();
    exploitFlash.duration = dur;
    exploitFlash.color[0] = r;
    exploitFlash.color[1] = g;
    exploitFlash.color[2] = b;
    exploitFlash.color[3] = a;
    PlayExploitSound();
}

void Overlay::TriggerHitMarker(float damage) {
    g_hitMarkerTime = (float)ImGui::GetTime();
    g_hitMarkerDamage = damage;
}

void Overlay::RenderExploitFlash() {
    if (!settings.showExploitFlash) return;
    if (!exploitFlash.active) return;
    float now = (float)ImGui::GetTime();
    float elapsed = now - exploitFlash.startTime;
    if (elapsed > exploitFlash.duration) {
        exploitFlash.active = false;
        return;
    }
    float progress = elapsed / exploitFlash.duration;
    float alpha = exploitFlash.color[3] * (1.0f - progress);
    if (alpha < 0.005f) return;

    auto dl = DL();

    // Screen-edge flash (bottom gradient bar)
    float barH = 8.0f * (1.0f - progress);
    ImU32 col1 = ImGui::GetColorU32(ImVec4(exploitFlash.color[0], exploitFlash.color[1], exploitFlash.color[2], alpha));
    ImU32 col2 = ImGui::GetColorU32(ImVec4(exploitFlash.color[0], exploitFlash.color[1], exploitFlash.color[2], 0));
    dl->AddRectFilledMultiColor(ImVec2(0, (float)screenH - barH),
                                ImVec2((float)screenW, (float)screenH), col1, col1, col2, col2);

    // Center crosshair flash
    float crossAlpha = alpha * 0.6f;
    float crossSize = 20.0f + 30.0f * progress;
    float cx = (float)screenW * 0.5f, cy = (float)screenH * 0.5f;
    ImU32 crossCol = ImGui::GetColorU32(ImVec4(exploitFlash.color[0], exploitFlash.color[1], exploitFlash.color[2], crossAlpha));
    dl->AddCircle(ImVec2(cx, cy), crossSize, crossCol, 32, 2.0f);

    // Corner indicators
    float cornerAlpha = alpha * 0.4f;
    ImU32 cornerCol = ImGui::GetColorU32(ImVec4(exploitFlash.color[0], exploitFlash.color[1], exploitFlash.color[2], cornerAlpha));
    float cs = 40.0f * (1.0f - progress);
    // Top-left
    dl->AddLine(ImVec2(20, 20), ImVec2(20 + cs, 20), cornerCol, 2.0f);
    dl->AddLine(ImVec2(20, 20), ImVec2(20, 20 + cs), cornerCol, 2.0f);
    // Top-right
    dl->AddLine(ImVec2((float)screenW - 20, 20), ImVec2((float)screenW - 20 - cs, 20), cornerCol, 2.0f);
    dl->AddLine(ImVec2((float)screenW - 20, 20), ImVec2((float)screenW - 20, 20 + cs), cornerCol, 2.0f);
    // Bottom-left
    dl->AddLine(ImVec2(20, (float)screenH - 20), ImVec2(20 + cs, (float)screenH - 20), cornerCol, 2.0f);
    dl->AddLine(ImVec2(20, (float)screenH - 20), ImVec2(20, (float)screenH - 20 - cs), cornerCol, 2.0f);
    // Bottom-right
    dl->AddLine(ImVec2((float)screenW - 20, (float)screenH - 20), ImVec2((float)screenW - 20 - cs, (float)screenH - 20), cornerCol, 2.0f);
    dl->AddLine(ImVec2((float)screenW - 20, (float)screenH - 20), ImVec2((float)screenW - 20, (float)screenH - 20 - cs), cornerCol, 2.0f);
}

void Overlay::Render(GameState* state, ExploitSelector* selector, AimController* aimController) {
    if (!initialized || !state) { return; }
    cachedAim = aimController;

    // Init smooth boxes on first frame
    static bool smoothInited = false;
    if (!smoothInited) { std::memset(smoothBox, 0, sizeof(smoothBox)); smoothInited = true; }

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
        settings.showWeapon || settings.showSkeleton || settings.showGlow || settings.showSnaplines)
        RenderESP(state);

    if (settings.showRadar) RenderRadar(state);
    if (settings.showNadeUI) RenderNadeUI(state);
    if (settings.showCrosshair) RenderAimbotFov(state);
    if (settings.showRecoilCrosshair) DrawRecoilCrosshair(state);
    if (settings.showVelocity) DrawVelocity(state);
    if (settings.showScopeOverlay) DrawScopeOverlay(state);
    if (settings.antiFlashEnabled) {
        auto* localAF = state ? state->GetLocal() : nullptr;
        if (localAF && localAF->flashed) {
            auto dlAF = DL();
            float flashAlpha = 0.15f + 0.05f * sinf((float)ImGui::GetTime() * 8.0f);
            ImU32 flashTint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.8f, flashAlpha));
            dlAF->AddRectFilled(ImVec2(0, 0), ImVec2((float)screenW, (float)screenH), flashTint);
            float flashIndCol[4] = { 1.0f, 1.0f, 0.3f, 0.9f };
            DrawTextOutlined("FLASHED", screenW / 2, 80, flashIndCol, true);
        }
    }
    if (settings.showSpectators) DrawSpectators(state);
    if (settings.showSoundESP) DrawSoundESP(state);
    if (settings.showDroppedWeapons) DrawDroppedWeapons(state);
    DrawHitMarker();
    RenderStatus(state, selector, aimController);
    RenderWatermark();
    RenderExploitFlash();

    if (menuOpen) {
        RenderImGui(aimController);
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
    float lx=fw*0.25f, ly=fh*0.25f;
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
    auto dl = DL();
    // Thin, clean bar — no glossy highlight
    int barW = 3;
    int barX = x + (w - barW) / 2;
    ImU32 bgC = ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.08f, 0.7f));
    dl->AddRectFilled(ImVec2((float)barX, (float)y), ImVec2((float)(barX + barW), (float)(y + h)), bgC, 1.5f);
    int fh = (int)(h * ratio);
    if (fh < 1) return;
    // Simple gradient: green→yellow→red
    float r, g, b;
    if (ratio > 0.6f) { r = 0.15f; g = 0.85f; b = 0.15f; }
    else if (ratio > 0.3f) { r = 1.0f; g = 0.75f; b = 0.0f; }
    else { r = 0.95f; g = 0.15f; b = 0.15f; }
    ImU32 topC = ImGui::GetColorU32(ImVec4(r, g, b, 0.95f));
    ImU32 botC = ImGui::GetColorU32(ImVec4(r * 0.6f, g * 0.6f, b * 0.6f, 0.95f));
    dl->AddRectFilledMultiColor(ImVec2((float)barX, (float)(y + h - fh)),
                                ImVec2((float)(barX + barW), (float)(y + h)),
                                topC, topC, botC, botC);
}

// ==================== WEAPON NAMES (compact icons) ====================

static std::string WeaponName(int id) {
    switch (id) {
        case 1: return "DEAGLE"; case 2: return "ELITE"; case 3: return "5-7";
        case 4: return "G18"; case 7: return "AK"; case 8: return "AUG";
        case 9: return "AWP"; case 10: return "FAMAS"; case 11: return "G3";
        case 13: return "GALIL"; case 14: return "M249"; case 16: return "M4A4";
        case 17: return "MAC"; case 19: return "P90"; case 23: return "MP5";
        case 24: return "UMP"; case 25: return "XM"; case 26: return "BIZON";
        case 27: return "MAG"; case 28: return "NEGEV"; case 29: return "SAWED";
        case 30: return "TEC9"; case 31: return "TASE"; case 32: return "P2K";
        case 33: return "MP7"; case 34: return "MP9"; case 35: return "NOVA";
        case 36: return "P250"; case 38: return "SCAR"; case 39: return "SG";
        case 40: return "SSG"; case 42: return "KNIFE"; case 43: return "FLASH";
        case 44: return "HE"; case 45: return "SMOKE"; case 46: return "MOLLY";
        case 47: return "DECOY"; case 48: return "INC"; case 49: return "C4";
        case 59: return "KNIFE"; case 60: return "M4A1"; case 61: return "USP";
        case 63: return "CZ"; case 64: return "REV";
        default: return "";
    }
}

const char* Overlay::WeaponIcon(int id) {
    switch (id) {
        case 7: return "[AK]";     case 9: return "[AWP]";    case 16: return "[M4]";
        case 60: return "[M4A1]";  case 8: return "[AUG]";    case 39: return "[SG]";
        case 40: return "[SSG]";   case 11: return "[G3]";    case 38: return "[SCAR]";
        case 1: return "[DE]";     case 32: return "[P2K]";   case 61: return "[USP]";
        case 36: return "[P250]";  case 4: return "[G18]";    case 30: return "[T9]";
        case 3: return "[57]";     case 63: return "[CZ]";    case 64: return "[REV]";
        case 2: return "[DG]";     case 33: return "[MP7]";   case 34: return "[MP9]";
        case 17: return "[MAC]";   case 24: return "[UMP]";   case 19: return "[P90]";
        case 23: return "[MP5]";   case 26: return "[BIZ]";   case 10: return "[FAM]";
        case 13: return "[GAL]";   case 25: return "[XM]";    case 27: return "[MAG]";
        case 35: return "[NOV]";   case 29: return "[SAW]";   case 14: return "[M249]";
        case 28: return "[NEG]";   case 42: return "[K]";     case 59: return "[K]";
        case 31: return "[ZEU]";   case 49: return "[C4]";
        default: return "";
    }
}

static bool IsSniperWeapon(int id) {
    return id == 9 || id == 11 || id == 38 || id == 40;
}

// ==================== FLAG INDICATORS (compact icons) ====================

void Overlay::DrawFlags(int bx, int by, int bw, int bh, GameState* state, int idx, float distFade) {
    auto& p = state->players[idx];
    if (!p.IsValid()) return;
    auto dl = DL();

    int px = bx + bw + 6;
    int py = by;
    float time = (float)ImGui::GetTime();
    float pulse = 0.85f + 0.15f * sinf(time * 4.0f);

    // Flag style: small colored pill with text
    auto drawFlag = [&](const char* label, float r, float g, float b) {
        ImVec2 ts = ImGui::CalcTextSize(label);
        float fw = ts.x + 8.0f;
        float fh = ts.y + 2.0f;
        ImU32 bg = ImGui::GetColorU32(ImVec4(r * 0.3f, g * 0.3f, b * 0.3f, 0.75f * distFade));
        ImU32 border = ImGui::GetColorU32(ImVec4(r, g, b, 0.9f * distFade));
        ImU32 txt = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.95f * distFade));
        dl->AddRectFilled(ImVec2((float)px, (float)py), ImVec2((float)px + fw, (float)py + fh), bg, 3.0f);
        dl->AddRect(ImVec2((float)px, (float)py), ImVec2((float)px + fw, (float)py + fh), border, 3.0f, 0, 0.8f);
        dl->AddText(ImVec2((float)px + 4, (float)py + 1), txt, label);
        py += (int)fh + 2;
    };

    if (p.scoped) drawFlag("SCOPE", 0.2f, 0.7f, 1.0f);
    if (p.flashed) drawFlag("FLASH", 1.0f, 0.9f, 0.2f);
    if (p.weaponId == 9 || p.weaponId == 11 || p.weaponId == 38)
        drawFlag("SNIPER", 0.9f, 0.2f, 0.2f);
    if (p.armor > 0)
        drawFlag("ARM", 0.2f, 0.8f, 0.4f);
}

// ==================== HEALTH COLOR HELPER ====================

static void HealthColor(float ratio, float* out) {
    if (ratio > 0.7f) { out[0]=0.1f; out[1]=1.0f; out[2]=0.1f; out[3]=1; }
    else if (ratio > 0.4f) { out[0]=1.0f; out[1]=0.8f; out[2]=0; out[3]=1; }
    else if (ratio > 0.2f) { out[0]=1.0f; out[1]=0.5f; out[2]=0; out[3]=1; }
    else { out[0]=1.0f; out[1]=0.1f; out[2]=0.1f; out[3]=1; }
}

// ==================== ESP / RADAR / MENU ====================

bool Overlay::GetPlayerBox(GameState* state, int idx, int& outX, int& outY, int& outW, int& outH) {
    auto& p = state->players[idx];
    Vector2 head, feet;

    if (!WorldToScreen(p.bonePos[6], head, state->viewMatrix)) return false;
    if (!WorldToScreen(p.origin, feet, state->viewMatrix)) return false;

    float h = feet.y - head.y;
    float w = h * settings.boxWidthRatio;

    outX = (int)(head.x - w * 0.5f);
    outY = (int)head.y;
    outW = (int)w;
    outH = (int)h;

    return (h > 10 && h < screenH * 2);
}

void Overlay::DrawBoneLine(Vector3 from, Vector3 to, float* vm, float* color) {
    Vector2 sFrom, sTo;
    if (!WorldToScreen(from, sFrom, vm)) return;
    if (!WorldToScreen(to, sTo, vm)) return;
    DrawLine((int)sFrom.x, (int)sFrom.y, (int)sTo.x, (int)sTo.y, color);
}

// ==================== HEALTH-BASED COLOR ====================

void Overlay::DrawHealthBasedColor(float health, float* outColor) {
    float ratio = (std::max)(0.0f, (std::min)(1.0f, health / 100.0f));
    if (ratio > 0.6f) {
        outColor[0] = 0.1f + (1.0f - ratio) * 0.6f;
        outColor[1] = 0.8f + (ratio - 0.6f) * 0.5f;
        outColor[2] = 0.1f;
    } else if (ratio > 0.3f) {
        outColor[0] = 1.0f;
        outColor[1] = 0.5f + (ratio - 0.3f) * 1.0f;
        outColor[2] = 0.0f;
    } else {
        outColor[0] = 0.9f + ratio * 0.33f;
        outColor[1] = 0.1f;
        outColor[2] = 0.1f;
    }
    outColor[3] = 1.0f;
}

// All 30 bones with size factors — dense silhouette glow
static const float kAllBoneSizes[30] = {
    0.12f, 0.12f, 0.14f, 0.15f, 0.16f, 0.18f,
    0.20f, 0.08f, 0.08f, 0.10f, 0.08f, 0.07f,
    0.07f, 0.10f, 0.08f, 0.07f, 0.07f, 0.07f,
    0.07f, 0.07f, 0.07f, 0.07f, 0.12f, 0.10f,
    0.09f, 0.12f, 0.10f, 0.09f, 0.07f, 0.07f
};

static bool ProjectBone(const Vector3& w, float* vm, int sw, int sh, float& ox, float& oy) {
    float cw = w.x * vm[12] + w.y * vm[13] + w.z * vm[14] + vm[15];
    if (cw < 0.001f) return false;
    float sx = vm[0]*w.x + vm[1]*w.y + vm[2]*w.z + vm[3];
    float sy = vm[4]*w.x + vm[5]*w.y + vm[6]*w.z + vm[7];
    ox = sw * 0.5f + (sx / cw) * sw * 0.5f;
    oy = sh * 0.5f - (sy / cw) * sh * 0.5f;
    return true;
}

// ==================== ENTITY SHELL GLOW (subtle, clean) ====================

void Overlay::DrawGlowShell(const Vector3 bones[30], float* vm, int screenW, int screenH,
                             float* color, float alphaPeak, int style, float time) {
    auto dl = DL();

    struct ScreenBone { float x, y; bool valid; };
    ScreenBone sb[30];
    for (int i = 0; i < 30; i++) {
        Vector2 s;
        if (!WorldToScreen(bones[i], s, vm)) { sb[i] = {0,0,false}; continue; }
        sb[i] = { s.x, s.y, true };
    }

    float effColor[4] = { color[0], color[1], color[2], alphaPeak };
    if (style == 2) {
        float pulse = 0.7f + 0.3f * sinf(time * 2.0f);
        effColor[3] *= pulse;
    } else if (style == 3) {
        float hue = fmodf(time * 0.15f, 1.0f);
        float r, g, b;
        if (hue < 1/6.f) { r=1; g=hue*6; b=0; }
        else if (hue < 2/6.f) { r=1-(hue-1/6.f)*6; g=1; b=0; }
        else if (hue < 3/6.f) { r=0; g=1; b=(hue-2/6.f)*6; }
        else if (hue < 4/6.f) { r=0; g=1-(hue-3/6.f)*6; b=1; }
        else if (hue < 5/6.f) { r=(hue-4/6.f)*6; g=0; b=1; }
        else { r=1; g=0; b=1-(hue-5/6.f)*6; }
        effColor[0] = r; effColor[1] = g; effColor[2] = b;
    }

    static const int kShellChains[][2] = {
        {6,5},{5,4},{4,3},{3,2},{2,0},
        {5,8},{8,9},{9,10},{10,11},
        {5,13},{13,14},{14,15},{15,16},
        {0,22},{22,23},{23,24},{24,25},
        {0,25},{25,26},{26,27},{27,28}
    };

    float outlineAlpha = effColor[3] * 0.3f;
    if (outlineAlpha > 0.003f) {
        ImU32 lc = ImGui::GetColorU32(ImVec4(effColor[0], effColor[1], effColor[2], outlineAlpha));
        for (auto& ch : kShellChains) {
            if (!sb[ch[0]].valid || !sb[ch[1]].valid) continue;
            dl->AddLine(ImVec2(sb[ch[0]].x, sb[ch[0]].y),
                        ImVec2(sb[ch[1]].x, sb[ch[1]].y), lc, 2.5f);
        }
    }

    // Pass 2: Wide outer glow for bloom effect
    float outerGlowAlpha = effColor[3] * 0.08f;
    if (outerGlowAlpha > 0.002f) {
        ImU32 og = ImGui::GetColorU32(ImVec4(effColor[0], effColor[1], effColor[2], outerGlowAlpha));
        for (auto& ch : kShellChains) {
            if (!sb[ch[0]].valid || !sb[ch[1]].valid) continue;
            dl->AddLine(ImVec2(sb[ch[0]].x, sb[ch[0]].y),
                        ImVec2(sb[ch[1]].x, sb[ch[1]].y), og, 8.0f);
        }
    }

    // Pass 3: Inner bright core for depth
    float coreAlpha = effColor[3] * 0.5f;
    if (coreAlpha > 0.003f) {
        float bright[4] = { (std::min)(1.0f, effColor[0] + 0.3f),
                            (std::min)(1.0f, effColor[1] + 0.3f),
                            (std::min)(1.0f, effColor[2] + 0.3f), coreAlpha };
        ImU32 cc = ImGui::GetColorU32(ImVec4(bright[0], bright[1], bright[2], bright[3]));
        for (auto& ch : kShellChains) {
            if (!sb[ch[0]].valid || !sb[ch[1]].valid) continue;
            dl->AddLine(ImVec2(sb[ch[0]].x, sb[ch[0]].y),
                        ImVec2(sb[ch[1]].x, sb[ch[1]].y), cc, 1.0f);
        }
    }

    float fillAlpha = effColor[3] * 0.04f;
    if (fillAlpha > 0.002f) {
        ImU32 fc = ImGui::GetColorU32(ImVec4(effColor[0], effColor[1], effColor[2], fillAlpha));
        for (auto& ch : kShellChains) {
            if (!sb[ch[0]].valid || !sb[ch[1]].valid) continue;
            dl->AddLine(ImVec2(sb[ch[0]].x, sb[ch[0]].y),
                        ImVec2(sb[ch[1]].x, sb[ch[1]].y), fc, 5.0f);
        }
    }
}

// ==================== PRESET SYSTEM ====================

void Overlay::ApplyPreset(int presetId) {
    settings.activePreset = presetId;
    switch (presetId) {
        case 1: // E-Sports — clean white boxes, minimal
            settings.showBox = true; settings.showSkeleton = false; settings.showHealth = true;
            settings.showWeapon = false; settings.showFlags = false; settings.showDistance = false;
            settings.boxStyle = 1; settings.boxThickness = 1.0f; settings.boxWidthRatio = 0.35f;
            settings.boxColor[0]=1; settings.boxColor[1]=1; settings.boxColor[2]=1; settings.boxColor[3]=1;
            settings.showGlow = false; settings.glowBloom = false; settings.healthStyle = 0;
            settings.showOOVIndicators = false; settings.showMoney = false;
            break;
        case 2: // Chronos Pro — neon purple, rounded, gradient health
            settings.showBox = true; settings.showSkeleton = true; settings.showHealth = true;
            settings.showWeapon = true; settings.showFlags = true; settings.showDistance = true;
            settings.boxStyle = 3; settings.boxThickness = 1.8f; settings.boxWidthRatio = 0.40f;
            settings.boxColor[0]=0.5f; settings.boxColor[1]=0.2f; settings.boxColor[2]=1.0f; settings.boxColor[3]=1;
            settings.skeletonColor[0]=0.5f; settings.skeletonColor[1]=0.2f; settings.skeletonColor[2]=1.0f; settings.skeletonColor[3]=0.6f;
            settings.showGlow = true; settings.glowAlpha = 0.35f; settings.glowBloom = true; settings.glowThickness = 5.f;
            settings.glowColor[0]=0.5f; settings.glowColor[1]=0.2f; settings.glowColor[2]=1.0f; settings.glowColor[3]=0.5f;
            settings.healthStyle = 2; settings.healthBarPos = 0;
            settings.showOOVIndicators = true; settings.showMoney = true;
            break;
        case 3: // Lethal — aggressive red, full info
            settings.showBox = true; settings.showSkeleton = true; settings.showHealth = true;
            settings.showWeapon = true; settings.showFlags = true; settings.showDistance = true;
            settings.boxStyle = 0; settings.boxThickness = 2.0f; settings.boxWidthRatio = 0.45f;
            settings.boxColor[0]=1; settings.boxColor[1]=0.1f; settings.boxColor[2]=0.1f; settings.boxColor[3]=1;
            settings.skeletonColor[0]=1; settings.skeletonColor[1]=0.1f; settings.skeletonColor[2]=0.1f; settings.skeletonColor[3]=0.7f;
            settings.showGlow = true; settings.glowAlpha = 0.5f; settings.glowBloom = true; settings.glowThickness = 7.f;
            settings.glowColor[0]=1; settings.glowColor[1]=0.05f; settings.glowColor[2]=0.05f; settings.glowColor[3]=0.6f;
            settings.healthStyle = 2; settings.healthBarPos = 1;
            settings.showOOVIndicators = true; settings.showMoney = true;
            break;
        case 4: // Neverlose — sleek blue-teal, filled boxes, gradient health
            settings.showBox = true; settings.showSkeleton = true; settings.showHealth = true;
            settings.showWeapon = true; settings.showFlags = true; settings.showDistance = true;
            settings.boxStyle = 2; settings.boxThickness = 1.5f; settings.boxWidthRatio = 0.42f;
            settings.boxColor[0]=0.0f; settings.boxColor[1]=0.7f; settings.boxColor[2]=1.0f; settings.boxColor[3]=0.9f;
            settings.skeletonColor[0]=0.0f; settings.skeletonColor[1]=0.9f; settings.skeletonColor[2]=1.0f; settings.skeletonColor[3]=0.5f;
            settings.showGlow = true; settings.glowAlpha = 0.3f; settings.glowBloom = true; settings.glowThickness = 4.f;
            settings.glowColor[0]=0.0f; settings.glowColor[1]=0.6f; settings.glowColor[2]=1.0f; settings.glowColor[3]=0.4f;
            settings.healthStyle = 2; settings.healthBarPos = 0;
            settings.showOOVIndicators = true; settings.showMoney = true;
            break;
        case 5: // Skeet — minimal dark, clean lines
            settings.showBox = true; settings.showSkeleton = true; settings.showHealth = true;
            settings.showWeapon = true; settings.showFlags = true; settings.showDistance = true;
            settings.boxStyle = 4; settings.boxThickness = 1.2f; settings.boxWidthRatio = 0.38f;
            settings.boxColor[0]=0.9f; settings.boxColor[1]=0.9f; settings.boxColor[2]=0.9f; settings.boxColor[3]=0.95f;
            settings.skeletonColor[0]=0.8f; settings.skeletonColor[1]=0.8f; settings.skeletonColor[2]=0.8f; settings.skeletonColor[3]=0.4f;
            settings.showGlow = true; settings.glowAlpha = 0.2f; settings.glowBloom = false; settings.glowThickness = 3.f;
            settings.glowColor[0]=0.3f; settings.glowColor[1]=0.9f; settings.glowColor[2]=0.3f; settings.glowColor[3]=0.3f;
            settings.healthStyle = 1; settings.healthBarPos = 0;
            settings.showOOVIndicators = false; settings.showMoney = true;
            break;
        default: break;
    }
}

// ==================== SMART TEXT (with black outline) ====================

void Overlay::DrawTextOutlined(const char* text, int x, int y, float* color, bool centered) {
    if (!text || !text[0]) return;
    auto dl = DL();
    ImVec2 sz = ImGui::CalcTextSize(text);
    float fx = (float)x, fy = (float)y;
    if (centered) fx -= sz.x * 0.5f;
    ImU32 outline = IM_COL32(0, 0, 0, 210);
    ImU32 mainCol = ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], color[3]));
    for (int ox = -1; ox <= 1; ox++)
        for (int oy = -1; oy <= 1; oy++)
            if (ox || oy)
                dl->AddText(ImVec2(fx + ox, fy + oy), outline, text);
    dl->AddText(ImVec2(fx, fy), mainCol, text);
}

// ==================== BLOOM PASS (subtle box glow) ====================

void Overlay::DrawBloomPass(const Vector3 bones[30], float* vm, int x, int y, int w, int h, float* color) {
    auto dl = DL();
    float fx = (float)x, fy = (float)y, fw = (float)w, fh = (float)h;
    if (fw < 1 || fh < 1) return;
    ImVec2 corners[4] = {
        ImVec2(fx, fy), ImVec2(fx+fw, fy),
        ImVec2(fx+fw, fy+fh), ImVec2(fx, fy+fh)
    };
    float a = color[3] * 0.05f;
    if (a < 0.005f) return;
    ImU32 c = ImGui::GetColorU32(ImVec4(color[0], color[1], color[2], a));
    dl->AddPolyline(corners, 4, c, ImDrawFlags_Closed, 3.0f);
}

// ==================== ARMOR BAR (thin, blue) ====================

void Overlay::DrawArmorBar(int x, int y, int w, int h, int armor) {
    float ratio = (std::max)(0.0f, (std::min)(1.0f, (float)armor / 100.0f));
    auto dl = DL();
    int barW = 3;
    int barX = x + (w - barW) / 2;
    ImU32 bgC = ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.08f, 0.7f));
    dl->AddRectFilled(ImVec2((float)barX, (float)y), ImVec2((float)(barX + barW), (float)(y + h)), bgC, 1.5f);
    int fh = (int)(h * ratio);
    if (fh < 1) return;
    ImU32 topC = ImGui::GetColorU32(ImVec4(0.15f, 0.55f, 0.95f, 0.95f));
    ImU32 botC = ImGui::GetColorU32(ImVec4(0.10f, 0.30f, 0.70f, 0.95f));
    dl->AddRectFilledMultiColor(ImVec2((float)barX, (float)(y + h - fh)),
                                ImVec2((float)(barX + barW), (float)(y + h)),
                                topC, topC, botC, botC);
}

// ==================== SKELETON (clean, single-pass) ====================

void Overlay::DrawSkeletonPro(int bx, int by, int bw, int bh, GameState* state, int idx, float distFade) {
    auto& p = state->players[idx];
    if (!p.IsValid()) return;
    auto dl = DL();
    float* vm = state->viewMatrix;
    float thickness = 1.2f;

    auto project = [&](int boneIdx, float& sx, float& sy) -> bool {
        Vector2 s;
        if (!WorldToScreen(p.bonePos[boneIdx], s, vm)) return false;
        sx = s.x; sy = s.y;
        return true;
    };

    // Simple single-pass bone lines — clean like Neverlose
    float skelAlpha = (std::max)(0.35f, settings.skeletonColor[3] * distFade);
    float skelCol[4] = { settings.skeletonColor[0], settings.skeletonColor[1],
                          settings.skeletonColor[2], skelAlpha };
    ImU32 col = ImGui::GetColorU32(ImVec4(skelCol[0], skelCol[1], skelCol[2], skelCol[3]));

    auto boneLine = [&](int fromIdx, int toIdx) {
        float ax, ay, bxx, byy;
        if (!project(fromIdx, ax, ay)) return;
        if (!project(toIdx, bxx, byy)) return;
        dl->AddLine(ImVec2(ax, ay), ImVec2(bxx, byy), col, thickness);
    };

    // Spine
    boneLine(6, 5); boneLine(5, 4); boneLine(4, 3); boneLine(3, 2); boneLine(2, 0);
    // Left arm
    boneLine(5, 8); boneLine(8, 9); boneLine(9, 10);
    // Right arm
    boneLine(5, 13); boneLine(13, 14); boneLine(14, 15);
    // Left leg
    boneLine(0, 22); boneLine(22, 23); boneLine(23, 24);
    // Right leg
    boneLine(0, 25); boneLine(25, 26); boneLine(26, 27);
}

// ==================== VIEW VECTOR (aim direction ray) ====================

void Overlay::DrawViewVector(int bx, int by, int bw, int bh, GameState* state, int idx) {
    auto& p = state->players[idx];
    if (!p.IsValid()) return;
    auto dl = DL();
    float* vm = state->viewMatrix;
    Vector2 head;
    if (!WorldToScreen(p.bonePos[6], head, vm)) return;

    float yawRad = p.viewAngle.yaw * 3.14159265f / 180.0f;
    float pitchRad = p.viewAngle.pitch * 3.14159265f / 180.0f;
    float len = (float)bh * 0.35f;
    Vector3 end3D;
    end3D.x = p.bonePos[6].x + cosf(pitchRad) * cosf(yawRad) * len;
    end3D.y = p.bonePos[6].y + cosf(pitchRad) * sinf(yawRad) * len;
    end3D.z = p.bonePos[6].z - sinf(pitchRad) * len;
    Vector2 end;
    if (!WorldToScreen(end3D, end, vm)) return;

    float dx = end.x - head.x;
    float dy = end.y - head.y;
    float lineLen = sqrtf(dx*dx + dy*dy);
    if (lineLen < 5.0f || lineLen > 500.0f) return;

    ImU32 col = C(settings.skeletonColor);
    float steps = 12.0f;
    float segLen = 1.0f / steps;
    for (int s = 0; s < (int)steps; s += 2) {
        float t0 = (float)s * segLen;
        float t1 = (float)(s + 1) * segLen;
        if (t1 > 1.0f) t1 = 1.0f;
        float x0 = head.x + dx * t0;
        float y0 = head.y + dy * t0;
        float x1 = head.x + dx * t1;
        float y1 = head.y + dy * t1;
        dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), col, 1.0f);
    }
}

// ==================== HEAD DOT (clean, subtle) ====================

void Overlay::DrawHeadDot(int bx, int by, int bw, int bh, GameState* state, int idx, float distFade) {
    auto& p = state->players[idx];
    if (!p.IsValid()) return;
    auto dl = DL();
    Vector2 headScr;
    if (!WorldToScreen(p.bonePos[6], headScr, state->viewMatrix)) return;

    float baseR = settings.headDotSize;

    // Simple dot — no multi-layer bloom
    ImU32 coreC = ImGui::GetColorU32(ImVec4(
        settings.headDotColor[0], settings.headDotColor[1],
        settings.headDotColor[2], settings.headDotColor[3] * distFade));
    dl->AddCircleFilled(ImVec2(headScr.x, headScr.y), baseR, coreC, 10);

    // Subtle outer glow
    if (settings.headDotGlow) {
        ImU32 glowC = ImGui::GetColorU32(ImVec4(
            settings.headDotColor[0], settings.headDotColor[1],
            settings.headDotColor[2], 0.15f * distFade));
        dl->AddCircleFilled(ImVec2(headScr.x, headScr.y), baseR * 2.0f, glowC, 12);
    }
}

// ==================== AMMO BAR (below health bar) ====================

void Overlay::DrawAmmoBar(int x, int y, int w, int h, int clip, int maxClip) {
    float ratio = (maxClip > 0) ? (float)clip / (float)maxClip : 0.0f;
    ratio = (std::max)(0.0f, (std::min)(1.0f, ratio));
    auto dl = DL();

    // Background
    ImU32 bgC = ImGui::GetColorU32(ImVec4(0.06f, 0.06f, 0.10f, 0.85f));
    dl->AddRectFilled(ImVec2((float)x, (float)y), ImVec2((float)(x+w), (float)(y+h)), bgC, 1.5f);

    if (ratio <= 0.0f) return;

    // Fill gradient (orange→red when low)
    float r = (ratio < 0.3f) ? 1.0f : settings.ammoColor[0];
    float g = (ratio < 0.3f) ? 0.2f : settings.ammoColor[1];
    float b = (ratio < 0.3f) ? 0.0f : settings.ammoColor[2];

    int fh = (int)(h * ratio);
    ImU32 topC = ImGui::GetColorU32(ImVec4(r * 1.2f > 1 ? 1 : r * 1.2f, g * 1.2f > 1 ? 1 : g * 1.2f, b * 1.2f > 1 ? 1 : b * 1.2f, 1.0f));
    ImU32 botC = ImGui::GetColorU32(ImVec4(r * 0.7f, g * 0.7f, b * 0.7f, 1.0f));
    dl->AddRectFilledMultiColor(ImVec2((float)x, (float)(y + h - fh)),
                                ImVec2((float)(x+w), (float)(y + h)),
                                topC, topC, botC, botC);

    // Glossy highlight
    ImU32 glossC = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.15f));
    dl->AddRectFilled(ImVec2((float)x + 1, (float)(y + h - fh)),
                      ImVec2((float)(x+w) - 1, (float)(y + h - fh + 2)), glossC, 1.0f);
}

// ==================== TRAJECTORY RENDERING (grenade helper) ====================

void Overlay::DrawTrajectory(GameState* state) {
    if (!settings.showTrajectory) return;
    if (!state->nadeEngine) return;
    NadeEngine* nadeEng = static_cast<NadeEngine*>(state->nadeEngine);
    auto& tc = nadeEng->GetTrajectoryCache();
    if (!tc.valid || tc.points.size() < 2) return;

    auto dl = DL();
    float time = (float)ImGui::GetTime();
    float pulse = 0.7f + 0.3f * sinf(time * 4.0f);

    // Draw trajectory path with fading segments
    int totalPts = (int)tc.points.size();
    for (int i = 1; i < totalPts; i++) {
        Vector2 s0, s1;
        if (!WorldToScreen(tc.points[i-1], s0, state->viewMatrix)) continue;
        if (!WorldToScreen(tc.points[i], s1, state->viewMatrix)) continue;

        float t = (float)i / (float)totalPts;
        float segAlpha = (1.0f - t * 0.6f) * pulse;

        // Color gradient: yellow → orange → red
        float r = 1.0f;
        float g = 1.0f - t * 0.7f;
        float b = 0.0f;

        ImU32 lineCol = ImGui::GetColorU32(ImVec4(r, g, b, segAlpha * settings.trajectoryColor[3]));
        dl->AddLine(ImVec2(s0.x, s0.y), ImVec2(s1.x, s1.y), lineCol, 2.0f);

        // Glow effect on each segment
        ImU32 glowCol = ImGui::GetColorU32(ImVec4(r, g, b, segAlpha * 0.15f));
        dl->AddLine(ImVec2(s0.x, s0.y), ImVec2(s1.x, s1.y), glowCol, 6.0f);
    }

    // Landing indicator (pulsing circle)
    if (totalPts > 0) {
        Vector2 landScr;
        if (WorldToScreen(tc.points[totalPts-1], landScr, state->viewMatrix)) {
            float landPulse = 8.0f + 4.0f * sinf(time * 6.0f);
            ImU32 landGlow = ImGui::GetColorU32(ImVec4(1.0f, 0.3f, 0.0f, 0.25f * pulse));
            ImU32 landCore = ImGui::GetColorU32(ImVec4(1.0f, 0.5f, 0.0f, 0.8f * pulse));
            ImU32 landHot = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.9f));
            dl->AddCircleFilled(ImVec2(landScr.x, landScr.y), landPulse, landGlow, 16);
            dl->AddCircleFilled(ImVec2(landScr.x, landScr.y), 4.0f, landCore, 12);
            dl->AddCircleFilled(ImVec2(landScr.x, landScr.y), 1.5f, landHot, 8);
        }
    }
}

// ==================== INFO PANEL (compact, minimal) ====================

void Overlay::DrawInfoPanel(int bx, int by, int bw, int bh, GameState* state, int idx, float distFade) {
    auto& p = state->players[idx];
    if (!p.IsValid()) return;
    auto dl = DL();
    int px = bx + bw + 6;
    int py = by + 2;

    // Compact info: weapon icon + health + money
    // Minimal, like Neverlose — no glassmorphism panel
    auto* local = state->GetLocal();
    float dist = 0;
    if (local) dist = p.origin.DistTo(local->origin) * 0.01905f;

    int lineH = 12;
    int ty = py;

    // Weapon icon (short)
    if (settings.useWeaponIcons && p.weaponId > 0) {
        const char* icon = WeaponIcon(p.weaponId);
        if (icon[0]) {
            float wc[4] = { settings.weaponColor[0], settings.weaponColor[1], settings.weaponColor[2], settings.weaponColor[3] * distFade };
            DrawTextOutlined(icon, px, ty, wc, false);
            ty += lineH;
        }
    }

    // Health text
    if (p.health > 0 && (p.health < 100 || settings.healthStyle == 1)) {
        float hr = p.health / 100.0f;
        float hc[4];
        if (hr > 0.6f) { hc[0] = 0.1f; hc[1] = 0.9f; hc[2] = 0.2f; }
        else if (hr > 0.3f) { hc[0] = 1.0f; hc[1] = 0.7f; hc[2] = 0.0f; }
        else { hc[0] = 1.0f; hc[1] = 0.2f; hc[2] = 0.1f; }
        hc[3] = distFade;
        char buf[8]; snprintf(buf, sizeof(buf), "%d", (int)p.health);
        DrawTextOutlined(buf, px, ty, hc, false);
        ty += lineH;
    }

    // Money (compact)
    if (p.money > 0 && settings.showMoney) {
        float mc[4] = { 0.4f, 0.9f, 0.4f, 0.8f * distFade };
        if (p.money > 4000) { mc[0] = 0.2f; mc[1] = 0.9f; mc[2] = 0.4f; }
        else if (p.money < 1000) { mc[0] = 0.9f; mc[1] = 0.3f; mc[2] = 0.2f; }
        char buf[8]; snprintf(buf, sizeof(buf), "$%d", p.money);
        DrawTextOutlined(buf, px, ty, mc, false);
        ty += lineH;
    }

    // Scope/Flash state (tiny indicators)
    if (p.scoped) {
        float sc[4] = { 0.2f, 0.7f, 1.0f, 0.9f * distFade };
        DrawTextOutlined("SCO", px, ty, sc, false);
        ty += lineH;
    }
    if (p.flashed) {
        float fc[4] = { 1.0f, 0.9f, 0.2f, 0.9f * distFade };
        DrawTextOutlined("FLSH", px, ty, fc, false);
    }
}

// ==================== AIMLINE (separate module from skeleton) ====================

void Overlay::DrawAimline(int bx, int by, int bw, int bh, GameState* state, int idx) {
    auto& p = state->players[idx];
    if (!p.IsValid()) return;
    if (!settings.showAimline) return;
    auto dl = DL();
    float* vm = state->viewMatrix;
    Vector2 head;
    if (!WorldToScreen(p.bonePos[6], head, vm)) return;
    float yawRad = p.viewAngle.yaw * 3.14159265f / 180.0f;
    float pitchRad = p.viewAngle.pitch * 3.14159265f / 180.0f;
    float len = (float)bh * 0.35f;
    Vector3 end3D;
    end3D.x = p.bonePos[6].x + cosf(pitchRad) * cosf(yawRad) * len;
    end3D.y = p.bonePos[6].y + cosf(pitchRad) * sinf(yawRad) * len;
    end3D.z = p.bonePos[6].z - sinf(pitchRad) * len;
    Vector2 end;
    if (!WorldToScreen(end3D, end, vm)) return;

    float dx = end.x - head.x;
    float dy = end.y - head.y;
    float lineLen = sqrtf(dx*dx + dy*dy);
    if (lineLen < 5.0f || lineLen > 500.0f) return;

    ImU32 col = ImGui::GetColorU32(ImVec4(settings.aimlineColor[0], settings.aimlineColor[1],
                                           settings.aimlineColor[2], settings.aimlineColor[3]));
    float steps = 12.0f;
    float segLen = 1.0f / steps;
    for (int s = 0; s < (int)steps; s += 2) {
        float t0 = (float)s * segLen;
        float t1 = (float)(s + 1) * segLen;
        if (t1 > 1.0f) t1 = 1.0f;
        float x0 = head.x + dx * t0;
        float y0 = head.y + dy * t0;
        float x1 = head.x + dx * t1;
        float y1 = head.y + dy * t1;
        dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), col, settings.aimlineThickness);
    }
}

// ==================== 3D BOX PROJECTION ====================

void Overlay::Draw3DBox(int bx, int by, int bw, int bh, GameState* state, int idx, float* color) {
    auto& p = state->players[idx];
    if (!p.IsValid()) return;
    auto dl = DL();
    float* vm = state->viewMatrix;

    // Use head bone (top) and origin (bottom) for proper vertical bounds
    Vector2 headScr, feetScr;
    if (!WorldToScreen(p.bonePos[6], headScr, vm)) return;
    if (!WorldToScreen(p.origin, feetScr, vm)) return;

    // Bounding box in screen space — simple 3D extrusion
    float topY = headScr.y;
    float botY = feetScr.y;
    float boxH = botY - topY;
    if (boxH < 10.f) return;
    float boxW = boxH * settings.boxWidthRatio;
    float leftX = (headScr.x + feetScr.x) * 0.5f - boxW * 0.5f;
    float topX = headScr.x;
    float botX = feetScr.x;

    // 8 corners: 4 at feet level, 4 at head level (with depth offset)
    float depthOff = boxW * 0.2f;
    ImU32 c = C(color);
    ImU32 cBack = ImGui::GetColorU32(ImVec4(color[0], color[1], color[2], color[3] * 0.2f));

    // Back face (offset left+up for depth)
    float bx2 = leftX - depthOff;
    float by2 = topY - depthOff * 0.3f;
    dl->AddRect(ImVec2(bx2, by2), ImVec2(bx2 + boxW, by2 + boxH), cBack, 0.f, 0, settings.boxThickness);

    // Connect front face to back face (8 edges)
    ImVec2 fl(leftX, topY);              // front top-left
    ImVec2 fr(leftX + boxW, topY);       // front top-right
    ImVec2 fbl(leftX, botY);             // front bottom-left
    ImVec2 fbr(leftX + boxW, botY);      // front bottom-right
    ImVec2 bl(bx2, by2);                 // back top-left
    ImVec2 br(bx2 + boxW, by2);          // back top-right
    ImVec2 bbl(bx2, by2 + boxH);         // back bottom-left
    ImVec2 bbr(bx2 + boxW, by2 + boxH);  // back bottom-right

    // Front face
    dl->AddRect(fl, fbr, c, 0.f, 0, settings.boxThickness);
    // Connecting edges
    dl->AddLine(fl, bl, cBack, settings.boxThickness);
    dl->AddLine(fr, br, cBack, settings.boxThickness);
    dl->AddLine(fbl, bbl, cBack, settings.boxThickness);
    dl->AddLine(fbr, bbr, cBack, settings.boxThickness);
}

// ==================== SNAPLINES ====================

void Overlay::DrawSnaplines(GameState* state) {
    if (!settings.showSnaplines) return;
    auto dl = DL();
    float* vm = state->viewMatrix;
    float lineOriginX = (float)screenW * 0.5f;
    float lineOriginY = (float)screenH; // bottom center

    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;

        Vector2 feetScr;
        if (!WorldToScreen(p.origin, feetScr, vm)) continue;

        float distFade = 1.0f;
        float thickFade = 1.0f;
        auto* local = state->GetLocal();
        if (local) {
            float dist = p.origin.DistTo(local->origin) * 0.01905f;
            distFade = 1.0f - dist / 2500.0f;
            thickFade = 1.0f - dist / 3000.0f;
            if (distFade < 0.1f) distFade = 0.1f;
            if (distFade > 1.0f) distFade = 1.0f;
            if (thickFade < 0.2f) thickFade = 0.2f;
            if (thickFade > 1.0f) thickFade = 1.0f;
        }

        float thickness = settings.snaplineThickness * thickFade;
        if (thickness < 0.5f) thickness = 0.5f;

        // Gradient from crosshair color to snapline color (fades out toward player)
        ImU32 col = ImGui::GetColorU32(ImVec4(
            settings.snaplineColor[0], settings.snaplineColor[1],
            settings.snaplineColor[2], settings.snaplineColor[3] * distFade));
        dl->AddLine(ImVec2(lineOriginX, lineOriginY), ImVec2(feetScr.x, feetScr.y), col, thickness);

        // Subtle glow on the line
        ImU32 glowCol = ImGui::GetColorU32(ImVec4(
            settings.snaplineColor[0], settings.snaplineColor[1],
            settings.snaplineColor[2], 0.08f * distFade));
        dl->AddLine(ImVec2(lineOriginX, lineOriginY), ImVec2(feetScr.x, feetScr.y), glowCol, thickness + 3.0f);

        // Distance text at end of snapline
        if (local) {
            float distM = p.origin.DistTo(local->origin) * 0.01905f;
            char distBuf[16];
            snprintf(distBuf, sizeof(distBuf), "%.0fm", distM);
            ImU32 distTextCol = ImGui::GetColorU32(ImVec4(
                settings.snaplineColor[0], settings.snaplineColor[1],
                settings.snaplineColor[2], 0.7f * distFade));
            dl->AddText(ImVec2(feetScr.x + 4, feetScr.y - 4), distTextCol, distBuf);
        }
    }
}

// ==================== MAIN ESP RENDER (Elite Architecture) ====================

void Overlay::RenderESP(GameState* state) {
    auto dl = DL();
    float time = (float)ImGui::GetTime();

    // ═══════════════════════════════════════════════════════════════
    // PASS 0 (Background): Glow Shell + Bloom
    // ═══════════════════════════════════════════════════════════════
    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;
        Vector2 headScr, feetScr;
        if (!WorldToScreen(p.bonePos[6], headScr, state->viewMatrix)) continue;
        if (!WorldToScreen(p.origin, feetScr, state->viewMatrix)) continue;
        float h = feetScr.y - headScr.y;
        float bw = h * settings.boxWidthRatio;
        int bx = (int)(headScr.x - bw * 0.5f), by = (int)headScr.y;
        int bwi = (int)bw, bhi = (int)h;
        if (bhi <= 10 || bhi > screenH * 2) continue;

        // Distance-based fade
        float distFade = 1.0f;
        auto* local = state->GetLocal();
        if (local) {
            float dist = p.origin.DistTo(local->origin) * 0.01905f;
            distFade = 1.0f - dist / 2500.0f;
            if (distFade < 0.15f) distFade = 0.15f;
            if (distFade > 1.0f) distFade = 1.0f;
        }

        // Health-based dynamic color for glow
        float glowCol[4];
        if (settings.healthBasedColor) {
            DrawHealthBasedColor(p.health, glowCol);
            glowCol[3] = settings.glowColor[3];
        } else {
            glowCol[0] = settings.glowColor[0];
            glowCol[1] = settings.glowColor[1];
            glowCol[2] = settings.glowColor[2];
            glowCol[3] = settings.glowColor[3];
        }

        if (settings.showGlow) {
            DrawGlowShell(p.bonePos, state->viewMatrix, screenW, screenH,
                          glowCol, settings.glowAlpha * distFade, settings.glowStyle, time);
            if (settings.glowBloom)
                DrawBloomPass(p.bonePos, state->viewMatrix, bx, by, bwi, bhi, glowCol);
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // PASS 1 (Body): Skeleton + Aimline + Box
    // ═══════════════════════════════════════════════════════════════
    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;
        Vector2 headScr, feetScr;
        if (!WorldToScreen(p.bonePos[6], headScr, state->viewMatrix)) continue;
        if (!WorldToScreen(p.origin, feetScr, state->viewMatrix)) continue;
        float h = feetScr.y - headScr.y;
        float bw = h * settings.boxWidthRatio;
        int rawX = (int)(headScr.x - bw * 0.5f), rawY = (int)headScr.y;
        int rawW = (int)bw, rawH = (int)h;
        if (rawH <= 10 || rawH > screenH * 2) continue;

        // Distance fade
        float distFade = 1.0f;
        auto* local = state->GetLocal();
        if (local) {
            float dist = p.origin.DistTo(local->origin) * 0.01905f;
            distFade = 1.0f - dist / 2500.0f;
            if (distFade < 0.15f) distFade = 0.15f;
            if (distFade > 1.0f) distFade = 1.0f;
        }

        // Adaptive smoothing
        auto& sb = smoothBox[i];
        float ddx = (float)rawX - sb.x, ddy = (float)rawY - sb.y;
        float ddw = (float)rawW - sb.w, ddh = (float)rawH - sb.h;
        if (sb.age < 2 || fabsf(ddx) > 20.f || fabsf(ddy) > 20.f || fabsf(ddw) > 20.f || fabsf(ddh) > 20.f) {
            sb.x = (float)rawX; sb.y = (float)rawY; sb.w = (float)rawW; sb.h = (float)rawH; sb.age = 2;
        } else {
            float lr = 0.35f;
            sb.x += ddx * lr; sb.y += ddy * lr; sb.w += ddw * lr; sb.h += ddh * lr;
        }
        int sx = (int)sb.x, sy = (int)sb.y, sw = (int)sb.w, sh = (int)sb.h;

        // Health-based dynamic color for box
        float boxCol[4];
        if (settings.healthBasedColor) {
            DrawHealthBasedColor(p.health, boxCol);
        } else {
            boxCol[0] = settings.boxColor[0];
            boxCol[1] = settings.boxColor[1];
            boxCol[2] = settings.boxColor[2];
            boxCol[3] = settings.boxColor[3];
        }

        // ── Skeleton Pro (separate module, auto-hidden if behind wall) ──
        if (settings.showSkeleton) {
            float skelAlpha = distFade;
            DrawSkeletonPro(sx, sy, sw, sh, state, i, skelAlpha);
        }

        // ── Head Dot (pulsing core + bloom) ──
        if (settings.showHeadDot)
            DrawHeadDot(sx, sy, sw, sh, state, i, distFade);

        // ── Aimline (separate module, independent from skeleton) ──
        if (settings.showAimline)
            DrawAimline(sx, sy, sw, sh, state, i);

        // ── Box drawing (Multi-Mode Selector) ──
        if (settings.boxStyle > 0) {
            // Box outer glow (drop shadow) — subtle, only when explicitly enabled
            if (settings.glowBloom) {
                for (int gi = 3; gi >= 1; gi--) {
                    float off = (float)gi * 1.5f;
                    float a = 0.02f / gi;
                    ImU32 glowC = ImGui::GetColorU32(ImVec4(boxCol[0], boxCol[1], boxCol[2], a * distFade));
                    float r = (settings.boxStyle == 4) ? 5.f + off : 0.f;
                    dl->AddRect(ImVec2((float)sx - off, (float)sy - off),
                                ImVec2((float)(sx+sw) + off, (float)(sy+sh) + off),
                                glowC, r, 0, settings.boxThickness * 0.6f);
                }
            }

            // Box style rendering
            ImU32 boxC = ImGui::GetColorU32(ImVec4(boxCol[0], boxCol[1], boxCol[2], boxCol[3] * distFade));

            switch (settings.boxStyle) {
                case 1: // Corner (1/4 ratio)
                    DrawCornerBox(sx, sy, sw, sh, boxCol, settings.boxThickness);
                    break;
                case 2: { // Square (filled + outline)
                    ImU32 fillCol = ImGui::GetColorU32(ImVec4(0, 0, 0, 0.25f * distFade));
                    dl->AddRectFilled(ImVec2((float)sx, (float)sy),
                                      ImVec2((float)(sx+sw), (float)(sy+sh)), fillCol);
                    dl->AddRect(ImVec2((float)sx, (float)sy),
                                ImVec2((float)(sx+sw), (float)(sy+sh)), boxC, 0.f, 0, settings.boxThickness);
                    break;
                }
                case 3: // 3D Box
                    Draw3DBox(sx, sy, sw, sh, state, i, boxCol);
                    break;
                case 4: // Rounded — dark fill + colored outline
                    dl->AddRectFilled(ImVec2((float)sx, (float)sy),
                                      ImVec2((float)(sx+sw), (float)(sy+sh)),
                                      ImGui::GetColorU32(ImVec4(0, 0, 0, 0.3f * distFade)), 5.f);
                    dl->AddRect(ImVec2((float)sx, (float)sy),
                                ImVec2((float)(sx+sw), (float)(sy+sh)), boxC, 5.f, 0, settings.boxThickness);
                    break;
                case 5: // Glow box style (entity shell for box)
                    dl->AddRect(ImVec2((float)sx, (float)sy),
                                ImVec2((float)(sx+sw), (float)(sy+sh)), boxC, 0.f, 0, settings.boxThickness);
                    break;
                default: break;
            }

        }
    }

    // ═══════════════════════════════════════════════════════════════
    // PASS 2 (UI): Info panel, bars, text — gated by global toggle
    // ═══════════════════════════════════════════════════════════════
    if (settings.showInfoPanel) {
        for (int i = 0; i < 64; i++) {
            auto& p = state->players[i];
            if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;

            auto& sb = smoothBox[i];
            int ux = (int)sb.x, uy = (int)sb.y, uw = (int)sb.w, uh = (int)sb.h;
            if (uh <= 10) continue;

            float distFade = 1.0f;
            auto* local = state->GetLocal();
            if (local) {
                float dist = p.origin.DistTo(local->origin) * 0.01905f;
                distFade = 1.0f - dist / 2500.0f;
                if (distFade < 0.15f) distFade = 0.15f;
                if (distFade > 1.0f) distFade = 1.0f;
            }

            // Info Panel — glassmorphism, magnetized to right edge of box
            DrawInfoPanel(ux, uy, uw, uh, state, i, distFade);

            // Flag indicators (scoped/flashed/sniper/kit)
            if (settings.showFlags)
                DrawFlags(ux, uy, uw, uh, state, i, distFade);

            // Name above box
            if (settings.showName && p.name[0]) {
                float nc[4] = { settings.nameColor[0], settings.nameColor[1], settings.nameColor[2], settings.nameColor[3] * distFade };
                DrawTextOutlined(p.name, ux + uw / 2, uy - 14, nc, true);
            }

            // Health + Armor bars
            bool barLeft = settings.healthBarPos == 0;
            int barW = 5, barGap = 3;

            if (settings.showHealth || settings.showArmor) {
                float hRatio = (std::max)(0.0f, (std::min)(1.0f, p.health / 100.0f));
                float hc[4]; HealthColor(hRatio, hc);

                if (settings.healthStyle == 0 || settings.healthStyle == 2) {
                    if (barLeft) {
                        int hpX = ux - barW - barGap;
                        int armX = hpX - barW - 2;
                        if (settings.showArmor && p.armor > 0)
                            DrawArmorBar(armX, uy, barW, uh, p.armor);
                        DrawHealthBar(hpX, uy, barW, uh, p.health, 100);
                    } else {
                        int hpX = ux + uw + barGap;
                        int armX = hpX + barW + 2;
                        DrawHealthBar(hpX, uy, barW, uh, p.health, 100);
                        if (settings.showArmor && p.armor > 0)
                            DrawArmorBar(armX, uy, barW, uh, p.armor);
                    }
                }
                if (settings.healthStyle == 1 || settings.healthStyle == 2) {
                    float hcf[4] = { hc[0], hc[1], hc[2], hc[3] * distFade };
                    int tx = barLeft ? ux + uw + 5 : ux - 5;
                    DrawTextOutlined(std::to_string(p.health).c_str(), tx, uy + 2, hcf, false);
                }

                // Ammo bar (below health bar)
                if (settings.showAmmo && p.ammo > 0) {
                    int ammoBarW = 5;
                    int ammoBarGap = 3;
                    int ammoH = (int)(uh * 0.6f);
                    int ammoY = uy + (uh - ammoH) / 2;
                    int maxClip = 30;
                    switch (p.weaponId) {
                        case 1: maxClip = 7; break;
                        case 2: maxClip = 30; break;
                        case 3: maxClip = 20; break;
                        case 4: maxClip = 20; break;
                        case 7: maxClip = 30; break;
                        case 8: maxClip = 25; break;
                        case 10: maxClip = 25; break;
                        case 11: maxClip = 20; break;
                        case 13: maxClip = 35; break;
                        case 14: maxClip = 100; break;
                        case 16: maxClip = 30; break;
                        case 17: maxClip = 30; break;
                        case 19: maxClip = 30; break;
                        case 23: maxClip = 30; break;
                        case 24: maxClip = 24; break;
                        case 25: maxClip = 8; break;
                        case 26: maxClip = 5; break;
                        case 27: maxClip = 5; break;
                        case 28: maxClip = 150; break;
                        case 29: maxClip = 8; break;
                        case 30: maxClip = 18; break;
                        case 32: maxClip = 13; break;
                        case 33: maxClip = 30; break;
                        case 34: maxClip = 30; break;
                        case 35: maxClip = 15; break;
                        case 36: maxClip = 13; break;
                        case 38: maxClip = 10; break;
                        case 39: maxClip = 30; break;
                        case 40: maxClip = 10; break;
                        case 60: maxClip = 20; break;
                        case 61: maxClip = 12; break;
                        case 63: maxClip = 12; break;
                        case 64: maxClip = 8; break;
                    }
                    if (barLeft) {
                        int ammoX = ux - barW * 2 - barGap - ammoBarGap;
                        DrawAmmoBar(ammoX, ammoY, ammoBarW, ammoH, p.ammo, maxClip);
                    } else {
                        int ammoX = ux + uw + barW + barGap + ammoBarGap;
                        DrawAmmoBar(ammoX, ammoY, ammoBarW, ammoH, p.ammo, maxClip);
                    }
                }
            }

            // Weapon name (with icon support)
            if (settings.showWeapon && p.weaponId > 0) {
                std::string wpn;
                if (settings.useWeaponIcons) {
                    wpn = WeaponIcon(p.weaponId);
                } else {
                    wpn = WeaponName(p.weaponId);
                }
                if (p.ammo > 0) {
                    wpn += " " + std::to_string(p.ammo);
                    if (p.reserveAmmo > 0)
                        wpn += "/" + std::to_string(p.reserveAmmo);
                }
                float wc[4] = { settings.weaponColor[0], settings.weaponColor[1], settings.weaponColor[2], settings.weaponColor[3] * distFade };
                DrawTextOutlined(wpn.c_str(), ux + uw / 2, uy + uh + 2, wc, true);
            }

            // Distance
            if (settings.showDistance) {
                auto* local2 = state->GetLocal();
                if (local2) {
                    float dist = p.origin.DistTo(local2->origin) * 0.01905f;
                    char buf[16]; snprintf(buf, sizeof(buf), "%.0fm", dist);
                    float fc[4] = { settings.flagColor[0], settings.flagColor[1], settings.flagColor[2], settings.flagColor[3] * distFade };
                    DrawTextOutlined(buf, ux + uw / 2, uy + uh + 14, fc, true);
                }
            }
        }
    }

    // ── PASS 3: Snaplines ──
    DrawSnaplines(state);

    // ── PASS 4: Out-of-View Indicators (screen-edge arrows) ──
    DrawOOVIndicators(state);

    // ── PASS 5: Trajectory rendering (grenade helper) ──
    DrawTrajectory(state);

    // ── PASS 6: Aim target indicator ──
    DrawAimTarget(state);

    // ── PASS 7: Bomb Timer (screen overlay) ──
    DrawBombTimer(state);

    // ── PASS 8: Dead player skulls ──
    DrawDeadSkulls(state);

    // ── PASS 9: Chams (filled player silhouette) ──
    DrawChams(state);
}

// ==================== AIM TARGET INDICATOR ====================

void Overlay::DrawAimTarget(GameState* state) {
    if (!cachedAim || !cachedAim->HasTarget()) return;
    int targetIdx = cachedAim->GetCurrentTarget();
    if (targetIdx < 0 || targetIdx >= 64) return;
    auto& p = state->players[targetIdx];
    if (!p.IsValid()) return;

    auto dl = DL();
    float time = (float)ImGui::GetTime();
    float pulse = 0.6f + 0.4f * sinf(time * 5.0f);

    Vector2 headScr;
    if (!WorldToScreen(p.bonePos[6], headScr, state->viewMatrix)) return;
    Vector2 feetScr;
    if (!WorldToScreen(p.origin, feetScr, state->viewMatrix)) return;

    float h = feetScr.y - headScr.y;
    float bw = h * settings.boxWidthRatio;
    float cx = headScr.x;
    float cy = (headScr.y + feetScr.y) * 0.5f;

    float hc = cachedAim->GetCurrentHitchance();
    float hcColor[4];
    if (hc > 70) { hcColor[0] = 0; hcColor[1] = 1; hcColor[2] = 0.3f; }
    else if (hc > 40) { hcColor[0] = 1; hcColor[1] = 0.8f; hcColor[2] = 0; }
    else { hcColor[0] = 1; hcColor[1] = 0.3f; hcColor[2] = 0; }

    float outerR = bw * 0.55f;
    // Simple single-ring indicator
    ImU32 ringCol = ImGui::GetColorU32(ImVec4(hcColor[0], hcColor[1], hcColor[2], 0.5f));
    ImU32 fillCol = ImGui::GetColorU32(ImVec4(hcColor[0], hcColor[1], hcColor[2], 0.08f));
    dl->AddCircleFilled(ImVec2(cx, cy), outerR, fillCol, 24);
    dl->AddCircle(ImVec2(cx, cy), outerR, ringCol, 24, 1.5f);

    // Simple dashed line from crosshair to target
    float crosshairX = (float)screenW * 0.5f;
    float crosshairY = (float)screenH * 0.5f;
    ImU32 lineCol = ImGui::GetColorU32(ImVec4(hcColor[0], hcColor[1], hcColor[2], 0.2f));
    float dx = cx - crosshairX;
    float dy = cy - crosshairY;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 20) {
        float nx = dx / len, ny = dy / len;
        dl->AddLine(ImVec2(crosshairX + nx * 20, crosshairY + ny * 20),
                    ImVec2(cx - nx * outerR, cy - ny * outerR), lineCol, 1.0f);
    }

    char label[32];
    snprintf(label, sizeof(label), "T%d  %.0f%%", targetIdx, hc);
    ImU32 textCol = ImGui::GetColorU32(ImVec4(hcColor[0], hcColor[1], hcColor[2], 0.9f));
    dl->AddText(ImVec2(cx - 15, cy - outerR - 14), textCol, label);
}

// ==================== OUT-OF-VIEW INDICATORS (screen-edge arrows) ====================

void Overlay::DrawOOVIndicators(GameState* state) {
    if (!settings.showOOVIndicators) return;
    auto dl = DL();
    float* vm = state->viewMatrix;
    float time = (float)ImGui::GetTime();
    float pulse = 0.75f + 0.25f * sinf(time * 4.0f);

    float margin = 40.0f;
    float arrowSize = 12.0f;
    float centerX = (float)screenW * 0.5f;
    float centerY = (float)screenH * 0.5f;

    auto* local = state->GetLocal();
    if (!local) return;

    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;

        Vector2 headScr;
        if (!WorldToScreen(p.bonePos[6], headScr, vm)) continue;

        // Check if on screen
        if (headScr.x > 0 && headScr.x < (float)screenW &&
            headScr.y > 0 && headScr.y < (float)screenH) continue;

        // Calculate direction from center to off-screen player
        float dx = headScr.x - centerX;
        float dy = headScr.y - centerY;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < 1.0f) continue;
        float nx = dx / dist;
        float ny = dy / dist;

        // Clamp to screen edge with margin
        float edgeX = centerX + nx * ((float)screenW * 0.5f - margin);
        float edgeY = centerY + ny * ((float)screenH * 0.5f - margin);

        // Keep within screen bounds
        edgeX = (std::max)(margin, (std::min)((float)screenW - margin, edgeX));
        edgeY = (std::max)(margin, (std::min)((float)screenH - margin, edgeY));

        // Arrow angle
        float angle = atan2f(ny, nx);

        // Health-based color
        float hr = (float)p.health / 100.0f;
        float ar, ag, ab;
        if (hr > 0.6f) { ar = 0.1f; ag = 0.9f; ab = 0.2f; }
        else if (hr > 0.3f) { ar = 1.0f; ag = 0.7f; ab = 0.0f; }
        else { ar = 1.0f; ag = 0.2f; ab = 0.1f; }

        // Draw arrow triangle
        float cosA = cosf(angle), sinA = sinf(angle);
        ImVec2 tip(edgeX + cosA * arrowSize, edgeY + sinA * arrowSize);
        ImVec2 left(edgeX + cosf(angle + 2.5f) * arrowSize * 0.6f,
                     edgeY + sinf(angle + 2.5f) * arrowSize * 0.6f);
        ImVec2 right(edgeX + cosf(angle - 2.5f) * arrowSize * 0.6f,
                      edgeY + sinf(angle - 2.5f) * arrowSize * 0.6f);

        // Outer glow
        ImU32 glowCol = ImGui::GetColorU32(ImVec4(ar, ag, ab, 0.15f * pulse));
        dl->AddCircleFilled(ImVec2(edgeX, edgeY), arrowSize * 1.8f, glowCol, 12);

        // Arrow fill
        ImU32 arrowCol = ImGui::GetColorU32(ImVec4(ar, ag, ab, 0.85f * pulse));
        dl->AddTriangleFilled(tip, left, right, arrowCol);

        // Arrow border
        ImU32 borderCol = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.4f * pulse));
        dl->AddTriangle(tip, left, right, borderCol, 1.0f);

        // Distance text below arrow
        float distM = p.origin.DistTo(local->origin) * 0.01905f;
        char distBuf[8]; snprintf(distBuf, sizeof(distBuf), "%.0fm", distM);
        ImU32 textCol = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.8f * pulse));
        ImVec2 ts = ImGui::CalcTextSize(distBuf);
        dl->AddText(ImVec2(edgeX - ts.x * 0.5f, edgeY + arrowSize * 0.5f + 2), textCol, distBuf);
    }
}

// ==================== BOMB TIMER (screen overlay) ====================

void Overlay::DrawBombTimer(GameState* state) {
    if (!settings.showBombInfo) return;
    if (!state->bombPlanted) {
        bombPlantTime = 0;
        return;
    }

    if (bombPlantTime <= 0) {
        bombPlantTime = (float)ImGui::GetTime();
    }

    auto dl = DL();
    float time = (float)ImGui::GetTime();
    float pulse = 0.7f + 0.3f * sinf(time * 5.0f);

    float bombTime = 40.0f;
    float elapsed = time - bombPlantTime;
    if (elapsed < 0) elapsed = 0;
    float remaining = bombTime - elapsed;
    if (remaining < 0) remaining = 0;

    float ratio = remaining / bombTime;

    // Position: top center
    float boxW = 240.0f;
    float boxH = 36.0f;
    float bx = (float)screenW * 0.5f - boxW * 0.5f;
    float by = 50.0f;

    // Background
    ImU32 bg = ImGui::GetColorU32(ImVec4(0.08f, 0.02f, 0.02f, 0.85f));
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + boxW, by + boxH), bg, 6.0f);

    // Progress bar (red→yellow→green)
    float barW = boxW - 8.0f;
    float barH = 4.0f;
    float barX = bx + 4.0f;
    float barY = by + boxH - 10.0f;
    ImU32 barBg = ImGui::GetColorU32(ImVec4(0.15f, 0.05f, 0.05f, 0.9f));
    dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), barBg, 2.0f);

    float fillW = barW * ratio;
    ImU32 barFill;
    if (ratio > 0.5f) barFill = ImGui::GetColorU32(ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
    else if (ratio > 0.25f) barFill = ImGui::GetColorU32(ImVec4(0.9f, 0.8f, 0.1f, 1.0f));
    else barFill = ImGui::GetColorU32(ImVec4(0.95f, 0.15f, 0.1f, pulse));
    dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + fillW, barY + barH), barFill, 2.0f);

    // Border
    ImU32 border = ImGui::GetColorU32(ImVec4(0.5f, 0.15f, 0.1f, 0.8f));
    dl->AddRect(ImVec2(bx, by), ImVec2(bx + boxW, by + boxH), border, 6.0f, 0, 1.5f);

    // Timer text
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%.1fs", remaining);
    ImU32 textCol;
    if (ratio > 0.5f) textCol = ImGui::GetColorU32(ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
    else if (ratio > 0.25f) textCol = ImGui::GetColorU32(ImVec4(1.0f, 0.9f, 0.2f, 1.0f));
    else textCol = ImGui::GetColorU32(ImVec4(1.0f, 0.2f, 0.1f, pulse));
    ImVec2 ts = ImGui::CalcTextSize(timeBuf);
    dl->AddText(ImVec2(bx + boxW * 0.5f - ts.x * 0.5f, by + 4), textCol, timeBuf);

    // Label
    ImU32 labelCol = ImGui::GetColorU32(ImVec4(0.7f, 0.7f, 0.7f, 0.9f));
    dl->AddText(ImVec2(bx + 6, by + 4), labelCol, "BOMB");
}

// ==================== DEAD PLAYER SKULLS ====================

void Overlay::DrawDeadSkulls(GameState* state) {
    if (!settings.showDeadSkulls) return;
    auto dl = DL();
    float* vm = state->viewMatrix;

    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (p.health > 0) continue;
        if (p.health == 0 && p.team == 0) continue;
        if (!p.IsEnemy(state->localTeam)) continue;

        Vector2 pos;
        if (!WorldToScreen(p.origin, pos, vm)) continue;

        float distFade = 1.0f;
        auto* local = state->GetLocal();
        if (local) {
            float dist = p.origin.DistTo(local->origin) * 0.01905f;
            distFade = 1.0f - dist / 2000.0f;
            if (distFade < 0.1f) distFade = 0.1f;
            if (distFade > 1.0f) distFade = 1.0f;
        }

        // Skull symbol: X mark
        float sz = 6.0f;
        ImU32 col = ImGui::GetColorU32(ImVec4(0.8f, 0.2f, 0.2f, 0.5f * distFade));
        dl->AddLine(ImVec2(pos.x - sz, pos.y - sz), ImVec2(pos.x + sz, pos.y + sz), col, 1.5f);
        dl->AddLine(ImVec2(pos.x + sz, pos.y - sz), ImVec2(pos.x - sz, pos.y + sz), col, 1.5f);
        dl->AddCircle(ImVec2(pos.x, pos.y), sz + 2, ImGui::GetColorU32(ImVec4(0.5f, 0.1f, 0.1f, 0.25f * distFade)), 8);
    }
}

// ==================== RADAR ====================

void Overlay::RenderRadar(GameState* state) {
    auto* local = state->GetLocal();
    if (!local) return;

    int rs = settings.radarSize;
    int margin = 20;
    int rx, ry;
    switch (settings.radarPosX) {
        case 0: rx = screenW - rs - margin; ry = margin; break;
        case 1: rx = margin; ry = margin; break;
        case 2: rx = screenW - rs - margin; ry = screenH - rs - margin; break;
        case 3: rx = margin; ry = screenH - rs - margin; break;
        default: rx = screenW - rs - margin; ry = margin;
    }
    int cx = rx + rs / 2;
    int cy = ry + rs / 2;
    int radius = rs / 2;
    auto dl = DL();

    bool isMinimal = (settings.radarStyle == 2);

    if (!isMinimal) {
        bool isCircle = (settings.radarStyle == 0);
        // Background
        if (isCircle)
            dl->AddCircleFilled(ImVec2((float)cx, (float)cy), (float)radius, C(settings.radarBg), 64);
        else
            dl->AddRectFilled(ImVec2((float)rx, (float)ry), ImVec2((float)(rx+rs), (float)(ry+rs)), C(settings.radarBg));
        // Border
        if (isCircle)
            dl->AddCircle(ImVec2((float)cx, (float)cy), (float)radius, C(settings.radarBorder), 64, 1.5f);
        else
            dl->AddRect(ImVec2((float)rx, (float)ry), ImVec2((float)(rx+rs), (float)(ry+rs)), C(settings.radarBorder), 0.f, 0, 1.5f);
        // Rings / grid
        if (isCircle) {
            float ringCol[4] = {0.3f, 0.3f, 0.3f, 0.25f};
            dl->AddCircle(ImVec2((float)cx, (float)cy), (float)(radius * 0.33f), C(ringCol), 48, 1.f);
            dl->AddCircle(ImVec2((float)cx, (float)cy), (float)(radius * 0.66f), C(ringCol), 48, 1.f);
        } else {
            float gridCol[4] = {0.3f, 0.3f, 0.3f, 0.2f};
            ImU32 gc = C(gridCol);
            dl->AddLine(ImVec2((float)(cx - radius), (float)cy), ImVec2((float)(cx + radius), (float)cy), gc);
            dl->AddLine(ImVec2((float)cx, (float)(cy - radius)), ImVec2((float)cx, (float)(cy + radius)), gc);
        }
    }

    // Local yaw for rotation
    float yaw = local->viewAngle.yaw;
    if (!settings.radarRotate) yaw = 0;
    float radAngle = -yaw * 3.14159265f / 180.0f;
    float cosY = cosf(radAngle), sinY = sinf(radAngle);
    float scale = settings.radarScale * 0.01905f;

    // Players
    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid()) continue;
        if (p.team == state->localTeam && !settings.radarShowTeam) continue;

        Vector3 d = p.origin - local->origin;
        float rx2 = d.x * cosY - d.y * sinY;
        float ry2 = d.x * sinY + d.y * cosY;

        int mx = cx + (int)(ry2 * scale);
        int my = cy - (int)(rx2 * scale);
        int dx = mx - cx, dy = my - cy;

        if (settings.radarStyle != 2) {
            if (dx * dx + dy * dy > radius * radius) continue;
        }

        if (p.team == state->localTeam) {
            float tc[4] = {0, 1, 0, 0.8f};
            dl->AddCircleFilled(ImVec2((float)mx, (float)my), 3, C(tc), 12);
        } else {
            float ec[4] = {1, 0.2f, 0.2f, 1};
            dl->AddCircleFilled(ImVec2((float)mx, (float)my), 3, C(ec), 12);
            float eRad = -p.viewAngle.yaw * 3.14159265f / 180.0f;
            float edx = sinf(eRad) * 5, edy = -cosf(eRad) * 5;
            dl->AddTriangleFilled(
                ImVec2(mx + edx, my + edy),
                ImVec2(mx - edy * 0.5f, my + edx * 0.5f),
                ImVec2(mx + edy * 0.5f, my - edx * 0.5f), C(ec));
        }
    }

    // Bomb position on radar
    if (state->bombPlanted) {
        Vector3 bombDelta = state->bombPos - local->origin;
        float bRx = bombDelta.x * cosY - bombDelta.y * sinY;
        float bRy = bombDelta.x * sinY + bombDelta.y * cosY;
        int bmx = cx + (int)(bRy * scale);
        int bmy = cy - (int)(bRx * scale);

        if (settings.radarStyle == 2 || ((bmx - cx) * (bmx - cx) + (bmy - cy) * (bmy - cy) <= radius * radius)) {
            float bombPulse = 0.6f + 0.4f * sinf((float)ImGui::GetTime() * 6.0f);
            float bombCol[4] = { 1.0f, 0.5f, 0.0f, bombPulse };
            dl->AddCircleFilled(ImVec2((float)bmx, (float)bmy), 5.0f, C(bombCol), 8);
            float bombOutline[4] = { 1.0f, 1.0f, 1.0f, 0.6f * bombPulse };
            dl->AddCircle(ImVec2((float)bmx, (float)bmy), 5.0f, C(bombOutline), 8, 1.0f);
            float bombLabel[4] = { 1.0f, 0.6f, 0.0f, 0.9f };
            DrawTextOutlined("C4", bmx - 4, bmy + 8, bombLabel, true);
        }
    }

    if (!isMinimal) {
        float lDir = settings.radarRotate ? 0 : -yaw;
        float lRad = lDir * 3.14159265f / 180.0f;
        float ldx = sinf(lRad) * 7, ldy = -cosf(lRad) * 7;
        float lc[4] = {0, 1, 0, 1};
        dl->AddTriangleFilled(
            ImVec2(cx + ldx, cy + ldy),
            ImVec2(cx - ldy * 0.5f, cy + ldx * 0.5f),
            ImVec2(cx + ldy * 0.5f, cy - ldx * 0.5f), C(lc));
    }
}

void Overlay::RenderNadeUI(GameState* state) {
    if (!settings.nadeHelperEnabled || !state) return;
    
    NadeEngine* nadeEng = static_cast<NadeEngine*>(nadeEnginePtr);
    if (!nadeEng) return;
    
    auto* local = state->GetLocal();
    if (!local) return;
    
    auto dl = DL();
    float* vm = state->viewMatrix;
    
    // Get nearby nade spots
    std::vector<const NadeSpot*> nearbySpots;
    for (auto& [map, nades] : nadeEng->GetDatabase()) {
        if (map != state->mapName) continue;
        for (auto& nade : nades) {
            float dist = local->origin.DistTo(nade.standPos);
            if (dist < settings.nadeHelperRadius) {
                nearbySpots.push_back(&nade);
            }
        }
    }
    
    // Draw each nearby spot
    for (const auto* spot : nearbySpots) {
        Vector2 spotScr;
        if (!WorldToScreen(spot->standPos, spotScr, vm)) continue;
        
        float dist = local->origin.DistTo(spot->standPos);
        float alpha = 1.0f - (dist / settings.nadeHelperRadius);
        if (alpha < 0.2f) alpha = 0.2f;
        
        // Color by nade type
        float col[4];
        switch (spot->type) {
            case SMOKE:  col[0]=0.2f; col[1]=0.6f; col[2]=1.0f; col[3]=alpha; break;
            case FLASH:  col[0]=1.0f; col[1]=1.0f; col[2]=0.2f; col[3]=alpha; break;
            case HE:     col[0]=1.0f; col[1]=0.3f; col[2]=0.2f; col[3]=alpha; break;
            case MOLOTOV:col[0]=1.0f; col[1]=0.5f; col[2]=0.0f; col[3]=alpha; break;
            default:     col[0]=0.8f; col[1]=0.8f; col[2]=0.8f; col[3]=alpha; break;
        }
        
        // Draw diamond marker
        float sz = 8.0f;
        dl->AddTriangleFilled(
            ImVec2(spotScr.x, spotScr.y - sz),
            ImVec2(spotScr.x + sz, spotScr.y),
            ImVec2(spotScr.x, spotScr.y + sz), C(col));
        dl->AddTriangleFilled(
            ImVec2(spotScr.x, spotScr.y - sz),
            ImVec2(spotScr.x - sz, spotScr.y),
            ImVec2(spotScr.x, spotScr.y + sz), C(col));
        
        // Draw name
        float nameCol[4] = {1, 1, 1, alpha};
        DrawTextOutlined(spot->name.c_str(), (int)spotScr.x, (int)(spotScr.y + sz + 4), nameCol, true);
        
        // Draw distance
        char distBuf[32];
        snprintf(distBuf, sizeof(distBuf), "%.0fm", dist / 50.0f);
        float distCol[4] = {0.7f, 0.7f, 0.7f, alpha * 0.8f};
        DrawTextOutlined(distBuf, (int)spotScr.x, (int)(spotScr.y + sz + 18), distCol, true);
        
        // Draw action type
        const char* actionText = "";
        switch (spot->action) {
            case JUMP_THROW:  actionText = "JUMP"; break;
            case CROUCH_THROW:actionText = "CROUCH"; break;
            case WALK_THROW:  actionText = "WALK"; break;
            case RUN_THROW:   actionText = "RUN"; break;
            default:          actionText = "STAND"; break;
        }
        float actCol[4] = {0.5f, 0.9f, 0.5f, alpha * 0.9f};
        DrawTextOutlined(actionText, (int)spotScr.x, (int)(spotScr.y + sz + 32), actCol, true);
    }
    
    // Draw closest spot info panel
    NadeSpot* closest = nadeEng->GetClosestNade(local->origin, settings.nadeHelperRadius);
    if (closest) {
        float dist = local->origin.DistTo(closest->standPos);
        
        // Info panel at top-center
        int panelW = 300, panelH = 80;
        int px = screenW / 2 - panelW / 2;
        int py = 60;
        
        float bg[4] = {0.05f, 0.05f, 0.1f, 0.85f};
        DrawRect(px, py, panelW, panelH, bg, true);
        float border[4] = {0.3f, 0.5f, 1.0f, 0.6f};
        DrawRect(px, py, panelW, panelH, border, false);
        
        // Title
        float titleCol[4] = {0.5f, 0.8f, 1.0f, 1.0f};
        DrawTextOutlined(closest->name.c_str(), px + panelW/2, py + 8, titleCol, true);
        
        // Description
        float descCol[4] = {0.8f, 0.8f, 0.8f, 0.9f};
        DrawTextOutlined(closest->description.c_str(), px + panelW/2, py + 28, descCol, true);
        
        // Action + distance
        char actBuf[64];
        const char* actStr = "Stand";
        switch (closest->action) {
            case JUMP_THROW:  actStr = "Jump + Throw"; break;
            case CROUCH_THROW:actStr = "Crouch + Throw"; break;
            case WALK_THROW:  actStr = "Walk + Throw"; break;
            case RUN_THROW:   actStr = "Run + Throw"; break;
        }
        snprintf(actBuf, sizeof(actBuf), "[%s]  %.0fm", actStr, dist / 50.0f);
        float actCol2[4] = {0.5f, 0.9f, 0.5f, 0.9f};
        DrawTextOutlined(actBuf, px + panelW/2, py + 48, actCol2, true);
        
        // Throw key hint
        char keyBuf[32];
        snprintf(keyBuf, sizeof(keyBuf), "Press [%c] to throw", (char)nadeEng->throwKeyBind);
        float keyCol[4] = {1.0f, 1.0f, 0.4f, 0.9f};
        DrawTextOutlined(keyBuf, px + panelW/2, py + 64, keyCol, true);
    }
    
    // Draw auto-trick indicator
    if (settings.autoTrickEnabled) {
        MovementTrick* trick = nadeEng->GetClosestTrick(local->origin, settings.autoTrickRadius);
        if (trick && settings.autoTrickShowIndicators) {
            Vector2 trickScr;
            if (WorldToScreen(trick->triggerPos, trickScr, vm)) {
                float tAlpha = 0.8f;
                float tCol[4] = {1.0f, 0.8f, 0.0f, tAlpha};
                
                // Draw pulsing circle
                float pulse = 0.5f + 0.5f * sinf((float)GetTickCount() * 0.005f);
                float radius = 12.0f + pulse * 4.0f;
                dl->AddCircle(ImVec2(trickScr.x, trickScr.y), radius, C(tCol), 20, 2.0f);
                
                // Draw name
                float tNameCol[4] = {1.0f, 0.9f, 0.3f, tAlpha};
                DrawTextOutlined(trick->name.c_str(), (int)trickScr.x, (int)(trickScr.y + 20), tNameCol, true);
                
                // Draw key hint
                char trickKeyBuf[32];
                snprintf(trickKeyBuf, sizeof(trickKeyBuf), "[%c] to execute", (char)settings.autoTrickKey);
                float tKeyCol[4] = {0.7f, 0.7f, 0.7f, 0.7f};
                DrawTextOutlined(trickKeyBuf, (int)trickScr.x, (int)(trickScr.y + 36), tKeyCol, true);
            }
        }
    }
}

// ==================== CHAMS (external: filled player silhouette) ====================

void Overlay::DrawChams(GameState* state) {
    if (!settings.showChams) return;
    auto dl = DL();
    float* vm = state->viewMatrix;

    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;

        Vector2 headScr, feetScr, chestScr, pelvisScr;
        if (!WorldToScreen(p.bonePos[6], headScr, vm)) continue;
        if (!WorldToScreen(p.origin, feetScr, vm)) continue;
        if (!WorldToScreen(p.bonePos[5], chestScr, vm)) continue;
        if (!WorldToScreen(p.bonePos[0], pelvisScr, vm)) continue;

        Vector2 lHandScr, rHandScr, lFootScr, rFootScr;
        bool hasLHand = WorldToScreen(p.bonePos[10], lHandScr, vm);
        bool hasRHand = WorldToScreen(p.bonePos[16], rHandScr, vm);
        bool hasLFoot = WorldToScreen(p.bonePos[24], lFootScr, vm);
        bool hasRFoot = WorldToScreen(p.bonePos[27], rFootScr, vm);

        float distFade = 1.0f;
        auto* local = state->GetLocal();
        if (local) {
            float dist = p.origin.DistTo(local->origin) * 0.01905f;
            distFade = 1.0f - dist / 2500.0f;
            if (distFade < 0.15f) distFade = 0.15f;
            if (distFade > 1.0f) distFade = 1.0f;
        }

        float chamsCol[4];
        bool isVisible = p.spotted;
        if (isVisible) {
            chamsCol[0] = settings.chamsColor[0];
            chamsCol[1] = settings.chamsColor[1];
            chamsCol[2] = settings.chamsColor[2];
            chamsCol[3] = settings.chamsColor[3] * distFade;
        } else {
            chamsCol[0] = settings.chamsHiddenColor[0];
            chamsCol[1] = settings.chamsHiddenColor[1];
            chamsCol[2] = settings.chamsHiddenColor[2];
            chamsCol[3] = settings.chamsHiddenColor[3] * distFade;
        }

        ImU32 col = ImGui::GetColorU32(ImVec4(chamsCol[0], chamsCol[1], chamsCol[2], chamsCol[3]));

        float playerH = feetScr.y - headScr.y;
        if (playerH < 5.0f) continue;

        float bodyW = playerH * 0.3f;
        float headR = playerH * 0.08f;
        float limbW = playerH * 0.06f;

        dl->AddCircleFilled(ImVec2(headScr.x, headScr.y), headR, col, 12);

        float shoulderW = bodyW * 0.85f;
        ImVec2 chestL(chestScr.x - shoulderW, chestScr.y);
        ImVec2 chestR(chestScr.x + shoulderW, chestScr.y);
        ImVec2 pelvisL(pelvisScr.x - bodyW * 0.6f, pelvisScr.y);
        ImVec2 pelvisR(pelvisScr.x + bodyW * 0.6f, pelvisScr.y);
        dl->AddQuadFilled(chestL, chestR, pelvisR, pelvisL, col);

        ImVec2 neckL(chestScr.x - shoulderW * 0.5f, chestScr.y - playerH * 0.02f);
        ImVec2 neckR(chestScr.x + shoulderW * 0.5f, chestScr.y - playerH * 0.02f);
        ImVec2 headBL(headScr.x - headR * 0.8f, headScr.y + headR * 0.5f);
        ImVec2 headBR(headScr.x + headR * 0.8f, headScr.y + headR * 0.5f);
        dl->AddQuadFilled(neckL, neckR, headBR, headBL, col);

        if (hasLHand)
            dl->AddLine(ImVec2(chestScr.x - shoulderW, chestScr.y), ImVec2(lHandScr.x, lHandScr.y), col, limbW);
        if (hasRHand)
            dl->AddLine(ImVec2(chestScr.x + shoulderW, chestScr.y), ImVec2(rHandScr.x, rHandScr.y), col, limbW);

        float hipW = bodyW * 0.35f;
        if (hasLFoot)
            dl->AddLine(ImVec2(pelvisScr.x - hipW, pelvisScr.y), ImVec2(lFootScr.x, lFootScr.y), col, limbW * 1.2f);
        if (hasRFoot)
            dl->AddLine(ImVec2(pelvisScr.x + hipW, pelvisScr.y), ImVec2(rFootScr.x, rFootScr.y), col, limbW * 1.2f);
    }
}

// ==================== HIT MARKER ====================

void Overlay::DrawHitMarker() {
    if (!settings.showHitMarker) return;
    float time = (float)ImGui::GetTime();
    float elapsed = time - g_hitMarkerTime;
    if (elapsed > 0.5f) return;

    auto dl = DL();
    float cx = (float)screenW * 0.5f;
    float cy = (float)screenH * 0.5f;
    float alpha = 1.0f - elapsed * 2.0f;
    if (alpha < 0) alpha = 0;
    float size = settings.hitMarkerSize + elapsed * 8.0f;

    ImU32 col = ImGui::GetColorU32(ImVec4(
        settings.hitMarkerColor[0], settings.hitMarkerColor[1],
        settings.hitMarkerColor[2], settings.hitMarkerColor[3] * alpha));

    float gap = 4.0f;
    dl->AddLine(ImVec2(cx - gap, cy - gap), ImVec2(cx - gap - size, cy - gap - size), col, 1.5f);
    dl->AddLine(ImVec2(cx + gap, cy - gap), ImVec2(cx + gap + size, cy - gap - size), col, 1.5f);
    dl->AddLine(ImVec2(cx - gap, cy + gap), ImVec2(cx - gap - size, cy + gap + size), col, 1.5f);
    dl->AddLine(ImVec2(cx + gap, cy + gap), ImVec2(cx + gap + size, cy + gap + size), col, 1.5f);

    // Damage number
    if (g_hitMarkerDamage > 0) {
        char dmgBuf[16];
        snprintf(dmgBuf, sizeof(dmgBuf), "%.0f", g_hitMarkerDamage);
        ImU32 dmgCol = ImGui::GetColorU32(ImVec4(1, 1, 1, alpha));
        dl->AddText(ImVec2(cx + size + 4, cy - size - 4), dmgCol, dmgBuf);
    }
}

// ==================== SPECTATOR LIST ====================

void Overlay::DrawSpectators(GameState* state) {
    if (!settings.showSpectators) return;
    if (!state) return;
    auto dl = DL();
    auto* local = state->GetLocal();
    if (!local) return;

    int specCount = 0;
    std::vector<std::string> specNames;

    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || p.IsEnemy(state->localTeam)) continue;
        if (p.health <= 0) continue;

        Vector3 toUs = local->origin - p.origin;
        float dist = toUs.Length();
        if (dist > 2000) continue;

        float yaw = atan2f(toUs.y, toUs.x) * 180.0f / 3.14159f;
        float pitch = -atan2f(toUs.z, sqrtf(toUs.x * toUs.x + toUs.y * toUs.y)) * 180.0f / 3.14159f;

        float yawDiff = fabsf(p.viewAngle.yaw - yaw);
        if (yawDiff > 180) yawDiff = 360 - yawDiff;
        float pitchDiff = fabsf(p.viewAngle.pitch - pitch);

        if (yawDiff < 15 && pitchDiff < 15) {
            specCount++;
            specNames.push_back(p.name);
        }
    }

    float panelW = 180.0f;
    float lineH = 16.0f;
    float panelH = 30.0f + specCount * lineH;
    if (panelH < 40.0f) panelH = 40.0f;
    float px = (float)screenW - panelW - 10.0f;
    float py = 100.0f;

    float bg[4] = {0.05f, 0.05f, 0.1f, 0.8f};
    DrawRect((int)px, (int)py, (int)panelW, (int)panelH, bg, true);
    float border[4] = {0.3f, 0.5f, 1.0f, 0.5f};
    DrawRect((int)px, (int)py, (int)panelW, (int)panelH, border, false);

    char title[32];
    snprintf(title, sizeof(title), "Spectators: %d", specCount);
    float titleCol[4] = {0.5f, 0.8f, 1.0f, 1.0f};
    DrawTextOutlined(title, (int)(px + 8), (int)(py + 6), titleCol);

    for (int i = 0; i < specCount && i < (int)specNames.size(); i++) {
        float nameCol[4] = {0.8f, 0.8f, 0.8f, 0.9f};
        DrawTextOutlined(specNames[i].c_str(), (int)(px + 8), (int)(py + 26 + i * lineH), nameCol);
    }
}

// ==================== VELOCITY INDICATOR ====================

void Overlay::DrawVelocity(GameState* state) {
    if (!settings.showVelocity) return;
    auto* local = state->GetLocal();
    if (!local) return;

    float speed = local->velocity.Length2D();
    if (speed < 1.0f) return;

    auto dl = DL();
    float cx = (float)screenW * 0.5f;
    float by = (float)screenH * 0.5f + 30.0f;

    char buf[16]; snprintf(buf, sizeof(buf), "%.0f", speed);
    float vc[4] = { settings.velocityColor[0], settings.velocityColor[1],
                     settings.velocityColor[2], settings.velocityColor[3] };
    // Color based on speed
    if (speed > 250.0f) { vc[0] = 1.0f; vc[1] = 0.3f; vc[2] = 0.1f; } // Red = running
    else if (speed > 100.0f) { vc[0] = 1.0f; vc[1] = 0.8f; vc[2] = 0.0f; } // Yellow = walking
    else { vc[0] = 0.3f; vc[1] = 0.9f; vc[2] = 0.3f; } // Green = slow

    DrawTextOutlined(buf, (int)cx, (int)by, vc, true);
}

// ==================== RECOIL CROSSHAIR ====================

void Overlay::DrawRecoilCrosshair(GameState* state) {
    if (!settings.showRecoilCrosshair) return;
    auto* local = state->GetLocal();
    if (!local) return;

    auto dl = DL();
    float cx = (float)screenW * 0.5f;
    float cy = (float)screenH * 0.5f;

    // Apply aim punch (recoil) offset
    float punchX = local->aimPunch.x * 2.0f;
    float punchY = local->aimPunch.y * 2.0f;

    float rx = cx + punchY; // yaw
    float ry = cy - punchX; // pitch

    float size = 5.0f;
    float gap = 2.0f;
    ImU32 col = C(settings.recoilCrosshairColor);

    dl->AddLine(ImVec2(rx, ry - gap), ImVec2(rx, ry - gap - size), col, 1.5f);
    dl->AddLine(ImVec2(rx, ry + gap), ImVec2(rx, ry + gap + size), col, 1.5f);
    dl->AddLine(ImVec2(rx - gap, ry), ImVec2(rx - gap - size, ry), col, 1.5f);
    dl->AddLine(ImVec2(rx + gap, ry), ImVec2(rx + gap + size, ry), col, 1.5f);
}

// ==================== SCOPE OVERLAY ====================

void Overlay::DrawScopeOverlay(GameState* state) {
    if (!settings.showScopeOverlay) return;
    auto* local = state->GetLocal();
    if (!local || !local->scoped) return;

    auto dl = DL();
    float cx = (float)screenW * 0.5f;
    float cy = (float)screenH * 0.5f;
    float lineLen = (float)screenH * 0.4f;

    ImU32 col = C(settings.scopeLineColor);

    // Horizontal lines
    dl->AddLine(ImVec2(cx - lineLen, cy), ImVec2(cx - 15.0f, cy), col, 1.0f);
    dl->AddLine(ImVec2(cx + 15.0f, cy), ImVec2(cx + lineLen, cy), col, 1.0f);

    // Vertical lines
    dl->AddLine(ImVec2(cx, cy - lineLen), ImVec2(cx, cy - 15.0f), col, 1.0f);
    dl->AddLine(ImVec2(cx, cy + 15.0f), ImVec2(cx, cy + lineLen), col, 1.0f);

    // Center dot
    dl->AddCircleFilled(ImVec2(cx, cy), 2.0f, col, 8);
}

// ==================== DROPPED WEAPONS ESP ====================

void Overlay::DrawDroppedWeapons(GameState* state) {
    if (!settings.showDroppedWeapons) return;
    if (!state) return;
    auto dl = DL();
    float* vm = state->viewMatrix;
    auto* local = state->GetLocal();
    if (!local) return;

    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid()) continue;

        // Use any valid player's origin to check distance to dropped weapons
        // Since we can't iterate world entities externally, show weapon info
        // on players who are not the local player's team
        // Dropped weapons are world entities - we approximate via dead player positions
        if (p.health <= 0 && p.weaponId > 0 && p.IsEnemy(state->localTeam)) {
            float dist = p.origin.DistTo(local->origin) * 0.01905f;
            if (dist > 500.0f) continue;

            float distFade = 1.0f - dist / 500.0f;
            if (distFade < 0.2f) distFade = 0.2f;

            Vector2 posScr;
            if (!WorldToScreen(p.origin, posScr, vm)) continue;

            float col[4] = { settings.droppedWeaponColor[0], settings.droppedWeaponColor[1],
                             settings.droppedWeaponColor[2], settings.droppedWeaponColor[3] * distFade };

            // Draw weapon icon/text at death position
            float sz = 6.0f;
            dl->AddCircleFilled(ImVec2(posScr.x, posScr.y), sz, C(col), 8);

            const char* wpn = WeaponIcon(p.weaponId);
            if (wpn[0]) {
                DrawTextOutlined(wpn, (int)posScr.x, (int)(posScr.y + sz + 2), col, true);
            }

            // Distance label
            char distBuf[16];
            snprintf(distBuf, sizeof(distBuf), "%.0fm", dist);
            float distCol[4] = { col[0], col[1], col[2], col[3] * 0.7f };
            DrawTextOutlined(distBuf, (int)posScr.x, (int)(posScr.y + sz + 14), distCol, true);
        }
    }
}

// ==================== SOUND ESP ====================

void Overlay::DrawSoundESP(GameState* state) {
    if (!settings.showSoundESP) return;
    if (!state) return;
    auto dl = DL();
    float* vm = state->viewMatrix;
    float time = (float)ImGui::GetTime();
    float pulse = 0.6f + 0.4f * sinf(time * 5.0f);

    auto* local = state->GetLocal();
    if (!local) return;

    float centerX = (float)screenW * 0.5f;
    float centerY = (float)screenH * 0.5f;

    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;

        float dist = p.origin.DistTo(local->origin) * 0.01905f;
        if (dist > 2000.0f) continue;

        Vector2 headScr;
        if (!WorldToScreen(p.bonePos[6], headScr, vm)) continue;

        // Check if on screen - only show for off-screen enemies
        if (headScr.x > 20 && headScr.x < (float)(screenW - 20) &&
            headScr.y > 20 && headScr.y < (float)(screenH - 20)) continue;

        // Direction from center to enemy
        float dx = headScr.x - centerX;
        float dy = headScr.y - centerY;
        float screenDist = sqrtf(dx * dx + dy * dy);
        if (screenDist < 1.0f) continue;
        float nx = dx / screenDist;
        float ny = dy / screenDist;

        // Clamp to screen edge
        float margin = 60.0f;
        float edgeX = centerX + nx * ((float)screenW * 0.5f - margin);
        float edgeY = centerY + ny * ((float)screenH * 0.5f - margin);
        edgeX = (std::max)(margin, (std::min)((float)screenW - margin, edgeX));
        edgeY = (std::max)(margin, (std::min)((float)screenH - margin, edgeY));

        // Sound direction indicator: pulsing arc/circle
        float soundCol[4] = { 1.0f, 0.8f, 0.0f, 0.7f * pulse };

        // Pulsing sound ring
        float ringR = 10.0f + 5.0f * sinf(time * 8.0f + (float)i);
        ImU32 ringC = ImGui::GetColorU32(ImVec4(soundCol[0], soundCol[1], soundCol[2], soundCol[3] * 0.5f));
        dl->AddCircle(ImVec2(edgeX, edgeY), ringR, ringC, 12, 1.5f);

        // Inner dot
        ImU32 dotC = ImGui::GetColorU32(ImVec4(soundCol[0], soundCol[1], soundCol[2], soundCol[3]));
        dl->AddCircleFilled(ImVec2(edgeX, edgeY), 3.0f, dotC, 8);

        // Distance text
        char distBuf[16];
        snprintf(distBuf, sizeof(distBuf), "%.0fm", dist);
        float textCol[4] = { 1.0f, 1.0f, 1.0f, 0.8f * pulse };
        DrawTextOutlined(distBuf, (int)edgeX, (int)(edgeY + 14), textCol, true);
    }
}

void Overlay::RenderMenu(AimController* aimController) {
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
    if (aimController && aimController->WasShotFired()) {
        DrawText(("Target: " + std::to_string(aimController->GetCurrentTarget()) + " HC:" + std::to_string((int)aimController->GetCurrentHitchance())).c_str(), x, y, green, false);
        y += 20;
    }
    DrawText("INS=Menu END=Exit", x, y + 30, grey, false);
}

void Overlay::RenderStatus(GameState* state, ExploitSelector*, AimController* aimController) {
    if (!state) return;
    float green[4] = {0, 1, 0, 1}, grey[4] = {0.5f, 0.5f, 0.5f, 1};
    std::string s = state->GetLocal() ? "Active" : "Waiting...";
    DrawText(s.c_str(), 10, 10, green, false);
    DrawText(("Players: " + std::to_string(state->playerCount)).c_str(), 10, 30, grey, false);
}

// ==================== WORLD TO SCREEN ====================

// Исправленная математика: жесткая привязка к Row-Major
bool Overlay::WorldToScreen(Vector3 wp, Vector2& sp, float* m) {
    if (!m) return false;

    float w = wp.x * m[12] + wp.y * m[13] + wp.z * m[14] + m[15];
    if (w < 0.001f) return false;

    float x = wp.x * m[0] + wp.y * m[1] + wp.z * m[2] + m[3];
    float y = wp.x * m[4] + wp.y * m[5] + wp.z * m[6] + m[7];

    float nx = x / w;
    float ny = y / w;

    sp.x = (screenW * 0.5f) + (nx * screenW * 0.5f);
    sp.y = (screenH * 0.5f) - (ny * screenH * 0.5f);
    return true;
}




