#include "quantum_aim.h"
#include "utils/logging.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

static constexpr float PI = 3.14159265f;
static std::mt19937 rng((unsigned int)time(nullptr));

// ==================== HUMAN ERROR (Perlin Noise) ====================

float HumanError::Perlin1D(float x) {
    // Simple 1D Perlin-like noise using sine interpolation
    float xi = floorf(x);
    float xf = x - xi;
    // Smooth interpolation (ease in-out)
    float t = xf * xf * (3.f - 2.f * xf);
    // Hash function for deterministic noise
    auto hash = [](float v) -> float {
        int iv = (int)v;
        iv = (iv << 13) ^ iv;
        return ((iv * (iv * iv * 15731 + 789221) + 1376312589) & 0x7fffffff) / (float)0x7fffffff;
    };
    float a = hash(xi);
    float b = hash(xi + 1.f);
    return a + (b - a) * t; // returns 0..1
}

void HumanError::Update(float dt) {
    state.noiseTimePitch += dt;
    state.noiseTimeYaw += dt * 1.3f; // Different frequency for yaw
}

float HumanError::CalculateErrorRate() {
    float base = 0.05f;
    float sessionMin = (GetTickCount() - state.sessionStart) / 60000.0f;
    state.fatigue = std::min(1.0f, sessionMin / 60.0f);
    base += state.fatigue * 0.10f;
    state.focus = std::max(0.3f, 1.0f - state.deaths * 0.1f);
    base += (1.0f - state.focus) * 0.15f;
    if (state.deaths > 3) state.mood = 0.3f;
    else if (state.kills > 3) state.mood = 0.9f;
    else state.mood = 0.7f;
    base += (1.0f - state.mood) * 0.05f;
    return std::max(0.02f, std::min(0.25f, base));
}

QAngle HumanError::ApplyError(QAngle angles) {
    float rate = CalculateErrorRate();
    if ((rand() % 1000) / 1000.0f < rate) {
        // Perlin noise error — smooth, organic, non-linear
        float pitchNoise = (SampleNoise(state.noiseTimePitch) - 0.5f) * 4.0f;
        float yawNoise = (SampleNoise(state.noiseTimeYaw) - 0.5f) * 4.0f;
        angles.pitch += pitchNoise;
        angles.yaw += yawNoise;
    }
    return angles;
}

// ==================== LUCK ENGINE ====================
float LuckEngine::CalculateLuck() {
    float base = 0.45f;
    // Markov-like: consecutive hits reduce luck, misses increase it
    if (state.consecutiveHits >= 3) base -= 0.15f * (state.consecutiveHits - 2);
    if (state.consecutiveMisses >= 3) base += 0.10f * (state.consecutiveMisses - 2);
    float timeSinceHit = (GetTickCount() - state.lastHitTime) / 1000.0f;
    if (timeSinceHit > 30) base += 0.05f;
    float random = ((rand() % 1000) / 1000.0f - 0.5f) * 0.4f;
    return std::max(0.1f, std::min(0.9f, base + random));
}

bool LuckEngine::RollDice(float chance) {
    float luck = CalculateLuck();
    float effective = chance * luck;
    bool hit = (rand() % 1000) / 1000.0f < effective;
    if (hit) OnHit(); else OnMiss();
    return hit;
}

// ==================== SPRAY DNA ====================

void SprayDNA::Generate(int weaponId, DWORD playerSeed) {
    seed = playerSeed + weaponId * 31337;
    // Unique pattern seeded per player
    srand(seed);
    pattern.horizontalDrift = ((rand() % 1000) / 1000.f - 0.5f) * 1.5f;
    pattern.verticalBias = ((rand() % 1000) / 1000.f) * 0.8f;
    pattern.jitter = 0.2f + ((rand() % 1000) / 1000.f) * 0.6f;
    pattern.recoveryRate = 0.02f + ((rand() % 1000) / 1000.f) * 0.04f;
}

QAngle SprayDNA::GetSprayOffset(int shotNumber, const QAngle& recoilAngle) {
    // Add unique DNA signature to spray
    float t = (float)shotNumber;
    float hDrift = pattern.horizontalDrift * sinf(t * 0.5f) * (1.f + pattern.jitter * sinf(t * 3.7f));
    float vBias = pattern.verticalBias * (1.f - expf(-t * pattern.recoveryRate));
    QAngle offset;
    offset.pitch = vBias + ((rand() % 1000) / 1000.f - 0.5f) * pattern.jitter;
    offset.yaw = hDrift + ((rand() % 1000) / 1000.f - 0.5f) * pattern.jitter;
    offset.roll = 0;
    return offset;
}

// ==================== DYNAMIC FOV ====================

float DynamicFOV::GetFOV(int weaponId, float distance, float baseFov) {
    float multiplier = 1.0f;

    // Weapon-based adjustment
    switch (weaponId) {
        case 9:  // AWP
        case 11: // G3SG1
        case 38: // SCAR-20
            multiplier = 0.3f; // Narrow FOV for snipers
            break;
        case 40: // SSG 08
            multiplier = 0.5f;
            break;
        case 7:  // AK-47
        case 16: // M4A4
        case 60: // M4A1-S
        case 8:  // AUG
        case 39: // SG 553
            multiplier = 1.0f; // Standard rifles
            break;
        case 30: // Tec-9
        case 4:  // Glock
        case 32: // P2000
        case 61: // USP
        case 36: // P250
        case 1:  // Deagle
        case 3:  // Five-SeveN
            multiplier = 0.8f; // pistols slightly narrow
            break;
        default:
            multiplier = 1.0f;
            break;
    }

    // Distance-based: wider FOV at close range, narrower at long range
    float distMult = 1.0f;
    if (distance < 500.f) distMult = 1.3f;      // Close range: wider
    else if (distance < 1500.f) distMult = 1.0f; // Mid range: normal
    else distMult = 0.7f;                         // Long range: narrow

    return baseFov * multiplier * distMult;
}

// ==================== EVENT DISPATCHER ====================

void EventDispatcher::Dispatch(Event evt, int playerId, int damage, int weapon) {
    EventData data;
    data.type = evt;
    data.playerId = playerId;
    data.timestamp = GetTickCount() / 1000.f;
    data.damage = damage;
    data.weapon = weapon;
    eventQueue.push_back(data);
    if (eventQueue.size() > 32) eventQueue.pop_front();
}

bool EventDispatcher::Poll(EventData& out) {
    if (eventQueue.empty()) return false;
    out = eventQueue.front();
    eventQueue.pop_front();
    return true;
}

void EventDispatcher::Clear() {
    eventQueue.clear();
}

// ==================== ANTI-AIM ====================

QAngle AntiAim::GetAntiAimAngles(QAngle viewAngle, float velocity, DWORD tickCount) {
    QAngle offset = {0, 0, 0};
    if (!settings.enabled) return offset;

    phase += 0.032f;

    if (settings.microDesync) {
        // Micro-desync: subtle yaw offset that cycles
        float desyncAmt = 35.0f; // max 35 degrees desync
        int side = GetDesyncSide(tickCount);
        offset.yaw = side * desyncAmt * (0.7f + 0.3f * sinf(phase * 1.5f));
    }

    if (velocity > 1.0f && !settings.fakeWalk) {
        // Moving: add subtle pitch jitter
        offset.pitch = sinf(phase * 2.0f) * 3.0f;
    }

    return offset;
}

