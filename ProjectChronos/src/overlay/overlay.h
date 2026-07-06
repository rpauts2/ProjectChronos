#pragma once
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_4.h>
#include "core/types.h"
#include "core/state_engine.h"
#include "exploits/exploit_selector.h"
#include "aimbot/aim_controller.h"
#include "nade/nade_engine.h"

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
    bool pendingStyleUpdate = false;

    char windowClass[16];
    bool classRegistered = false;

    struct { float x, y, w, h; int age; } smoothBox[64];

public:
    // ==================== VISUAL CONFIG ====================
    struct VisualConfig {
        int activePreset = 0;

        // --- ESP Master Toggles ---
        bool showBox = true;
        bool showName = true;
        bool showHealth = true;
        bool showWeapon = true;
        bool showSkeleton = false;
        bool showArmor = false;
        bool showFlags = true;
        bool showDistance = false;
        bool showSnaplines = false;
        bool showViewVector = true;
        bool showRadar = true;
        bool showCrosshair = true;
        bool showNadeUI = true;

        // --- Global Info Panel Toggle ---
        bool showInfoPanel = true;

        // --- Glow ---
        bool showGlow = false;
        float glowAlpha = 0.4f;
        float glowColor[4] = {0.3f, 0.6f, 1.0f, 0.5f};
        float glowThickness = 6.f;
        bool glowBloom = true;
        int glowStyle = 0; // 0=Solid, 1=Outline, 2=Pulse, 3=Rainbow

        // --- Box ---
        int boxStyle = 1;      // 0=None, 1=Corner, 2=Square, 3=3D, 4=Rounded, 5=Glow
        float boxWidthRatio = 0.40f;
        float boxThickness = 1.5f;
        float boxColor[4] = {1, 1, 1, 1};
        float enemyColor[4] = {1, 0.2f, 0.2f, 1};
        float teamColor[4] = {0.2f, 1, 0.2f, 1};

        // --- Health / Armor ---
        int healthStyle = 0;
        float healthBarPos = 0;
        bool healthBarVertical = true;
        bool healthBasedColor = true;

        // --- Skeleton ---
        float skeletonThickness = 1.5f;
        float skeletonColor[4] = {1, 1, 1, 0.6f};
        float skeletonHiddenAlpha = 0.2f;

        // --- Aimline (separate from skeleton) ---
        bool showAimline = true;
        float aimlineColor[4] = {0.5f, 0.2f, 1.0f, 0.7f};
        float aimlineThickness = 1.2f;

        // --- Text ---
        float nameColor[4] = {1, 1, 0.4f, 1};
        float weaponColor[4] = {0.8f, 0.8f, 0.8f, 1};
        float flagColor[4] = {1, 1, 1, 0.8f};

        // --- Snaplines ---
        float snaplineColor[4] = {0.5f, 0.2f, 1.0f, 0.4f};
        float snaplineThickness = 1.0f;
        int snaplineStyle = 0;

        // --- Visibility-Based Styling ---
        bool visibilityBasedStyle = true;

        // --- Radar ---
        int radarStyle = 0;
        float radarScale = 0.5f;
        int radarSize = 200;
        bool radarRotate = true;
        bool radarShowTeam = false;
        float radarBg[4] = {0, 0, 0, 0.6f};
        float radarBorder[4] = {0.3f, 0.5f, 1.0f, 0.7f};
        int radarPosX = 0;

        // --- Crosshair ---
        bool crosshairDot = true;
        bool crosshairLines = true;
        bool crosshairFovCircle = true;
        int crosshairSize = 6;
        int crosshairGap = 3;
        float crosshairThickness = 1.5f;
        float crosshairColor[4] = {0, 1, 0, 1};
        float crosshairFovColor[4] = {0.6f, 0.2f, 0.8f, 0.25f};
        float fovRadius = 150.0f;

        // --- Visual Feedback ---
        bool showExploitFlash = true;
        float exploitFlashColor[4] = {0.5f, 0.2f, 1.0f, 0.8f};
        bool exploitSound = true;
        int exploitSoundType = 0; // 0=click, 1=beep, 2=coin
        float exploitFlashDuration = 0.3f;

        // --- Decision Engine Display ---
        bool showDecisionInfo = false;

        // --- Anti-Aim ---
        bool antiAimEnabled = false;
        bool fakeWalk = false;
        bool microDesync = false;
        int desyncSide = 0;         // 0=auto, 1=left, 2=right
        float fakeWalkSpeed = 1.0f; // 0.5=slow, 1.0=normal

        // --- BunnyHop ---
        bool bhopEnabled = false;
        int bhopHitchance = 80;     // 0-100
        bool bhopAutoStrafe = false;

        // --- HeadDot ---
        bool showHeadDot = true;
        float headDotSize = 4.0f;
        float headDotColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        bool headDotGlow = true;
        float headDotGlowSize = 12.0f;

        // --- Ammo ---
        bool showAmmo = true;
        float ammoColor[4] = {1.0f, 0.75f, 0.0f, 1.0f};

        // --- Watermark ---
        bool showWatermark = true;
        float watermarkColor[4] = {0.5f, 0.2f, 1.0f, 1.0f};

        // --- Trajectory ---
        bool showTrajectory = true;
        float trajectoryColor[4] = {1.0f, 1.0f, 0.0f, 0.8f};

        // --- Other ---
        int aimMode = 0;
        int exploitLevel = 1;

        // --- Out-of-View Indicators ---
        bool showOOVIndicators = true;
        float oovColor[4] = {0.5f, 0.2f, 1.0f, 0.8f};

        // --- Money Display ---
        bool showMoney = true;

        // --- Bomb Info ---
        bool showBombInfo = true;

        // --- Dead Skulls ---
        bool showDeadSkulls = true;

        // --- Weapon Icons (short icon text instead of full name) ---
        bool useWeaponIcons = true;

        // --- Chams (external: filled player silhouette through walls) ---
        bool showChams = false;
        float chamsColor[4] = {0.0f, 0.7f, 1.0f, 0.6f};
        float chamsHiddenColor[4] = {0.0f, 0.4f, 0.8f, 0.35f};

        // --- Hit Marker ---
        bool showHitMarker = false;
        float hitMarkerColor[4] = {1.0f, 1.0f, 1.0f, 0.9f};
        float hitMarkerSize = 8.0f;

        // --- Spectator List ---
        bool showSpectators = false;

        // --- Velocity Indicator ---
        bool showVelocity = false;
        float velocityColor[4] = {0.5f, 0.8f, 1.0f, 0.9f};

        // --- Recoil Crosshair ---
        bool showRecoilCrosshair = false;
        float recoilCrosshairColor[4] = {1.0f, 1.0f, 1.0f, 0.7f};

        // --- Scope Overlay ---
        bool showScopeOverlay = false;
        float scopeLineColor[4] = {0.0f, 0.0f, 0.0f, 0.8f};

        // --- Anti-Flash ---
        bool antiFlashEnabled = false;

        // --- Third Person ---
        bool thirdPerson = false;

        // --- Dropped Weapons ---
        bool showDroppedWeapons = false;
        float droppedWeaponColor[4] = {0.8f, 0.8f, 0.3f, 0.8f};

        // --- Sound ESP ---
        bool showSoundESP = false;

        // --- Grenade Helper ---
        bool nadeHelperEnabled = true;
        int nadeHelperKey = 0x47;           // G key (default)
        float nadeHelperRadius = 500.0f;    // show spots within this distance
        int nadeHelperThrowKey = 'V';       // key to execute throw
        float nadeHelperAimSpeed = 0.3f;    // smooth aim speed for nade helper (0-1)
        bool nadeHelperAutoThrow = false;   // auto-throw when in position

        // --- Auto-Tricks ---
        bool autoTrickEnabled = true;
        int autoTrickKey = 0x54;            // T key
        float autoTrickRadius = 150.0f;     // trigger radius
        bool autoTrickShowIndicators = true; // show nearby trick markers

        // --- Anti-Aim (Fake Angles) ---
        bool antiAimPitchEnabled = false;
        int antiAimPitch = 0;               // 0=off, 1=up, 2=down, 3=random
        int antiAimYaw = 0;                 // 0=off, 1=left, 2=right, 3=back, 4=random

        // --- Fake Duck ---
        bool fakeDuckEnabled = false;

        // --- Edge Anti-Aim ---
        bool edgeAntiAim = false;

        // --- Fake Latency ---
        bool fakeLatencyEnabled = false;
        float fakeLatencyAmount = 100.0f;

        // --- Quick Switch / Quick Stop ---
        bool quickSwitchEnabled = false;
        bool quickStopEnabled = false;

        // --- Knife Bot ---
        bool knifeBotEnabled = false;
        float knifeBotRange = 80.0f;

        // --- Auto Defuse ---
        bool autoDefuseEnabled = false;

        // --- Skin Changer ---
        bool skinChangerEnabled = true;

        // --- Clan Tag ---
        bool clanTagEnabled = false;
        int clanTagStyle = 0;
    } settings;

    void ApplyPreset(int presetId);

    // --- Exploit flash state ---
    struct {
        bool active = false;
        float startTime = 0.f;
        float duration = 0.3f;
        float color[4] = {0.5f, 0.2f, 1.0f, 0.8f};
    } exploitFlash;

