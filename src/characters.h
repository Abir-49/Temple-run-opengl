// =====================================================================
//  characters.h  --  the runner and the monkey
//  Both are built from primitives with a hierarchy of joints:
//      root -> hip -> knee      root -> shoulder -> elbow
//  Each joint is a matrix that rotates around X (swinging forward /
//  backward).  The "Pose" struct blends between running, jumping,
//  sliding, stumbling, tumbling and lying poses.
// =====================================================================
#pragma once
#include "scene.h"

struct Pose {
    float phase = 0;       // run cycle angle (radians)
    float air = 0;         // 0..1  jump pose
    float slide = 0;       // 0..1  slide pose
    float stumble = 0;     // 0..1  tripping
    float lie = 0;         // 0..1  lying face down (caught)
    float spin = 0;        // tumble angle (falling into a pit)
    float lunge = 0;       // 0..1  monkey reaching with both arms
    float t = 0;           // animation time (tail wave ...)
};

inline mat4 jointRoot(const mat4& base, float pivotY, float dropY, float pitch) {
    return base * T(0, -dropY, 0) * T(0, pivotY, 0) * RX(pitch) * T(0, -pivotY, 0);
}
inline void limbDown(const mat4& joint, float len, float rad, const Mat& m) {
    drawMesh(G.cyl, joint * T(0, -len, 0) * S(rad, len, rad), m);
}

// ----------------------------------------------------------------------
inline void drawPlayer(const mat4& base, const Pose& p) {
    float sw = std::sin(p.phase);
    float run = clampf(1.0f - p.air - p.slide - p.lie - p.stumble * 0.5f, 0.0f, 1.0f);

    // joint angles for each pose, blended together
    float thL = sw * 0.85f, thR = -sw * 0.85f;
    float knL = -(0.55f + 0.55f * std::sin(p.phase + 0.9f)), knR = -(0.55f + 0.55f * std::sin(p.phase + 0.9f + PI));
    float arL = -sw * 0.9f, arR = sw * 0.9f;
    float elL = 1.0f + 0.25f * sw, elR = 1.0f - 0.25f * sw;
    auto blend = [&](float& v, float target, float w) { v = lerpf(v, target, clampf(w, 0, 1)); };
    blend(thL, 1.0f, p.air);   blend(thR, 0.45f, p.air);   blend(knL, -1.2f, p.air);  blend(knR, -0.5f, p.air);
    blend(arL, 2.3f, p.air);   blend(arR, 2.0f, p.air);    blend(elL, 0.3f, p.air);   blend(elR, 0.3f, p.air);
    blend(thL, 1.45f, p.slide); blend(thR, 1.4f, p.slide); blend(knL, -0.1f, p.slide); blend(knR, -0.1f, p.slide);
    blend(arL, 1.5f, p.slide);  blend(arR, 1.3f, p.slide); blend(elL, 0.4f, p.slide);  blend(elR, 0.4f, p.slide);
    float fl = std::sin(p.t * 22.0f);
    blend(arL, -1.6f + 0.5f * fl, p.stumble); blend(arR, -1.4f - 0.5f * fl, p.stumble);
    blend(thL, 0.5f, p.stumble * 0.6f);       blend(knL, -0.9f, p.stumble * 0.6f);
    blend(thL, 0.2f, p.lie); blend(thR, -0.2f, p.lie); blend(arL, 2.4f, p.lie); blend(arR, 2.4f, p.lie);

    float pitch = -0.14f * run + p.slide * 1.35f - p.stumble * 0.55f - p.lie * 1.45f + p.spin;
    float drop = p.slide * 0.55f + p.lie * 0.55f;
    float bob = run * std::fabs(std::cos(p.phase)) * 0.07f;
    mat4 root = jointRoot(base * T(0, bob, 0), 0.92f, drop, pitch);

    Mat shirt = mat(vec3(0.10f, 0.50f, 0.58f), T_NONE, vec2(1), 0.08f);
    Mat pants = mat(vec3(0.38f, 0.27f, 0.16f), T_NONE, vec2(1), 0.05f);
    Mat skin  = mat(vec3(0.92f, 0.68f, 0.52f), T_NONE, vec2(1), 0.10f, 30.0f);
    Mat hat   = mat(vec3(0.80f, 0.63f, 0.34f), T_NONE, vec2(1), 0.05f);
    Mat boot  = mat(vec3(0.20f, 0.12f, 0.08f), T_NONE, vec2(1), 0.20f);
    Mat pack  = mat(vec3(0.58f, 0.22f, 0.12f), T_NONE, vec2(1), 0.05f);
    Mat dark  = mat(vec3(0.03f), T_NONE, vec2(1), 0.0f);

    drawMesh(G.cube,   root * T(0, 1.24f, 0) * S(0.50f, 0.66f, 0.30f), shirt);                // torso
    drawMesh(G.cube,   root * T(0, 0.93f, 0) * S(0.46f, 0.20f, 0.28f), pants);                // pelvis
    drawMesh(G.sphere, root * T(0, 1.78f, 0) * S(0.21f), skin);                               // head
    drawMesh(G.cyl,    root * T(0, 1.89f, 0) * S(0.36f, 0.035f, 0.36f), hat);                 // hat brim
    drawMesh(G.cyl,    root * T(0, 1.90f, 0) * S(0.215f, 0.16f, 0.215f), hat);                // hat crown
    drawMesh(G.sphereLo, root * T(-0.075f, 1.80f, -0.19f) * S(0.03f), dark);                  // eyes
    drawMesh(G.sphereLo, root * T(0.075f, 1.80f, -0.19f) * S(0.03f), dark);
    drawMesh(G.cube,   root * T(0, 1.28f, 0.24f) * S(0.36f, 0.46f, 0.18f), pack);             // backpack

    for (int s = -1; s <= 1; s += 2) {
        float th = s < 0 ? thL : thR, kn = s < 0 ? knL : knR;
        mat4 hip = root * T(0.13f * s, 0.90f, 0) * RX(th);
        limbDown(hip, 0.46f, 0.095f, pants);
        mat4 knee = hip * T(0, -0.46f, 0) * RX(kn);
        limbDown(knee, 0.46f, 0.08f, pants);
        drawMesh(G.cube, knee * T(0, -0.47f, -0.07f) * S(0.15f, 0.09f, 0.30f), boot);
        float ar = s < 0 ? arL : arR, el = s < 0 ? elL : elR;
        mat4 sh = root * T(0.33f * s, 1.50f, 0) * RX(ar);
        limbDown(sh, 0.34f, 0.07f, shirt);
        mat4 elbow = sh * T(0, -0.34f, 0) * RX(el);
        limbDown(elbow, 0.32f, 0.06f, skin);
        drawMesh(G.sphereLo, elbow * T(0, -0.34f, 0) * S(0.075f), skin);
    }
}