int AntiAim::GetDesyncSide(DWORD tickCount) {
    if (settings.desyncSide == 1) return -1;
    if (settings.desyncSide == 2) return 1;

    // Auto: switch every ~400ms
    if (tickCount - lastSideSwitch > 400) {
        autoSide = -autoSide;
        lastSideSwitch = tickCount;
    }
    return autoSide;
}

bool AntiAim::ShouldSlowDown(DWORD tickCount) {
    if (!settings.fakeWalk) return false;
    // Fake walk: alternate between moving and stopping
    int cycle = (tickCount / 300) % 2; // every 300ms
    return cycle == 0;
}

// ==================== BUNNYHOP ====================

void BunnyHop::Update(bool onGround, QAngle viewAngle, Vector3 velocity) {
    if (onGround && !wasOnGround) {
        // Just landed
    }
    wasOnGround = onGround;

    // Auto-strafe: when in air, add yaw based on velocity direction
    if (!onGround && settings.autoStrafe) {
        float speed = velocity.Length2D();
        if (speed > 50) {
            float velYaw = atan2f(velocity.y, velocity.x) * 180.0f / 3.14159f;
            float angleDiff = viewAngle.yaw - velYaw;
            if (angleDiff > 180) angleDiff -= 360;
            if (angleDiff < -180) angleDiff += 360;
            strafeAngle = angleDiff * 0.02f;
        }
    }
}

bool BunnyHop::ShouldJump(DWORD tickCount) {
    if (!settings.enabled) return false;
    if (!wasOnGround) return false;

    // Hitchance check
    if ((rand() % 100) >= settings.hitchance) return false;

    // Cooldown: prevent jumping every tick
    if (tickCount - lastJumpTick < 150) return false;

    lastJumpTick = tickCount;
    return true;
}

float BunnyHop::GetStrafeAngle(QAngle viewAngle, Vector3 velocity, DWORD tickCount) {
    if (!settings.autoStrafe || wasOnGround) return 0.0f;
    return strafeAngle;
}

// ==================== MOMENTUM SHOT ====================

void MomentumShot::Update(Vector3 vel) {
    float speed = vel.Length2D();
    float prevSpeed = prevVelocity.Length2D();
    float speedChange = speed - prevSpeed;
    if (speedChange < -200 && speed > 100) {
        isCounterStrafing = true;
        counterProgress = 0.0f;
    }
    if (isCounterStrafing) {
        counterProgress += 0.1f;
        if (counterProgress >= 1.0f) isCounterStrafing = false;
    }
    prevVelocity = vel;
}

bool MomentumShot::ShouldShoot(Vector3 vel, float angleChange) {
    float speed = vel.Length2D();
    if (isCounterStrafing && counterProgress > 0.8f) return true;
    if (speed > 250 && speed < 280) return (rand() % 100) < 20;
    if (fabsf(angleChange) > 45) return (rand() % 100) < 15;
    return true;
}

// ==================== VELOCITY ENGINE ====================
Vector3 VelocityEngine::PredictPosition(Vector3 pos, Vector3 vel, float t) {
    float speed = vel.Length2D();
    if (speed > 250) return pos + vel * t;
    if (speed > 50) {
        Vector3 accel = (vel - prevVelocity) / 0.016f;
        prevVelocity = vel;
        return pos + vel * t + accel * 0.5f * t * t;
    }
    return pos + Vector3(((rand()%1000)/1000.0f-0.5f)*0.5f, ((rand()%1000)/1000.0f-0.5f)*0.5f, 0);
}

bool VelocityEngine::PredictCounterStrafe(Vector3 vel, float dt) {
    float decel = (prevVelocity.Length2D() - vel.Length2D()) / dt;
    prevVelocity = vel;
    return decel > 500;
}

// ==================== RECOIL FLOW ====================
QAngle RecoilFlow::GetNaturalRecoil(int weaponId, int shots, const QAngle& punch) {
    QAngle r;
    switch (weaponId) {
        case 7:  r.pitch = -shots * 0.12f; r.yaw = sinf(shots * 0.5f) * 0.3f; break;
        case 13: r.pitch = -shots * 0.10f; r.yaw = sinf(shots * 0.4f) * 0.25f; break;
        default: r.pitch = -shots * 0.10f; r.yaw = sinf(shots * 0.4f) * 0.2f; break;
    }
    float jitter = 0.3f + shots * 0.02f;
    r.pitch += ((rand()%1000)/1000.0f - 0.5f) * jitter;
    r.yaw += ((rand()%1000)/1000.0f - 0.5f) * jitter;
    if (shots > 10) r.pitch += (shots - 10) * 0.05f * ((rand()%1000)/1000.0f);
    return r;
}

// ==================== RICOCHET AIMBOT ====================
RicochetAimbot::Surface RicochetAimbot::FindSurface(Vector3 from, Vector3 to) {
    Vector3 mid = (from + to) * 0.5f;
    Vector3 n = (to - from).Normalized();
    return {mid, n, 100};
}

Vector3 RicochetAimbot::CalculateRicochetPoint(Vector3 shooter, Vector3 target, const Surface& surf) {
    Vector3 toTarget = target - shooter;
    float dot = toTarget.x * surf.normal.x + toTarget.y * surf.normal.y + toTarget.z * surf.normal.z;
    return target - Vector3(2*dot*surf.normal.x, 2*dot*surf.normal.y, 2*dot*surf.normal.z);
}

bool RicochetAimbot::Apply(Vector3 from, Vector3 to, QAngle& outAngle) {
    Surface surf = FindSurface(from, to);
    if (surf.distance > 1000) return false;
    Vector3 rp = CalculateRicochetPoint(from, to, surf);
    Vector3 d = rp - from;
    float hyp = sqrtf(d.x*d.x + d.y*d.y);
    outAngle.pitch = -atan2f(d.z, hyp) * (180/PI);
    outAngle.yaw = atan2f(d.y, d.x) * (180/PI);
    outAngle.pitch += ((rand()%1000)/1000.0f - 0.5f) * 0.5f;
    outAngle.yaw += ((rand()%1000)/1000.0f - 0.5f) * 0.5f;
    outAngle.Clamp();
    return true;
}

// ==================== BULLET TIME ====================
void BulletTime::Activate(float durationMs) {
    active = true;
    activeUntil = GetTickCount() + (DWORD)durationMs;
}

void BulletTime::Update() {
    if (!active) return;
    if (GetTickCount() > activeUntil) active = false;
}

// ==================== FAKE LAG ====================
bool FakeLag::ShouldSkipPacket() {
    if (chokeAmount <= 0) return false;
    chokeCounter = (chokeCounter + 1) % (chokeAmount + 1);
    return chokeCounter < chokeAmount;
}

