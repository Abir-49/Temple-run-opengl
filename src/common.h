// =====================================================================
//  common.h  --  shared includes, tiny math helpers and a random number
//  generator used by every other file.
// =====================================================================
#pragma once

#include <glad/glad.h>     // must come BEFORE glfw3.h
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::mat3;
using glm::mat4;

constexpr float PI  = 3.14159265358979f;
constexpr float TAU = 6.28318530717959f;

// ---- tiny xorshift random generator (deterministic, fast) -----------
inline uint32_t& rngState() { static uint32_t s = 2463534242u; return s; }
inline float frand() {                       // uniform in [0,1)
    uint32_t& x = rngState();
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (x >> 8) / 16777216.0f;
}
inline float frange(float a, float b) { return a + (b - a) * frand(); }
inline int   irange(int a, int b) {          // inclusive both ends
    int v = a + (int)(frand() * (b - a + 1));
    return v > b ? b : v;
}
inline bool chance(float p) { return frand() < p; }

// ---- small math helpers ---------------------------------------------
inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float lerpf(float a, float b, float t)  { return a + (b - a) * t; }
inline float smoothf(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
// shortest signed difference between two angles (radians)
inline float angleDiff(float from, float to) {
    float d = std::fmod(to - from + PI, TAU);
    if (d < 0) d += TAU;
    return d - PI;
}
// move `cur` toward `target` by at most `maxDelta`
inline float approach(float cur, float target, float maxDelta) {
    if (cur < target) return std::min(cur + maxDelta, target);
    return std::max(cur - maxDelta, target);
}
// exponential smoothing that is frame-rate independent
inline float damp(float cur, float target, float rate, float dt) {
    return lerpf(cur, target, 1.0f - std::exp(-rate * dt));
}

// direction / right vector of a heading angle (yaw = 0 looks down -Z,
// positive yaw turns LEFT). Used everywhere the world is not axis aligned.
inline vec3 dirFromYaw(float yaw)   { return vec3(-std::sin(yaw), 0.0f, -std::cos(yaw)); }
inline vec3 rightFromYaw(float yaw) { return vec3( std::cos(yaw), 0.0f, -std::sin(yaw)); }
