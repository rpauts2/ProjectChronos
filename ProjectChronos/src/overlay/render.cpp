#include "overlay.h"
#include "core/types.h"
#include <cmath>

// Bone indices (CS2 standard: Rshwmom, litware, IMXNOOBX)
enum { BONE_PELVIS=0, BONE_SPINE_0=2, BONE_SPINE_1=3, BONE_SPINE_2=4,
       BONE_NECK=5, BONE_HEAD=6,
       BONE_ARM_UP_L=8, BONE_ARM_LO_L=9, BONE_HAND_L=10,
       BONE_ARM_UP_R=13, BONE_ARM_LO_R=14, BONE_HAND_R=15,
       BONE_LEG_UP_L=22, BONE_LEG_LO_L=23, BONE_ANKLE_L=24,
       BONE_LEG_UP_R=25, BONE_LEG_LO_R=26, BONE_ANKLE_R=27 };

static const int kBoneChains[5][5] = {
    { BONE_HEAD, BONE_NECK, BONE_SPINE_2, BONE_SPINE_1, BONE_PELVIS },  // spine
    { BONE_NECK, BONE_ARM_UP_L, BONE_ARM_LO_L, BONE_HAND_L },           // left arm
    { BONE_NECK, BONE_ARM_UP_R, BONE_ARM_LO_R, BONE_HAND_R },           // right arm
    { BONE_PELVIS, BONE_LEG_UP_L, BONE_LEG_LO_L, BONE_ANKLE_L },        // left leg
    { BONE_PELVIS, BONE_LEG_UP_R, BONE_LEG_LO_R, BONE_ANKLE_R },        // right leg
};
static const int kBoneChainLen[5] = {5, 4, 4, 4, 4};

static bool IsValidBonePos(const Vector3& v) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
        return false;
    if (fabsf(v.x) > 100000.f || fabsf(v.y) > 100000.f || fabsf(v.z) > 100000.f)
        return false;
    if (fabsf(v.x) < 0.001f && fabsf(v.y) < 0.001f && fabsf(v.z) < 0.001f)
        return false;
    return true;
}

void Overlay::RenderSkeleton(GameState* state, int playerIdx) {
    auto& p = state->players[playerIdx];
    if (!p.IsValid()) return;

    float white[4] = { 1, 1, 1, 0.6f };
    float* vm = state->viewMatrix;

    for (int chain = 0; chain < 5; chain++) {
        int len = kBoneChainLen[chain];
        Vector2 prevScr;
        bool hasPrev = false;
        for (int j = 0; j < len; j++) {
            int bi = kBoneChains[chain][j];
            if (!IsValidBonePos(p.bonePos[bi])) { hasPrev = false; continue; }
            Vector2 scr;
            if (!WorldToScreen(p.bonePos[bi], scr, vm)) { hasPrev = false; continue; }
            if (hasPrev)
                DrawLine((int)prevScr.x, (int)prevScr.y, (int)scr.x, (int)scr.y, white);
            prevScr = scr;
            hasPrev = true;
        }
    }
}

void Overlay::RenderAimbotFov(GameState* state) {
    auto* local = state->GetLocal();
    if (!local) return;
    float purple[4] = { 0.6f, 0.2f, 0.8f, 0.3f };
    DrawCircle(screenW / 2, screenH / 2, 150, purple, 48);
}

void Overlay::RenderWatermark() {
    float green[4] = { 0, 1, 0.5f, 0.7f };
    DrawText("Chronos v9", screenW - 120, 10, green, false);
}
