// =====================================================================
//  shaders.h  --  all GLSL source code lives here as C++ strings.
//
//  The SAME lighting code is used for Phong and Gouraud shading:
//     * Phong   : computeLighting() is called in the FRAGMENT shader
//                 (once per pixel, normal is interpolated)
//     * Gouraud : computeLighting() is called in the VERTEX shader
//                 (once per vertex, the resulting colour is interpolated)
//  The switch is a "#define GOURAUD" put in front of the source text.
// =====================================================================
#pragma once
#include <string>

// ---------------------------------------------------------------------
//  Lighting code shared by both stages
// ---------------------------------------------------------------------
static const char* GLSL_LIGHTING = R"GLSL(
#define MAX_POINT 8
#define MAX_SPOT  2
#define MAX_OCC   32

uniform vec3  uEyePos;
uniform vec3  uAmbSky;        // hemisphere ambient light: colour from above
uniform vec3  uAmbGround;     // ... and from below
uniform vec3  uSunDir;        // DIRECTIONAL light: direction TOWARD the light (sun or moon)
uniform vec3  uSunCol;
uniform int   uUseSun;
uniform int   uUsePoint;
uniform int   uUseSpot;
uniform int   uNumPoint;      // POINT lights (torches, fire, lava ...)
uniform vec3  uPointPos[MAX_POINT];
uniform vec3  uPointCol[MAX_POINT];
uniform int   uNumSpot;       // SPOT lights (head lamp, junction lamp)
uniform vec3  uSpotPos[MAX_SPOT];
uniform vec3  uSpotDir[MAX_SPOT];
uniform vec3  uSpotCol[MAX_SPOT];
uniform vec2  uSpotCone[MAX_SPOT];   // (cos inner angle, cos outer angle)
uniform float uShin;          // material shininess
uniform float uSpec;          // material specular strength

// Phong reflection model:  ambient + diffuse (N.L) + specular (R.V)^shininess
// The sun term is returned separately because ray traced shadows only
// affect the sun / moon light.
void computeLighting(vec3 P, vec3 N, vec3 V,
                     out vec3 diff, out vec3 sunD, out vec3 spec, out vec3 sunS)
{
    diff = mix(uAmbGround, uAmbSky, N.y * 0.5 + 0.5);
    sunD = vec3(0.0);
    sunS = vec3(0.0);
    spec = vec3(0.0);

    if (uUseSun == 1) {
        float ndl = max(dot(N, uSunDir), 0.0);
        sunD = uSunCol * ndl;
        if (ndl > 0.0) {
            vec3 R = reflect(-uSunDir, N);
            sunS = uSunCol * (pow(max(dot(R, V), 0.0), uShin) * uSpec);
        }
    }
    if (uUsePoint == 1) {
        for (int i = 0; i < uNumPoint; i++) {
            vec3 Lv = uPointPos[i] - P;
            float d = length(Lv);
            vec3 L = Lv / d;
            float ndl = max(dot(N, L), 0.0);
            float att = 1.0 / (1.0 + 0.10 * d + 0.05 * d * d);       // distance falloff
            diff += uPointCol[i] * (ndl * att);
            if (ndl > 0.0) {
                vec3 R = reflect(-L, N);
                spec += uPointCol[i] * (pow(max(dot(R, V), 0.0), uShin) * uSpec * att);
            }
        }
    }
    if (uUseSpot == 1) {
        for (int i = 0; i < uNumSpot; i++) {
            vec3 Lv = uSpotPos[i] - P;
            float d = length(Lv);
            vec3 L = Lv / d;
            float cd = dot(-L, uSpotDir[i]);                         // angle to the cone axis
            float cone = smoothstep(uSpotCone[i].y, uSpotCone[i].x, cd);
            float ndl = max(dot(N, L), 0.0);
            float att = 1.0 / (1.0 + 0.04 * d + 0.006 * d * d);
            diff += uSpotCol[i] * (ndl * att * cone);
            if (ndl > 0.0 && cone > 0.0) {
                vec3 R = reflect(-L, N);
                spec += uSpotCol[i] * (pow(max(dot(R, V), 0.0), uShin) * uSpec * att * cone);
            }
        }
    }
}
)GLSL";

