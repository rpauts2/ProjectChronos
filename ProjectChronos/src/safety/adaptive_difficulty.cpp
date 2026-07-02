#include "adaptive_difficulty.h"
#include <sstream>
#include <algorithm>

void AdaptiveDifficulty::Update(const GameState& state) {
    // Update risk level each frame
    RecalculateRisk();
    ApplyModifiers();
}

void AdaptiveDifficulty::OnKill(bool headshot, int weaponId) {
    currentStats.kills++;
    if (headshot) currentStats.headshots++;
}

void AdaptiveDifficulty::OnDeath() {
    currentStats.deaths++;
}

void AdaptiveDifficulty::OnShot(bool hit) {
    currentStats.shots++;
    if (hit) currentStats.hits++;
}

void AdaptiveDifficulty::RecalculateRisk() {
    float risk = 0.0f;
    
    // High headshot rate = suspicious
    if (currentStats.HS_Rate() > 0.55f)
        risk += 0.3f;
    else if (currentStats.HS_Rate() > 0.45f)
        risk += 0.1f;
    
    // High accuracy = suspicious
    if (currentStats.Accuracy() > 0.40f)
        risk += 0.3f;
    else if (currentStats.Accuracy() > 0.30f)
        risk += 0.1f;
    
    // High KDR = suspicious
    if (currentStats.KDR() > 3.0f)
        risk += 0.3f;
    else if (currentStats.KDR() > 2.0f)
        risk += 0.1f;
    
    // Low deaths = less risk
    if (currentStats.deaths > 5)
        risk -= 0.1f;
    if (currentStats.deaths > 10)
        risk -= 0.1f;
    
    // Clamp
    currentRisk = std::max(0.0f, std::min(1.0f, risk));
}

void AdaptiveDifficulty::ApplyModifiers() {
    if (currentRisk > 0.5f) {
        // High risk: reduce help significantly
        modifiers.aimIntensity = 0.4f;
        modifiers.errorRate = 0.15f;
        modifiers.maxConsecutiveHits = 3;
    } else if (currentRisk > 0.3f) {
        // Medium risk: moderate reduction
        modifiers.aimIntensity = 0.6f;
        modifiers.errorRate = 0.10f;
        modifiers.maxConsecutiveHits = 4;
    } else {
        // Low risk: full help
        modifiers.aimIntensity = 0.85f;
        modifiers.errorRate = 0.05f;
        modifiers.maxConsecutiveHits = 5;
    }
}

bool AdaptiveDifficulty::ShouldForceBodyShot() {
    if (modifiers.forceBodyShots > 0) {
        modifiers.forceBodyShots--;
        return true;
    }
    return false;
}

void AdaptiveDifficulty::AdjustSolution(ExploitSolution& solution) {
    // Reduce confidence based on risk
    solution.confidence *= modifiers.aimIntensity;
    
    // Increase risk score based on current risk
    solution.riskScore += modifiers.errorRate;
    
    // If risk is very high, cap the exploits
    if (currentRisk > 0.7f) {
        // Only allow low-risk exploits
        if (solution.riskScore > 0.2f) {
            solution.confidence = 0;
        }
    }
}

std::string AdaptiveDifficulty::GetStatusString() const {
    std::stringstream ss;
    ss << "Risk: " << (int)(currentRisk * 100) << "%";
    ss << " | HS: " << (int)(currentStats.HS_Rate() * 100) << "%";
    ss << " | Acc: " << (int)(currentStats.Accuracy() * 100) << "%";
    ss << " | K/D: " << currentStats.KDR();
    return ss.str();
}