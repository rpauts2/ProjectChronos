#pragma once
#include "core/types.h"
#include <map>

enum ResolverMode {
    RESOLVER_NONE = 0,
    RESOLVER_LBY,            // Lower Body Yaw tracking
    RESOLVER_FREESTANDING,   // Freestanding (wall orientation)
    RESOLVER_BRUTEFORCE,     // Brute-force through angles
    RESOLVER_BACKTRACK,      // Use history to find real angle
    RESOLVER_DESYNC          // Desync detection via eye/body angle delta
};

struct PlayerAngleData {
    // LBY tracking
    float lastLBY = 0;
    float lastLBYUpdateTime = 0;
    float lbyDelta = 0;
    int lbyFlickCount = 0;

    // Resolver state
    float resolvedYaw = 0;
    float resolvedPitch = 0;
    int currentBruteIndex = 0;
    int resolveMisses = 0;
    int resolveHits = 0;

    // History
    QAngle lastAngles[64];
    int angleHistoryIndex = 0;
    float lastMoveSpeed = 0;

    // Freestanding
    Vector3 lastOrigin;
    QAngle lastEyeAngle;
    bool wasMoving = false;
    float stationaryTime = 0;

    // Anti-aim detection
    bool isAntiAiming = false;
    float lastSimTime = 0;
    float lbyUpdateTimer = 0;
    bool lbyFlicked = false;
    int antiAimSide = 0; // -1=left, 0=center, 1=right

    // Freestanding wall detection
    float freestandingAngle = 0;
    bool hasFreestanding = false;

    // Animation layer data
    float lastBodyYaw = 0;
    float eyeToBodyDelta = 0;

    // Brute force miss tracking (weighted)
    int recentMisses[8] = {};
    int recentHits[8] = {};
    int bruteHistoryIdx = 0;
};

class Resolver {
    std::map<int, PlayerAngleData> playerData;

    static constexpr float PI = 3.14159265f;

    // Brute-force angles to try
    static constexpr float BRUTE_ANGLES[] = {
        0.0f, 180.0f, -180.0f, 90.0f, -90.0f,
        45.0f, -45.0f, 135.0f, -135.0f,
        60.0f, -60.0f, 120.0f, -120.0f,
        30.0f, -30.0f, 150.0f, -150.0f,
        180.0f + 90.0f, 180.0f - 90.0f,
        0.0f + 180.0f, 0.0f - 180.0f
    };
    static constexpr int BRUTE_COUNT = sizeof(BRUTE_ANGLES) / sizeof(BRUTE_ANGLES[0]);

public:
    Resolver() = default;

    // Main resolve function
    QAngle ResolveAngle(Player* player, QAngle aimAngle, int mode, int playerIndex = -1);

    // Individual resolver modes
    QAngle ResolveLBY(Player* player, QAngle aimAngle);
    QAngle ResolveFreestanding(Player* player, QAngle aimAngle);
    QAngle ResolveBruteforce(Player* player, QAngle aimAngle, int playerIndex = -1);
    QAngle ResolveBacktrack(Player* player, QAngle aimAngle, int playerIndex = -1);
    QAngle ResolveDesync(Player* player, QAngle aimAngle, int playerIndex);

    // Update tracking data
    void UpdatePlayerData(Player* player, int playerIndex);

    // Feedback
    void OnShotHit(int playerIndex);
    void OnShotMiss(int playerIndex);

    // Reset
    void Reset() { playerData.clear(); }

    PlayerAngleData* GetData(int playerIndex) {
        auto it = playerData.find(playerIndex);
        return it != playerData.end() ? &it->second : nullptr;
    }

    float NormalizeYaw(float yaw);
    bool IsMoving(Player* player);
};