public:
    Overlay() {}
    ~Overlay() { Cleanup(); }

    bool Create();
    void Cleanup();
    void Render(GameState* state, ExploitSelector* selector, AimController* aimController = nullptr);
    bool IsActive() const { return initialized; }
    bool IsMenuOpen() const { return menuOpen; }
    void ToggleMenu();
    void ApplyPendingStyle();
    bool WorldToScreen(Vector3 worldPos, Vector2& screenPos, float* matrix);

    HWND GetWindow() const { return hwnd; }

    // --- Exploit visual feedback ---
    void TriggerExploitFlash(float r, float g, float b, float a = 0.8f, float dur = 0.3f);
    void TriggerHitMarker(float damage = 0);

    // Nade engine for menu access
    void* nadeEnginePtr = nullptr;

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
    void RenderMenu(AimController* aimController);
    void RenderImGui(AimController* aimController);
    void RenderStatus(GameState* state, ExploitSelector* selector, AimController* aimController);
    void RenderAimbotFov(GameState* state);
    void RenderWatermark();
    void RenderExploitFlash();
    void PlayExploitSound();

private:
    void BeginDraw();
    void EndDraw();

    void DrawLine(int x1, int y1, int x2, int y2, float* color, float thickness = 1);
    void DrawBox(int x, int y, int w, int h, float* color, float thickness = 1);
    void DrawCornerBox(int x, int y, int w, int h, float* color, float thickness = 1);
    void DrawRect(int x, int y, int w, int h, float* color, bool filled = false);
    void DrawCircle(int cx, int cy, int r, float* color, int segments = 24);
    void DrawText(const char* text, int x, int y, float* color, bool centered = false);
    void DrawHealthBar(int x, int y, int w, int h, int health, int maxHealth);
    void DrawArmorBar(int x, int y, int w, int h, int armor);
    void DrawBoneLine(Vector3 from, Vector3 to, float* vm, float* color);
    bool GetPlayerBox(GameState* state, int idx, int& outX, int& outY, int& outW, int& outH);
    void DrawTextOutlined(const char* text, int x, int y, float* color, bool centered = false);
    void DrawBloomPass(const Vector3 bones[30], float* vm, int x, int y, int w, int h, float* color);
    void DrawGlowShell(const Vector3 bones[30], float* vm, int screenW, int screenH, float* color, float alphaPeak, int style, float time);
    void DrawSkeletonPro(int bx, int by, int bw, int bh, GameState* state, int idx, float distFade);
    void DrawAimline(int bx, int by, int bw, int bh, GameState* state, int idx);
    void DrawViewVector(int bx, int by, int bw, int bh, GameState* state, int idx);
    void DrawInfoPanel(int bx, int by, int bw, int bh, GameState* state, int idx, float distFade);
    void DrawSnaplines(GameState* state);
    void Draw3DBox(int bx, int by, int bw, int bh, GameState* state, int idx, float* color);
    void DrawHealthBasedColor(float health, float* outColor);
    void DrawAimTarget(GameState* state);
    void DrawHeadDot(int bx, int by, int bw, int bh, GameState* state, int idx, float distFade);
    void DrawFlags(int bx, int by, int bw, int bh, GameState* state, int idx, float distFade);
    void DrawOOVIndicators(GameState* state);
    void DrawBombTimer(GameState* state);
    void DrawDeadSkulls(GameState* state);
    void DrawAmmoBar(int x, int y, int w, int h, int clip, int maxClip);
    void DrawTrajectory(GameState* state);
    void DrawChams(GameState* state);
    void DrawHitMarker();
    void DrawSpectators(GameState* state);
    void DrawVelocity(GameState* state);
    void DrawRecoilCrosshair(GameState* state);
    void DrawScopeOverlay(GameState* state);
    void DrawDroppedWeapons(GameState* state);
    void DrawSoundESP(GameState* state);
    static const char* WeaponIcon(int id);
    AimController* cachedAim = nullptr;
    float bombPlantTime = 0;
};