// ----------------------------------------------------------------------
inline void drawMonkey(const mat4& base, const Pose& p) {
    float sw = std::sin(p.phase);
    float thL = sw * 0.9f, thR = -sw * 0.9f;
    float knL = -(0.7f + 0.6f * std::sin(p.phase + 1.0f)), knR = -(0.7f + 0.6f * std::sin(p.phase + 1.0f + PI));
    float arL = -sw * 1.1f - 0.3f, arR = sw * 1.1f - 0.3f;
    float elL = 0.5f + 0.3f * sw, elR = 0.5f - 0.3f * sw;
    auto blend = [&](float& v, float target, float w) { v = lerpf(v, target, clampf(w, 0, 1)); };
    blend(arL, 2.6f, p.lunge); blend(arR, 2.5f, p.lunge); blend(elL, 0.2f, p.lunge); blend(elR, 0.2f, p.lunge);
    blend(thL, 0.2f, p.lie); blend(thR, -0.2f, p.lie);

    float run = 1.0f - p.lunge;
    float bob = run * std::fabs(std::cos(p.phase)) * 0.09f;
    float pitch = -0.42f - p.lunge * 0.25f;                      // hunched forward
    mat4 root = jointRoot(base * T(0, bob, 0), 0.85f, 0.0f, pitch);

    Mat fur   = mat(vec3(0.34f, 0.20f, 0.10f), T_NONE, vec2(1), 0.04f);
    Mat furD  = mat(vec3(0.22f, 0.12f, 0.06f), T_NONE, vec2(1), 0.04f);
    Mat face  = mat(vec3(0.86f, 0.66f, 0.52f), T_NONE, vec2(1), 0.08f, 30.0f);
    Mat belly = mat(vec3(0.62f, 0.45f, 0.30f), T_NONE, vec2(1), 0.04f);
    Mat eye   = mat(vec3(1.0f, 0.1f, 0.05f), T_NONE, vec2(1), 0.0f, 8.0f, 1.0f);       // glowing red eyes
    Mat teeth = mat(vec3(0.97f), T_NONE, vec2(1), 0.2f);
    Mat dark  = mat(vec3(0.05f), T_NONE, vec2(1), 0.0f);

    drawMesh(G.cube,   root * T(0, 1.05f, 0) * S(0.66f, 0.70f, 0.44f), fur);                  // torso
    drawMesh(G.cube,   root * T(0, 1.02f, -0.12f) * S(0.46f, 0.52f, 0.30f), belly);           // belly patch
    drawMesh(G.cube,   root * T(0, 0.72f, 0) * S(0.52f, 0.24f, 0.36f), furD);                 // hips
    drawMesh(G.sphere, root * T(0, 1.62f, -0.18f) * S(0.30f, 0.28f, 0.28f), fur);             // head
    drawMesh(G.sphere, root * T(0, 1.52f, -0.42f) * S(0.17f, 0.13f, 0.15f), face);            // muzzle
    drawMesh(G.sphere, root * T(0, 1.60f, -0.37f) * S(0.22f, 0.15f, 0.12f), face);            // face mask
    for (int s = -1; s <= 1; s += 2) {
        drawMesh(G.sphereLo, root * T(0.11f * s, 1.66f, -0.40f) * S(0.065f), eye);            // eyes
        drawMesh(G.cube, root * T(0.11f * s, 1.74f, -0.41f) * RZ(-0.5f * s) * S(0.14f, 0.035f, 0.05f), dark);   // angry brows
        drawMesh(G.sphere, root * T(0.31f * s, 1.70f, -0.12f) * S(0.13f, 0.13f, 0.06f), furD);    // ears
        drawMesh(G.sphereLo, root * T(0.31f * s, 1.70f, -0.17f) * S(0.075f, 0.075f, 0.03f), face);
        drawMesh(G.cone, root * T(0.05f * s, 1.45f, -0.50f) * RX(PI) * S(0.025f, 0.07f, 0.025f), teeth);   // fangs
    }
    drawMesh(G.cube, root * T(0, 1.45f, -0.46f) * S(0.15f, 0.025f, 0.05f), dark);              // mouth

    for (int s = -1; s <= 1; s += 2) {                                                        // legs
        float th = s < 0 ? thL : thR, kn = s < 0 ? knL : knR;
        mat4 hip = root * T(0.17f * s, 0.70f, 0) * RX(th);
        limbDown(hip, 0.40f, 0.10f, furD);
        mat4 knee = hip * T(0, -0.40f, 0) * RX(kn);
        limbDown(knee, 0.40f, 0.085f, furD);
        drawMesh(G.cube, knee * T(0, -0.41f, -0.06f) * S(0.15f, 0.07f, 0.26f), face);
        float ar = s < 0 ? arL : arR, el = s < 0 ? elL : elR;                                 // long arms
        mat4 sh = root * T(0.40f * s, 1.32f, 0) * RX(ar);
        limbDown(sh, 0.52f, 0.085f, fur);
        mat4 elbow = sh * T(0, -0.52f, 0) * RX(el);
        limbDown(elbow, 0.50f, 0.075f, fur);
        drawMesh(G.sphereLo, elbow * T(0, -0.52f, 0) * S(0.095f), face);                      // hands
    }
    for (int i = 0; i < 7; i++) {                                                             // curly tail
        float u = i / 6.0f;
        vec3 tp(std::sin(p.t * 7.0f + u * 3.0f) * 0.12f * u, 0.78f + 0.62f * u * u + 0.1f * u, 0.28f + 0.33f * u);
        drawMesh(G.sphereLo, root * T(tp) * S(0.075f - 0.03f * u), furD);
    }
}
