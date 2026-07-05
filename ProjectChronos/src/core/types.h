#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <map>
#include <functional>

// ==================== VECTORS & ANGLES ====================

struct Vector3 {
    float x, y, z;
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    Vector3 operator+(const Vector3& v) const { return {x+v.x, y+v.y, z+v.z}; }
    Vector3 operator-(const Vector3& v) const { return {x-v.x, y-v.y, z-v.z}; }
    Vector3 operator*(float s) const { return {x*s, y*s, z*s}; }
    Vector3 operator/(float s) const { return {x/s, y/s, z/s}; }
    Vector3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    
    float Length() const { return sqrtf(x*x + y*y + z*z); }
    float Length2D() const { return sqrtf(x*x + y*y); }
    float DistTo(const Vector3& v) const { return (*this - v).Length(); }
    
    Vector3 Normalized() const {
        float len = Length();
        if (len > 0) { return Vector3(x / len, y / len, z / len); }
        return *this;
    }
};

struct Vector2 {
    float x, y;
    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}
};

struct QAngle {
    float pitch, yaw, roll;
    QAngle() : pitch(0), yaw(0), roll(0) {}
    QAngle(float p, float y, float r) : pitch(p), yaw(y), roll(r) {}
    
    QAngle operator-(const QAngle& a) const { return {pitch-a.pitch, yaw-a.yaw, roll-a.roll}; }
    QAngle operator+(const QAngle& a) const { return {pitch+a.pitch, yaw+a.yaw, roll+a.roll}; }
    QAngle operator*(float s) const { return {pitch*s, yaw*s, roll*s}; }
    
    float Diff(const QAngle& a) const {
        return sqrtf(powf(pitch - a.pitch, 2) + powf(yaw - a.yaw, 2));
    }
    
    void Clamp() {
        while (yaw > 180) yaw -= 360;
        while (yaw < -180) yaw += 360;
        pitch = (std::max)(-89.0f, (std::min)(89.0f, pitch));
    }
};

// ==================== GAME STATE ====================

struct Player {
    uintptr_t controllerAddr;
    uintptr_t pawnAddr;
    
    Vector3 origin;
    Vector3 velocity;
    Vector3 aimPunch;
    Vector3 viewOffset;
    QAngle viewAngle;
    QAngle eyeAngle;
    
    int health;
    int armor;
    int team;           // 2=TT, 3=CT
    int weaponId;
    int ammo;
    int reserveAmmo;
    int money;
    int kills, deaths, assists;
    
    bool alive;
    bool flashed;
    bool scoped;
    bool defusing;
    bool hasBomb;
    bool spotted;
    
    char name[32];
    float lastShotTime;
    float spawnTime;
    float simulationTime;
    float duckAmount;
    int flags;

    Vector3 bonePos[30];
    
    Vector3 GetHeadPos() const { return origin + viewOffset; }
    
    bool IsValid() const { return health > 0 && health <= 100 && alive; }
    bool IsEnemy(int localTeam) const { return team != localTeam; }
};

struct GameState {
    Player players[64];
    int playerCount;
    int localPlayerIndex;
    int localTeam;
    
    Vector3 bombPos;
    bool bombPlanted;
    float bombTime;
    
    float roundTime;
    float mapTime;
    int roundNum;
    char mapName[64];
    
    int serverTick;
    float subTickFraction;
    int ping;
    
    float viewMatrix[16];       // 4x4 view-projection matrix for W2S

    // NadeEngine pointer for trajectory rendering
    void* nadeEngine = nullptr;  // NadeEngine* — avoid circular include

    struct {
        bool connected;
        bool inGame;
        bool spectating;
    } status;
    
    Player* GetLocal() {
        if (localPlayerIndex >= 0 && localPlayerIndex < playerCount)
            return &players[localPlayerIndex];
        return nullptr;
    }
    
    std::vector<Player*> GetEnemies() {
        std::vector<Player*> enemies;
        for (int i = 0; i < 64; i++)
            if (players[i].IsValid() && players[i].IsEnemy(localTeam))
                enemies.push_back(&players[i]);
        return enemies;
    }
};

// ==================== SITUATION CONTEXT ====================