// ==================== TIME DILATION ====================
void TimeDilation::Update(float dt) {
    phase += dt * 0.5f;
}

int TimeDilation::GetFakePingMs() {
    return (int)(20 + 130 * (sinf(phase) * 0.5f + 0.5f));
}

// ==================== INTERPOLATION EXPLOIT ====================
void InterpolationExploit::OnShot() {
    changeUntil = GetTickCount() + 50;
}

void InterpolationExploit::Update() {
    if (GetTickCount() < changeUntil) {
        // Set cl_interp_ratio to 0.1 during the exploit window
    }
}

// ==================== PREDICTIVE AIMBOT ====================
Vector3 PredictiveAimbot::PredictTarget(Vector3 pos, Vector3 vel, float t) {
    return velocity.PredictPosition(pos, vel, t);
}

// ==================== DECISION ENGINE (Enhanced FSM) ====================

void DecisionEngine::AssessThreats(Situation& sit, GameState* state, Vector3 localOrigin) {
    sit.threatCount = 0;
    for (int i = 0; i < 64 && sit.threatCount < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;

        ThreatInfo& t = sit.threats[sit.threatCount];
        t.playerIndex = i;
        t.distance = localOrigin.DistTo(p.origin);
        t.health = (float)p.health;

        // Angle to us: 0 = looking at us, 180 = looking away
        float yaw = p.viewAngle.yaw * 3.14159265f / 180.0f;
        Vector3 dir(sinf(yaw), cosf(yaw), 0);
        Vector3 toUs = localOrigin - p.origin;
        toUs.z = 0;
        float len = toUs.Length();
        if (len > 0.1f) toUs = toUs / len;
        float dot = dir.x * toUs.x + dir.y * toUs.y;
        t.angleToUs = acosf(std::max(-1.f, std::min(1.f, dot))) * 180.f / 3.14159265f;

        // Priority score: (1/dist) * (facing us factor) * (1/health)
        float facingFactor = (t.angleToUs < 45.f) ? 1.0f : (t.angleToUs < 90.f) ? 0.7f : 0.3f;
        float healthFactor = (100.f - t.health) / 100.f + 0.2f;
        float distFactor = 1.0f / (1.0f + t.distance / 1000.f);
        t.priorityScore = distFactor * facingFactor * healthFactor;

        sit.threatCount++;
    }
}

DecisionEngine::Decision DecisionEngine::Evaluate(const Situation& s) {
    Decision d;
    DWORD now = GetTickCount();

    // FSM state transitions
    if (s.health < 25 && s.enemiesAlive > 1) {
        if (currentMode != GODMODE) {
            currentMode = GODMODE;
            modeStartTime = now;
        }
    } else if (s.beingShotAt || (s.enemiesAlive > s.teammatesAlive + 1)) {
        if (currentMode != PANIC) {
            currentMode = PANIC;
            modeStartTime = now;
        }
    } else if (s.enemiesAlive > 0) {
        if (currentMode != ENGAGEMENT) {
            currentMode = ENGAGEMENT;
            modeStartTime = now;
        }
    } else {
        currentMode = IDLE;
    }

    // Mode-specific parameters
    switch (currentMode) {
        case IDLE:
            d.aggression = 0.3f;
            d.errorRate = 0.12f;
            d.luckWeight = 0.3f;
            d.networkWeight = 0.1f;
            d.fovMultiplier = 0.8f;
            d.smoothnessMultiplier = 1.5f;
            break;
        case ENGAGEMENT:
            d.aggression = 0.6f;
            d.errorRate = 0.06f;
            d.luckWeight = 0.5f;
            d.networkWeight = 0.4f;
            d.fovMultiplier = 1.0f;
            d.smoothnessMultiplier = 1.0f;
            break;
        case PANIC:
            d.aggression = 0.8f;
            d.errorRate = 0.03f;
            d.luckWeight = 0.7f;
            d.networkWeight = 0.7f;
            d.useFakeLag = true;
            d.useRicochet = true;
            d.fovMultiplier = 1.3f;
            d.smoothnessMultiplier = 0.6f;
            break;
        case GODMODE:
            d.aggression = 0.95f;
            d.errorRate = 0.01f;
            d.luckWeight = 0.9f;
            d.networkWeight = 0.9f;
            d.useWallPen = true;
            d.useFakeLag = true;
            d.useTimeDilation = true;
            d.useRicochet = true;
            d.fovMultiplier = 1.5f;
            d.smoothnessMultiplier = 0.3f;
            break;
    }

    d.mode = currentMode;

    // Adaptive error rate based on session performance
    if (sessionShots > 20) {
        float avgError = sessionErrorAccum / sessionShots;
        d.errorRate = d.errorRate * 0.7f + avgError * 0.3f;
    }

    // Ping-based network weight adjustment
    if (s.ping > 80) d.networkWeight += 0.2f;

    // Bomb plant urgency
    if (s.bombPlanted && s.roundTime < 30) {
        d.aggression = std::min(1.0f, d.aggression + 0.2f);
        d.errorRate *= 0.5f;
    }

    return d;
}

// ==================== QUANTUM AIM ====================
QuantumAim::QuantumAim(MemoryReader* reader, Resolver* res, Autowall* aw)
    : mem(reader), resolver(res), autowall(aw) {}

// ==================== NEURAL CONTROLLER — Self-Learning Brain ====================

NeuralController::NeuralController() {
    // Initialize context params with defaults
    for (int i = 0; i < CONTEXT_BINS * 4 * 6 * 6 * 4; i++) {
        contextParams[i].fov = 180.0f;
        contextParams[i].smoothing = 1.0f;
        contextParams[i].errorRate = 0.08f;
        contextParams[i].hitchance = 35.0f;
        contextParams[i].predictionTime = 0.15f;
        contextParams[i].humanErrorAmp = 1.0f;
        contextParams[i].luckWeight = 0.5f;
        contextParams[i].resolverMode = 0;
        contextParams[i].hitboxPreference = 0;
        for (int m = 0; m < MODULE_COUNT; m++)
            contextParams[i].activeModules[m] = -1; // -1 = auto
    }
}

int NeuralController::WeaponToGroup(int weaponId) const {
    // 0=pistol, 1=rifle, 2=sniper, 3=smg, 4=shotgun
    switch (weaponId) {
        case 1: case 3: case 4: case 30: case 32: case 36: case 61:
            return 0; // pistol
        case 7: case 8: case 10: case 13: case 16: case 39: case 60:
            return 1; // rifle
        case 9: case 11: case 38: case 40:
            return 2; // sniper
        case 17: case 19: case 23: case 24: case 26: case 33:
            return 3; // smg
        case 25: case 27: case 29: case 35:
            return 4; // shotgun
        default:
            return 1;
    }
}

int NeuralController::DistanceToBin(float dist) const {
    // 0=point blank, 1=close, 2=short, 3=mid, 4=mid-long, 5=long, 6=far, 7=extreme
    if (dist < 200) return 0;
    if (dist < 500) return 1;
    if (dist < 1000) return 2;
    if (dist < 1500) return 3;
    if (dist < 2500) return 4;
    if (dist < 3500) return 5;
    if (dist < 5000) return 6;
    return 7;
}

