#include "aim_controller.h"
#include "utils/logging.h"
#include <cmath>
#include <algorithm>

static constexpr float PI = 3.14159265f;

static double GetQPCSeconds() {
    static LARGE_INTEGER freq = {};
    static bool inited = false;
    if (!inited) { QueryPerformanceFrequency(&freq); inited = true; }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

AimController::AimController(MemoryReader* reader, Resolver* res, Autowall* aw)
    : mem(reader), resolver(res), autowall(aw) {
    finalAimAngle = {};
    lastSmoothTarget = {};
    std::memset(records, 0, sizeof(records));
    std::memset(writeIndex, 0, sizeof(writeIndex));
    lastShotTime = (float)GetQPCSeconds();
}

// ==================== MAIN UPDATE ====================

void AimController::Update(GameState* state, float deltaTime) {
    if (!state || !mem) return;

    auto* local = state->GetLocal();
    if (!local || local->health <= 0) {
        hasTarget = false;
        currentTarget = -1;
        return;
    }

    currentWeaponId = local->weaponId;

    if (settings.aimKey != 0) {
        if (!(GetAsyncKeyState(settings.aimKey) & 0x8000)) {
            hasTarget = false;
            currentTarget = -1;
            return;
        }
    }

    RecordTick(state);

    QAngle aimAngle;
    float hitchance = 0;
    int target = SelectBestTarget(state, aimAngle, hitchance);

    if (target >= 0 && settings.enabled && settings.aimbot) {
        finalAimAngle = aimAngle;
        if (settings.rcs) {
            finalAimAngle.pitch -= local->aimPunch.x * 2.0f;
            finalAimAngle.yaw -= local->aimPunch.y * 2.0f;
        }
        finalAimAngle.Clamp();

        currentTarget = target;
        currentHitchance = hitchance;
        hasTarget = true;

        ApplyMouseAim(state, finalAimAngle, deltaTime);
    } else {
        hasTarget = false;
        currentTarget = -1;
    }

    if (settings.triggerEnabled || settings.triggerbot) {
        triggerbotTimer += deltaTime;
        if (hasTarget && currentHitchance >= settings.minHitchance) {
            float delay = settings.triggerDelay / 1000.0f;
            float fireRate = GetFireRate(currentWeaponId);
            if (triggerbotTimer >= delay && (float)GetQPCSeconds() - lastShotTime >= fireRate) {
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                lastShotTime = (float)GetQPCSeconds();
                triggerbotTimer = 0;
            }
        }
    }
}

// ==================== MOUSE AIM (external: moves crosshair via mouse_event) ====================

void AimController::ApplyMouseAim(GameState* state, QAngle targetAngle, float deltaTime) {
    if (!state) return;
    auto* local = state->GetLocal();
    if (!local) return;

    QAngle currentAngle = local->viewAngle;
    QAngle delta = targetAngle - currentAngle;

    while (delta.yaw > 180.0f) delta.yaw -= 360.0f;
    while (delta.yaw < -180.0f) delta.yaw += 360.0f;
    delta.pitch = (std::max)(-89.0f, (std::min)(89.0f, delta.pitch));

    float onTargetThreshold = settings.rageMode ? 5.0f : settings.onTargetThreshold;
    if (fabsf(delta.yaw) >= onTargetThreshold || fabsf(delta.pitch) >= onTargetThreshold) {
        if (settings.autoFire) {
            if (settings.fireMode == 0 && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            }
        }
    }

    float angleDist = sqrtf(delta.pitch * delta.pitch + delta.yaw * delta.yaw);
    if (angleDist < 0.5f) {
        if (settings.autoFire && ShouldFire()) {
            if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                shotsFired++;
            }
        }
        return;
    }

    float horizFov = 90.0f;
    float pixelsPerDegree = (float)settings.screenWidth / horizFov;

    float dx = delta.yaw * pixelsPerDegree * settings.mouseSensitivity;
    float dy = delta.pitch * pixelsPerDegree * settings.mouseSensitivity;

    if (settings.smoothEnabled && !settings.rageMode) {
        float smooth = settings.smoothSpeed / 100.0f;
        smooth = (std::max)(0.01f, (std::min)(1.0f, smooth));
        dx *= smooth;
        dy *= smooth;
    }

    float maxAnglePerFrame = settings.smoothMaxAngle;
    float maxPixels = maxAnglePerFrame * pixelsPerDegree;
    float mag = sqrtf(dx * dx + dy * dy);
    if (mag > maxPixels && maxPixels > 0) {
        dx = dx / mag * maxPixels;
        dy = dy / mag * maxPixels;
    }

    int moveX = (int)lroundf(dx);
    int moveY = (int)lroundf(dy);
    if (moveX != 0 || moveY != 0) {
        mouse_event(MOUSEEVENTF_MOVE, moveX, moveY, 0, 0);
    }

    if (settings.autoFire && ShouldFire()) {
        if (fabsf(delta.yaw) < onTargetThreshold && fabsf(delta.pitch) < onTargetThreshold) {
            if (settings.fireMode == 0) {
                if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                    shotsFired++;
                }
            } else {
                float now = (float)GetQPCSeconds();
                float fireRate = GetFireRate(currentWeaponId);
                if (now - lastShotTime >= fireRate) {
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    lastShotTime = now;
                    shotFired = true;
                }
            }
        }
    }
}

