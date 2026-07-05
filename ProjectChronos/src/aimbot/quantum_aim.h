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

// ==================== 4.2 HUMAN ERROR (Perlin Noise) ====================
class HumanError {
    struct {
        float fatigue = 0.0f;
        float focus = 1.0f;
        float mood = 0.7f;
        int kills = 0;
        int deaths = 0;
        DWORD sessionStart = GetTickCount();
        // Perlin noise state
        float noiseTimePitch = 0.0f;
        float noiseTimeYaw = 0.0f;
        float noiseFreq = 0.7f;   // How fast error changes
        float noiseAmp = 1.2f;    // Max error magnitude
    } state;

    // Perlin noise helper (1D)
    static float Perlin1D(float x);
    float SampleNoise(float t) { return Perlin1D(t * state.noiseFreq) * state.noiseAmp; }

public:
    float CalculateErrorRate();
    QAngle ApplyError(QAngle angles);
    void OnKill() { state.kills++; }
    void OnDeath() { state.deaths++; }
    void Update(float dt); // Advance noise time
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

// ==================== 7.1 BACKTRACK ENGINE — Lag Compensation ====================
class BacktrackEngine {
public:
    struct TickRecord {
        Vector3 origin;
        Vector3 bonePos[30];
        QAngle viewAngle;
        float simulationTime;
        float duckAmount;
        int flags;
        bool valid;
    };

    static constexpr int MAX_RECORDS = 64;   // per player
    static constexpr int MAX_PLAYERS = 64;
    static constexpr float MAX_BACKTRACK_TIME = 0.2f; // 200ms

    TickRecord records[MAX_PLAYERS][MAX_RECORDS];
    int writeIndex[MAX_PLAYERS] = {};

    void RecordTick(Player* players, int playerCount, int localTeam);
    bool GetBestRecord(Player& player, int playerIndex, TickRecord& outRecord);
    Vector3 GetHitboxPosFromRecord(const TickRecord& rec, int hitbox);
    int GetTickCount() const { return tickCount; }

private:
    int tickCount = 0;
};

// ==================== 7.2 SMOOTH AIM — Easing Curves ====================
class SmoothAim {
public:
    enum CurveType {
        CURVE_LINEAR = 0,
        CURVE_EASE_IN,       // slow start, fast end
        CURVE_EASE_OUT,      // fast start, slow end
        CURVE_EASE_IN_OUT,   // slow start, slow end, fast middle
        CURVE_CIRCLE,        // circular ease
        CURVE_EXPONENTIAL,   // exponential decay
    };

    struct Settings {
        bool enabled = true;
        float speed = 10.0f;          // angles per frame (higher = faster)
        float maxAnglePerFrame = 15.0f; // clamp per-frame rotation
        CurveType curve = CURVE_EASE_OUT;
        bool adaptiveSpeed = true;    // faster for bigger angle diffs
    } settings;

    // Returns smoothed angle for this frame
    QAngle Smooth(QAngle current, QAngle target, float dt);

    // Reset interpolation state (call when target changes)
    void Reset() { startAngle = {}; lastTarget = {}; firstFrame = true; }

private:
    QAngle startAngle;
    QAngle lastTarget;
    bool firstFrame = true;
    float progress = 0.0f;

    float ApplyCurve(float t, CurveType type) const;
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

// ==================== 6.1 SPRAY DNA ====================
class SprayDNA {
    struct Pattern {
        float horizontalDrift;  // unique per-player horizontal offset
        float verticalBias;     // unique per-player vertical bias
        float jitter;           // unique micro-tremor amount
        float recoveryRate;     // how fast it "recovers" to center
    } pattern;

    // Seeded from player SteamID or session
    DWORD seed;
public:
    void Generate(int weaponId, DWORD playerSeed);
    QAngle GetSprayOffset(int shotNumber, const QAngle& recoilAngle);
    float GetJitter() const { return pattern.jitter; }
};

// ==================== 6.2 DYNAMIC FOV ====================
class DynamicFOV {
public:
    // Returns adjusted FOV based on weapon type and distance
    static float GetFOV(int weaponId, float distance, float baseFov);
};

// ==================== 6.3 EVENT DISPATCHER ====================
class EventDispatcher {
public:
    enum Event {
        EVT_NONE,
        EVT_PLAYER_HURT,
        EVT_PLAYER_DEATH,
        EVT_ROUND_START,
        EVT_ROUND_END,
        EVT_BOMB_PLANT,
        EVT_BOMB_DEFUSE,
        EVT_WEAPON_FIRE,
        EVT_SHOT_FIRED,
        EVT_ENEMY_SPOTTED
    };

