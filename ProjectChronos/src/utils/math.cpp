#include "math.h"
#include <cstdlib>

float Math::RandomFloat(float min, float max) {
    return min + (rand() / (float)RAND_MAX) * (max - min);
}

int Math::RandomInt(int min, int max) {
    return min + rand() % (max - min + 1);
}

QAngle Math::CalcAngle(Vector3 from, Vector3 to) {
    Vector3 delta = to - from;
    float hyp = sqrtf(delta.x * delta.x + delta.y * delta.y);
    
    QAngle angle;
    angle.pitch = -atan2f(delta.z, hyp) * (180.0f / PI);
    angle.yaw = atan2f(delta.y, delta.x) * (180.0f / PI);
    angle.roll = 0;
    angle.Clamp();
    
    return angle;
}

float Math::AngleDiff(QAngle a, QAngle b) {
    float diff = sqrtf(powf(a.pitch - b.pitch, 2) + powf(a.yaw - b.yaw, 2));
    return diff;
}

Vector3 Math::AngleToDirection(QAngle angle) {
    float pitch = angle.pitch * PI / 180.0f;
    float yaw = angle.yaw * PI / 180.0f;
    
    return Vector3(
        -cosf(pitch) * cosf(yaw),
        cosf(pitch) * sinf(yaw),
        -sinf(pitch)
    );
}

float Math::NormalizeAngle(float angle) {
    while (angle > 180) angle -= 360;
    while (angle < -180) angle += 360;
    return angle;
}

Vector3 Math::Lerp(Vector3 a, Vector3 b, float t) {
    return a + (b - a) * t;
}

QAngle Math::Lerp(QAngle a, QAngle b, float t) {
    return QAngle(
        a.pitch + (b.pitch - a.pitch) * t,
        a.yaw + (b.yaw - a.yaw) * t,
        0
    );
}

float Math::SmoothStep(float edge0, float edge1, float x) {
    float t = (std::max)(0.0f, (std::min)(1.0f, (x - edge0) / (edge1 - edge0)));
    return t * t * (3 - 2 * t);
}