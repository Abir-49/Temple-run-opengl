// =====================================================================
//  models.h  --  everything needed to turn meshes into pictures
//    * SceneProg / Mat / drawMesh : the draw call helper
//    * frustum culling            : skip objects that are off screen
//    * buildMeshes()              : builds every mesh once at start-up
// =====================================================================
#pragma once
#include "gfx.h"

// ---------------------------------------------------------------------
//  Scene shader program + cached uniform locations (looked up once)
// ---------------------------------------------------------------------
struct SceneProg {
    Program p;
    GLint uModel, uNormalMat, uViewProj, uColor, uUVScale, uShin, uSpec, uEmis, uUseTex, uWorldUV, uAlpha, uFlatten;
    void fetch() {
        uModel = p.loc("uModel");       uNormalMat = p.loc("uNormalMat"); uViewProj = p.loc("uViewProj");
        uColor = p.loc("uColor");       uUVScale = p.loc("uUVScale");     uShin = p.loc("uShin");
        uSpec = p.loc("uSpec");         uEmis = p.loc("uEmis");           uUseTex = p.loc("uUseTex");
        uWorldUV = p.loc("uWorldUV");   uAlpha = p.loc("uAlpha");         uFlatten = p.loc("uFlatten");
    }
};
inline SceneProg  gProgPhong, gProgGouraud;
inline SceneProg* gCur = &gProgPhong;
inline bool       gTexturesOn = true;
inline int        gDrawCalls = 0;

// A material = how one draw call should look
struct Mat {
    vec3  col = vec3(1.0f);
    int   tex = T_NONE;
    vec2  uv = vec2(1.0f);
    float shin = 24.0f;
    float spec = 0.10f;
    float emis = 0.0f;
    float alpha = 1.0f;
    bool  worldUV = false;
};

inline Mat mat(const vec3& c, int tex = T_NONE, vec2 uv = vec2(1.0f), float spec = 0.10f, float shin = 24.0f, float emis = 0.0f) {
    Mat m; m.col = c; m.tex = tex; m.uv = uv; m.spec = spec; m.shin = shin; m.emis = emis; return m;
}

inline GLuint gBoundTex = 0xFFFFFFFFu;

inline void drawMesh(const Mesh& me, const mat4& model, const Mat& m) {
    SceneProg& s = *gCur;
    glUniformMatrix4fv(s.uModel, 1, GL_FALSE, glm::value_ptr(model));
    mat3 nm = glm::transpose(glm::inverse(mat3(model)));
    glUniformMatrix3fv(s.uNormalMat, 1, GL_FALSE, glm::value_ptr(nm));
    glUniform3f(s.uColor, m.col.x, m.col.y, m.col.z);
    glUniform2f(s.uUVScale, m.uv.x, m.uv.y);
    glUniform1f(s.uShin, m.shin);
    glUniform1f(s.uSpec, m.spec);
    glUniform1f(s.uEmis, m.emis);
    glUniform1f(s.uAlpha, m.alpha);
    glUniform1i(s.uWorldUV, m.worldUV ? 1 : 0);
    bool useTex = gTexturesOn && m.tex != T_NONE;
    glUniform1i(s.uUseTex, useTex ? 1 : 0);
    if (useTex && gBoundTex != gTex[m.tex]) { glBindTexture(GL_TEXTURE_2D, gTex[m.tex]); gBoundTex = gTex[m.tex]; }
    glBindVertexArray(me.vao);
    glDrawArrays(GL_TRIANGLES, 0, me.count);
    gDrawCalls++;
}

// ---------------------------------------------------------------------
//  Frustum culling (sphere vs camera pyramid)
// ---------------------------------------------------------------------
struct CullInfo { mat4 view = mat4(1.0f); float tanX = 1, tanY = 1, farD = 300; };
inline CullInfo gCull;

inline bool inView(const vec3& p, float r) {
    vec4 v = gCull.view * vec4(p, 1.0f);
    float z = -v.z;
    if (z < -r) return false;
    if (z > gCull.farD + r) return false;
    if (std::fabs(v.x) > z * gCull.tanX + r * 1.5f) return false;
    if (std::fabs(v.y) > z * gCull.tanY + r * 1.5f) return false;
    return true;
}