enum ExploitType : int {
    EXPLOIT_NONE = 0,
    // Packet exploits
    SUBTICK_REWIND,
    CONFLICT_WINNER,
    DAMAGE_DELAY,
    LAG_COMP_HIJACK,
    MULTI_TIMESTAMP,
    // Memory exploits
    INPUT_HISTORY_REPLAY,
    BONE_MERGE_OFFSET,
    SPREAD_CANCEL,
    // Timing exploits
    SUBTICK_SPRAY_ALIGN,
    PREDICTION_OVERSHOOT,
    SUBTICK_PERFECT,
    SMART_CANCELLER,
    GRAVITY_PREDICT,
    // Config exploits
    INTERPOLATION_INVERT,
    WALLBANG_CALC,
    // Information
    GHOST_CONTEXT
};

struct ExploitSolution {
    ExploitType type = EXPLOIT_NONE;
    float confidence = 0.0f;
    float riskScore = 0.0f;
    int delayMs = 0;
    float timeShiftMs = 0;
    Vector3 positionShift = {0,0,0};
    QAngle angleOverride = {0,0,0};
    bool overrideAngle = false;
    bool shouldShoot = false;
    bool blockShot = false;
    std::string description = "none";
    
    float Score() const { return confidence - riskScore * 2.0f; }
};

struct SituationContext {
    // Target
    int targetId = -1;
    Vector3 targetPos;
    Vector3 targetVelocity;
    float targetSpeed = 0;
    float targetDistance = 0;
    bool targetVisible = false;
    bool targetMoving = false;
    bool targetPeeking = false;
    bool targetShooting = false;
    bool targetJumping = false;
    bool targetFlashed = false;
    bool targetScoped = false;
    int targetHealth = 100;
    int targetWeapon = 0;
    float targetAngleToLocal = 0;
    
    // Local
    Vector3 localPos;
    QAngle localAngle;
    float localSpeed = 0;
    int localHealth = 100;
    int localWeapon = 0;
    int localAmmo = 30;
    int shotsFired = 0;
    int ping = 30;
    
    // Context
    int enemiesAlive = 0;
    int teammatesAlive = 0;
    float roundTime = 0;
    bool isClutch = false;
    bool isBeingShot = false;
    int consecutiveHits = 0;
    int consecutiveKills = 0;
    
    // Calculated
    QAngle angleToEnemy;
    float angleDiff = 0;
};

// ==================== NADE ====================

enum NadeType { SMOKE, FLASH, HE, MOLOTOV, DECOY };
enum NadeAction { STAND_THROW, CROUCH_THROW, JUMP_THROW, WALK_THROW, RUN_THROW };

struct NadeSpot {
    std::string name;
    std::string map;
    NadeType type;
    
    Vector3 standPos;
    QAngle aimAngle;
    int action; // NadeAction
    
    Vector3 landingPos;
    float flightTime;
    
    std::string description;
    std::vector<std::string> targets;
    
    bool IsValid() const {
        return standPos.x != 0 || standPos.y != 0 || standPos.z != 0;
    }
};

// ==================== OFFSETS ====================

struct OffsetDatabase {
    // client.dll offsets (cs2-dumper 2026-07-01)
    uintptr_t dwLocalPlayerController = 0x2320570;
    uintptr_t dwLocalPlayerPawn = 0x2341528;
    uintptr_t dwEntityList = 0x24E7680;
    uintptr_t dwViewMatrix = 0x23469C0;
    uintptr_t dwGlobalVars = 0x20616D0;
    uintptr_t dwInputSystem = 0x42B50;          // inputsystem.dll
    uintptr_t dwPlantedC4 = 0x234FE28;
    uintptr_t dwGameRules = 0x2340FE8;
    
