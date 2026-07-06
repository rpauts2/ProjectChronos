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
        case RESOLVER_DESYNC:
            return ResolveDesync(player, aimAngle, playerIndex);
        default:
            return aimAngle;
    }
}

QAngle Resolver::ResolveLBY(Player* player, QAngle aimAngle) {
    float speed = player->velocity.Length2D();
    bool moving = speed > 5.0f;

    // When moving: LBY matches eye angles, use actual angles directly
    if (moving) {
        return aimAngle;
    }

    float lby = player->viewAngle.yaw;
    float currentSimTime = player->simulationTime;

    // Get or create player data
    auto it = playerData.find(0);
    PlayerAngleData* data = nullptr;
    for (auto& [idx, d] : playerData) {
        if (d.lastSimTime > 0) { data = &d; break; }
    }

    if (data) {
        // Detect LBY flick: if LBY changed more than 35 degrees in one tick = fake
        float lbyDelta = NormalizeYaw(lby - data->lastLBY);
        if (fabsf(lbyDelta) > 35.0f) {
            data->lbyFlicked = true;
            data->lbyUpdateTimer = 0;
            data->lastLBY = lby;

            // LBY flicked = fake angle; aim at opposite side of the flick
            QAngle result = aimAngle;
            result.yaw = NormalizeYaw(lby + 180.0f);
            return result;
        }

        // Track stationary time for LBY update prediction
        if (!moving) {
            data->lbyUpdateTimer += currentSimTime - data->lastSimTime;
        } else {
            data->lbyUpdateTimer = 0;
        }

        // If stationary > 1.2s, LBY will update soon - predict next LBY
        if (data->lbyUpdateTimer > 1.2f) {
            // LBY updates to the largest component of the velocity vector
            // When truly stationary, it updates to the last moving direction
            QAngle result = aimAngle;
            result.yaw = NormalizeYaw(lby + 180.0f);
            data->lastLBY = lby;
            return result;
        }

        data->lastLBY = lby;
        data->lastSimTime = currentSimTime;
    }

    // Fallback: standard anti-aim detection — real angle is opposite
    QAngle result = aimAngle;
    result.yaw = NormalizeYaw(lby + 180.0f);
    return result;
}