int NeuralController::HealthToBin(int hp) const {
    if (hp <= 25) return 0;
    if (hp <= 50) return 1;
    if (hp <= 75) return 2;
    return 3;
}

void NeuralController::UpdateContext(GameState* state, float dt) {
    DWORD now = GetTickCount();
    if (now - lastContextUpdate < 100) return; // update 10x/sec
    lastContextUpdate = now;

    auto* local = state->GetLocal();
    if (!local) return;

    auto enemies = state->GetEnemies();

    currentContext.weaponGroup = WeaponToGroup(local->weaponId);
    currentContext.healthBin = HealthToBin(local->health);
    currentContext.enemyCount = (int)enemies.size();
    currentContext.isMoving = local->velocity.Length2D() > 50;
    currentContext.isAirborne = false; // TODO: trace for ground
    currentContext.isScoped = local->scoped;

    float speed = local->velocity.Length2D();
    if (speed < 10) currentContext.movementState = 0;       // still
    else if (speed > 200) currentContext.movementState = 2;  // running
    else currentContext.movementState = 1;                    // strafing

    // Distance to nearest enemy
    float minDist = 9999;
    for (auto* e : enemies) {
        float d = local->origin.DistTo(e->origin);
        if (d < minDist) minDist = d;
    }
    currentContext.distanceBin = DistanceToBin(minDist);
}

bool NeuralController::ShouldUseModule(int moduleID) const {
    float w = modules[moduleID].score;
    // Module is useful if its success rate is above 45% (better than random)
    // and it's been used enough to be meaningful (at least 5 shots)
    if (modules[moduleID].totalUses < 5) return true; // not enough data, keep it on
    return w > 0.45f;
}

float NeuralController::TuneParameter(int paramID, float currentValue) const {
    int ctxIdx = currentContext.ToIndex() % (CONTEXT_BINS * 4 * 6 * 6 * 4);
    const LearnedParams& lp = contextParams[ctxIdx];

    // Blend learned value with current setting (70% learned, 30% user)
    switch (paramID) {
        case 0: return lp.fov * 0.7f + currentValue * 0.3f;
        case 1: return lp.smoothing * 0.7f + currentValue * 0.3f;
        case 2: return lp.errorRate * 0.7f + currentValue * 0.3f;
        case 3: return lp.hitchance * 0.7f + currentValue * 0.3f;
        case 4: return lp.predictionTime * 0.7f + currentValue * 0.3f;
        case 5: return lp.humanErrorAmp * 0.7f + currentValue * 0.3f;
        case 6: return lp.luckWeight * 0.7f + currentValue * 0.3f;
        default: return currentValue;
    }
}

void NeuralController::PrepareShot(LearnedParams& outParams, bool currentSettings[]) {
    int ctxIdx = currentContext.ToIndex() % (CONTEXT_BINS * 4 * 6 * 6 * 4);
    LearnedParams& lp = contextParams[ctxIdx];

    // Start with learned params
    outParams = lp;

    // Auto-select modules based on effectiveness
    for (int m = 0; m < MODULE_COUNT; m++) {
        if (lp.activeModules[m] == -1) {
            // Auto mode: use module if effective
            outParams.activeModules[m] = ShouldUseModule(m) ? 1 : 0;
        } else {
            outParams.activeModules[m] = lp.activeModules[m];
        }
    }
}

void NeuralController::RecordOutcome(bool hit, int targetIdx, float distance) {
    totalShots++;
    if (hit) totalHits++;

    // Record in history
    ShotRecord& rec = history[historyHead];
    rec.context = currentContext;
    rec.hit = hit;
    rec.timestamp = GetTickCount() / 1000.0f;
    rec.targetIdx = targetIdx;
    rec.distance = distance;

    // Record which modules were active
    for (int m = 0; m < MODULE_COUNT; m++) {
        rec.activeModules[m] = modules[m].totalUses > 0 ? 1 : 0;
    }

    historyHead = (historyHead + 1) % HISTORY_SIZE;
    if (historyCount < HISTORY_SIZE) historyCount++;

    // Update per-module effectiveness
    for (int m = 0; m < MODULE_COUNT; m++) {
        if (rec.activeModules[m]) {
            modules[m].RecordShot(hit);
        }
    }

    // Propagate learning to context params
    PropagateLearning(rec);
}

void NeuralController::PropagateLearning(const ShotRecord& record) {
    int ctxIdx = record.context.ToIndex() % (CONTEXT_BINS * 4 * 6 * 6 * 4);
    LearnedParams& lp = contextParams[ctxIdx];

    float reward = record.hit ? 1.0f : -0.3f;
    float lr = 0.05f; // learning rate

    // Adapt parameters based on outcome
    if (record.hit) {
        // Reinforce current params
        lp.fov = lp.fov * (1 - lr) + lp.fov * lr; // keep same
        lp.hitchance = lp.hitchance * (1 - lr * 0.5f); // slightly lower bar if hitting
    } else {
        // Miss: adjust params
        if (record.distance > 3000) {
            lp.fov = std::min(180.0f, lp.fov + lr * 10); // widen FOV at long range
        }
        lp.hitchance = std::min(100.0f, lp.hitchance + lr * 5); // raise bar
        lp.errorRate = std::max(0.01f, lp.errorRate - lr * 0.02f); // less error
    }

    // Distance-based hitbox learning
    if (record.distance < 500) {
        lp.hitboxPreference = 0; // close range: head
    } else if (record.distance < 2000) {
        lp.hitboxPreference = record.hit ? 0 : 1; // adapt
    } else {
        lp.hitboxPreference = 1; // long range: chest (bigger target)
    }

    // Weapon group learning
    int wg = record.context.weaponGroup;
    if (wg == 2) { // sniper
        lp.hitchance = std::max(60.0f, lp.hitchance); // snipers need high HC
        lp.predictionTime = 0.1f; // less prediction for scoped
    } else if (wg == 3) { // smg
        lp.predictionTime = 0.2f; // more prediction for fast weapons
    }

    // Clamp all values
    lp.fov = std::max(10.0f, std::min(180.0f, lp.fov));
    lp.smoothing = std::max(0.3f, std::min(5.0f, lp.smoothing));
    lp.errorRate = std::max(0.01f, std::min(0.25f, lp.errorRate));
    lp.hitchance = std::max(10.0f, std::min(100.0f, lp.hitchance));
    lp.predictionTime = std::max(0.05f, std::min(0.4f, lp.predictionTime));
    lp.humanErrorAmp = std::max(0.1f, std::min(3.0f, lp.humanErrorAmp));
    lp.luckWeight = std::max(0.1f, std::min(1.0f, lp.luckWeight));
}