    struct EventData {
        Event type = EVT_NONE;
        int playerId = -1;
        float timestamp = 0;
        int damage = 0;
        int weapon = 0;
    };

    void Dispatch(Event evt, int playerId = -1, int damage = 0, int weapon = 0);
    bool Poll(EventData& out);
    void Clear();

private:
    std::deque<EventData> eventQueue;
};

// ==================== 6.4 ANTI-AIM ====================
class AntiAim {
public:
    struct Settings {
        bool enabled = false;
        bool fakeWalk = false;
        bool microDesync = false;
        int desyncSide = 0;     // 0=auto, 1=left, 2=right
        float fakeWalkSpeed = 1.0f;
    } settings;

    // Returns a QAngle offset to add to view angles
    QAngle GetAntiAimAngles(QAngle viewAngle, float velocity, DWORD tickCount);
    // Returns desync side: -1=left, 0=center, 1=right
    int GetDesyncSide(DWORD tickCount);
    // Fake walk: returns true if we should slow down this tick
    bool ShouldSlowDown(DWORD tickCount);

private:
    int autoSide = 1;
    DWORD lastSideSwitch = 0;
    float phase = 0.0f;
};

// ==================== 6.5 BUNNYHOP ====================
class BunnyHop {
public:
    struct Settings {
        bool enabled = false;
        int hitchance = 80;     // 0-100
        bool autoStrafe = false;
    } settings;

    // Returns true if IN_JUMP should be pressed this tick
    bool ShouldJump(DWORD tickCount);
    // Returns lateral strafe amount for auto-strafe
    float GetStrafeAngle(QAngle viewAngle, Vector3 velocity, DWORD tickCount);
    // Update with current state
    void Update(bool onGround, QAngle viewAngle, Vector3 velocity);

private:
    DWORD lastJumpTick = 0;
    bool wasOnGround = true;
    float strafeAngle = 0.0f;
};

// ==================== 5.1 DECISION ENGINE (Enhanced FSM) ====================
class DecisionEngine {
public:
    enum Mode { IDLE, ENGAGEMENT, PANIC, GODMODE };

    struct ThreatInfo {
        int playerIndex;
        float distance;
        float angleToUs;      // 0=looking at us, 180=looking away
        float health;
        float priorityScore;
    };

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
        ThreatInfo threats[64];
        int threatCount = 0;
    };

    struct Decision {
        float aggression = 0.5f;
        float errorRate = 0.08f;
        float luckWeight = 0.5f;
        float networkWeight = 0.3f;
        float ricochetWeight = 0.0f;
        float fovMultiplier = 1.0f;
        float smoothnessMultiplier = 1.0f;
        bool useWallPen = false;
        bool useFakeLag = false;
        bool useTimeDilation = false;
        bool useRicochet = false;
        Mode mode = IDLE;
    };

    // Threat Assessment — weighted scoring per target
    void AssessThreats(Situation& sit, GameState* state, Vector3 localOrigin);

    // FSM evaluate
    Decision Evaluate(const Situation& s);

private:
    Mode currentMode = IDLE;
    DWORD modeStartTime = 0;
    DWORD lastModeChange = 0;

    // Adaptive error rate — learns from session
    float sessionErrorAccum = 0.f;
    int sessionShots = 0;
};

// ==================== 7.0 NEURAL CONTROLLER — Self-Learning Brain ====================
//
// The NeuralController is the META-INTELLIGENCE that learns which combination
// of modules works best in every situation. It tracks:
//   - Context fingerprints (weapon, distance, health, movement, enemy count)
//   - Per-module effectiveness (hit rate when module was active)
//   - Per-combination success (which modules together work best)
//   - Parameter optimization (auto-tunes FOV, smoothing, error rate, etc.)
//
// After enough shots, it automatically configures all subsystems for
// maximum performance in any given situation. No manual tuning needed.
//
class NeuralController {
public:
    // Number of distinct modules the brain tracks
    static constexpr int MODULE_COUNT = 16;
    static constexpr int CONTEXT_BINS = 8;    // distance bins
    static constexpr int WEAPON_GROUPS = 5;   // pistol/rifle/sniper/smg/shotgun
    static constexpr int HISTORY_SIZE = 128;

