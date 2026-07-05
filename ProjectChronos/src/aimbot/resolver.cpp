#include "resolver.h"
#include "utils/logging.h"
#include <cmath>
#include <algorithm>

QAngle Resolver::ResolveAngle(Player* player, QAngle aimAngle, int mode, int playerIndex) {
    if (!player || mode == RESOLVER_NONE) return aimAngle;

    switch (mode) {
        case RESOLVER_LBY:
            return ResolveLBY(player, aimAngle);
        case RESOLVER_FREESTANDING:
            return ResolveFreestanding(player, aimAngle);
        case RESOLVER_BRUTEFORCE:
            return ResolveBruteforce(player, aimAngle, playerIndex);
        case RESOLVER_BACKTRACK:
            return ResolveBacktrack(player, aimAngle, playerIndex);
        default:
            return aimAngle;
    }
}

QAngle Resolver::ResolveLBY(Player* player, QAngle aimAngle) {
    float speed = player->velocity.Length2D();
    bool moving = speed > 5.0f;

    // When moving: LBY matches eye angles, use actual angles
    if (moving) {
        return aimAngle;
    }

    // When stationary: LBY can be desynced
    float lby = player->viewAngle.yaw;

    // Anti-aim detection: when stationary, real angle is usually opposite
    QAngle backward = aimAngle;
    backward.yaw = NormalizeYaw(lby + 180.0f);

    QAngle forward = aimAngle;
    forward.yaw = lby;

    // Choose based on which side the player appears to be facing
    float diffFwd = fabsf(NormalizeYaw(forward.yaw - aimAngle.yaw));
    float diffBwd = fabsf(NormalizeYaw(backward.yaw - aimAngle.yaw));

    if (diffBwd < diffFwd) {
        return backward;
    }
    return forward;
}

QAngle Resolver::ResolveFreestanding(Player* player, QAngle aimAngle) {
    float speed = player->velocity.Length2D();

    if (speed < 5.0f) {
        // Stationary: use perpendicular angles as heuristic
        float perp1 = NormalizeYaw(aimAngle.yaw + 90.0f);
        float perp2 = NormalizeYaw(aimAngle.yaw - 90.0f);

        float playerYaw = player->viewAngle.yaw;
        float diff1 = fabsf(NormalizeYaw(perp1 - playerYaw));
        float diff2 = fabsf(NormalizeYaw(perp2 - playerYaw));

        QAngle result = aimAngle;
        result.yaw = (diff1 < diff2) ? perp1 : perp2;
        return result;
    }

    return aimAngle;
}

QAngle Resolver::ResolveBruteforce(Player* player, QAngle aimAngle, int playerIndex) {
    if (playerIndex < 0) return aimAngle;

    auto it = playerData.find(playerIndex);
    if (it == playerData.end()) {
        playerData[playerIndex] = PlayerAngleData{};
        it = playerData.find(playerIndex);
    }

    auto& data = it->second;
    int bruteIndex = data.currentBruteIndex % BRUTE_COUNT;

    QAngle result = aimAngle;
    result.yaw = NormalizeYaw(aimAngle.yaw + BRUTE_ANGLES[bruteIndex]);

    return result;
}

QAngle Resolver::ResolveBacktrack(Player* player, QAngle aimAngle, int playerIndex) {
    if (playerIndex < 0) return aimAngle;

    auto it = playerData.find(playerIndex);
    if (it == playerData.end()) return aimAngle;

    auto& data = it->second;
    if (data.angleHistoryIndex < 10) return aimAngle;

    // Count angle frequencies (binned to 15-degree increments)
    static constexpr int BINS = 24;
    int binCounts[BINS] = {};
    float binSums[BINS] = {};

    for (int i = 0; i < 64; i++) {
        if (data.lastAngles[i].yaw == 0 && data.lastAngles[i].pitch == 0) continue;
        int bin = ((int)(NormalizeYaw(data.lastAngles[i].yaw) + 180) / 15) % BINS;
        binCounts[bin]++;
        binSums[bin] += data.lastAngles[i].yaw;
    }

    int bestBin = 0;
    int bestCount = 0;
    for (int i = 0; i < BINS; i++) {
        if (binCounts[i] > bestCount) {
            bestCount = binCounts[i];
            bestBin = i;
        }
    }

    if (bestCount < 3) return aimAngle;

    QAngle result = aimAngle;
    result.yaw = NormalizeYaw(binSums[bestBin] / bestCount);
    return result;
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
        data.stationaryTime += 0.016f;
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
        it->second.currentBruteIndex = 0;
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
