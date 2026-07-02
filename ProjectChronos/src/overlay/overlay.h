#pragma once
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_4.h>
#include "core/types.h"
#include "core/state_engine.h"
#include "exploits/exploit_selector.h"
#include "aimbot/ragebot.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")

class Overlay {
    IDCompositionDevice* dcompDevice = nullptr;
    IDCompositionTarget* dcompTarget = nullptr;
    IDCompositionVisual* dcompVisual = nullptr;
    ID3D11Texture2D* dcompTexture = nullptr;

    HWND hwnd = nullptr;
    HWND targetHwnd = nullptr;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* rtView = nullptr;


    int screenW = 1920;
    int screenH = 1080;
    int overlayX = 0;
    int overlayY = 0;
    bool dcompMode = false;
    bool initialized = false;
    bool menuOpen = false;

    char windowClass[16];
    bool classRegistered = false;

    struct {
        bool showBox = true;
        bool showName = true;
        bool showHealth = true;
        bool showWeapon = true;
        bool showSkeleton = false;
        bool showGhost = true;
        bool showNadeUI = true;
        bool showRadar = true;
        bool showMenu = true;
        bool showCrosshair = true;
        float boxColor[4] = {1, 0, 0, 1};
        float ghostColor[4] = {0, 1, 1, 0.5f};
        int boxStyle = 1;           // 0=full, 1=corner, 2=filled
        int skeletonStyle = 0;      // 0=box-ratio, 1=full
        float boxWidthRatio = 0.40f; // width = height / 2.5 (tiansongyu 0.4, Litware: 0.52, Rshwmom: 0.6)
        bool healthBarVertical = true;
        bool showAmmoBar = false;
        int aimMode = 0;
        int exploitLevel = 1;
    } settings;

public:
    Overlay() {}
    ~Overlay() { Cleanup(); }

    bool Create();
    void Cleanup();
    void Render(GameState* state, ExploitSelector* selector, Ragebot* ragebot = nullptr);
    bool IsActive() const { return initialized; }
    bool IsMenuOpen() const { return menuOpen; }
    void ToggleMenu();
    bool WorldToScreen(Vector3 worldPos, Vector2& screenPos, float* matrix);

    HWND GetWindow() const { return hwnd; }

private:
    bool InitDComp();
    bool InitClassicWindow();
    bool InitSwapChain();
    bool FinishCreate();
    void CleanupDComp();
    bool FindTargetWindow();
    void RandomizeClassName();
    void UpdateInputState();

    void RenderESP(GameState* state);
    void RenderRadar(GameState* state);
    void RenderNadeUI(GameState* state);
    void RenderMenu(Ragebot* ragebot);
    void RenderImGui(Ragebot* ragebot);
    void RenderStatus(GameState* state, ExploitSelector* selector, Ragebot* ragebot);
    void RenderSkeleton(GameState* state, int playerIdx);
    void RenderAimbotFov(GameState* state);
    void RenderWatermark();

    void BeginDraw();
    void EndDraw();

    void DrawLine(int x1, int y1, int x2, int y2, float* color, float thickness = 1);
    void DrawBox(int x, int y, int w, int h, float* color, float thickness = 1);
    void DrawCornerBox(int x, int y, int w, int h, float* color, float thickness = 1);
    void DrawRect(int x, int y, int w, int h, float* color, bool filled = false);
    void DrawCircle(int cx, int cy, int r, float* color, int segments = 24);
    void DrawText(const char* text, int x, int y, float* color, bool centered = false);
    void DrawHealthBar(int x, int y, int w, int h, int health, int maxHealth);
    void DrawBoneLine(Vector3 from, Vector3 to, float* vm, float* color);
    bool GetPlayerBox(GameState* state, int idx, int& outX, int& outY, int& outW, int& outH);
};