    // Module IDs — each maps to a subsystem
    enum ModuleID {
        MOD_RCS = 0,
        MOD_PREDICTIVE,
        MOD_HUMAN_ERROR,
        MOD_LUCK_ENGINE,
        MOD_MOMENTUM,
        MOD_VELOCITY,
        MOD_RECOIL_FLOW,
        MOD_RICOCHET,
        MOD_BULLET_TIME,
        MOD_FAKE_LAG,
        MOD_TIME_DILATION,
        MOD_INTERP,
        MOD_RESOLVER,
        MOD_ANTIAIM,
        MOD_BHOP,
        MOD_SPRAY_DNA
    };

    // Context fingerprint — uniquely identifies a combat situation
    struct Context {
        int weaponGroup;     // 0-4 (pistol/rifle/sniper/smg/shotgun)
        int distanceBin;     // 0-7 (close to far)
        int healthBin;       // 0-3 (low to high)
        int enemyCount;      // 0-5+
        bool isMoving;       // local player velocity > 50
        bool isAirborne;     // local player not on ground
        bool isScoped;       // scoped in
        int movementState;   // 0=still, 1=strafing, 2=running, 3=counter-strafing

        int ToIndex() const {
            return weaponGroup * (CONTEXT_BINS * 4 * 6 * 4) +
                   distanceBin * (4 * 6 * 4) +
                   healthBin * (6 * 4) +
                   std::min(enemyCount, 5) * 4 +
                   movementState;
        }
    };

    // Per-module effectiveness record
    struct ModuleEffectiveness {
        float score;           // weighted success rate (0-1)
        int totalUses;         // how many times used
        int successfulUses;    // how many times contributed to a hit
        float recentHistory[32]; // ring buffer of recent outcomes
        int historyIdx;

        ModuleEffectiveness() : score(0.5f), totalUses(0), successfulUses(0), historyIdx(0) {
            for (int i = 0; i < 32; i++) recentHistory[i] = 0.5f;
        }

        void RecordShot(bool hit) {
            recentHistory[historyIdx] = hit ? 1.0f : 0.0f;
            historyIdx = (historyIdx + 1) % 32;
            totalUses++;
            if (hit) successfulUses++;
            // Exponential moving average
            float recentAvg = 0;
            for (int i = 0; i < 32; i++) recentAvg += recentHistory[i];
            recentAvg /= 32.0f;
            score = score * 0.7f + recentAvg * 0.3f;
        }

        float GetSuccessRate() const {
            return totalUses > 0 ? (float)successfulUses / totalUses : 0.5f;
        }
    };

    // Parameter set — auto-tuned per context
    struct LearnedParams {
        float fov = 180.0f;
        float smoothing = 1.0f;
        float errorRate = 0.08f;
        float hitchance = 35.0f;
        float predictionTime = 0.15f;
        float humanErrorAmp = 1.0f;
        float luckWeight = 0.5f;
        int resolverMode = 0;
        int hitboxPreference = 0;   // 0=head, 1=chest, 2=stomach
        int activeModules[MODULE_COUNT]; // 0=off, 1=on, -1=auto
    };

    // Shot record for learning
    struct ShotRecord {
        Context context;
        int activeModules[MODULE_COUNT];
        float params[8];     // fov, smoothing, errorRate, etc.
        bool hit;
        float timestamp;
        int targetIdx;
        float distance;
    };

    // ---- Core methods ----
    NeuralController();

    // Called every frame to encode context
    void UpdateContext(GameState* state, float dt);

    // Called before shot — returns which modules to use and their params
    void PrepareShot(LearnedParams& outParams, bool currentSettings[]);

    // Called after shot outcome — learns from result
    void RecordOutcome(bool hit, int targetIdx, float distance);

    // Get learned module effectiveness for display
    const ModuleEffectiveness& GetModuleStats(int moduleID) const { return modules[moduleID]; }

    // Get current context
    const Context& GetCurrentContext() const { return currentContext; }

    // Get total shots tracked
    int GetTotalShots() const { return totalShots; }
    int GetTotalHits() const { return totalHits; }
    float GetOverallAccuracy() const { return totalShots > 0 ? (float)totalHits / totalShots : 0; }

    // Get best performing module combo for current context
    float GetModuleWeight(int moduleID) const { return modules[moduleID].score; }

    // Should this module be used in current context?
    bool ShouldUseModule(int moduleID) const;

    // Auto-tune a parameter based on context
    float TuneParameter(int paramID, float currentValue) const;

    // Access params for display
    const LearnedParams& GetContextParams() const {
        return contextParams[currentContext.ToIndex() % (CONTEXT_BINS * 4 * 6 * 6 * 4)];
    }