void NeuralController::UpdateModuleWeights() {
    // This is called periodically to rebalance module weights
    // based on recent performance across all contexts
    for (int m = 0; m < MODULE_COUNT; m++) {
        if (modules[m].totalUses < 10) continue; // not enough data

        // Decay old history
        float decay = 0.98f;
        modules[m].score *= decay;

        // Bonus for recently successful modules
        float recentSuccess = 0;
        int recentCount = std::min(32, modules[m].totalUses);
        for (int i = 0; i < 32; i++) {
            recentSuccess += modules[m].recentHistory[i];
        }
        recentSuccess /= 32.0f;

        // Blend
        modules[m].score = modules[m].score * 0.8f + recentSuccess * 0.2f;
    }
}

void QuantumAim::Update(GameState* state, float dt) {
    if (!mem || !state || !settings.enabled) { hasTarget = false; return; }
    auto* local = state->GetLocal();
    if (!local || !local->IsValid()) { hasTarget = false; return; }

    currentWeaponId = local->weaponId;
    timeAccum += dt;
    shotFired = false;
    auto enemies = state->GetEnemies();

    // ── BACKTRACK: Record tick data for lag compensation ──
    if (settings.backtrackEnabled) {
        backtrack.RecordTick(state->players, 64, state->localTeam);
    }

    static int logFrame = 0;
    logFrame++;

    if (enemies.empty()) { hasTarget = false; currentTarget = -1; return; }

    // ── NEURAL CONTROLLER: Update context ──
    if (settings.neuralEnabled) {
        neural.UpdateContext(state, dt);
    }

    // ── HIT DETECTION: Track health deltas for real feedback ──
    static int prevHealth[64] = {};
    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;
        if (prevHealth[i] == 0) prevHealth[i] = p.health;
        int delta = prevHealth[i] - p.health;
        if (delta > 0 && delta < 100) {
            neural.RecordOutcome(true, i, local->origin.DistTo(p.origin));
            human.OnKill();
        }
        prevHealth[i] = p.health;
    }

    // Decision Engine
    if (settings.decisionEngine) {
        DecisionEngine::Situation sit;
        sit.enemiesAlive = (int)enemies.size();
        sit.teammatesAlive = state->playerCount - sit.enemiesAlive;
        sit.health = (float)local->health;
        sit.ping = (float)state->ping;
        sit.roundTime = state->roundTime;
        sit.beingShotAt = false;
        sit.bombPlanted = state->bombPlanted;
        sit.distance = enemies[0] ? local->origin.DistTo(enemies[0]->origin) : 0;
        sit.localWeapon = local->weaponId;
        decision.AssessThreats(sit, state, local->origin);
        currentDecision = decision.Evaluate(sit);
    }

    // ── NEURAL: Prepare shot ──
    NeuralController::LearnedParams neuralParams;
    bool moduleActivation[NeuralController::MODULE_COUNT];
    if (settings.neuralEnabled && settings.neuralAutoSelect) {
        bool baseSettings[NeuralController::MODULE_COUNT] = {
            settings.rcs, settings.predictive, settings.humanError,
            settings.luckEngine, settings.momentumShot, settings.velocityEngine,
            settings.recoilFlow, settings.ricochet, settings.bulletTime,
            settings.fakeLag, settings.timeDilation, settings.interpExploit,
            (bool)settings.resolverMode, settings.antiAim, settings.bhop, false
        };
        neural.PrepareShot(neuralParams, baseSettings);
        for (int m = 0; m < NeuralController::MODULE_COUNT; m++)
            moduleActivation[m] = neuralParams.activeModules[m] != 0;
    } else {
        for (int m = 0; m < NeuralController::MODULE_COUNT; m++)
            moduleActivation[m] = true;
        neuralParams.fov = settings.fov;
        neuralParams.hitchance = settings.minHitchance;
        neuralParams.predictionTime = 0.15f;
        neuralParams.errorRate = 0.08f;
        neuralParams.humanErrorAmp = 0.0f;
    }

    // Human Error
    if (moduleActivation[NeuralController::MOD_HUMAN_ERROR]) human.Update(dt);
    if (moduleActivation[NeuralController::MOD_MOMENTUM]) momentum.Update(local->velocity);
    if (moduleActivation[NeuralController::MOD_TIME_DILATION]) timeDilation.Update(dt);
    if (moduleActivation[NeuralController::MOD_FAKE_LAG] && currentDecision.useFakeLag)
        fakeLag.SetChoke(5 + rand() % 10);
    if (moduleActivation[NeuralController::MOD_BULLET_TIME]) bulletTime.Update();
    if (moduleActivation[NeuralController::MOD_INTERP]) interpExploit.Update();

    if (settings.antiAim) {
        antiAim.settings.enabled = true;
        antiAim.settings.microDesync = settings.microDesync;
        antiAim.settings.desyncSide = settings.desyncSide;
        antiAim.settings.fakeWalk = settings.fakeWalk;
    } else antiAim.settings.enabled = false;

    if (settings.bhop) {
        bhop.settings.enabled = true;
        bhop.settings.hitchance = settings.bhopHitchance;
        bhop.settings.autoStrafe = settings.bhopAutoStrafe;
    } else bhop.settings.enabled = false;

    // ── WEAPON PROFILES: Apply weapon-specific tuning ──
    float effFov = settings.fov;
    float effSmoothSpeed = settings.smoothSpeed;
    float effSmoothMaxAngle = settings.smoothMaxAngle;
    float effMinHC = settings.minHitchance;
    float effMaxDist = settings.maxDist;
    if (settings.useWeaponProfiles) {
        Settings::WeaponProfile* prof = nullptr;
        int wid = local->weaponId;
        // Snipers
        if (wid == 9 || wid == 11 || wid == 38 || wid == 40)
            prof = &settings.sniperProfile;
        // SMGs
        else if (wid == 17 || wid == 19 || wid == 23 || wid == 24 || wid == 26 || wid == 33 || wid == 34)
            prof = &settings.smgProfile;
        // Shotguns
        else if (wid == 25 || wid == 27 || wid == 29 || wid == 35)
            prof = &settings.shotgunProfile;
        // Pistols
        else if (wid == 1 || wid == 2 || wid == 3 || wid == 4 || wid == 30 || wid == 32 ||
                 wid == 36 || wid == 61 || wid == 63 || wid == 64)
            prof = &settings.pistolProfile;
        // Rifles (AK, M4, AUG, SG, FAMAS, Galil)
        else if (wid == 7 || wid == 8 || wid == 10 || wid == 13 || wid == 16 || wid == 39 || wid == 60)
            prof = &settings.rifleProfile;

        if (prof) {
            effFov = prof->fov;
            effSmoothSpeed = prof->smoothSpeed;
            effSmoothMaxAngle = prof->smoothMaxAngle;
            effMinHC = prof->minHitchance;
            effMaxDist = prof->maxDist;
        }
    }

    float effectiveFOV = effFov;
    if (settings.neuralEnabled && settings.neuralAutoTune)
        effectiveFOV = neural.TuneParameter(0, effFov);

    // Select best target
    QAngle aimAngle;
    float hitchance, damage;
    int target = SelectBestTarget(state, aimAngle, hitchance, damage);

    if (target < 0) { hasTarget = false; currentTarget = -1; targetLostTime = 0; return; }

    // Only filter by neural hitchance if it's meaningfully higher than base setting
    float effectiveMinHC = effMinHC;
    if (settings.neuralEnabled && neuralParams.hitchance > effMinHC + 10.0f)
        effectiveMinHC = neuralParams.hitchance;
    if (hitchance < effectiveMinHC) { hasTarget = false; currentTarget = -1; return; }

    // ── TARGET HYSTERESIS: Prevent flip-flopping between targets ──
    float now2 = GetTickCount() / 1000.0f;
    if (target != currentTarget && currentTarget >= 0) {
        // Calculate score for the new candidate
        float newScore = 0;
        float newDist = local->origin.DistTo(state->players[target].origin);
        newScore += (180.0f - GetFov(local->viewAngle, aimAngle)) * 2.0f;
        newScore += hitchance * 0.5f;
        newScore -= newDist * 0.01f;

        // Only switch if new target is significantly better
        if (newScore < lastTargetScore * settings.targetSwitchThreshold) {
            // Keep the old target — new one isn't good enough to justify switch
            target = currentTarget;
            // Recalculate aim angle for the kept target
            auto& kept = state->players[target];
            Vector3 keptHB = GetHitboxPos(state, target, HEAD);
            aimAngle = CalcAngle(local->origin + local->viewOffset, keptHB);
            hitchance = CalcHitchance(local->origin + local->viewOffset, keptHB,
                                       GetWeaponSpread(kept.weaponId), newDist);
        }
    }
    lastTargetScore = 0;
    {
        float td = local->origin.DistTo(state->players[target].origin);
        lastTargetScore += (180.0f - GetFov(local->viewAngle, aimAngle)) * 2.0f;
        lastTargetScore += hitchance * 0.5f;
        lastTargetScore -= td * 0.01f;
    }

    currentTarget = target;
    currentHitchance = hitchance;

    // Ricochet
    if (moduleActivation[NeuralController::MOD_RICOCHET] && currentDecision.useRicochet && !enemies.empty()) {
        QAngle ricAngle;
        Vector3 eye = local->origin; eye.z += 64;
        if (ricochet.Apply(eye, enemies[0]->origin, ricAngle)) {
            float ricFov = GetFov(local->viewAngle, ricAngle);
            if (ricFov < effectiveFOV) aimAngle = ricAngle;
        }
    }

    // Resolver
    if (resolver && settings.resolverMode > 0 && target >= 0)
        aimAngle = resolver->ResolveAngle(&state->players[target], aimAngle, settings.resolverMode, target);

    // Prediction
    if (moduleActivation[NeuralController::MOD_PREDICTIVE] && target >= 0) {
        Vector3 predPos = predictive.PredictTarget(
            state->players[target].origin,
            state->players[target].velocity,
            neuralParams.predictionTime);
        aimAngle = CalcAngle(local->origin + local->viewOffset, predPos);
    }

    // Human Error
    if (moduleActivation[NeuralController::MOD_HUMAN_ERROR]) {
        float amp = neuralParams.humanErrorAmp;
        QAngle err = human.ApplyError(aimAngle);
        aimAngle.pitch = aimAngle.pitch + (err.pitch - aimAngle.pitch) * amp;
        aimAngle.yaw = aimAngle.yaw + (err.yaw - aimAngle.yaw) * amp;
    }

    // ── SMOOTH AIM (using weapon profile values) ──
    if (settings.smoothAimEnabled) {
        smoothAim.settings.enabled = true;
        smoothAim.settings.speed = effSmoothSpeed;
        smoothAim.settings.maxAnglePerFrame = effSmoothMaxAngle;
        smoothAim.settings.curve = (SmoothAim::CurveType)settings.smoothCurve;
        smoothAim.settings.adaptiveSpeed = settings.smoothAdaptive;
        aimAngle = smoothAim.Smooth(local->viewAngle, aimAngle, dt);
    }

    finalAimAngle = aimAngle;
    finalAimAngle.Clamp();
    hasTarget = true;

    // ── AUTO-SCOPE: Scope in when target is in FOV (snipers only) ──
    if (settings.autoScope && target >= 0) {
        bool isSniper = (currentWeaponId == 9 || currentWeaponId == 11 ||
                         currentWeaponId == 38 || currentWeaponId == 40);
        if (isSniper && !local->scoped) {
            float fovToTarget = GetFov(local->viewAngle, aimAngle);
            if (fovToTarget < effFov * 2.0f) {
                // Press secondary attack (zoom) via CUserCmd
                uintptr_t client = mem->GetClient();
                if (client) {
                    uintptr_t inputSys = mem->Read<uintptr_t>(client + offsets.dwInputSystem);
                    if (inputSys) {
                        uintptr_t cmds = mem->Read<uintptr_t>(inputSys + offsets.m_pCommands);
                        int cmdNum = mem->Read<int>(inputSys + offsets.m_nCmdCount);
                        if (cmds) {
                            uintptr_t cmd = cmds + (cmdNum % 150) * (int)offsets.m_cmdSize;
                            int buttons = mem->Read<int>(cmd + offsets.m_nButtons);
                            buttons |= (1 << 1); // IN_ATTACK2 (zoom)
                            mem->Write<int>(cmd + offsets.m_nButtons, buttons);
                        }
                    }
                }
            }
        }
    }

    // ── FIRE ──
    float now = GetTickCount() / 1000.0f;
    float fireRate = GetFireRate(currentWeaponId);
    bool canFire = hasTarget && (now - lastShotTime >= fireRate);

    // Rage mode: fire as soon as crosshair passes over enemy (lower hitchance threshold)
    if (settings.rageMode && hasTarget) {
        float fovToTarget = GetFov(local->viewAngle, finalAimAngle);
        if (fovToTarget < 5.0f) canFire = true; // very close to target = fire
    }

    if (canFire) {
        shotFired = true;
        shotsFired++;

        if (moduleActivation[NeuralController::MOD_RCS]) {
            aimAngle.pitch -= local->aimPunch.x * 0.75f;
            aimAngle.yaw -= local->aimPunch.y * 0.75f;
            aimAngle.Clamp();
        }
        if (moduleActivation[NeuralController::MOD_RECOIL_FLOW]) {
            QAngle punchQA(local->aimPunch.x, local->aimPunch.y, 0);
            aimAngle = aimAngle + recoil.GetNaturalRecoil(local->weaponId, shotsFired, punchQA);
            aimAngle.Clamp();
        }

        finalAimAngle = aimAngle;
        lastShotTime = now;

        if (moduleActivation[NeuralController::MOD_INTERP]) interpExploit.OnShot();
        if (moduleActivation[NeuralController::MOD_BULLET_TIME]) bulletTime.Activate(100);

        ExecuteShot(state);

        static int totalShotsLogged = 0;
        totalShotsLogged++;
        if (totalShotsLogged <= 5 || totalShotsLogged % 200 == 0) {
            LogMessage("[AIM] SHOT #" + std::to_string(totalShotsLogged) +
                " t=" + std::to_string(target) +
                " hc=" + std::to_string((int)hitchance) +
                " aim=(" + std::to_string(finalAimAngle.pitch) + "," + std::to_string(finalAimAngle.yaw) + ")" +
                " pawn=" + std::to_string(local->pawnAddr));
        }
    }

    static DWORD lastWeightUpdate = 0;
    if (GetTickCount() - lastWeightUpdate > 5000) {
        neural.UpdateModuleWeights();
        lastWeightUpdate = GetTickCount();
    }
}

