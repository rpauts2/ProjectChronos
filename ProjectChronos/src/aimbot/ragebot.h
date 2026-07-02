#pragma once
#include "core/types.h"
#include "core/memory_reader.h"
#include "core/state_engine.h"
#include "resolver.h"
#include "autowall.h"
#include <vector>
#include <algorithm>

enum HitboxType {
    HITBOX_HEAD = 0,
    HITBOX_NECK,
    HITBOX_CHEST,
    HITBOX_STOMACH,
    HITBOX_PELVIS,
    HITBOX_LEFT_UPPER_ARM,
    HITBOX_RIGHT_UPPER_ARM,
    HITBOX_LEFT_FOREARM,
    HITBOX_RIGHT_FOREARM,
    HITBOX_LEFT_THIGH,
    HITBOX_RIGHT_THIGH,
    HITBOX_LEFT_CALF,
    HITBOX_RIGHT_CALF,
    HITBOX_COUNT
};

struct HitboxWeight {
    HitboxType type;
    float weight;
    float damageMultiplier;
};

class Ragebot {
    MemoryReader* mem;
    Resolver* resolver;
    Autowall* autowall;

    // Target tracking
    int currentTarget = -1;
    int previousTarget = -1;
    HitboxType targetHitbox = HITBOX_HEAD;
    int targetChangeCount = 0;

    // Shot control
    bool shotFiredThisTick = false;
    float lastShotTime = 0;
    int shotsFired = 0;
    float lastAutoScopeTime = 0;
    int currentWeaponId = 0;

    // Hitchance
    float currentHitchance = 0;

    // Aim smoothing
    QAngle targetAngle;
    QAngle lastAimAngle;
    bool hasAimTarget = false;

    static constexpr float PI = 3.14159265f;

public:
    struct RagebotSettings {
        bool enabled = true;
        bool autofire = true;
        bool autoScope = true;
        bool autoStop = true;
        bool silentAim = true;
        float maxFov = 180.0f;
        float maxDistance = 4096.0f;
        float minHitchance = 35.0f;
        float minDamage = 5.0f;
        int hitboxPriority = 0;
        int resolverMode = 0;
        int aimTargetMode = 0;
        bool visibleOnly = false;
        bool wallOnly = false;
        bool aimOnFire = true;
        bool aimAtBomb = false;
        float smoothing = 1.0f;
        int hitboxSelection = 0;
        bool drawFov = true;
        // Hitbox toggles
        bool hitbox_enable[HITBOX_COUNT];
        // Hitbox weights
        float hitbox_weight[HITBOX_COUNT];
    } settings;

    Ragebot(MemoryReader* reader, Resolver* res, Autowall* aw);
    ~Ragebot() = default;

    void SetReader(MemoryReader* reader) { mem = reader; }

    // Main update - called each frame
    void Update(GameState* state, float frameTime);

    // Get current aim angle (for silent aim override)
    QAngle GetAimAngle() const { return targetAngle; }
    bool HasAimTarget() const { return hasAimTarget; }
    bool ShouldFire() const;
    int GetCurrentTarget() const { return currentTarget; }
    float GetCurrentHitchance() const { return currentHitchance; }
    bool WasShotFired() const { return shotFiredThisTick; }
    void ResetShotFlag() { shotFiredThisTick = false; }
    HitboxType GetTargetHitbox() const { return targetHitbox; }

    // Target selection
    struct RageTarget {
        int playerIndex;
        HitboxType hitbox;
        float hitchance;
        float damage;
        float fov;
        float distance;
        float priorityScore;

        bool operator<(const RageTarget& other) const {
            return priorityScore > other.priorityScore;
        }
    };

    RageTarget SelectBestTarget(GameState* state);
    void Reset() { currentTarget = -1; hasAimTarget = false; }
    void SetResolverMode(int mode) { settings.resolverMode = mode; }

private:
    HitboxType GetBestHitbox(GameState* state, int playerIdx);
    float CalcHitchance(GameState* state, int playerIdx, HitboxType hitbox, QAngle aimAngle);
    float CalcDamage(GameState* state, int playerIdx, HitboxType hitbox, QAngle aimAngle);
    QAngle CalcAimAngle(Vector3 from, Vector3 to);
    bool IsVisible(GameState* state, Vector3 from, Vector3 to);
    bool ShouldAutoScope(GameState* state);
    void AutoStop(GameState* state);
    void ResetTarget();
    Vector3 GetHitboxPosition(GameState* state, int playerIdx, HitboxType hitbox);

    float GetFovToTarget(QAngle currentAngle, QAngle targetAngle);
    static float GetWeaponHitchanceBase(int weaponId);
    static float GetWeaponDamageBase(int weaponId);
    static float GetFireRate(int weaponId);
};
