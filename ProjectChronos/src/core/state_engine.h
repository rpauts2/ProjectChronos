#pragma once
#include "types.h"
#include "memory_reader.h"
#include "../utils/logging.h"
#include <cstring>

struct WeaponData {
    int weaponId = 0;
    int clipAmmo = 0;
    int reserveAmmo = 0;
    float accuracyPenalty = 0;
    float spread = 0;
    float recoilIndex = 0;
};

class StateEngine {
    MemoryReader* mem;
    OffsetDatabase offsets;
    GameState state;
    int lastPlayerCount = 0;

    static constexpr int MAX_PLAYERS = 64;
    static constexpr int MAX_ENTITIES = 4096;

public:
    StateEngine(MemoryReader* reader) : mem(reader) {}
    void SetOffsets(const OffsetDatabase& off) { offsets = off; }

    bool Update() {
        if (!mem) return false;
        uintptr_t client = mem->GetClient();
        if (!client) return false;

        // 1. ViewMatrix
        mem->ReadBuffer(client + offsets.dwViewMatrix, state.viewMatrix, 64);

        // 2. Entity list pointer
        uintptr_t entityList = mem->Read<uintptr_t>(client + offsets.dwEntityList);
        if (!entityList) return false;

        // 3. Local player pointers (may be stale in CS2 after respawn)
        uintptr_t localPawn = mem->Read<uintptr_t>(client + offsets.dwLocalPlayerPawn);
        uintptr_t localCtrl = mem->Read<uintptr_t>(client + offsets.dwLocalPlayerController);

        // 4. Read local data from stale pawn (used for origin fallback)
        Vector3 localOrigin = {};
        Vector3 localViewOffset = {};
        int localTeam = 0;
        if (localPawn && localPawn > 0x10000) {
            localOrigin = mem->Read<Vector3>(localPawn + offsets.m_vOldOrigin);
            localViewOffset = mem->Read<Vector3>(localPawn + offsets.m_vecViewOffset);
            localTeam = mem->Read<uint8_t>(localPawn + offsets.m_iTeamNum);
        }
        state.localTeam = localTeam;

        // Local player name
        char localName[32] = {};
        if (localCtrl && localCtrl > 0x10000) {
            mem->ReadBuffer(localCtrl + offsets.m_iszPlayerName, localName, 32);
            localName[31] = 0;
        }

        // 5. Global vars
        uintptr_t globalVars = mem->Read<uintptr_t>(client + offsets.dwGlobalVars);
        if (globalVars) {
            state.mapTime = mem->Read<float>(globalVars + 0x20);
            state.serverTick = mem->Read<int>(globalVars + 0x1C);
            uintptr_t mapNamePtr = mem->Read<uintptr_t>(globalVars + 0x18);
            if (mapNamePtr && mapNamePtr > 0x10000) {
                char rawMap[64] = {};
                mem->ReadBuffer(mapNamePtr, rawMap, 63);
                rawMap[63] = 0;
                if (rawMap[0] && strncmp(rawMap, state.mapName, 63) != 0) {
                    strncpy(state.mapName, rawMap, 63);
                    state.mapName[63] = 0;
                    LogMessage("Map changed: " + std::string(state.mapName));
                }
            }
        }

        // 5b. Bomb planted detection
        uintptr_t bomb = mem->Read<uintptr_t>(client + offsets.dwPlantedC4);
        state.bombPlanted = (bomb != 0);
        if (state.bombPlanted && bomb > 0x10000) {
            state.bombPos = mem->Read<Vector3>(bomb + offsets.m_vOldOrigin);
        }

        // 6. Enumerate players
        state.playerCount = 0;
        state.localPlayerIndex = -1;

        for (int i = 1; i < MAX_PLAYERS; i++) {
            uintptr_t listEntry = mem->Read<uintptr_t>(entityList + 0x10 + 8 * (i >> 9));
            if (!listEntry) continue;

            uintptr_t controller = mem->Read<uintptr_t>(listEntry + 0x70 * (i & 0x1FF));
            if (!controller) continue;

            uint32_t pawnHandle = mem->Read<uint32_t>(controller + offsets.m_hPawn);
            if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;

            uint32_t pawnIndex = pawnHandle & 0x7FFF;
            if (pawnIndex >= MAX_ENTITIES) continue;

            uintptr_t pawnListEntry = mem->Read<uintptr_t>(entityList + 0x10 + 8 * (pawnIndex >> 9));
            if (!pawnListEntry) continue;

            uintptr_t pawn = mem->Read<uintptr_t>(pawnListEntry + 0x70 * (pawnIndex & 0x1FF));
            if (!pawn) continue;

            // Read health — pawn first, fallback to controller's m_iPawnHealth
            int health = mem->Read<int>(pawn + offsets.m_iHealth);
            if (health > 100) {
                health = mem->Read<int>(controller + offsets.m_iPawnHealth);
                if (health <= 0 || health > 100) continue;
            }

            int team = mem->Read<uint8_t>(pawn + offsets.m_iTeamNum);
            if (team != 2 && team != 3) continue;

            uint8_t lifeState = mem->Read<uint8_t>(pawn + offsets.m_lifeState);
            Vector3 origin = mem->Read<Vector3>(pawn + offsets.m_vOldOrigin);

            auto& p = state.players[state.playerCount];
            p.pawnAddr = pawn;
            p.controllerAddr = controller;
            p.team = team;
            p.health = health;
            p.origin = origin;
            p.alive = (lifeState == 0);
            p.velocity = mem->Read<Vector3>(pawn + offsets.m_vecVelocity);
            p.viewOffset = mem->Read<Vector3>(pawn + offsets.m_vecViewOffset);
            p.viewAngle = mem->Read<QAngle>(pawn + offsets.m_angEyeAngles);
            p.aimPunch = mem->Read<Vector3>(pawn + 0x1430);
            p.flashed = mem->Read<float>(pawn + offsets.m_flFlashDuration) > 0.1f;
            p.scoped = mem->Read<bool>(pawn + offsets.m_bIsScoped);
            p.armor = mem->Read<int>(pawn + offsets.m_ArmorValue);
            p.weaponId = 0;
            p.ammo = 0;
            p.reserveAmmo = 0;

            // Weapon info
            uintptr_t weaponServices = mem->Read<uintptr_t>(pawn + offsets.m_pWeaponServices);
            if (weaponServices) {
                int activeWeaponHandle = mem->Read<int>(weaponServices + offsets.m_hActiveWeapon);
                if (activeWeaponHandle > 0) {
                    uint32_t weaponIdx = (uint32_t)(activeWeaponHandle & 0x7FFF);
                    if (weaponIdx < MAX_ENTITIES) {
                        uintptr_t wpnListEntry = mem->Read<uintptr_t>(entityList + 0x10 + 8 * (weaponIdx >> 9));
                        if (wpnListEntry) {
                            uintptr_t weaponEntity = mem->Read<uintptr_t>(wpnListEntry + 0x70 * (weaponIdx & 0x1FF));
                            if (weaponEntity) {
                                p.weaponId = mem->Read<uint16_t>(weaponEntity + offsets.m_iItemDefinitionIndex);
                                p.ammo = mem->Read<int>(weaponEntity + offsets.m_iClip1);
                                p.reserveAmmo = mem->Read<int>(weaponEntity + 0x15E8);
                            }
                        }
                    }
                }
            }

            // Bones
            ZeroMemory(p.bonePos, sizeof(p.bonePos));
            uintptr_t sn = mem->Read<uintptr_t>(pawn + offsets.m_pGameSceneNode);
            if (sn) {
                uintptr_t boneArray = mem->Read<uintptr_t>(sn + 0x1D0);
                if (boneArray) {
                    struct BoneJoint { Vector3 pos; char pad[20]; };
                    BoneJoint bones[30];
                    if (mem->ReadBuffer(boneArray, bones, sizeof(bones))) {
                        for (int bi = 0; bi < 30; bi++)
                            p.bonePos[bi] = bones[bi].pos;
                    }
                }
            }

            // Read player name from controller
            mem->ReadBuffer(controller + offsets.m_iszPlayerName, p.name, 32);
            p.name[31] = 0;

            // Read money from controller (m_iAccount)
            p.money = mem->Read<int>(controller + offsets.m_iAccount);

            state.playerCount++;
        }

        // Identify local player: match pawn/controller address
        if (localPawn && localPawn > 0x10000) {
            for (int pi = 0; pi < state.playerCount; pi++) {
                if (state.players[pi].pawnAddr == localPawn || state.players[pi].controllerAddr == localCtrl) {
                    state.localPlayerIndex = pi;
                    break;
                }
            }
        }

        // Fallback: try m_bIsLocalPlayerController flag
        if (state.localPlayerIndex == -1) {
            for (int pi = 0; pi < state.playerCount; pi++) {
                bool isLocal = mem->Read<bool>(state.players[pi].controllerAddr + offsets.m_bIsLocalCtrl);
                if (isLocal) {
                    state.localPlayerIndex = pi;
                    break;
                }
            }
        }

        // Fallback: find player closest to stale origin
        if (state.localPlayerIndex == -1 && state.playerCount > 0 && localOrigin.x != 0) {
            float bestDist = 999999.0f;
            int bestIdx = -1;
            for (int pi = 0; pi < state.playerCount; pi++) {
                float d = state.players[pi].origin.DistTo(localOrigin);
                if (d < bestDist) {
                    bestDist = d;
                    bestIdx = pi;
                }
            }
            if (bestIdx >= 0 && bestDist < 500.0f) {
                state.localPlayerIndex = bestIdx;
            }
        }

        if (state.playerCount != lastPlayerCount) {
            LogMessage("StateEngine: found " + std::to_string(state.playerCount) + " players, local idx=" + std::to_string(state.localPlayerIndex) + " localTeam=" + std::to_string(state.localTeam));
            lastPlayerCount = state.playerCount;
        }

        return true;
    }

    GameState* GetState() { return &state; }
    const OffsetDatabase& GetOffsets() const { return offsets; }
    void UpdateOffsets(const OffsetDatabase& newOffsets) { offsets = newOffsets; }
};