bool QuantumAim::ShouldFire() const {
    if (!hasTarget || currentTarget < 0) return false;
    if (currentHitchance < settings.minHitchance) return false;
    float now = GetTickCount() / 1000.0f;
    if (now - lastShotTime < GetFireRate(currentWeaponId)) return false;
    return true;
}

int QuantumAim::SelectBestTarget(GameState* state, QAngle& aimAngle, float& hitchance, float& damage) {
    auto* local = state->GetLocal();
    if (!local) return -1;

    int bestIdx = -1;
    float bestScore = -999;
    Vector3 eye = local->origin + local->viewOffset;

    auto enemies = state->GetEnemies();

    static int debugCounter = 0;
    debugCounter++;
    bool logThis = (debugCounter % 120 == 0);
    if (logThis) LogMessage("[AIM] SelectBest: enemies=" + std::to_string(enemies.size()) +
        " fov=" + std::to_string(settings.fov) + " weapon=" + std::to_string(local->weaponId));

    for (auto* enemy : enemies) {
        int idx = (int)(enemy - state->players);
        float dist = local->origin.DistTo(enemy->origin);
        float adjustedFov = DynamicFOV::GetFOV(local->weaponId, dist, settings.fov * currentDecision.fovMultiplier);

        if (dist > settings.maxDist) continue;

        // Distance-based hitbox priority
        Hitbox hitboxOrder[4];
        if (dist < 500) {
            hitboxOrder[0] = HEAD; hitboxOrder[1] = NECK; hitboxOrder[2] = CHEST; hitboxOrder[3] = STOMACH;
        } else if (dist < 1500) {
            hitboxOrder[0] = CHEST; hitboxOrder[1] = HEAD; hitboxOrder[2] = STOMACH; hitboxOrder[3] = PELVIS;
        } else if (dist < 3000) {
            hitboxOrder[0] = STOMACH; hitboxOrder[1] = CHEST; hitboxOrder[2] = HEAD; hitboxOrder[3] = PELVIS;
        } else {
            hitboxOrder[0] = CHEST; hitboxOrder[1] = STOMACH; hitboxOrder[2] = PELVIS; hitboxOrder[3] = HEAD;
        }

        for (auto hb : hitboxOrder) {
            // Try backtracked position first for better hitbox accuracy
            Vector3 hbPos;
            if (settings.backtrackEnabled) {
                BacktrackEngine::TickRecord bestRec;
                if (backtrack.GetBestRecord(*enemy, idx, bestRec)) {
                    hbPos = backtrack.GetHitboxPosFromRecord(bestRec, (int)hb);
                } else {
                    hbPos = GetHitboxPos(state, idx, hb);
                }
            } else {
                hbPos = GetHitboxPos(state, idx, hb);
            }
            QAngle angle = CalcAngle(eye, hbPos);
            float fov = GetFov(local->viewAngle, angle);
            if (fov > adjustedFov) continue;

            float spread = GetWeaponSpread(enemy->weaponId);
            float hc = CalcHitchance(eye, hbPos, spread, dist);
            if (hc < settings.minHitchance) continue;

            float score = 0;
            score += (180 - fov) * 2.0f;
            score += hc * 0.5f;
            score += (100 - enemy->health) * 0.5f;
            score -= dist * 0.01f;
            if (enemy->flashed) score += 15.0f;
            if (enemy->scoped) score += 5.0f;
            float hbWeight = (hb == HEAD) ? 10.0f : (hb == CHEST) ? 6.0f : (hb == STOMACH) ? 4.0f : 2.0f;
            score += hbWeight;

            if (score > bestScore) {
                bestScore = score;
                bestIdx = idx;
                aimAngle = angle;
                hitchance = hc;
                damage = 100;
                targetHitbox = hb;
            }
        }
    }
    if (logThis && bestIdx >= 0)
        LogMessage("[AIM] Best target idx=" + std::to_string(bestIdx) + " score=" + std::to_string(bestScore));
    else if (logThis)
        LogMessage("[AIM] No target found");
    return bestIdx;
}