// ==================== SHOULD FIRE ====================

bool AimController::ShouldFire() const {
    if (!hasTarget || !settings.enabled) return false;
    if (currentHitchance < settings.minHitchance) return false;
    if (currentTarget < 0 || currentTarget >= 64) return false;

    float now = (float)GetQPCSeconds();
    float fireRate = GetFireRate(currentWeaponId);
    if (now - lastShotTime < fireRate) return false;

    return true;
}

// ==================== TARGET SELECTION ====================

int AimController::SelectBestTarget(GameState* state, QAngle& outAngle, float& outHitchance) {
    auto* local = state->GetLocal();
    if (!local) return -1;

    WeaponProfile profile = GetActiveProfile();
    float adjustedFov = settings.rageMode ? 180.0f : profile.fov;
    Vector3 eye = local->GetHeadPos();

    int bestIdx = -1;
    float bestScore = -1e9f;

    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;

        float dist = p.origin.DistTo(local->origin);
        if (dist > profile.maxDist) continue;

        // Try head, then body
        for (int hb = 0; hb < 3; hb++) {
            Vector3 hbPos = GetHitboxPos(p, hb);

            // Try backtrack
            Vector3 btPos;
            bool usedBacktrack = false;
            if (settings.backtrackEnabled && GetBestBacktrack(i, btPos)) {
                // Check if backtrack position gives better angle
                QAngle btAngle = CalcAngle(eye, btPos);
                float btFov = GetFov(local->viewAngle, btAngle);
                if (btFov < adjustedFov) {
                    hbPos = btPos;
                    usedBacktrack = true;
                }
            }

            QAngle angle = CalcAngle(eye, hbPos);
            float fov = GetFov(local->viewAngle, angle);
            if (fov > adjustedFov) continue;

            float spread = profile.spread;
            float hc = CalcHitchance(eye, hbPos, spread, dist);
            if (hc < settings.minHitchance) continue;

            // Score: lower FOV is better, higher hitchance is better
            float score = 0;
            score += (adjustedFov - fov) * 2.0f;
            score += hc * 0.5f;
            score += (100 - p.health) * 0.1f;
            score -= dist * 0.005f;
            if (p.flashed) score += 10.0f;
            if (p.scoped) score -= 5.0f;
            // Head is preferred
            if (hb == 0) score += 15.0f;
            else if (hb == 1) score += 8.0f;

            // Target hysteresis
            if (i == currentTarget) {
                score += 5.0f; // prefer keeping current target
            }

            if (score > bestScore) {
                bestScore = score;
                bestIdx = i;
                outAngle = angle;
                outHitchance = hc;
            }
        }
    }

    return bestIdx;
}

