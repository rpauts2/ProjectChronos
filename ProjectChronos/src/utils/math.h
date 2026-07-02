#pragma once
#include "core/types.h"
#include <algorithm>

class Math {
public:
    static constexpr float PI = 3.14159265f;
    
    static float RandomFloat(float min, float max);
    static int RandomInt(int min, int max);
    
    static QAngle CalcAngle(Vector3 from, Vector3 to);
    static float AngleDiff(QAngle a, QAngle b);
    static Vector3 AngleToDirection(QAngle angle);
    static float NormalizeAngle(float angle);
    
    static Vector3 Lerp(Vector3 a, Vector3 b, float t);
    static QAngle Lerp(QAngle a, QAngle b, float t);
    static float SmoothStep(float edge0, float edge1, float x);
};