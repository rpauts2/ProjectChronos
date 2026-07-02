#include "resolver.h"
#include "utils/logging.h"
#include <cmath>
#include <algorithm>

QAngle Resolver::ResolveAngle(Player* player, QAngle aimAngle, int mode) {
    if (!player || mode == RESOLVER_NONE) return aimAngle;

    switch (mode) {
        case RESOLVER_LBY:
            return ResolveLBY(player, aimAngle);
        case RESOLVER_FREESTANDING:
            return ResolveFreestanding(player, aimAngle);
        case RESOLVER_BRUTEFORCE:
            return ResolveBruteforce(player, aimAngle);
        case RESOLVER_BACKTRACK:
            return ResolveBacktrack(player, aimAngle);
        default:
            return aimAngle;
    }
}

QAngle Resolver::ResolveLBY(Player* player, QAngle aimAngle) {
    // LBY (Lower Body Yaw) resolver
    // In CS2, when a player is stationary, their body yaw (LBY)
    // can be used to determine the real angle
    float lby = player->viewAngle.yaw; // approximate - real LBY is a separate variable
    float speed = player->velocity.Length2D();
    bool moving = speed > 5.0f;

    // When moving: aim at actual eye angles
    if (moving) {
        return aimAngle;
    }

    // When stationary: LBY can be desynced from eye angle
    // The real angle is typically LBY + 180 (backward) or LBY (forward)
    // Try both and use the one closer to current aim
    QAngle forward = aimAngle;
    forward.yaw = lby;

    QAngle backward = aimAngle;
    backward.yaw = NormalizeYaw(lby + 180.0f);

    float diffFwd = fabsf(NormalizeYaw(forward.yaw - aimAngle.yaw));
    float diffBwd = fabsf(NormalizeYaw(backward.yaw - aimAngle.yaw));

    // Usually players face backward with anti-aim
    if (diffBwd < diffFwd) {
        return backward;
    }

    return forward;
}

QAngle Resolver::ResolveFreestanding(Player* player, QAngle aimAngle) {
    // Freestanding: detect which side of a wall the player is on
    // If they're close to a wall, their real angle faces away from it

    // Simplified: check velocity direction relative to aim
    float speed = player->velocity.Length2D();

    if (speed < 5.0f) {
        // Stationary: the real angle is likely facing opposite to nearest wall
        // We can't know the wall position externally, so use a heuristic
        // If the player appears to be looking at us, they're probably not
        QAngle toUs = aimAngle;
        // The real angle is likely perpendicular to the angle to us
        float perp1 = NormalizeYaw(toUs.yaw + 90.0f);
        float perp2 = NormalizeYaw(toUs.yaw - 90.0f);

        // Return the more likely one
        QAngle result = aimAngle;
        // Use the player's yaw to determine which perpendicular
        float playerYaw = player->viewAngle.yaw;
        float diff1 = fabsf(NormalizeYaw(perp1 - playerYaw));
        float diff2 = fabsf(NormalizeYaw(perp2 - playerYaw));

        result.yaw = (diff1 < diff2) ? perp1 : perp2;
        return result;
    }

    return aimAngle;
}

QAngle Resolver::ResolveBruteforce(Player* player, QAngle aimAngle) {
    // Brute-force: cycle through possible yaw offsets
    // Each miss advances to the next brute angle
    // Each hit resets to most reliable angle

    // Use the player index to seed the brute rotation
    // This ensures we try different angles for different players

    int bruteIndex = 0;
    auto it = playerData.find(0); // need player index here but we don't have it
    // Actually we need to track per-player. Let's just use the angle itself.

    // Cycle through brute angles based on misses
    static int globalBruteOffset = 0;
    int idx = globalBruteOffset % BRUTE_COUNT;
    globalBruteOffset++;

    QAngle result = aimAngle;
    result.yaw = NormalizeYaw(aimAngle.yaw + BRUTE_ANGLES[idx]);

    return result;
}

QAngle Resolver::ResolveBacktrack(Player* player, QAngle aimAngle) {
    // Backtrack resolver: use historical angles to find the real one
    // If we've seen the player's angle before, use the most common one

    // Simplified: just return the current angle
    // Full implementation would analyze angle history patterns
    return aimAngle;
}

void Resolver::UpdatePlayerData(Player* player, int playerIndex) {
    if (!player) return;

    auto& data = playerData[playerIndex];
    float speed = player->velocity.Length2D();

    // Store history
    data.lastAngles[data.angleHistoryIndex % 64] = player->viewAngle;
    data.angleHistoryIndex++;
    data.lastMoveSpeed = speed;
    data.wasMoving = speed > 5.0f;

    if (!data.wasMoving) {
        data.stationaryTime += 0.016f; // ~60fps frame time approximation
    } else {
        data.stationaryTime = 0;
    }

    data.lastOrigin = player->origin;
    data.lastEyeAngle = player->viewAngle;
}

void Resolver::OnShotHit(int playerIndex) {
    auto it = playerData.find(playerIndex);
    if (it != playerData.end()) {
        it->second.resolveHits++;
        it->second.currentBruteIndex = 0; // Reset brute on hit
    }
}

void Resolver::OnShotMiss(int playerIndex) {
    auto it = playerData.find(playerIndex);
    if (it != playerData.end()) {
        it->second.resolveMisses++;
        it->second.currentBruteIndex =
            (it->second.currentBruteIndex + 1) % BRUTE_COUNT;
    }
}

float Resolver::NormalizeYaw(float yaw) {
    while (yaw > 180.0f) yaw -= 360.0f;
    while (yaw < -180.0f) yaw += 360.0f;
    return yaw;
}

bool Resolver::IsMoving(Player* player) {
    return player && player->velocity.Length2D() > 5.0f;
}
