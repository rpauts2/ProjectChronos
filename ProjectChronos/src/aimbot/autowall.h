#pragma once
#include "core/types.h"

class Autowall {
public:
    struct AutowallResult {
        bool penetrable = false;
        float damage = 0;
        float penetrationPower = 0;
        int hitbox = -1;
        Vector3 hitPoint;
        Vector3 exitPoint;
        bool hitHead = false;
    };

    // Trace data for bullet simulation
    struct TraceData {
        Vector3 start;
        Vector3 end;
        Vector3 normal;
        float fraction;
        bool hit = false;
        int material = 0;
        float penetrationPower = 0;
        float damage = 0;
    };

    Autowall() = default;

    // Main FireBullet simulation
    AutowallResult FireBullet(Vector3 start, Vector3 end, int weaponId);

    // Get weapon penetration data
    struct WeaponPenetrationData {
        float damage;
        float penetrationPower;
        float range;
        float rangeModifier;
    };
    WeaponPenetrationData GetWeaponData(int weaponId);

private:
    static constexpr float PI = 3.14159265f;

    // Helper for penetration depth
    bool HandlePenetration(TraceData& trace, float& remainingPower, float& damage, float wallThickness);

    // Material penetration modifiers
    struct MaterialData {
        float penetrationModifier;
        float energyModifier;
    };
    MaterialData GetMaterialData(int material);

    // Calculate distance damage falloff
    float CalcDistanceDamage(float damage, float distance, float rangeModifier);

    // Bullet type definitions
    enum BulletType : int {
        BULLET_PENETRATE_NONE = -1,
        MATERIAL_GLASS = 0,
        MATERIAL_WOOD = 1,
        MATERIAL_METAL = 2,
        MATERIAL_CONCRETE = 3,
        MATERIAL_FLESH = 4,
        MATERIAL_DEFAULT = 5
    };
};
