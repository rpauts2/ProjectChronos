#include "ragebot.h"
#include "utils/logging.h"
#include <random>
#include <cmath>

Ragebot::Ragebot(MemoryReader* reader, Resolver* res, Autowall* aw)
    : mem(reader), resolver(res), autowall(aw)
{
    // Default hitbox weights (Axion inspired)
    for (int i = 0; i < HITBOX_COUNT; i++) {
        settings.hitbox_enable[i] = true;
        settings.hitbox_weight[i] = 0.0f;
    }
    settings.hitbox_weight[HITBOX_HEAD] = 100.0f;
    settings.hitbox_weight[HITBOX_NECK] = 75.0f;
    settings.hitbox_weight[HITBOX_CHEST] = 40.0f;
    settings.hitbox_weight[HITBOX_STOMACH] = 35.0f;
    settings.hitbox_weight[HITBOX_PELVIS] = 30.0f;
    settings.hitbox_weight[HITBOX_LEFT_UPPER_ARM] = 20.0f;
    settings.hitbox_weight[HITBOX_RIGHT_UPPER_ARM] = 20.0f;
    settings.hitbox_weight[HITBOX_LEFT_FOREARM] = 15.0f;
    settings.hitbox_weight[HITBOX_RIGHT_FOREARM] = 15.0f;
    settings.hitbox_weight[HITBOX_LEFT_THIGH] = 25.0f;
    settings.hitbox_weight[HITBOX_RIGHT_THIGH] = 25.0f;
    settings.hitbox_weight[HITBOX_LEFT_CALF] = 15.0f;
    settings.hitbox_weight[HITBOX_RIGHT_CALF] = 15.0f;

    lastShotTime = 0;
    currentTarget = -1;
    previousTarget = -1;
    hasAimTarget = false;
    shotFiredThisTick = false;
}

void Ragebot::Update(GameState* state, float frameTime) {
    if (!mem || !state || !settings.enabled) {
        hasAimTarget = false;
        return;
    }

    auto* local = state->GetLocal();
    if (!local || !local->IsValid()) {
        hasAimTarget = false;
        return;
    }

    auto enemies = state->GetEnemies();
    if (enemies.empty()) {
        hasAimTarget = false;
        currentTarget = -1;
        return;
    }

    currentWeaponId = local->weaponId;
    shotFiredThisTick = false;

    // 1. Select best target
    RageTarget best = SelectBestTarget(state);

    if (best.playerIndex < 0 || best.hitchance < settings.minHitchance) {
        hasAimTarget = false;
        currentTarget = -1;
        return;
    }

    currentTarget = best.playerIndex;
    targetHitbox = best.hitbox;
    currentHitchance = best.hitchance;

    // 2. Get aim angle to target
    Vector3 hitboxPos = GetHitboxPosition(state, best.playerIndex, best.hitbox);
    Vector3 localEye = local->origin;
    localEye.z += 64.0f; // approximate eye height

    // Apply resolver if needed
    QAngle aimAngle = CalcAimAngle(localEye, hitboxPos);
    if (resolver && settings.resolverMode > 0) {
        Player* target = &state->players[best.playerIndex];
        aimAngle = resolver->ResolveAngle(target, aimAngle, settings.resolverMode);
    }

    // Apply hitchance spread simulation
    if (currentHitchance < settings.minHitchance) {
        hasAimTarget = false;
        return;
    }

    targetAngle = aimAngle;
    targetAngle.Clamp();

    // 3. Apply smoothing
    if (settings.smoothing > 1.0f && hasAimTarget) {
        float smoothFactor = 1.0f / settings.smoothing;
        targetAngle.pitch = lastAimAngle.pitch + (targetAngle.pitch - lastAimAngle.pitch) * smoothFactor;
        targetAngle.yaw = lastAimAngle.yaw + (targetAngle.yaw - lastAimAngle.yaw) * smoothFactor;
        targetAngle.Clamp();
    }

    lastAimAngle = targetAngle;
    hasAimTarget = true;

    // 4. Auto scope
    if (settings.autoScope) {
        ShouldAutoScope(state);
    }

    // 5. Auto stop
    if (settings.autoStop && ShouldFire()) {
        AutoStop(state);
    }

    // 6. Fire check
    if (settings.autofire && ShouldFire()) {
        shotFiredThisTick = true;
    }
}