Vector3 QuantumAim::GetHitboxPos(GameState* state, int idx, Hitbox hb) {
    auto& p = state->players[idx];
    switch (hb) {
        case HEAD:   return p.bonePos[6];    // head bone
        case NECK:   return p.bonePos[5];    // neck
        case CHEST:  return p.bonePos[4];    // spine upper
        case STOMACH:return p.bonePos[3];    // spine mid
        case PELVIS: return p.bonePos[0];    // pelvis
        case ARM_L:  return p.bonePos[9];    // left elbow
        case ARM_R:  return p.bonePos[14];   // right elbow
        case LEG_L:  return p.bonePos[23];   // left knee
        case LEG_R:  return p.bonePos[26];   // right knee
        default:     return p.bonePos[4];    // chest fallback
    }
}

QAngle QuantumAim::CalcAngle(Vector3 from, Vector3 to) {
    Vector3 d = to - from;
    float hyp = sqrtf(d.x*d.x + d.y*d.y);
    QAngle a;
    a.pitch = -atan2f(d.z, hyp) * (180/PI);
    a.yaw = atan2f(d.y, d.x) * (180/PI);
    a.Clamp();
    return a;
}

float QuantumAim::GetFov(QAngle cur, QAngle target) {
    QAngle d = cur - target;
    d.Clamp();
    return sqrtf(d.pitch*d.pitch + d.yaw*d.yaw);
}

float QuantumAim::CalcHitchance(Vector3 from, Vector3 to, float spread, float dist) {
    Vector3 delta = to - from;
    float d = delta.Length();
    if (d < 1.0f) d = 1.0f;

    // Adaptive hitbox radius based on distance
    float hitboxRadius = 36.0f;
    if (d < 500) hitboxRadius = 20.0f;      // close range — head is smaller target
    else if (d < 1500) hitboxRadius = 30.0f;
    else hitboxRadius = 40.0f;               // long range — stomach/chest

    float hitboxAngle = atanf(hitboxRadius / d) * 180.0f / PI;
    float spreadAngle = spread * 180.0f / PI * (1.0f + dist / 10000.0f);
    if (spreadAngle < 0.01f) spreadAngle = 0.01f;

    float hc = (hitboxAngle / spreadAngle) * 100.0f;
    if (hc > 100.0f) hc = 100.0f;
    if (hc < 0.0f) hc = 0.0f;
    return hc;
}

float QuantumAim::GetWeaponSpread(int weaponId) {
    switch (weaponId) {
        case 1: return 0.12f; case 7: return 0.20f; case 8: return 0.16f;
        case 9: return 0.10f; case 11: return 0.06f; case 13: return 0.16f;
        case 14: return 0.20f; case 16: return 0.15f; case 17: return 0.18f;
        default: return 0.18f;
    }
}