// ---------------------------------------------------------------------
//  Scene vertex shader
// ---------------------------------------------------------------------
static const char* SCENE_VS = R"GLSL(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aColor;

uniform mat4  uModel;
uniform mat3  uNormalMat;
uniform mat4  uViewProj;
uniform vec2  uUVScale;
uniform int   uWorldUV;      // 1 = texture coordinates come from world position (endless ground)
uniform float uFlatten;      // 1 = push vertex to the far plane (used to "punch" pits)

out vec3 vPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vColor;
#ifdef GOURAUD
out vec3 vDiff;
out vec3 vSunD;
out vec3 vSpec;
out vec3 vSunS;
#endif

void main()
{
    vec4 wp = uModel * vec4(aPos, 1.0);
    vPos    = wp.xyz;
    vNormal = normalize(uNormalMat * aNormal);
    vUV     = (uWorldUV == 1) ? wp.xz * uUVScale : aUV * uUVScale;
    vColor  = aColor;
    gl_Position = uViewProj * wp;
    if (uFlatten > 0.5) gl_Position.z = gl_Position.w;
#ifdef GOURAUD
    // Gouraud shading: light is evaluated per VERTEX here
    computeLighting(wp.xyz, vNormal, normalize(uEyePos - wp.xyz), vDiff, vSunD, vSpec, vSunS);
#endif
}
)GLSL";

// ---------------------------------------------------------------------
//  Scene fragment shader (also contains the ray traced shadows + fog)
// ---------------------------------------------------------------------
static const char* SCENE_FS = R"GLSL(
in vec3 vPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vColor;
#ifdef GOURAUD
in vec3 vDiff;
in vec3 vSunD;
in vec3 vSpec;
in vec3 vSunS;
#endif
out vec4 FragColor;

uniform sampler2D uTex;
uniform int   uUseTex;
uniform vec3  uColor;
uniform float uEmis;         // 0..1 : how much the object glows by itself
uniform float uAlpha;

uniform int   uFogOn;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

// ---- ray traced shadows ----------------------------------------------
// For every pixel we shoot a ray toward the sun/moon and test it against
// a handful of spheres that approximate trees, rocks, the player ...
uniform int  uShadowOn;
uniform int  uNumOcc;
uniform vec4 uOcc[MAX_OCC];      // xyz = centre, w = radius

float shadowRay(vec3 P, vec3 L)
{
    float lit = 1.0;
    for (int i = 0; i < uNumOcc; i++) {
        vec3 oc = uOcc[i].xyz - P;
        float r = uOcc[i].w;
        float t = dot(oc, L);                    // distance along the ray to the closest point
        if (t > 0.0) {
            float d2 = dot(oc, oc) - t * t;      // squared distance ray <-> sphere centre
            float r2 = r * r;
            if (d2 < r2) lit = min(lit, smoothstep(0.25 * r2, r2, d2));   // soft edge
        }
    }
    return lit;
}

void main()
{
    vec3 N = normalize(vNormal);
    vec4 tex = (uUseTex == 1) ? texture(uTex, vUV) : vec4(1.0);
    vec3 albedo = tex.rgb * vColor * uColor;

    vec3 diff, sunD, spec, sunS;
#ifdef GOURAUD
    diff = vDiff; sunD = vSunD; spec = vSpec; sunS = vSunS;       // interpolated from the vertices
#else
    computeLighting(vPos, N, normalize(uEyePos - vPos), diff, sunD, spec, sunS);   // per pixel
#endif

    float sh = 1.0;
    if (uShadowOn == 1 && uUseSun == 1) sh = shadowRay(vPos, uSunDir);

    vec3 col = albedo * (diff + sunD * sh) + (spec + sunS * sh);
    col += albedo * uEmis;

    if (uFogOn == 1) {
        float d = length(vPos - uEyePos);
        float f = smoothstep(uFogStart, uFogEnd, d);
        col = mix(col, uFogColor, f);
    }
    FragColor = vec4(col, uAlpha);
}
)GLSL";