// ==================== HITBOX POSITIONS ====================

Vector3 AimController::GetHitboxPos(Player& p, int hitbox) {
    switch (hitbox) {
        case 0: return p.bonePos[6];    // head
        case 1: return p.bonePos[4];    // chest
        case 2: return p.bonePos[3];    // stomach
        default: return p.bonePos[4];
    }
}

// ==================== WEAPON PROFILE ====================

AimController::WeaponProfile AimController::GetActiveProfile() {
    if (!settings.useWeaponProfiles) {
        WeaponProfile p;
        p.fov = settings.fov;
        p.smoothSpeed = settings.smoothSpeed;
        p.maxAnglePerFrame = settings.smoothMaxAngle;
        p.minHitchance = settings.minHitchance;
        p.maxDist = settings.maxDist;
        p.fireRate = GetFireRate(currentWeaponId);
        p.spread = GetWeaponSpread(currentWeaponId);
        return p;
    }

    // Determine weapon group
    int id = currentWeaponId;
    if (id == 9 || id == 11 || id == 38 || id == 40) return settings.sniperProfile;
    if (id == 36 || id == 4 || id == 3 || id == 32 || id == 61 || id == 1 || id == 63 || id == 64 || id == 30 || id == 2)
        return settings.pistolProfile;
    if (id == 17 || id == 33 || id == 34 || id == 19 || id == 23 || id == 24 || id == 26)
        return settings.smgProfile;
    if (id == 25 || id == 27 || id == 35 || id == 29)
        return settings.shotgunProfile;
    return settings.rifleProfile;
}

// ==================== ANGLE MATH ====================

QAngle AimController::CalcAngle(Vector3 from, Vector3 to) {
    Vector3 d = to - from;
    float hyp = sqrtf(d.x * d.x + d.y * d.y);
    QAngle a;
    a.pitch = -atan2f(d.z, hyp) * (180.0f / PI);
    a.yaw = atan2f(d.y, d.x) * (180.0f / PI);
    a.Clamp();
    return a;
}

float AimController::GetFov(QAngle cur, QAngle target) {
    QAngle d = cur - target;
    d.Clamp();
    return sqrtf(d.pitch * d.pitch + d.yaw * d.yaw);
}

float AimController::CalcHitchance(Vector3 from, Vector3 to, float spread, float dist) const {
    Vector3 delta = to - from;
    float d = delta.Length();
    if (d < 1.0f) d = 1.0f;

    float hitboxRadius = 36.0f;
    if (d < 500) hitboxRadius = 20.0f;
    else if (d < 1500) hitboxRadius = 30.0f;

    float hitboxAngle = atanf(hitboxRadius / d) * 180.0f / PI;
    float spreadAngle = spread * 180.0f / PI * (1.0f + dist / 10000.0f);
    if (spreadAngle < 0.01f) spreadAngle = 0.01f;

    float hc = (hitboxAngle / spreadAngle) * 100.0f;
    return (std::max)(0.0f, (std::min)(100.0f, hc));
}

float AimController::GetWeaponSpread(int weaponId) const {
    switch (weaponId) {
        case 1: return 0.12f;  case 7: return 0.20f;  case 8: return 0.16f;
        case 9: return 0.10f;  case 11: return 0.06f; case 13: return 0.16f;
        case 16: return 0.15f; case 17: return 0.18f; case 19: return 0.15f;
        case 23: return 0.15f; case 24: return 0.17f; case 25: return 0.22f;
        case 26: return 0.18f; case 27: return 0.20f; case 28: return 0.20f;
        case 29: return 0.22f; case 30: return 0.16f; case 33: return 0.17f;
        case 34: return 0.16f; case 35: return 0.22f; case 36: return 0.14f;
        case 38: return 0.06f; case 39: return 0.16f; case 40: return 0.08f;
        case 60: return 0.15f; case 61: return 0.14f; case 63: return 0.14f;
        case 64: return 0.12f;
        default: return 0.18f;
    }
}