float QuantumAim::GetFireRate(int weaponId) {
    switch (weaponId) {
        case 1: return 0.22f; case 7: return 0.10f; case 8: return 0.09f;
        case 9: return 1.50f; case 11: return 1.25f; case 13: return 0.09f;
        default: return 0.10f;
    }
}

void QuantumAim::ExecuteShot(GameState* state) {
    if (!mem || !state) return;
    auto* local = state->GetLocal();
    if (!local) return;

    uintptr_t client = mem->GetClient();
    if (!client) return;

    // ── METHOD 1: Write to player pawn m_angEyeAngles ──
    if (local->pawnAddr) {
        mem->Write<float>(local->pawnAddr + offsets.m_angEyeAngles, finalAimAngle.pitch);
        mem->Write<float>(local->pawnAddr + offsets.m_angEyeAngles + 4, finalAimAngle.yaw);
    }

    // ── METHOD 2: Write to CUserCmd (client-side input) ──
    uintptr_t inputSys = mem->Read<uintptr_t>(client + offsets.dwInputSystem);
    if (!inputSys) return;

    uintptr_t cmds = mem->Read<uintptr_t>(inputSys + offsets.m_pCommands);
    if (!cmds) return;

    int cmdNum = mem->Read<int>(inputSys + offsets.m_nCmdCount);
    int cmdSize = (int)offsets.m_cmdSize;

    // Write to BOTH current and next command for maximum reliability
    for (int offset = 0; offset <= 1; offset++) {
        uintptr_t cmd = cmds + ((cmdNum + offset) % 150) * cmdSize;
        mem->Write<float>(cmd + offsets.m_viewangles, finalAimAngle.pitch);
        mem->Write<float>(cmd + offsets.m_viewangles + 4, finalAimAngle.yaw);
        int buttons = mem->Read<int>(cmd + offsets.m_nButtons);
        buttons |= 1; // IN_ATTACK
        mem->Write<int>(cmd + offsets.m_nButtons, buttons);
        mem->Write<int>(cmd + offsets.m_nCommandNumber, (int)(cmdNum + offset));

        // Auto-stop: zero movement on firing tick for accuracy
        mem->Write<float>(cmd + offsets.m_flForwardMove, 0.0f);
        mem->Write<float>(cmd + offsets.m_flSideMove, 0.0f);
    }

    // Write subtick attack state
    uintptr_t cmd = cmds + (cmdNum % 150) * cmdSize;
    mem->Write<int>(cmd + offsets.m_subtickAttack, 1);
}

// ==================== BACKTRACK ENGINE — Lag Compensation ====================

void BacktrackEngine::RecordTick(Player* players, int playerCount, int localTeam) {
    tickCount++;
    for (int i = 0; i < playerCount && i < MAX_PLAYERS; i++) {
        auto& p = players[i];
        if (!p.IsValid() || !p.IsEnemy(localTeam)) continue;

        int& wi = writeIndex[i];
        TickRecord& rec = records[i][wi % MAX_RECORDS];
        rec.origin = p.origin;
        for (int b = 0; b < 30; b++) rec.bonePos[b] = p.bonePos[b];
        rec.viewAngle = p.viewAngle;
        rec.simulationTime = p.simulationTime;
        rec.duckAmount = p.duckAmount;
        rec.flags = p.flags;
        rec.valid = true;
        wi++;
    }
}

bool BacktrackEngine::GetBestRecord(Player& player, int playerIndex, TickRecord& outRecord) {
    if (playerIndex < 0 || playerIndex >= MAX_PLAYERS) return false;

    int wi = writeIndex[playerIndex];
    if (wi == 0) return false;

    float bestSimTime = -1;
    bool found = false;

    // Find the most recent valid record within max backtrack time
    for (int i = wi - 1; i >= std::max(0, wi - MAX_RECORDS); i--) {
        TickRecord& rec = records[playerIndex][i % MAX_RECORDS];
        if (!rec.valid) continue;

        float delta = player.simulationTime - rec.simulationTime;
        if (delta < 0) delta = -delta;
        if (delta > MAX_BACKTRACK_TIME) continue;

        if (rec.simulationTime > bestSimTime) {
            bestSimTime = rec.simulationTime;
            outRecord = rec;
            found = true;
        }
    }
    return found;
}

Vector3 BacktrackEngine::GetHitboxPosFromRecord(const TickRecord& rec, int hitbox) {
    if (hitbox >= 0 && hitbox < 30) return rec.bonePos[hitbox];
    return rec.origin + Vector3(0, 0, 50);
}

// ==================== SMOOTH AIM — Easing Curves ====================

float SmoothAim::ApplyCurve(float t, CurveType type) const {
    t = std::max(0.0f, std::min(1.0f, t));
    switch (type) {
        case CURVE_LINEAR: return t;
        case CURVE_EASE_IN: return t * t;
        case CURVE_EASE_OUT: return 1.0f - (1.0f - t) * (1.0f - t);
        case CURVE_EASE_IN_OUT:
            return t < 0.5f ? 2.0f * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
        case CURVE_CIRCLE: return sqrtf(1.0f - (t - 1.0f) * (t - 1.0f));
        case CURVE_EXPONENTIAL: return 1.0f - expf(-3.0f * t);
        default: return t;
    }
}

QAngle SmoothAim::Smooth(QAngle current, QAngle target, float dt) {
    if (!settings.enabled) return target;

    QAngle delta = target - current;
    delta.Clamp();

    float angleDiff = sqrtf(delta.pitch * delta.pitch + delta.yaw * delta.yaw);
    if (angleDiff < 0.1f) return target;

    // Adaptive speed: faster for bigger angle diffs
    float effectiveSpeed = settings.speed;
    if (settings.adaptiveSpeed) {
        effectiveSpeed *= (1.0f + angleDiff / 90.0f);
    }

    // Calculate progress (0..1 based on remaining angle)
    if (firstFrame) {
        startAngle = current;
        lastTarget = target;
        progress = 0.0f;
        firstFrame = false;
    }

    // Reset if target changed significantly
    float targetDelta = sqrtf(
        (target.pitch - lastTarget.pitch) * (target.pitch - lastTarget.pitch) +
        (target.yaw - lastTarget.yaw) * (target.yaw - lastTarget.yaw));
    if (targetDelta > 45.0f) {
        startAngle = current;
        progress = 0.0f;
    }
    lastTarget = target;

    // Advance progress based on angle difference and speed
    float step = effectiveSpeed * dt * 60.0f; // normalize to 60fps
    if (angleDiff < 5.0f) step *= angleDiff / 5.0f; // slow down near target
    progress += step / angleDiff;
    if (progress > 1.0f) progress = 1.0f;

    // Apply easing curve
    float easedProgress = ApplyCurve(progress, settings.curve);

    // Interpolate from start to target
    QAngle result;
    result.pitch = startAngle.pitch + delta.pitch * easedProgress;
    result.yaw = startAngle.yaw + delta.yaw * easedProgress;
    result.Clamp();

    return result;
}
