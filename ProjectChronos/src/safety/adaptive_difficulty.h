#pragma once
#include "core/types.h"
#include <deque>
#include <map>

// ==================== ADAPTIVE DIFFICULTY ====================
// Keeps player statistics in VACNET's "green zone"
// Automatically reduces help if stats look suspicious

class AdaptiveDifficulty {
    struct MatchStats {
        int kills = 0;
        int deaths = 0;
        int headshots = 0;
        int shots = 0;
        int hits = 0;
        int trades = 0;
        
        float HS_Rate() const { return kills > 0 ? (float)headshots / kills : 0; }
        float Accuracy() const { return shots > 0 ? (float)hits / shots : 0; }
        float KDR() const { return deaths > 0 ? (float)kills / deaths : 999; }
        
        void Reset() { kills = deaths = headshots = shots = hits = trades = 0; }
    };
    
    MatchStats currentStats;
    std::deque<MatchStats> matchHistory; // last 10 matches
    
    // Current multipliers
    struct {
        float aimIntensity = 1.0f;
        float errorRate = 0.05f;
        float reactionDelay = 0.0f;
        int maxConsecutiveHits = 5;
        int forceBodyShots = 0; // remaining forced body shots
    } modifiers;
    
    // Risk level (0.0 - 1.0)
    float currentRisk = 0.0f;
    
public:
    AdaptiveDifficulty() {}
    
    void Update(const GameState& state);
    void OnKill(bool headshot, int weaponId);
    void OnDeath();
    void OnShot(bool hit);
    
    // Get current modifiers
    float GetAimIntensity() const { return modifiers.aimIntensity; }
    float GetErrorRate() const { return modifiers.errorRate; }
    float GetCurrentRisk() const { return currentRisk; }
    bool ShouldForceBodyShot();
    
    // Adjust an exploit solution's parameters
    void AdjustSolution(ExploitSolution& solution);
    
    // Status
    std::string GetStatusString() const;
    
private:
    void RecalculateRisk();
    void ApplyModifiers();
};