// ---------------------------------------------------------------------
//  The mesh library
// ---------------------------------------------------------------------
struct Meshes {
    // unit primitives (scaled/rotated at draw time)
    Mesh cube, sphere, sphereLo, cyl, cone, pyramid, grid, ground;
    // composite models
    Mesh trunk, canopy, pine, bush, rockDec[3], rockObs[3], pillarFull, pillarBroken, statue,
         temple, gate, torch, coin, magnet, boost, shield, flame, ring, torus_unit;
};
inline Meshes G;

inline mat4 T(float x, float y, float z) { return glm::translate(mat4(1.0f), vec3(x, y, z)); }
inline mat4 T(const vec3& v)             { return glm::translate(mat4(1.0f), v); }
inline mat4 S(float x, float y, float z) { return glm::scale(mat4(1.0f), vec3(x, y, z)); }
inline mat4 S(float s)                   { return glm::scale(mat4(1.0f), vec3(s)); }
inline mat4 RX(float a) { return glm::rotate(mat4(1.0f), a, vec3(1, 0, 0)); }
inline mat4 RY(float a) { return glm::rotate(mat4(1.0f), a, vec3(0, 1, 0)); }
inline mat4 RZ(float a) { return glm::rotate(mat4(1.0f), a, vec3(0, 0, 1)); }