Ragebot::RageTarget Ragebot::SelectBestTarget(GameState* state) {
    RageTarget best;
    best.playerIndex = -1;
    best.priorityScore = -1.0f;

    auto* local = state->GetLocal();
    if (!local) return best;

    Vector3 localEye = local->origin;
    localEye.z += 64.0f;

    auto enemies = state->GetEnemies();
    for (auto* enemy : enemies) {
        int idx = (int)(enemy - state->players);

        float dist = local->origin.DistTo(enemy->origin);
        if (dist > settings.maxDistance) continue;

        // Get best hitbox for this target
        HitboxType bestHitbox = GetBestHitbox(state, idx);
        Vector3 hitboxPos = GetHitboxPosition(state, idx, bestHitbox);

        QAngle aimAngle = CalcAimAngle(localEye, hitboxPos);
        float fov = GetFovToTarget(local->viewAngle, aimAngle);
        if (fov > settings.maxFov) continue;

        // Visibility check
        bool visible = IsVisible(state, localEye, hitboxPos);
        if (settings.visibleOnly && !visible) continue;

        // Calculate hitchance
        float hitchance = CalcHitchance(state, idx, bestHitbox, aimAngle);
        if (hitchance < settings.minHitchance) continue;

        // Calculate damage
        float damage = CalcDamage(state, idx, bestHitbox, aimAngle);
        if (damage < settings.minDamage) continue;

        // Check wall penetration
        if (settings.wallOnly && !visible) {
            Autowall::AutowallResult aw = autowall->FireBullet(localEye, hitboxPos, enemy->weaponId);
            if (!aw.penetrable || aw.damage < settings.minDamage) continue;
            damage = aw.damage;
        }

        // Calculate priority score (multi-factor)
        float prio = 0;
        prio += (100.0f - fov) * 2.0f;                           // FOV bonus
        prio += hitboxPos.y; // hack: use it differently
        prio += (1.0f - (dist / settings.maxDistance)) * 30.0f;  // Distance bonus
        prio += (100.0f - enemy->health) * 0.5f;                  // Low HP bonus
        prio += hitchance * 0.5f;                                  // Hitchance bonus
        prio += damage * 0.2f;                                     // Damage bonus
        if (visible) prio += 20.0f;                                // Visibility bonus
        if (enemy->flashed) prio += 15.0f;                         // Flashed bonus

        RageTarget t;
        t.playerIndex = idx;
        t.hitbox = bestHitbox;
        t.hitchance = hitchance;
        t.damage = damage;
        t.fov = fov;
        t.distance = dist;
        t.priorityScore = prio;

        if (t.priorityScore > best.priorityScore) {
            best = t;
        }
    }

    return best;
}

HitboxType Ragebot::GetBestHitbox(GameState* state, int playerIdx) {
    auto* local = state->GetLocal();
    auto& enemy = state->players[playerIdx];
    if (!local) return HITBOX_CHEST;

    float dist = local->origin.DistTo(enemy.origin);

    // Close range: head priority
    if (dist < 500.0f && settings.hitbox_enable[HITBOX_HEAD]) {
        return HITBOX_HEAD;
    }
    // Medium range: chest for reliability
    if (dist < 1500.0f && settings.hitbox_enable[HITBOX_CHEST]) {
        return HITBOX_CHEST;
    }
    // Long range: chest or stomach
    if (dist < 3000.0f && settings.hitbox_enable[HITBOX_STOMACH]) {
        return HITBOX_STOMACH;
    }
    // Very long range: pelvis
    if (settings.hitbox_enable[HITBOX_PELVIS]) {
        return HITBOX_PELVIS;
    }

    return HITBOX_CHEST;
}