    // Player fields (C_BaseEntity / C_CSPlayerPawn) — verified against cs2-dumper 2026-07-03
    uintptr_t m_iHealth = 0x34C;              // C_BaseEntity::m_iHealth (844)
    uintptr_t m_iTeamNum = 0x3EB;             // C_BaseEntity::m_iTeamNum (1003)
    uintptr_t m_vOldOrigin = 0x1390;          // C_BasePlayerPawn::m_vOldOrigin (5008)
    uintptr_t m_vecViewOffset = 0xE70;        // C_BaseModelEntity::m_vecViewOffset (3696) — FIXED
    uintptr_t m_vecVelocity = 0x430;          // C_BaseEntity::m_vecVelocity (1072)
    uintptr_t m_angEyeAngles = 0x3320;        // C_CSPlayerPawn::m_angEyeAngles (13088)
    uintptr_t m_iClip1 = 0x16D8;              // C_BasePlayerWeapon::m_iClip1 (5848)
    uintptr_t m_iShotsFired = 0x1C64;         // C_CSPlayerPawn::m_iShotsFired (7268)
    uintptr_t m_bIsScoped = 0x1C50;           // C_CSPlayerPawn::m_bIsScoped (7248)
    uintptr_t m_bIsDefusing = 0x1C52;         // C_CSPlayerPawn::m_bIsDefusing (7250)
    uintptr_t m_flFlashDuration = 0x1400;     // C_CSPlayerPawnBase::m_flFlashDuration (5120)
    uintptr_t m_fFlags = 0x3F8;               // C_BaseEntity::m_fFlags (1016)
    uintptr_t m_iHasBomb = 0;
    uintptr_t m_szName = 0x860;              // CCSPlayerController::m_sSanitizedPlayerName (CUtlString)
    uintptr_t m_iszPlayerName = 0x6F4;       // CBasePlayerController::m_iszPlayerName (1780)
    uintptr_t m_iPawnHealth = 0x918;         // CCSPlayerController::m_iPawnHealth (2328)
    uintptr_t m_hPawn = 0x90C;               // CCSPlayerController::m_hPlayerPawn (2316)
    uintptr_t m_bIsLocalCtrl = 0x788;        // CBasePlayerController::m_bIsLocalPlayerController (1928) — NEW
    uintptr_t m_lifeState = 0x354;           // C_BaseEntity::m_lifeState (852)
    uintptr_t m_pGameSceneNode = 0x330;      // C_BaseEntity::m_pGameSceneNode (816)
    uintptr_t m_pBoneMergeCache = 0x8A0;
    uintptr_t m_pWeaponServices = 0x11E0;    // C_BasePlayerPawn::m_pWeaponServices (4576)
    uintptr_t m_hActiveWeapon = 0x60;        // CPlayer_WeaponServices::m_hActiveWeapon (96)
    uintptr_t m_ArmorValue = 0x1C7C;         // C_CSPlayerPawn::m_ArmorValue (7292)
    
    // Weapon (C_CSWeaponBase / C_EconItemView)
    uintptr_t m_fAccuracyPenalty = 0x17D0;
    uintptr_t m_fSpread = 0;
    uintptr_t m_flRecoilIndex = 0x17E0;
    uintptr_t m_iItemDefinitionIndex = 0x1BA;
    uintptr_t m_pAimPunchServices = 0x1490;   // C_CSPlayerPawn::m_pAimPunchServices
    uintptr_t m_aimPunchAngle = 0x50;         // AimPunchServices::m_aimPunchAngle (predictableBaseAngle)
    uintptr_t m_flSimulationTime = 0x1C8;     // C_BaseEntity::m_flSimulationTime (456)
    uintptr_t m_bSpotted = 0x8;               // CGameSceneNode::m_bSpotted (entity visibility)
    
    // Input History (CUserCmd)
    uintptr_t m_pCommands = 0x148;
    uintptr_t m_nCommandNumber = 0x4;
    uintptr_t m_nTickCount = 0x8;
    uintptr_t m_viewangles = 0x10;
    uintptr_t m_nButtons = 0x28;
    uintptr_t m_subtickAttack = 0x3C;    // CSubtickMoveStep or attack flag
    uintptr_t m_cmdSize = 0x64;          // Size of each CUserCmd entry
    uintptr_t m_nCmdCount = 0x4C;        // InputSystem::m_nCmdCount (total commands)
    uintptr_t m_flForwardMove = 0x1C;    // CBaseUserCmd::m_flForwardMove (auto-stop)
    uintptr_t m_flSideMove = 0x20;       // CBaseUserCmd::m_flSideMove (auto-stop)
    uintptr_t m_flUpMove = 0x24;         // CBaseUserCmd::m_flUpMove
    uintptr_t m_hBomb = 0x23C0;          // C_CSPlayerPawn::m_hBomb (defuse/bomb)
    uintptr_t m_iAccount = 0x2378;       // C_CSTeam::m_iAccount (money, needs verification)
};