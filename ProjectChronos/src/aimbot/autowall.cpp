#include "autowall.h"
#include <cmath>
#include <algorithm>

Autowall::AutowallResult Autowall::FireBullet(Vector3 start, Vector3 end, int weaponId) {
    AutowallResult result;
    WeaponPenetrationData wp = GetWeaponData(weaponId);

    if (wp.damage <= 0) return result;

    float currentDamage = wp.damage;
    float currentPenetration = wp.penetrationPower;
    Vector3 currentPos = start;
    int penetrationCount = 0;
    const int MAX_PENETRATIONS = 4;

    // Simulate bullet travel with ray stepping
    Vector3 dir = end - start;
    float totalDist = dir.Length();
    Vector3 dirNorm = dir;
    dirNorm = dirNorm.Normalized();

    // Step through the bullet path (simplified)
    // Real implementation would use CS2's TraceLine with material detection
    float stepSize = 10.0f;
    float traveled = 0;

    while (traveled < totalDist && currentDamage > 0 && penetrationCount < MAX_PENETRATIONS) {
        Vector3 checkPos = currentPos + dirNorm * stepSize;
        traveled += stepSize;

        // Distance falloff
        currentDamage = CalcDistanceDamage(wp.damage, traveled, wp.rangeModifier);

        // Check if we've reached the end
        if (traveled >= totalDist) {
            result.penetrable = true;
            result.damage = currentDamage;
            result.hitPoint = end;
            return result;
        }

        currentPos = checkPos;
    }

    // If we got here, something blocked the bullet
    result.penetrable = false;
    result.damage = currentDamage;
    return result;
}

bool Autowall::HandlePenetration(TraceData& trace, float& remainingPower, float& damage, float wallThickness) {
    if (!trace.hit) return false;

    MaterialData mat = GetMaterialData(trace.material);
    float mod = mat.penetrationModifier;
    float energy = mat.energyModifier;

    // Calculate penetration loss
    float loss = (wallThickness * wallThickness * mod + wallThickness) / 50.0f;

    if (loss > remainingPower) {
        // Cannot penetrate this wall
        return false;
    }

    // Reduce damage based on wall material
    damage -= loss * energy;
    remainingPower -= loss;

    return damage > 0;
}

Autowall::WeaponPenetrationData Autowall::GetWeaponData(int weaponId) {
    WeaponPenetrationData wp = {0, 0, 0, 0};

    switch (weaponId) {
        case 1:  // Deagle
            wp = {53, 250, 4000, 0.81f}; break;
        case 2:  // Dual Berettas
            wp = {25, 150, 3000, 0.85f}; break;
        case 3:  // Five-SeveN
            wp = {24, 150, 3000, 0.85f}; break;
        case 4:  // Glock
            wp = {22, 100, 2500, 0.88f}; break;
        case 5:  // USP
            wp = {24, 150, 3000, 0.85f}; break;
        case 7:  // AK-47
            wp = {36, 250, 5000, 0.78f}; break;
        case 8:  // M4A4
            wp = {33, 230, 5000, 0.80f}; break;
        case 9:  // AWP
            wp = {115, 450, 7000, 0.71f}; break;
        case 10: // Scout
            wp = {80, 350, 6000, 0.73f}; break;
        case 13: // M4A4 (in case of enum mismatch)
            wp = {33, 230, 5000, 0.80f}; break;
        case 14: // FAMAS
            wp = {30, 200, 4500, 0.82f}; break;
        case 15: // Galil
            wp = {30, 210, 4500, 0.82f}; break;
        case 16: // M4A1-S
            wp = {31, 230, 5000, 0.80f}; break;
        case 17: // AUG
            wp = {28, 220, 5000, 0.80f}; break;
        case 19: // SG 553
            wp = {30, 220, 5000, 0.80f}; break;
        default:
            wp = {25, 150, 4000, 0.85f}; break;
    }

    return wp;
}

Autowall::MaterialData Autowall::GetMaterialData(int material) {
    switch (material) {
        case MATERIAL_GLASS:
            return {0.35f, 0.40f};
        case MATERIAL_WOOD:
            return {0.50f, 0.55f};
        case MATERIAL_METAL:
            return {0.70f, 0.75f};
        case MATERIAL_CONCRETE:
            return {0.90f, 0.85f};
        case MATERIAL_FLESH:
            return {0.10f, 0.15f};
        default:
            return {0.50f, 0.55f};
    }
}

float Autowall::CalcDistanceDamage(float damage, float distance, float rangeModifier) {
    if (distance <= 0) return damage;
    // CS2 distance falloff: damage *= rangeModifier^(distance / 500)
    float factor = powf(rangeModifier, distance / 500.0f);
    return damage * factor;
}