float Ragebot::CalcHitchance(GameState* state, int playerIdx, HitboxType hitbox, QAngle aimAngle) {
    auto* local = state->GetLocal();
    auto& enemy = state->players[playerIdx];
    if (!local) return 0;

    // Simulate multiple random spread samples (like Axion does)
    const int NUM_SAMPLES = 256;
    int hits = 0;

    Vector3 localEye = local->origin;
    localEye.z += 64.0f;
    Vector3 targetPos = GetHitboxPosition(state, playerIdx, hitbox);

    float weaponSpread = GetWeaponHitchanceBase(enemy.weaponId);
    float distance = local->origin.DistTo(enemy.origin);
    float spreadAngle = weaponSpread * (1.0f + distance / 5000.0f);

    std::mt19937 rng(rand());
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (int i = 0; i < NUM_SAMPLES; i++) {
        // Random spread offset
        float r1 = dist(rng);
        float r2 = dist(rng);

        QAngle spread;
        spread.pitch = aimAngle.pitch + r1 * spreadAngle;
        spread.yaw = aimAngle.yaw + r2 * spreadAngle;
        spread.Clamp();

        Vector3 dir;
        float pitch = spread.pitch * PI / 180.0f;
        float yaw = spread.yaw * PI / 180.0f;
        dir.x = -cosf(pitch) * cosf(yaw);
        dir.y = cosf(pitch) * sinf(yaw);
        dir.z = -sinf(pitch);

        // Check if this random bullet direction would hit
        // Simplified: check if direction is within a cone to target hitbox
        Vector3 toTarget = targetPos - localEye;
        float distToTarget = toTarget.Length();
        toTarget = toTarget.Normalized();

        float dot = dir.x * toTarget.x + dir.y * toTarget.y + dir.z * toTarget.z;
        float angleToTarget = acosf((std::max)(-1.0f, (std::min)(1.0f, dot)));

        // Hitbox radius ~16-32 units depending on body part
        float hitboxRadius = 16.0f;
        switch (hitbox) {
            case HITBOX_HEAD: hitboxRadius = 12.0f; break;
            case HITBOX_CHEST: hitboxRadius = 24.0f; break;
            case HITBOX_STOMACH: hitboxRadius = 20.0f; break;
            case HITBOX_PELVIS: hitboxRadius = 18.0f; break;
            default: hitboxRadius = 14.0f; break;
        }

        float hitAngle = atan2f(hitboxRadius, distToTarget);
        if (angleToTarget < hitAngle) {
            hits++;
        }
    }

    return (float)hits / (float)NUM_SAMPLES * 100.0f;
}

float Ragebot::CalcDamage(GameState* state, int playerIdx, HitboxType hitbox, QAngle aimAngle) {
    auto* local = state->GetLocal();
    auto& enemy = state->players[playerIdx];
    if (!local) return 0;

    float baseDamage = GetWeaponDamageBase(enemy.weaponId);

    // Hitbox damage multipliers
    float multiplier = 1.0f;
    switch (hitbox) {
        case HITBOX_HEAD: multiplier = 4.0f; break;
        case HITBOX_CHEST: multiplier = 1.0f; break;
        case HITBOX_STOMACH: multiplier = 1.25f; break;
        case HITBOX_PELVIS: multiplier = 1.0f; break;
        case HITBOX_LEFT_UPPER_ARM: multiplier = 0.75f; break;
        case HITBOX_RIGHT_UPPER_ARM: multiplier = 0.75f; break;
        case HITBOX_LEFT_THIGH: multiplier = 0.75f; break;
        case HITBOX_RIGHT_THIGH: multiplier = 0.75f; break;
        default: multiplier = 1.0f; break;
    }

    float distance = local->origin.DistTo(enemy.origin);
    float distanceMod = 1.0f - (distance / 5000.0f) * 0.5f;
    if (distanceMod < 0.5f) distanceMod = 0.5f;

    // Armor reduction
    float armorReduction = 1.0f;
    if (enemy.armor > 0) {
        armorReduction = 0.85f;
    }

    float damage = baseDamage * multiplier * distanceMod * armorReduction;

    // Minimum damage check
    if (enemy.health > 0 && damage > enemy.health) {
        damage = enemy.health + 5.0f; // overkill to ensure kill
    }

    return damage;
}

