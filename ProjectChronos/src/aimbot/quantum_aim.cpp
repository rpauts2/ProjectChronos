#include "quantum_aim.h"
#include "utils/logging.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

static constexpr float PI = 3.14159265f;
static std::mt19937 rng((unsigned int)time(nullptr));

// ==================== LUCK ENGINE ====================
float LuckEngine::CalculateLuck() {
    float base = 0.45f;
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

// ==================== HUMAN ERROR ====================
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
    float r = ((rand() % 1000) / 1000.0f - 0.5f) * 0.05f;
    return std::max(0.02f, std::min(0.25f, base + r));
}

QAngle HumanError::ApplyError(QAngle angles) {
    float rate = CalculateErrorRate();
    if ((rand() % 1000) / 1000.0f < rate) {
        angles.pitch += ((rand() % 1000) / 1000.0f - 0.5f) * 3.0f;
        angles.yaw += ((rand() % 1000) / 1000.0f - 0.5f) * 3.0f;
    }
    return angles;
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
    Vector3 n = (to - from).Normalize();
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

// ==================== DECISION ENGINE ====================
DecisionEngine::Decision DecisionEngine::Evaluate(const Situation& s) {
    Decision d;
    d.mode = Decision::LEGIT;
    d.useWallPen = false;

    if (s.enemiesAlive > s.teammatesAlive + 2 && s.roundTime < 30) {
        d.mode = Decision::GODMODE;
        d.aggression = 0.9f;
        d.errorRate = 0.02f;
        d.luckWeight = 0.9f;
        d.networkWeight = 0.9f;
        d.ricochetWeight = 0.8f;
        d.useWallPen = true;
        d.useFakeLag = true;
    } else if (s.beingShotAt && s.health < 30) {
        d.mode = Decision::AGGRESSIVE;
        d.aggression = 0.7f;
        d.errorRate = 0.03f;
        d.luckWeight = 0.6f;
        d.networkWeight = 0.6f;
        d.ricochetWeight = 0.5f;
        d.useFakeLag = true;
    } else {
        d.aggression = 0.5f;
        d.errorRate = 0.08f;
        d.luckWeight = 0.3f;
        d.networkWeight = 0.3f;
        d.ricochetWeight = 0.0f;
    }

    if (s.ping > 80) d.networkWeight += 0.2f;
    return d;
}

// ==================== QUANTUM AIM ====================
QuantumAim::QuantumAim(MemoryReader* reader, Resolver* res, Autowall* aw)
    : mem(reader), resolver(res), autowall(aw) {}

void QuantumAim::Update(GameState* state, float dt) {
    if (!mem || !state || !settings.enabled) { hasTarget = false; return; }
    auto* local = state->GetLocal();
    if (!local || !local->IsValid()) { hasTarget = false; return; }

    currentWeaponId = local->weaponId;
    timeAccum += dt;
    shotFired = false;
    auto enemies = state->GetEnemies();
    if (enemies.empty()) { hasTarget = false; currentTarget = -1; return; }

    // Decision Engine (5.1)
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
        currentDecision = decision.Evaluate(sit);
    }

    // Momentum Shot (4.3)
    if (settings.momentumShot) momentum.Update(local->velocity);

    // Time Dilation (2.4)
    if (settings.timeDilation) timeDilation.Update(dt);

    // Fake Lag (2.3)
    if (settings.fakeLag && currentDecision.useFakeLag) {
        fakeLag.SetChoke(5 + rand() % 10);
    }

    // BulletTime update (3.3)
    if (settings.bulletTime) bulletTime.Update();

    // Interpolation Exploit (2.5)
    if (settings.interpExploit) interpExploit.Update();

    // Select best target
    QAngle aimAngle;
    float hitchance, damage;
    int target = SelectBestTarget(state, aimAngle, hitchance, damage);
    if (target < 0 || hitchance < settings.minHitchance) {
        hasTarget = false; currentTarget = -1; return;
    }

    currentTarget = target;
    currentHitchance = hitchance;

    // Ricochet check (3.1)
    if (settings.ricochet && currentDecision.useRicochet && !enemies.empty()) {
        QAngle ricAngle;
        Vector3 eye = local->origin; eye.z += 64;
        if (ricochet.Apply(eye, enemies[0]->origin, ricAngle)) {
            float ricFov = GetFov(local->viewAngle, ricAngle);
            if (ricFov < settings.fov) aimAngle = ricAngle;
        }
    }

    // Resolver (from earlier module)
    if (resolver && settings.resolverMode > 0 && target >= 0) {
        aimAngle = resolver->ResolveAngle(&state->players[target], aimAngle, settings.resolverMode);
    }

    // Prediction (1.4)
    if (settings.predictive && target >= 0) {
        Vector3 predPos = predictive.PredictTarget(
            state->players[target].origin,
            state->players[target].velocity,
            0.15f);
        aimAngle = CalcAngle(local->origin + Vector3(0,0,64), predPos);
    }

    // Human Error (4.2)
    if (settings.humanError) aimAngle = human.ApplyError(aimAngle);

    finalAimAngle = aimAngle;
    finalAimAngle.Clamp();
    hasTarget = true;

    // Luck Engine check (4.1)
    if (settings.luckEngine) {
        float luckValue = luck.CalculateLuck();
        float chance = 0.7f * currentDecision.luckWeight + 0.3f;
        if (!luck.RollDice(chance)) {
            hasTarget = false; // Miss this shot
            human.OnDeath();
            return;
        }
    }

    // Fire check
    if (ShouldFire()) {
        shotFired = true;
        shotsFired++;

        // RCS (1.2)
        if (settings.rcs) {
            aimAngle.pitch -= local->aimPunch.x * 0.75f;
            aimAngle.yaw -= local->aimPunch.y * 0.75f;
            aimAngle.Clamp();
        }

        // Recoil Flow (4.5)
        if (settings.recoilFlow) {
            QAngle punchQA(local->aimPunch.x, local->aimPunch.y, 0);
            QAngle recoilOffset = recoil.GetNaturalRecoil(local->weaponId, shotsFired, punchQA);
            aimAngle = aimAngle + recoilOffset;
            aimAngle.Clamp();
        }

        finalAimAngle = aimAngle;
        lastShotTime = GetTickCount() / 1000.0f;

        // Network exploits
        if (settings.interpExploit) interpExploit.OnShot();
        if (settings.bulletTime) bulletTime.Activate(100);

        // Execute the shot via CUserCmd rewrite + IN_ATTACK
        ExecuteShot(state);
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
    Vector3 eye = local->origin + Vector3(0, 0, 64);

    auto enemies = state->GetEnemies();
    for (auto* enemy : enemies) {
        int idx = (int)(enemy - state->players);
        float dist = local->origin.DistTo(enemy->origin);
        if (dist > settings.maxDist) continue;

        // Try different hitboxes
        Hitbox hitboxes[] = { HEAD, CHEST, STOMACH, PELVIS };
        for (auto hb : hitboxes) {
            Vector3 hbPos = GetHitboxPos(state, idx, hb);
            QAngle angle = CalcAngle(eye, hbPos);
            float fov = GetFov(local->viewAngle, angle);
            if (fov > settings.fov) continue;

            float spread = GetWeaponSpread(enemy->weaponId);
            float hc = CalcHitchance(eye, hbPos, spread, dist);
            if (hc < settings.minHitchance) continue;

            float score = (180 - fov) * 2 + hc * 0.5f + (100 - enemy->health) * 0.5f - dist * 0.01f;
            if (enemy->flashed) score += 15;
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
    return bestIdx;
}

Vector3 QuantumAim::GetHitboxPos(GameState* state, int idx, Hitbox hb) {
    auto& p = state->players[idx];
    switch (hb) {
        case HEAD:   return p.origin + Vector3(0, 0, 72);
        case NECK:   return p.origin + Vector3(0, 0, 62);
        case CHEST:  return p.origin + Vector3(0, 0, 50);
        case STOMACH:return p.origin + Vector3(0, 0, 38);
        case PELVIS: return p.origin + Vector3(0, 0, 28);
        case ARM_L:  return p.origin + Vector3(-14, 5, 48);
        case ARM_R:  return p.origin + Vector3(14, 5, 48);
        case LEG_L:  return p.origin + Vector3(-8, 0, 12);
        case LEG_R:  return p.origin + Vector3(8, 0, 12);
        default:     return p.origin + Vector3(0, 0, 50);
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
    const int SAMPLES = 128;
    int hits = 0;
    QAngle aim = CalcAngle(from, to);
    float spreadAngle = spread * (1 + dist / 5000);

    for (int i = 0; i < SAMPLES; i++) {
        float r1 = ((float)rand() / RAND_MAX) * 2 - 1;
        float r2 = ((float)rand() / RAND_MAX) * 2 - 1;
        QAngle s;
        s.pitch = aim.pitch + r1 * spreadAngle;
        s.yaw = aim.yaw + r2 * spreadAngle;
        Vector3 dir;
        float p = s.pitch * PI / 180, y = s.yaw * PI / 180;
        dir.x = -cosf(p) * cosf(y);
        dir.y = cosf(p) * sinf(y);
        dir.z = -sinf(p);

        Vector3 toTarget = to - from;
        float d = toTarget.Length();
        toTarget.Normalize();
        float dot = dir.x*toTarget.x + dir.y*toTarget.y + dir.z*toTarget.z;
        float angleToTarget = acosf(std::max(-1.0f, std::min(1.0f, dot)));
        float hitRadius = 16;
        if (angleToTarget < atan2f(hitRadius, d)) hits++;
    }
    return (float)hits / SAMPLES * 100;
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
    uintptr_t inputSys = mem->Read<uintptr_t>(client + 0x1A0F0);
    if (!inputSys) return;
    uintptr_t cmds = mem->Read<uintptr_t>(inputSys + 0x148);
    if (!cmds) return;

    int cmdNum = mem->Read<int>(inputSys + 0x4C);
    uintptr_t cmd = cmds + (cmdNum % 150) * 0x64;

    // Silent aim: rewrite CUserCmd angles
    mem->Write<float>(cmd + 0x10, finalAimAngle.pitch);
    mem->Write<float>(cmd + 0x14, finalAimAngle.yaw);

    // Trigger shot: IN_ATTACK
    int buttons = mem->Read<int>(cmd + 0x28);
    buttons |= 1;
    mem->Write<int>(cmd + 0x28, buttons);
}