    // Periodic weight update (call every few seconds)
    void UpdateModuleWeights();

private:
    Context currentContext;
    ModuleEffectiveness modules[MODULE_COUNT];
    LearnedParams contextParams[CONTEXT_BINS * 4 * 6 * 6 * 4]; // indexed by context
    ShotRecord history[HISTORY_SIZE];
    int historyCount = 0;
    int historyHead = 0;
    int totalShots = 0;
    int totalHits = 0;
    DWORD lastContextUpdate = 0;

    // Internal
    int WeaponToGroup(int weaponId) const;
    int DistanceToBin(float dist) const;
    int HealthToBin(int hp) const;
    void PropagateLearning(const ShotRecord& record);
};

// ==================== QUANTUM AIM — THE ULTIMATE 19-IN-1 ====================
class QuantumAim {
    MemoryReader* mem;
    Resolver* resolver;
    Autowall* autowall;
    OffsetDatabase offsets;

    // All subsystems
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
    SprayDNA sprayDNA;
    EventDispatcher events;
    AntiAim antiAim;
    BunnyHop bhop;
    NeuralController neural;
    BacktrackEngine backtrack;
    SmoothAim smoothAim;

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

    // Target hysteresis
    float lastTargetScore = 0;
    float targetLostTime = 0;
    bool hasPersistentTarget = false;

    // Rage mode
    float rageLastCrosshairTime = 0;

    // Noise
    float timeAccum = 0;
    float noiseAmplitude = 0.8f;
    float noiseFrequency = 2.5f;

    // Decision cache
    DecisionEngine::Decision currentDecision;

    // Neural shot tracking
    NeuralController::Context pendingShotContext;
    bool hasPendingShot = false;

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
        // Anti-Aim
        bool antiAim = false;
        bool fakeWalk = false;
        bool microDesync = false;
        int desyncSide = 0;         // 0=auto, 1=left, 2=right
        // Bhop
        bool bhop = false;
        int bhopHitchance = 80;
        bool bhopAutoStrafe = false;
        // Parameters
        float fov = 40.0f;
        float maxDist = 8192.0f;
        float smoothness = 1.0f;
        float minHitchance = 5.0f;
        float minDamage = 5.0f;
        int resolverMode = 0;
        bool silentAim = true;
        // Backtracking
        bool backtrackEnabled = true;
        float backtrackMaxTime = 0.2f;   // 200ms max lookback
        // Smooth Aim
        bool smoothAimEnabled = true;
        float smoothSpeed = 25.0f;
        float smoothMaxAngle = 36.0f;
        int smoothCurve = 3;
        bool smoothAdaptive = true;
        // Rage Mode
        bool rageMode = false;
        bool autoScope = true;
        bool autoStop = true;
        // Target Hysteresis
        float targetSwitchThreshold = 1.2f; // new target score must be 1.2x better to switch
        float targetLossTimeout = 0.15f;    // lose target after 150ms without seeing it
        // Weapon Profiles
        struct WeaponProfile {
            float fov;
            float smoothSpeed;
            float smoothMaxAngle;
            float minHitchance;
            float maxDist;
        };
        WeaponProfile rifleProfile   = { 40.0f, 25.0f, 36.0f, 5.0f, 8192.0f };
        WeaponProfile pistolProfile  = { 50.0f, 35.0f, 40.0f, 3.0f, 4096.0f  };
        WeaponProfile sniperProfile  = { 8.0f,  8.0f,  10.0f, 15.0f, 16384.0f };
        WeaponProfile smgProfile     = { 60.0f, 30.0f, 42.0f, 3.0f, 4096.0f  };
        WeaponProfile shotgunProfile = { 70.0f, 40.0f, 50.0f, 2.0f, 1024.0f  };
        bool useWeaponProfiles = true;
        // Neural (self-learning)
        bool neuralEnabled = true;
        bool neuralAutoTune = true;
        bool neuralAutoSelect = true;
    } settings;

    QuantumAim(MemoryReader* reader, Resolver* res, Autowall* aw);
    ~QuantumAim() = default;

    void SetOffsets(const OffsetDatabase& off) { offsets = off; }

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
    AntiAim* GetAntiAim() { return &antiAim; }
    BunnyHop* GetBunnyHop() { return &bhop; }
    NeuralController* GetNeural() { return &neural; }
    BacktrackEngine* GetBacktrack() { return &backtrack; }
    SmoothAim* GetSmoothAim() { return &smoothAim; }

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