QAngle Ragebot::CalcAimAngle(Vector3 from, Vector3 to) {
    Vector3 delta = to - from;
    float hyp = sqrtf(delta.x * delta.x + delta.y * delta.y);

    QAngle angle;
    angle.pitch = -atan2f(delta.z, hyp) * (180.0f / PI);
    angle.yaw = atan2f(delta.y, delta.x) * (180.0f / PI);
    angle.roll = 0;
    angle.Clamp();

    return angle;
}

bool Ragebot::IsVisible(GameState* state, Vector3 from, Vector3 to) {
    // Simple visibility check using trace line
    // In external we can't easily trace, so use spotted flag
    auto* local = state->GetLocal();
    if (!local) return false;

    // Check if the target is marked as spotted by the game
    // This is an approximation - real vis check needs TraceLine
    for (int i = 0; i < 64; i++) {
        auto& p = state->players[i];
        if (!p.IsValid()) continue;
        // Only check the specific target (matched by position proximity)
        float dist = p.origin.DistTo(to);
        if (dist > 10.0f) continue;  // not the target player
        // Spotted check from memory
        bool spotted = mem->Read<bool>(p.pawnAddr + 0x1C8C);
        return spotted;
    }

    return false;  // target not found or not spotted
}

bool Ragebot::ShouldAutoScope(GameState* state) {
    if (!state) return false;
    auto* local = state->GetLocal();
    if (!local) return false;

    // Auto-scope for snipers when we have a target
    bool isSniper = (local->weaponId == 9 || local->weaponId == 11); // Scout, AWP
    if (isSniper && !local->scoped && currentTarget >= 0) {
        // Inject scope button via memory
        float now = GetTickCount() / 1000.0f;
        if (now - lastAutoScopeTime > 0.5f) {
            lastAutoScopeTime = now;
            // Scope button - we'd write to input in real impl
            return true;
        }
    }

    return false;
}

void Ragebot::AutoStop(GameState* state) {
    if (!state) return;
    auto* local = state->GetLocal();
    if (!local) return;

    float speed = local->velocity.Length2D();
    if (speed < 5.0f) return; // already stopped

    // We'd write movement keys to stop in a real implementation
    // For now this is a stub - the executor handles movement
}

bool Ragebot::ShouldFire() const {
    if (!hasAimTarget || !settings.autofire) return false;
    if (currentTarget < 0) return false;
    if (currentHitchance < settings.minHitchance) return false;

    float now = GetTickCount() / 1000.0f;
    float fireRate = GetFireRate(currentWeaponId);
    if (now - lastShotTime < fireRate) return false;

    return true;
}

float Ragebot::GetFireRate(int weaponId) {
    switch (weaponId) {
        case 1:  return 0.22f;  // Deagle
        case 2:  return 0.12f;  // Dual Berettas
        case 3:  return 0.10f;  // Five-SeveN
        case 4:  return 0.10f;  // Glock
        case 5:  return 0.10f;  // USP
        case 7:  return 0.10f;  // AK-47
        case 8:  return 0.09f;  // M4A4
        case 9:  return 1.50f;  // AWP
        case 10: return 1.25f;  // Scout
        case 13: return 0.09f;  // M4A4
        case 14: return 0.10f;  // FAMAS
        case 15: return 0.10f;  // Galil
        case 16: return 0.10f;  // M4A1-S
        case 17: return 0.10f;  // AUG
        case 19: return 0.10f;  // SG 553
        default: return 0.10f;
    }
}