inline void buildMeshes() {
    auto flat = [](const vec3&) { return vec3(1.0f); };
    { MeshData m; addBox(m, mat4(1.0f), vec3(1)); G.cube = uploadMesh(m); }
    { MeshData m; addSphere(m, mat4(1.0f), vec3(1), 14, 22); G.sphere = uploadMesh(m); }
    { MeshData m; addSphere(m, mat4(1.0f), vec3(1), 8, 12); G.sphereLo = uploadMesh(m); }
    { MeshData m; addCyl(m, mat4(1.0f), vec3(1), 20); G.cyl = uploadMesh(m); }
    { MeshData m; addCyl(m, mat4(1.0f), vec3(1), 18, 1.0f, 0.0f, true, false); G.cone = uploadMesh(m); }
    { MeshData m; addCyl(m, RY(PI / 4), vec3(1), 4, 1.0f, 0.0f, true, false); G.pyramid = uploadMesh(m); }
    { MeshData m; addGrid(m, 8, 48, vec3(1)); G.grid = uploadMesh(m); }
    { MeshData m; addGrid(m, 72, 72, vec3(1)); G.ground = uploadMesh(m); }
    (void)flat;

    // ---- tree trunk (bark texture) ----
    {
        MeshData m;
        // tall tapered trunk 0..4.4, two branches reaching into the canopy, root flare at the base
        addCyl(m, S(1.0f, 4.4f, 1.0f), vec3(1), 12, 0.34f, 0.20f, true, false, 2.0f);
        addCyl(m, T(0, 3.2f, 0) * RZ(0.7f) * S(1.0f, 1.5f, 1.0f), vec3(1), 8, 0.12f, 0.07f, false, false);
        addCyl(m, T(0, 3.0f, 0) * RZ(-0.8f) * S(1.0f, 1.4f, 1.0f), vec3(1), 8, 0.12f, 0.07f, false, false);
        addCyl(m, T(0, -0.05f, 0) * S(1, 0.6f, 1), vec3(1), 12, 0.55f, 0.30f, false, false);   // root flare
        G.trunk = uploadMesh(m);
    }
    // ---- broad-leaf canopy (several bumpy blobs, slightly different greens) ----
    {
        MeshData m;
        addSphere(m, T(0, 4.5f, 0) * S(2.1f, 1.7f, 2.1f), vec3(0.95f, 1.05f, 0.9f), 12, 16, 0.16f, 1.0f);
        addSphere(m, T(1.2f, 3.8f, 0.6f) * S(1.5f), vec3(0.8f, 1.0f, 0.7f), 10, 14, 0.16f, 2.0f);
        addSphere(m, T(-1.2f, 3.9f, -0.5f) * S(1.55f), vec3(1.0f, 1.1f, 0.85f), 10, 14, 0.16f, 3.0f);
        addSphere(m, T(0.3f, 5.5f, -0.3f) * S(1.35f), vec3(1.1f, 1.2f, 0.9f), 10, 14, 0.16f, 4.0f);
        addSphere(m, T(-0.4f, 3.7f, 1.2f) * S(1.2f), vec3(0.75f, 0.95f, 0.65f), 10, 14, 0.16f, 5.0f);
        G.canopy = uploadMesh(m);
    }
    // ---- pine: trunk + stacked cones in ONE mesh (vertex colours: brown / greens) ----
    {
        MeshData m;
        addCyl(m, S(1, 2.2f, 1), vec3(0.45f, 0.30f, 0.18f), 10, 0.28f, 0.2f, true, false);
        for (int k = 0; k < 4; k++) {
            float r = 2.3f - 0.46f * k;
            vec3 col = glm::mix(vec3(0.55f, 0.85f, 0.55f), vec3(0.8f, 1.1f, 0.7f), k / 3.0f);
            addCyl(m, T(0, 1.4f + 1.25f * k, 0) * S(r, 2.1f, r), col, 14, 1.0f, 0.0f, true, false);
        }
        G.pine = uploadMesh(m);
    }
    // ---- bush ----
    {
        MeshData m;
        addSphere(m, T(0, 0.55f, 0) * S(0.9f, 0.7f, 0.9f), vec3(0.9f, 1.0f, 0.8f), 8, 12, 0.2f, 1.0f);
        addSphere(m, T(0.7f, 0.4f, 0.3f) * S(0.65f, 0.5f, 0.6f), vec3(0.75f, 0.95f, 0.7f), 8, 12, 0.2f, 2.0f);
        addSphere(m, T(-0.6f, 0.42f, -0.2f) * S(0.7f, 0.55f, 0.65f), vec3(1.0f, 1.1f, 0.8f), 8, 12, 0.2f, 3.0f);
        G.bush = uploadMesh(m);
    }
    // ---- rocks: low decorative boulders and tall obstacle boulders ----
    for (int i = 0; i < 3; i++) {
        MeshData a;
        addSphere(a, T(0, 0.45f, 0) * S(0.95f + 0.2f * i, 0.62f, 0.85f + 0.15f * i), vec3(1), 10, 14, 0.28f, 1.7f * i + 0.5f);
        G.rockDec[i] = uploadMesh(a);
        MeshData b;
        addSphere(b, T(0, 1.2f, 0) * S(0.95f, 1.3f, 0.9f), vec3(1), 14, 18, 0.2f, 2.3f * i + 1.1f);
        addSphere(b, T(0.55f, 0.5f, 0.2f) * S(0.55f), vec3(0.9f), 8, 12, 0.25f, 4.0f + i);
        G.rockObs[i] = uploadMesh(b);
    }
    // ---- ruin pillars ----
    for (int broken = 0; broken < 2; broken++) {
        MeshData m;
        float h = broken ? 1.9f : 3.4f;
        addBox(m, T(0, 0.2f, 0) * S(1.4f, 0.4f, 1.4f), vec3(1), vec2(1, 1));
        addCyl(m, T(0, 0.4f, 0) * S(0.5f, h, 0.5f), vec3(1), 14, 1.0f, 0.92f, true, !broken, 3.0f);
        if (!broken) addBox(m, T(0, 0.4f + h + 0.15f, 0) * S(1.15f, 0.3f, 1.15f), vec3(1));
        else         addBox(m, T(0.9f, 0.3f, 0.5f) * RY(0.7f) * S(0.9f, 0.6f, 0.8f), vec3(0.9f));   // fallen chunk
        (broken ? G.pillarBroken : G.pillarFull) = uploadMesh(m);
    }
    // ---- monkey idol statue ----
    {
        MeshData m;
        addBox(m, T(0, 0.4f, 0) * S(1.5f, 0.8f, 1.5f), vec3(1));
        addBox(m, T(0, 1.5f, 0) * S(0.95f, 1.3f, 0.7f), vec3(1));
        addSphere(m, T(0, 2.45f, -0.05f) * S(0.5f), vec3(1), 10, 14);
        addSphere(m, T(0.45f, 2.55f, -0.05f) * S(0.2f), vec3(1), 8, 10);
        addSphere(m, T(-0.45f, 2.55f, -0.05f) * S(0.2f), vec3(1), 8, 10);
        addBox(m, T(0.62f, 1.5f, 0) * S(0.28f, 1.1f, 0.3f), vec3(1));
        addBox(m, T(-0.62f, 1.5f, 0) * S(0.28f, 1.1f, 0.3f), vec3(1));
        addSphere(m, T(0, 2.4f, -0.45f) * S(0.22f, 0.16f, 0.2f), vec3(0.9f), 8, 10);   // muzzle
        G.statue = uploadMesh(m);
    }
    // ---- temple facade. Local origin = front-centre at ground; front faces +Z ----
    {
        MeshData m;
        vec3 w(1.0f);
        addBox(m, T(0, 0.3f, -0.5f) * S(11.5f, 0.6f, 7.0f), w, vec2(5, 1));                     // plinth
        for (int k = 0; k < 3; k++)                                                              // stairs
            addBox(m, T(0, 0.6f + 0.175f + 0.35f * k, 2.4f - 0.9f * k) * S(5.2f, 0.35f, 1.0f), w, vec2(3, 1));
        addBox(m, T(0, 4.1f, -1.5f) * S(8.4f, 7.0f, 3.0f), w, vec2(4, 3));                      // main block
        for (int s = -1; s <= 1; s += 2) {
            addBox(m, T(s * 5.4f, 1.9f, -0.6f) * S(3.2f, 2.6f, 2.6f), vec3(0.9f), vec2(2, 1));   // wings
            for (int b = 0; b < 3; b++)
                addBox(m, T(s * (4.2f + b * 1.15f), 3.4f, -0.6f) * S(0.7f, 0.5f, 0.7f), vec3(0.85f));
            addCyl(m, T(s * 3.4f, 0.6f, 0.8f) * S(0.55f, 6.4f, 0.55f), w, 16, 1.0f, 0.9f, true, true, 3.0f);   // pillars
            addBox(m, T(s * 3.4f, 0.75f, 0.8f) * S(1.3f, 0.3f, 1.3f), w);
            addBox(m, T(s * 3.4f, 7.15f, 0.8f) * S(1.4f, 0.35f, 1.4f), w);
        }
        addBox(m, T(0, 7.5f, 0.8f) * S(8.6f, 0.6f, 1.5f), vec3(0.95f), vec2(4, 1));            // lintel
        addBox(m, T(0, 2.9f, 0.02f) * S(2.4f, 3.9f, 0.12f), vec3(0.04f, 0.03f, 0.03f));       // doorway
        addTorus(m, T(0, 4.85f, 0.04f), 1.2f, 0.2f, PI, 18, 8, [](float) { return vec3(0.9f); });   // door arch
        addCyl(m, T(0, 7.8f, -1.5f) * RY(PI / 4) * S(7.4f, 3.4f, 7.4f), vec3(0.95f, 0.5f, 0.38f), 4, 1.0f, 0.0f, true, false);   // roof
        G.temple = uploadMesh(m);
    }
    // ---- decorative stone gate with a curved arch over the path ----
    {
        MeshData m;
        for (int s = -1; s <= 1; s += 2) {
            addBox(m, T(s * 3.9f, 0.25f, 0) * S(1.5f, 0.5f, 1.5f), vec3(1));
            addCyl(m, T(s * 3.9f, 0.5f, 0) * S(0.55f, 5.0f, 0.55f), vec3(1), 14, 1.0f, 0.92f, true, true, 3.0f);
        }
        addTorus(m, T(0, 5.4f, 0) * S(1.0f, 0.55f, 1.0f), 3.9f, 0.5f, PI, 36, 10, [](float) { return vec3(1.0f); });
        addBox(m, T(0, 7.55f, 0) * S(1.1f, 1.0f, 1.2f), vec3(1));                               // keystone
        G.gate = uploadMesh(m);
    }
    // ---- torch post ----
    {
        MeshData m;
        addCyl(m, S(0.09f, 1.9f, 0.09f), vec3(0.55f, 0.38f, 0.22f), 8);
        addCyl(m, T(0, 1.75f, 0) * S(1, 0.32f, 1), vec3(0.32f, 0.3f, 0.3f), 10, 0.12f, 0.28f, true, false);
        G.torch = uploadMesh(m);
    }
    // ---- flame (a stretched cone) ----
    {
        MeshData m;
        addCyl(m, S(1, 1, 1), vec3(1), 10, 1.0f, 0.0f, true, false);
        G.flame = uploadMesh(m);
    }
    // ---- flat ring lying on the ground (junction mark, power-up glow) ----
    {
        MeshData m;
        addTorus(m, RX(PI / 2), 1.0f, 0.045f, TAU, 48, 8, [](float) { return vec3(1.0f); });
        G.ring = uploadMesh(m);
    }
    // ---- upright ring facing +Z (blade rim) ----
    {
        MeshData m;
        addTorus(m, mat4(1.0f), 1.0f, 0.09f, TAU, 36, 8, [](float) { return vec3(1.0f); });
        G.torus_unit = uploadMesh(m);
    }
    // ---- coin: disc + rim ring + raised centre (face looks along +Z) ----
    {
        MeshData m;
        mat4 face = RX(PI / 2);
        addCyl(m, T(0, 0, -0.045f) * face * S(0.38f, 0.09f, 0.38f), vec3(1.0f, 0.78f, 0.14f), 20);
        addTorus(m, mat4(1.0f), 0.38f, 0.055f, TAU, 28, 8, [](float) { return vec3(0.95f, 0.62f, 0.08f); });
        addCyl(m, T(0, 0, -0.065f) * face * S(0.22f, 0.13f, 0.22f), vec3(1.0f, 0.92f, 0.45f), 16);
        G.coin = uploadMesh(m);
    }
    // ---- magnet: curved horseshoe (half torus) with silver tips ----
    {
        MeshData m;
        addTorus(m, T(0, -0.18f, 0), 0.32f, 0.105f, PI, 28, 12, [](float t) {
            return (t < 0.17f || t > 0.83f) ? vec3(0.85f, 0.87f, 0.92f) : vec3(0.88f, 0.1f, 0.1f);
        });
        G.magnet = uploadMesh(m);
    }
    // ---- speed boost: two stacked cones pointing up (arrow) ----
    {
        MeshData m;
        addCyl(m, T(0, -0.42f, 0) * S(0.3f, 0.5f, 0.3f), vec3(0.2f, 0.85f, 1.0f), 14, 1.0f, 0.0f, true, false);
        addCyl(m, T(0, -0.05f, 0) * S(0.3f, 0.5f, 0.3f), vec3(0.9f, 1.0f, 1.0f), 14, 1.0f, 0.0f, true, false);
        addCyl(m, T(0, 0.3f, 0) * S(0.3f, 0.5f, 0.3f), vec3(0.2f, 0.85f, 1.0f), 14, 1.0f, 0.0f, true, false);
        G.boost = uploadMesh(m);
    }
    // ---- shield: round shield (disc + gold rim + boss) ----
    {
        MeshData m;
        mat4 face = RX(PI / 2);
        addCyl(m, T(0, 0, -0.05f) * face * S(0.42f, 0.1f, 0.42f), vec3(0.2f, 0.45f, 0.95f), 24);
        addTorus(m, mat4(1.0f), 0.42f, 0.06f, TAU, 30, 8, [](float) { return vec3(1.0f, 0.78f, 0.2f); });
        addSphere(m, T(0, 0, 0.04f) * S(0.15f, 0.15f, 0.12f), vec3(1.0f, 0.85f, 0.3f), 8, 12);
        G.shield = uploadMesh(m);
    }
}
