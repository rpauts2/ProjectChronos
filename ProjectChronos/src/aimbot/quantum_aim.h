#pragma once
#include "core/types.h"
#include "core/memory_reader.h"
#include "core/state_engine.h"
#include "aimbot/resolver.h"
#include "aimbot/autowall.h"
#include <deque>
#include <random>

// ==================== 4.1 LUCK ENGINE ====================
class LuckEngine {
    struct {
        float currentLuck = 0.5f;
        int consecutiveHits = 0;
        int consecutiveMisses = 0;
        DWORD lastHitTime = 0;
    } state;
public:
    float CalculateLuck();
    bool RollDice(float chance);
    void OnHit() { state.consecutiveHits++; state.consecutiveMisses = 0; state.lastHitTime = GetTickCount(); }
    void OnMiss() { state.consecutiveMisses++; state.consecutiveHits = 0; }
    void Reset() { state = {}; }
};

// ==================== 4.2 HUMAN ERROR ====================
class HumanError {
    struct {
        float fatigue = 0.0f;
        float focus = 1.0f;
        float mood = 0.7f;
        int kills = 0;
        int deaths = 0;
        DWORD sessionStart = GetTickCount();
    } state;
public:
    float CalculateErrorRate();
    QAngle ApplyError(QAngle angles);
    void OnKill() { state.kills++; }
    void OnDeath() { state.deaths++; }
};

// ==================== 4.3 MOMENTUM SHOT ====================
class MomentumShot {
    Vector3 prevVelocity;
    bool isCounterStrafing = false;
    float counterProgress = 0.0f;
public:
    void Update(Vector3 velocity);
    bool ShouldShoot(Vector3 velocity, float angleChange);
    bool IsCounterStrafing() const { return isCounterStrafing && counterProgress > 0.8f; }
};

// ==================== 4.4 VELOCITY ENGINE ====================
class VelocityEngine {
    Vector3 prevVelocity;
public:
    Vector3 PredictPosition(Vector3 pos, Vector3 velocity, float timeAhead);
    bool PredictCounterStrafe(Vector3 velocity, float deltaTime);
};

// ==================== 4.5 RECOIL FLOW ====================
class RecoilFlow {
public:
    QAngle GetNaturalRecoil(int weaponId, int shotsFired, const QAngle& punchAngle);
};

// ==================== 3.1 RICOCHET AIMBOT ====================
class RicochetAimbot {
    struct Surface { Vector3 point; Vector3 normal; float distance; };
    Surface FindSurface(Vector3 from, Vector3 to);
    Vector3 CalculateRicochetPoint(Vector3 shooter, Vector3 target, const Surface& surf);
public:
    bool Apply(Vector3 from, Vector3 to, QAngle& outAngle);
};

// ==================== 3.3 BULLET TIME ====================
class BulletTime {
    float originalSens = 2.0f;
    DWORD activeUntil = 0;
    bool active = false;
public:
    void Activate(float durationMs = 100);
    void Update();
};

// ==================== 2.3 FAKE LAG ====================
class FakeLag {
    int chokeAmount = 0;
    int chokeCounter = 0;
public:
    bool ShouldSkipPacket();       // returns true to skip this packet
    void SetChoke(int amount) { chokeAmount = amount; }
};

// ==================== 2.4 TIME DILATION ====================
class TimeDilation {
    float phase = 0.0f;
public:
    void Update(float deltaTime);
    int GetFakePingMs();
};

// ==================== 2.5 INTERPOLATION EXPLOIT ====================
class InterpolationExploit {
    DWORD changeUntil = 0;
    float originalRatio = 2.0f;
public:
    void OnShot();
    void Update();
    void SetOriginalRatio(float r) { originalRatio = r; }
};

// ==================== 2.6 PREDICTIVE AIMBOT ====================
class PredictiveAimbot {
    VelocityEngine velocity;
public:
    Vector3 PredictTarget(Vector3 pos, Vector3 vel, float timeAhead = 0.15f);
};

// ==================== 5.1 DECISION ENGINE ====================
class DecisionEngine {
public:
    struct Situation {
        int enemiesAlive = 0;
        int teammatesAlive = 0;
        float health = 100;
        float ping = 30;
        float roundTime = 115;
        bool beingShotAt = false;
        bool bombPlanted = false;
        float distance = 0;
        int localWeapon = 0;
    };