Vector3 Ragebot::GetHitboxPosition(GameState* state, int playerIdx, HitboxType hitbox) {
    auto& p = state->players[playerIdx];
    Vector3 origin = p.origin;

    // Simplified hitbox positions based on origin + offset
    // Real implementation would read bone matrix
    switch (hitbox) {
        case HITBOX_HEAD:    return origin + Vector3(0, 0, 70);
        case HITBOX_NECK:    return origin + Vector3(0, 0, 60);
        case HITBOX_CHEST:   return origin + Vector3(0, 0, 48);
        case HITBOX_STOMACH: return origin + Vector3(0, 0, 35);
        case HITBOX_PELVIS:  return origin + Vector3(0, 0, 25);
        case HITBOX_LEFT_UPPER_ARM:  return origin + Vector3(-14, 5, 50);
        case HITBOX_RIGHT_UPPER_ARM: return origin + Vector3(14, 5, 50);
        case HITBOX_LEFT_FOREARM:    return origin + Vector3(-12, 8, 35);
        case HITBOX_RIGHT_FOREARM:   return origin + Vector3(12, 8, 35);
        case HITBOX_LEFT_THIGH:      return origin + Vector3(-8, 0, 12);
        case HITBOX_RIGHT_THIGH:     return origin + Vector3(8, 0, 12);
        case HITBOX_LEFT_CALF:       return origin + Vector3(-6, -3, -8);
        case HITBOX_RIGHT_CALF:      return origin + Vector3(6, -3, -8);
        default: return origin + Vector3(0, 0, 48);
    }
}

float Ragebot::GetFovToTarget(QAngle currentAngle, QAngle targetAngle) {
    QAngle delta = currentAngle - targetAngle;
    delta.Clamp();
    return sqrtf(delta.pitch * delta.pitch + delta.yaw * delta.yaw);
}

float Ragebot::GetWeaponHitchanceBase(int weaponId) {
    // Base spread values per weapon (lower = more accurate)
    switch (weaponId) {
        case 1:  return 0.12f;  // Deagle
        case 2:  return 0.15f;  // Dual Berettas
        case 3:  return 0.14f;  // Five-SeveN
        case 4:  return 0.13f;  // Glock
        case 5:  return 0.12f;  // USP
        case 7:  return 0.20f;  // AK-47
        case 8:  return 0.16f;  // M4A4
        case 9:  return 0.10f;  // AWP
        case 10: return 0.08f;  // Scout
        case 11: return 0.06f;  // AWP (really scout? follow cs2 enum)
        case 13: return 0.16f;  // M4A4 (alt)
        case 14: return 0.20f;  // FAMAS
        case 15: return 0.18f;  // Galil
        case 16: return 0.15f;  // M4A1-S
        case 17: return 0.18f;  // AUG
        case 19: return 0.18f;  // SG 553
        default: return 0.18f;
    }
}

float Ragebot::GetWeaponDamageBase(int weaponId) {
    switch (weaponId) {
        case 1:  return 53.0f;   // Deagle
        case 2:  return 25.0f;   // Dual Berettas
        case 3:  return 24.0f;   // Five-SeveN
        case 4:  return 22.0f;   // Glock
        case 5:  return 24.0f;   // USP
        case 7:  return 36.0f;   // AK-47
        case 8:  return 33.0f;   // M4A4
        case 9:  return 115.0f;  // AWP
        case 10: return 80.0f;   // Scout
        case 13: return 33.0f;   // M4A4
        case 14: return 30.0f;   // FAMAS
        case 15: return 30.0f;   // Galil
        case 16: return 31.0f;   // M4A1-S
        case 17: return 28.0f;   // AUG
        case 19: return 30.0f;   // SG 553
        default: return 30.0f;
    }
}
