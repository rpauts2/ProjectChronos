#pragma once
#include "types.h"
#include "memory_reader.h"
#include "../utils/logging.h"

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
    static constexpr int MAX_ENTITIES = 1024;

public:
    StateEngine(MemoryReader* reader) : mem(reader) {}
    void SetOffsets(const OffsetDatabase& off) { offsets = off; }

    bool Update() {
        if (!mem) return false;
        uintptr_t client = mem->GetClient();
        if (!client) { LogMessage("StateEngine: client null"); return false; }

        // Read entity list pointer
        uintptr_t entityList = mem->Read<uintptr_t>(client + offsets.dwEntityList);
        if (!entityList) { LogMessage("StateEngine: entityList null"); return false; }

        // Read local player pawn directly
        uintptr_t localPawn = mem->Read<uintptr_t>(client + offsets.dwLocalPlayerPawn);
        if (!localPawn) { LogMessage("StateEngine: localPawn null"); return false; }

        // Read local player controller for name
        uintptr_t localCtrl = mem->Read<uintptr_t>(client + offsets.dwLocalPlayerController);

        // Read view matrix directly (cs2-dumper dwViewMatrix points to view-projection data)
        mem->ReadBuffer(client + offsets.dwViewMatrix, state.viewMatrix, 64);

        // Read local pawn data
        Vector3 localOrigin = mem->Read<Vector3>(localPawn + offsets.m_vOldOrigin);
        Vector3 localViewOffset = mem->Read<Vector3>(localPawn + offsets.m_vecViewOffset);
        Vector3 localEyePos = localOrigin + localViewOffset;
        int localTeam = mem->Read<int>(localPawn + offsets.m_iTeamNum);
        int localHealth = mem->Read<int>(localPawn + offsets.m_iHealth);
        state.localTeam = localTeam;

        // Read local player name from controller
        char localName[32] = {};
        if (localCtrl) {
            uintptr_t namePtr = mem->Read<uintptr_t>(localCtrl + offsets.m_szName);
            if (namePtr && namePtr > 0x10000)
                mem->ReadBuffer(namePtr, localName, 32);
        }

        // Read global vars
        uintptr_t globalVars = mem->Read<uintptr_t>(client + offsets.dwGlobalVars);
        if (globalVars) {
            state.mapTime = mem->Read<float>(globalVars + 0x20);
            state.serverTick = mem->Read<int>(globalVars + 0x1C);
        }

        // Enumerate players via entity list
        state.playerCount = 0;
        state.localPlayerIndex = -1;

        for (int i = 1; i < MAX_PLAYERS; i++) {
            // Read list entry for controller
            uintptr_t listEntry = mem->Read<uintptr_t>(entityList + 0x10 + 8 * (i >> 9));
            if (!listEntry) continue;

            uintptr_t controller = mem->Read<uintptr_t>(listEntry + 0x70 * (i & 0x1FF));
            if (!controller) continue;

            // Get pawn handle from controller
            uint32_t pawnHandle = mem->Read<uint32_t>(controller + offsets.m_hPawn);
            if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;

            uint32_t pawnIndex = pawnHandle & 0x7FFF;
            if (pawnIndex >= MAX_ENTITIES) continue;

            // Read pawn from entity list
            uintptr_t pawnListEntry = mem->Read<uintptr_t>(entityList + 0x10 + 8 * (pawnIndex >> 9));
            if (!pawnListEntry) continue;

            uintptr_t pawn = mem->Read<uintptr_t>(pawnListEntry + 0x70 * (pawnIndex & 0x1FF));
            if (!pawn) continue;

            // Read pawn data
            int health = mem->Read<int>(pawn + offsets.m_iHealth);
            if (health <= 0 || health > 100) continue;

            int team = mem->Read<int>(pawn + offsets.m_iTeamNum);
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
            p.velocity = mem->Read<Vector3>(pawn + offsets.m_vVelocity);
            p.viewOffset = mem->Read<Vector3>(pawn + offsets.m_vecViewOffset);
            p.viewAngle = mem->Read<QAngle>(pawn + offsets.m_angEyeAngles);
            p.aimPunch = mem->Read<Vector3>(pawn + 0x1430);
            p.flashed = mem->Read<float>(pawn + offsets.m_flFlashDuration) > 0.1f;
            p.scoped = mem->Read<bool>(pawn + offsets.m_bIsScoped);
            p.armor = mem->Read<int>(pawn + 0x1C7C);
            p.lastShotTime = 0;

            // Get weapon
            p.weaponId = 0;
            p.ammo = 0;
            uintptr_t weaponServices = mem->Read<uintptr_t>(pawn + offsets.m_pWeaponServices);
            if (weaponServices) {
                int activeWeaponHandle = mem->Read<int>(weaponServices + offsets.m_hActiveWeapon);
                if (activeWeaponHandle > 0) {
                    uint32_t weaponIdx = (uint32_t)(activeWeaponHandle & 0x7FFF);
                    uintptr_t wpnListEntry = mem->Read<uintptr_t>(entityList + 0x10 + 8 * (weaponIdx >> 9));
                    if (wpnListEntry) {
                        uintptr_t weaponEntity = mem->Read<uintptr_t>(wpnListEntry + 0x70 * (weaponIdx & 0x1FF));
                        if (weaponEntity) {
                            p.weaponId = mem->Read<int>(weaponEntity + offsets.m_iItemDefinitionIndex);
                            p.ammo = mem->Read<int>(weaponEntity + offsets.m_iClip1);
                        }
                    }
                }
            }

            // Read bone positions (Rshwmom pattern)
            {
                ZeroMemory(p.bonePos, sizeof(p.bonePos));
                uintptr_t sceneNode = mem->Read<uintptr_t>(pawn + offsets.m_pGameSceneNode);
                if (sceneNode) {
                    uintptr_t boneArray = mem->Read<uintptr_t>(sceneNode + 0x1E0);
                    if (boneArray) {
                        struct BoneJoint { Vector3 pos; char pad[20]; };
                        BoneJoint bones[30];
                        if (mem->ReadBuffer(boneArray, bones, sizeof(bones))) {
                            for (int bi = 0; bi < 30; bi++)
                                p.bonePos[bi] = bones[bi].pos;
                        }
                    }
                }
            }

            // Identify local player
            if (pawn == localPawn) {
                state.localPlayerIndex = state.playerCount;
                strcpy_s(p.name, localName);
            } else {
                p.name[0] = 0;
            }

            state.playerCount++;
            if (state.playerCount >= MAX_PLAYERS) break;
        }

        if (state.playerCount != lastPlayerCount) {
            LogMessage("StateEngine: found " + std::to_string(state.playerCount) + " players, local idx=" + std::to_string(state.localPlayerIndex));
            lastPlayerCount = state.playerCount;
        }
        return true;
    }

    GameState* GetState() { return &state; }
    const OffsetDatabase& GetOffsets() const { return offsets; }
    void UpdateOffsets(const OffsetDatabase& newOffsets) { offsets = newOffsets; }
};