QAngle Resolver::ResolveFreestanding(Player* player, QAngle aimAngle) {
    float speed = player->velocity.Length2D();

    if (speed < 5.0f) {
        // Cast rays to left and right to find which side has cover
        // Use the side with MORE cover as the "safe" side
        // This catches players who anti-aim toward walls

        float leftAngle = NormalizeYaw(aimAngle.yaw + 90.0f);
        float rightAngle = NormalizeYaw(aimAngle.yaw - 90.0f);

        // Simulate wall check by checking distance to origin in each direction
        Vector3 origin = player->origin;
        float leftDist = 0;
        float rightDist = 0;

        // Check left direction
        for (float step = 50.0f; step <= 200.0f; step += 50.0f) {
            Vector3 leftPt;
            leftPt.x = origin.x + cosf(leftAngle * PI / 180.0f) * step;
            leftPt.y = origin.y + sinf(leftAngle * PI / 180.0f) * step;
            leftPt.z = origin.z;
            leftDist += step;
        }

        // Check right direction
        for (float step = 50.0f; step <= 200.0f; step += 50.0f) {
            Vector3 rightPt;
            rightPt.x = origin.x + cosf(rightAngle * PI / 180.0f) * step;
            rightPt.y = origin.y + sinf(rightAngle * PI / 180.0f) * step;
            rightPt.z = origin.z;
            rightDist += step;
        }

        // Choose the side that has more cover (longer total distance = more open)
        // For anti-aim, we want to aim toward the side with LESS cover
        float playerYaw = player->viewAngle.yaw;
        float diffLeft = fabsf(NormalizeYaw(leftAngle - playerYaw));
        float diffRight = fabsf(NormalizeYaw(rightAngle - playerYaw));

        QAngle result = aimAngle;
        if (leftDist < rightDist) {
            // Left has more cover — player is likely facing right
            result.yaw = leftAngle;
        } else if (rightDist < leftDist) {
            // Right has more cover — player is likely facing left
            result.yaw = rightAngle;
        } else {
            // Equal — use the side closer to player's view
            result.yaw = (diffLeft < diffRight) ? leftAngle : rightAngle;
        }

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

    // Track weighted miss/hit counts — recent results weighted higher
    int totalMisses = 0;
    int totalHits = 0;
    for (int i = 0; i < 8; i++) {
        int age = (data.bruteHistoryIdx - i + 8) % 8;
        int weight = 8 - age; // newer = higher weight
        totalMisses += data.recentMisses[i] * weight;
        totalHits += data.recentHits[i] * weight;
    }

    // After 2 weighted misses, switch to perpendicular angle
    // After 4 weighted misses, try opposite
    int bruteIndex;
    if (totalMisses >= 4) {
        // Try opposite (4th bracket)
        bruteIndex = 2; // 180 degrees
    } else if (totalMisses >= 2) {
        // Switch to perpendicular (2nd bracket)
        bruteIndex = 3 + (data.currentBruteIndex % 2); // 90 or -90
    } else {
        bruteIndex = data.currentBruteIndex % BRUTE_COUNT;
    }

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

    // Track eye-to-body delta for desync detection
    float bodyYaw = player->viewAngle.yaw;
    float eyeYaw = player->viewAngle.yaw;
    data.eyeToBodyDelta = NormalizeYaw(eyeYaw - bodyYaw);
    data.lastBodyYaw = bodyYaw;

    data.lastOrigin = player->origin;
    data.lastEyeAngle = player->viewAngle;
    data.lastSimTime = player->simulationTime;
}

void Resolver::OnShotHit(int playerIndex) {
    auto it = playerData.find(playerIndex);
    if (it != playerData.end()) {
        it->second.resolveHits++;
        it->second.currentBruteIndex = 0;

        // Record hit in weighted history
        int idx = it->second.bruteHistoryIdx % 8;
        it->second.recentHits[idx]++;
        it->second.recentMisses[idx] = 0; // reset misses at this slot
        it->second.bruteHistoryIdx++;
    }
}

void Resolver::OnShotMiss(int playerIndex) {
    auto it = playerData.find(playerIndex);
    if (it != playerData.end()) {
        it->second.resolveMisses++;
        it->second.currentBruteIndex =
            (it->second.currentBruteIndex + 1) % BRUTE_COUNT;

        // Record miss in weighted history
        int idx = it->second.bruteHistoryIdx % 8;
        it->second.recentMisses[idx]++;
        it->second.bruteHistoryIdx++;
    }
}

QAngle Resolver::ResolveDesync(Player* player, QAngle aimAngle, int playerIndex) {
    if (!player) return aimAngle;

    auto it = playerData.find(playerIndex);
    if (it == playerData.end()) {
        playerData[playerIndex] = PlayerAngleData{};
        it = playerData.find(playerIndex);
    }

    auto& data = it->second;

    // Detect desync: compare eye angle velocity to body angle
    // If eye is moving fast but body is slow = desync active
    float eyeVelocity = fabsf(NormalizeYaw(player->viewAngle.yaw - data.lastEyeAngle.yaw));
    float bodyVelocity = fabsf(NormalizeYaw(player->viewAngle.yaw - data.lastBodyYaw));

    bool desyncDetected = false;
    if (eyeVelocity > 15.0f && bodyVelocity < 5.0f) {
        desyncDetected = true;
    }

    // Also check if eye-to-body delta is large
    float ebd = fabsf(data.eyeToBodyDelta);
    if (ebd > 30.0f) {
        desyncDetected = true;
    }

    if (desyncDetected) {
        // Use LBY as the real angle in this case
        float lby = player->viewAngle.yaw;
        QAngle result = aimAngle;
        result.yaw = NormalizeYaw(lby + 180.0f);
        return result;
    }

    // Fallback to LBY-based resolution
    return ResolveLBY(player, aimAngle);
}

float Resolver::NormalizeYaw(float yaw) {
    while (yaw > 180.0f) yaw -= 360.0f;
    while (yaw < -180.0f) yaw += 360.0f;
    return yaw;
}

bool Resolver::IsMoving(Player* player) {
    if (!player) return false;
    return player->velocity.Length2D() > 5.0f;
}
