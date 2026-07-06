#pragma once
#include "core/types.h"
#include "core/memory_reader.h"
#include "core/state_engine.h"
#include "aimbot/resolver.h"
#include "aimbot/autowall.h"
#include <deque>

class AimController {
public:
    // ==================== WEAPON PROFILES ====================
    struct WeaponProfile {
        float fov = 40.0f;
        float smoothSpeed = 25.0f;
        float maxAnglePerFrame = 36.0f;
        float minHitchance = 5.0f;
        float maxDist = 8192.0f;
        float fireRate = 0.10f;      // seconds between shots
        float spread = 0.18f;
    };

    // ==================== SETTINGS ====================
    struct Settings {
        bool enabled = true;
        bool aimbot = true;
        bool triggerbot = false;
        bool silentAim = true;
        bool rcs = true;
        bool predictive = true;

        // Core params
        float fov = 40.0f;
        float maxDist = 8192.0f;
        float minHitchance = 5.0f;
        float minDamage = 5.0f;

        // Smooth aim
        bool smoothEnabled = true;
        float smoothSpeed = 25.0f;
        float smoothMaxAngle = 36.0f;
        int smoothCurve = 3;         // 0=Linear 1=EaseIn 2=EaseOut 3=EaseInOut 4=Circle 5=Exp

        // Backtrack
        bool backtrackEnabled = true;
        float backtrackMaxTime = 0.2f;

        // Triggerbot
        bool triggerEnabled = false;
        int triggerDelay = 0;        // ms
        int triggerBurst = 1;

        // Rage
        bool rageMode = false;
        bool autoScope = true;
        bool autoStop = true;

        // Resolver
        int resolverMode = 0;        // 0=off, 1=LBY, 2=Freestanding, 3=Bruteforce

        // Weapon profiles
        bool useWeaponProfiles = true;
        WeaponProfile rifleProfile   = { 40.0f, 25.0f, 36.0f, 5.0f, 8192.0f, 0.10f, 0.18f };
        WeaponProfile pistolProfile  = { 50.0f, 35.0f, 40.0f, 3.0f, 4096.0f, 0.15f, 0.12f };
        WeaponProfile sniperProfile  = { 8.0f,  8.0f,  10.0f, 15.0f, 16384.0f, 1.50f, 0.05f };
        WeaponProfile smgProfile     = { 60.0f, 30.0f, 42.0f, 3.0f, 4096.0f, 0.07f, 0.20f };
        WeaponProfile shotgunProfile = { 70.0f, 40.0f, 50.0f, 2.0f, 1024.0f, 0.80f, 0.25f };

        // Target switching
        float targetSwitchThreshold = 1.2f;

        // Keybind (0=always, VK_RBUTTON=right click, VK_LBUTTON=left click, etc.)
        int aimKey = 0;               // 0 = always on

        // Mouse aim (external: use mouse_event instead of CUserCmd)
        float mouseSensitivity = 1.0f;  // multiplier for mouse movement
        bool autoFire = true;           // auto-press LMB when on target
        float onTargetThreshold = 3.0f; // degrees: how close before firing
        int fireMode = 0;               // 0=hold to fire, 1=burst (click each shot)

        // Screen dimensions for pixel conversion
        int screenWidth = 1920;
        int screenHeight = 1080;
    } settings;

    // ==================== BACKTRACK ====================
    struct TickRecord {
        Vector3 origin;
        Vector3 bonePos[30];
        QAngle viewAngle;
        float simulationTime;
        bool valid;
    };

    static constexpr int MAX_RECORDS = 128;
    static constexpr int MAX_PLAYERS = 64;

    TickRecord records[MAX_PLAYERS][MAX_RECORDS];
    int writeIndex[MAX_PLAYERS] = {};
    int tickCount = 0;

    // ==================== CONSTRUCTOR ====================
    AimController(MemoryReader* reader, Resolver* res, Autowall* aw);
    ~AimController() = default;

    void SetOffsets(const OffsetDatabase& off) { offsets = off; }

    // ==================== MAIN UPDATE ====================
    void Update(GameState* state, float deltaTime);

    // ==================== RESULTS ====================
    bool HasTarget() const { return hasTarget; }
    QAngle GetAimAngle() const { return finalAimAngle; }
    bool ShouldFire() const;
    bool WasShotFired() const { return shotFired; }
    void ResetShotFlag() { shotFired = false; }
    int GetCurrentTarget() const { return currentTarget; }
    float GetCurrentHitchance() const { return currentHitchance; }

    // ==================== SUBSYSTEM ACCESS ====================
    Resolver* GetResolver() { return resolver; }

private:
    MemoryReader* mem;
    Resolver* resolver;
    Autowall* autowall;
    OffsetDatabase offsets;

    // State
    int currentTarget = -1;
    float currentHitchance = 0;
    QAngle finalAimAngle;
    bool hasTarget = false;
    bool shotFired = false;
    float lastShotTime = 0;
    int shotsFired = 0;
    int currentWeaponId = 0;
    QAngle lastPunchAngle;

    // ==================== TARGET SELECTION ====================
    int SelectBestTarget(GameState* state, QAngle& outAngle, float& outHitchance);
    Vector3 GetHitboxPos(Player& p, int hitbox);
    WeaponProfile GetActiveProfile();

    // ==================== ANGLE MATH ====================
    QAngle CalcAngle(Vector3 from, Vector3 to);
    float GetFov(QAngle cur, QAngle target);
    float CalcHitchance(Vector3 from, Vector3 to, float spread, float dist) const;
    float GetWeaponSpread(int weaponId) const;
    float GetFireRate(int weaponId) const;

    // ==================== SMOOTH AIM ====================
    QAngle lastSmoothTarget;
    bool smoothFirstFrame = true;
    float smoothProgress = 0;

    // ==================== BACKTRACK ====================
    void RecordTick(GameState* state);
    bool GetBestBacktrack(int playerIdx, Vector3& outPos);

    // ==================== SHOT EXECUTION ====================
    void ExecuteShot(GameState* state, QAngle aimAngle);
    void ApplyMouseAim(GameState* state, QAngle targetAngle, float deltaTime);

    // ==================== TRIGGERBOT ====================
    float triggerbotTimer = 0;

    // ==================== RCS ====================
    int previousShotsFired = 0;
};