    struct Decision {
        float aggression = 0.5f;
        float errorRate = 0.08f;
        float luckWeight = 0.5f;
        float networkWeight = 0.3f;
        float ricochetWeight = 0.0f;
        bool useWallPen = false;
        bool useFakeLag = false;
        bool useTimeDilation = false;
        bool useRicochet = false;
        enum Mode { LEGIT, AGGRESSIVE, GODMODE } mode = LEGIT;
    };

    Decision Evaluate(const Situation& s);
};

// ==================== QUANTUM AIM — THE ULTIMATE 19-IN-1 ====================
class QuantumAim {
    MemoryReader* mem;
    Resolver* resolver;
    Autowall* autowall;

    // All 19 subsystems
    LuckEngine luck;
    HumanError human;
    MomentumShot momentum;
    VelocityEngine velocity;
    RecoilFlow recoil;
    RicochetAimbot ricochet;
    BulletTime bulletTime;
    FakeLag fakeLag;
    TimeDilation timeDilation;
    InterpolationExploit interpExploit;
    PredictiveAimbot predictive;
    DecisionEngine decision;

    // Hitbox system
    enum Hitbox { HEAD, NECK, CHEST, STOMACH, PELVIS, ARM_L, ARM_R, LEG_L, LEG_R };

    // State
    int currentTarget = -1;
    Hitbox targetHitbox = HEAD;
    float currentHitchance = 0;
    QAngle finalAimAngle;
    bool hasTarget = false;
    bool shotFired = false;
    float lastShotTime = 0;
    int shotsFired = 0;
    int currentWeaponId = 0;

    // Noise
    float timeAccum = 0;
    float noiseAmplitude = 0.8f;
    float noiseFrequency = 2.5f;

    // Decision cache
    DecisionEngine::Decision currentDecision;

public:
    struct Settings {
        bool enabled = true;
        // Basic
        bool aimbot = true;
        bool rcs = true;
        bool triggerbot = false;
        bool predictive = true;
        // Network
        bool subTick = true;
        bool lagComp = true;
        bool fakeLag = false;
        bool timeDilation = false;
        bool interpExploit = false;
        // Ballistic
        bool ricochet = false;
        bool wallPen = false;
        bool bulletTime = false;
        // Psychology
        bool luckEngine = true;
        bool humanError = true;
        bool momentumShot = true;
        bool velocityEngine = true;
        bool recoilFlow = true;
        // Adaptive
        bool decisionEngine = true;
        // Parameters
        float fov = 180.0f;
        float maxDist = 8192.0f;
        float smoothness = 1.0f;
        float minHitchance = 35.0f;
        float minDamage = 5.0f;
        int resolverMode = 0;
        bool silentAim = true;
    } settings;

    QuantumAim(MemoryReader* reader, Resolver* res, Autowall* aw);
    ~QuantumAim() = default;

    // Main update
    void Update(GameState* state, float deltaTime);

    // Results
    bool HasTarget() const { return hasTarget; }
    QAngle GetAimAngle() const { return finalAimAngle; }
    bool ShouldFire() const;
    bool WasShotFired() const { return shotFired; }
    void ResetShotFlag() { shotFired = false; }
    int GetCurrentTarget() const { return currentTarget; }
    float GetCurrentHitchance() const { return currentHitchance; }
    DecisionEngine::Decision GetCurrentDecision() const { return currentDecision; }

    // Subsystem access
    LuckEngine* GetLuck() { return &luck; }
    HumanError* GetHuman() { return &human; }
    FakeLag* GetFakeLag() { return &fakeLag; }
    TimeDilation* GetTimeDilation() { return &timeDilation; }

private:
    // Selection
    int SelectBestTarget(GameState* state, QAngle& aimAngle, float& hitchance, float& damage);
    Vector3 GetHitboxPos(GameState* state, int idx, Hitbox hb);

    // Angle calculation
    QAngle CalcAngle(Vector3 from, Vector3 to);
    float GetFov(QAngle cur, QAngle target);
    float CalcHitchance(Vector3 from, Vector3 to, float weaponSpread, float dist);
    float GetWeaponSpread(int weaponId);

    // Shot execution
    void ExecuteShot(GameState* state);

    // Timing
    static float GetFireRate(int weaponId);
};