float AimController::GetFireRate(int weaponId) const {
    switch (weaponId) {
        case 1: return 0.22f;  case 7: return 0.10f;  case 8: return 0.09f;
        case 9: return 1.50f;  case 11: return 1.25f; case 13: return 0.09f;
        case 14: return 0.08f; case 16: return 0.09f; case 17: return 0.075f;
        case 19: return 0.065f;case 23: return 0.08f; case 24: return 0.09f;
        case 25: return 0.85f; case 26: return 0.075f;case 27: return 0.80f;
        case 28: return 0.065f;case 29: return 0.80f; case 30: return 0.07f;
        case 33: return 0.08f; case 34: return 0.075f;case 35: return 0.88f;
        case 36: return 0.09f; case 38: return 0.15f; case 39: return 0.09f;
        case 40: return 1.25f; case 60: return 0.09f; case 61: return 0.17f;
        case 63: return 0.10f; case 64: return 0.25f;
        default: return 0.10f;
    }
}

// ==================== BACKTRACK ====================

void AimController::RecordTick(GameState* state) {
    if (!settings.backtrackEnabled) return;
    tickCount++;

    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid()) continue;

        int& wi = writeIndex[i];
        records[i][wi].origin = p.origin;
        records[i][wi].viewAngle = p.viewAngle;
        records[i][wi].simulationTime = p.simulationTime;
        records[i][wi].valid = true;
        std::memcpy(records[i][wi].bonePos, p.bonePos, sizeof(p.bonePos));

        wi = (wi + 1) % MAX_RECORDS;
    }
}

bool AimController::GetBestBacktrack(int playerIdx, Vector3& outPos) {
    if (playerIdx < 0 || playerIdx >= MAX_PLAYERS) return false;

    int wi = writeIndex[playerIdx];
    int refIdx = (wi - 1 + MAX_RECORDS) % MAX_RECORDS;
    float maxTime = settings.backtrackMaxTime;

    float bestScore = -1e9f;
    bool found = false;

    for (int i = MAX_RECORDS - 1; i >= 0; i--) {
        int idx = (wi - 1 - i + MAX_RECORDS) % MAX_RECORDS;
        auto& rec = records[playerIdx][idx];
        if (!rec.valid) continue;

        float dt = records[playerIdx][refIdx].simulationTime - rec.simulationTime;
        if (dt > maxTime) continue;

        // Lag compensation detection: skip records with dramatic position changes
        // (teleport/LC break — delta > 50 units in one tick)
        int nextIdx = (idx + 1) % MAX_RECORDS;
        if (records[playerIdx][nextIdx].valid) {
            float posDelta = rec.origin.DistTo(records[playerIdx][nextIdx].origin);
            if (posDelta > 50.0f) continue;
        }

        // Prefer records closest to max allowed time for best lag comp
        float timeScore = dt / maxTime;

        // Distance-based scoring: closer records = better lag comp benefit
        Vector3 eye = {}; // approximate from local player
        float dist = rec.bonePos[6].DistTo(outPos);
        float distScore = 1.0f / (1.0f + dist * 0.001f);

        float score = timeScore * 0.7f + distScore * 0.3f;

        if (score > bestScore) {
            bestScore = score;
            outPos = rec.bonePos[6]; // head
            found = true;
        }
    }
    return found;
}

// ==================== SHOT EXECUTION ====================

void AimController::ExecuteShot(GameState* state, QAngle aimAngle) {
    ApplyMouseAim(state, aimAngle, 0.016f);
}