// ---------------------------------------------------------------------
//  Sky: one full-screen triangle, colour computed from the view direction
// ---------------------------------------------------------------------
static const char* SKY_VS = R"GLSL(
#version 330 core
out vec2 vNdc;
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vNdc = p * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0);          // z = w  -> far plane
}
)GLSL";

static const char* SKY_FS = R"GLSL(
#version 330 core
in vec2 vNdc;
out vec4 FragColor;

uniform mat4  uInvViewProj;
uniform vec3  uEyePos;
uniform vec3  uSunDir;       // direction toward the sun
uniform vec3  uZenith;
uniform vec3  uHorizon;
uniform float uNight;        // 0 = day, 1 = deep night
uniform float uTime;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(vec3(i, 1.0)), b = hash(vec3(i + vec2(1, 0), 1.0));
    float c = hash(vec3(i + vec2(0, 1), 1.0)), d = hash(vec3(i + vec2(1, 1), 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float fbm2(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { s += a * valueNoise(p); p *= 2.03; a *= 0.5; }
    return s;
}

void main()
{
    vec4 farPt = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(farPt.xyz / farPt.w - uEyePos);
    float h = dir.y;

    vec3 col = mix(uHorizon, uZenith, pow(clamp(h, 0.0, 1.0), 0.55));
    if (h < 0.0) col = mix(uHorizon, uHorizon * 0.55, clamp(-h * 5.0, 0.0, 1.0));

    // sun
    float sd = max(dot(dir, uSunDir), 0.0);
    float sunVis = smoothstep(-0.10, 0.05, uSunDir.y);
    vec3 sunCol = mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.95, 0.8), smoothstep(0.0, 0.5, uSunDir.y));
    col += sunCol * (smoothstep(0.9991, 0.9997, sd) * 2.0 + pow(sd, 48.0) * 0.55 + pow(sd, 6.0) * 0.12) * sunVis;

    // moon
    float md = max(dot(dir, -uSunDir), 0.0);
    float moonVis = smoothstep(0.05, -0.10, uSunDir.y);
    col += vec3(0.85, 0.9, 1.0) * smoothstep(0.9993, 0.9996, md) * moonVis * 1.2;
    col += vec3(0.25, 0.3, 0.5) * pow(md, 40.0) * moonVis * 0.25;

    // stars
    if (h > 0.0) {
        vec3 g = floor(dir * 260.0);
        float s = step(0.9975, hash(g));
        float tw = 0.6 + 0.4 * sin(uTime * 3.0 + hash(g + 3.0) * 40.0);
        col += vec3(1.0) * s * tw * uNight * smoothstep(0.0, 0.15, h);
    }

    // clouds (drift slowly)
    if (h > 0.02) {
        vec2 cuv = dir.xz / (h + 0.25) * 1.6 + vec2(uTime * 0.012, 0.0);
        float c = fbm2(cuv);
        float cm = smoothstep(0.52, 0.78, c) * smoothstep(0.02, 0.2, h);
        vec3 cloudCol = mix(vec3(0.95, 0.95, 1.0), vec3(0.12, 0.14, 0.22), uNight);
        cloudCol = mix(cloudCol, uHorizon * 1.2 + 0.15, 0.35 * (1.0 - uNight));
        col = mix(col, cloudCol, cm * 0.75);
    }
    FragColor = vec4(col, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------
//  2D HUD (positions are in pixels)
// ---------------------------------------------------------------------
static const char* HUD_VS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aCol;
uniform vec2 uScreen;
out vec4 vCol;
void main()
{
    vCol = aCol;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)GLSL";

static const char* HUD_FS = R"GLSL(
#version 330 core
in vec4 vCol;
out vec4 FragColor;
void main() { FragColor = vCol; }
)GLSL";

// Builds the final source for the scene shaders. `gouraud` decides where
// the lighting is evaluated.
inline std::string sceneVertexSource(bool gouraud) {
    return std::string("#version 330 core\n") + (gouraud ? "#define GOURAUD\n" : "") + GLSL_LIGHTING + SCENE_VS;
}
inline std::string sceneFragmentSource(bool gouraud) {
    return std::string("#version 330 core\n") + (gouraud ? "#define GOURAUD\n" : "") + GLSL_LIGHTING + SCENE_FS;
}