// =====================================================================
//  TEMPLE RUN STYLE 3D GAME  --  Computer Graphics course project
//  C++17 / OpenGL 3.3 core / GLFW / GLAD / GLM
//
//  File map
//    common.h      math helpers, random numbers
//    gfx.h         shader wrapper, mesh builders, procedural textures
//    shaders.h     all GLSL code (Phong / Gouraud / sky / HUD)
//    models.h      mesh library (trees, temple, coin ...) + draw helper
//    world.h       endless level: segments, forks, obstacles, coins
//    scene.h       drawing of the level
//    characters.h  runner + monkey models and animation
//    hud.h         2D text/bars
//    main.cpp      game rules, camera, lights, input, main loop  (this file)
// =====================================================================
#include "characters.h"
#include "hud.h"
#include <cstring>

// ---------------------------------------------------------------------
//  Tuning constants
// ---------------------------------------------------------------------
constexpr float BASE_SPEED = 9.5f, MAX_SPEED = 21.0f, SPEED_RAMP = 0.07f;   // units/s, units/s^2
constexpr float BOOST_MULT = 1.6f;
constexpr float GAP_NORMAL = 4.2f, GAP_CLOSE = 1.8f, CAUGHT_DIST = 0.9f;    // monkey distance behind the runner
constexpr float STUMBLE_WINDOW = 8.0f;                                      // seconds: 2nd stumble = caught
constexpr float MAGNET_TIME = 9.0f, BOOST_TIME = 5.0f, SHIELD_TIME = 15.0f, MAGNET_RADIUS = 10.0f;
constexpr float GRAVITY = 24.0f, JUMP_V = 10.0f, SLIDE_TIME = 0.8f;
constexpr float DAY_LENGTH = 130.0f;                                        // seconds for a full day+night

// ---------------------------------------------------------------------
//  Global state
// ---------------------------------------------------------------------
GLFWwindow* gWin = nullptr;
int   gFbW = 1280, gFbH = 720;
Hud   gHud;
GLuint gSkyVao = 0;
Program gSky;

// time
float gAnim = 0.0f;                  // real time (cosmetic animation keeps running while paused)
float gGame = 0.0f;                  // gameplay time (frozen while paused)
float gDayPhase = 0.16f;             // 0 = sunrise, 0.25 = noon, 0.5 = sunset, 0.75 = midnight

// switches (keys)
bool gPaused = false, gShowHelp = true;
bool gGouraud = false;
bool gUseSun = true, gUsePoint = true, gUseSpot = true, gShadows = true, gFog = true;

// ---- game ----
enum GState { GS_PLAY, GS_DYING, GS_OVER };
enum Cause  { C_CAUGHT, C_FELL, C_WALL };
GState gState = GS_PLAY;
Cause  gCause = C_CAUGHT;
float  gDyingT = 0.0f;
float  gOrbit = 0.0f;                // camera swings around the runner when caught

SegPtr pSeg, pPrev;                  // segment the runner is on, and the one just left
float pS = 2.0f, pLat = 0.0f, pY = 0.0f, pVy = 0.0f;
int   pLane = 1;
bool  pGround = true, pFalling = false;
float pSlideT = 0.0f, pPhase = 0.0f, pSpeed = BASE_SPEED;
int   pTurnPending = 0;
float pFacing = 0.0f;                // smoothed visual heading
float pAirW = 0, pSlideW = 0, pStumbleW = 0, pLieW = 0, pSpin = 0;
float pStumbleSlow = 0.0f, pInvuln = 0.0f;
float gRunTime = 0.0f, gDist = 0.0f;
int   gCoins = 0;

float mGap = GAP_NORMAL, mLat = 0.0f, mPhase = 0.0f, mYaw = 0.0f, mLunge = 0.0f;
float gStumbleTimer = 0.0f;          // > 0 : monkey is close, one more stumble = game over
float gMagnetT = 0, gBoostT = 0, gShieldT = 0;
float gShake = 0.0f;
int   gBestScore = 0;

bool  gGod = false;                  // test builds only

// ---- cameras ----
float camYaw = 0.0f, camLat = 0.0f, camJump = 0.0f, camFov = 60.0f;
vec3  gFreePos(0, 5, 10);
float gFreeYaw = 0.0f, gFreePitch = -0.2f, gFreeSpeed = 14.0f;
bool  gFirstMouse = true;
double gLastMx = 0, gLastMy = 0;
float gBlendT = 0.0f;                // >0 : smoothly flying back from the free camera to the chase camera
vec3  gBlendEye, gBlendTarget;
vec3  gEye(0, 4, 8), gTarget(0, 1, -10);

// ---------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------
inline bool inTurnWindow() {
    return pSeg && pS >= pSeg->L - TURN_WINDOW && pS < pSeg->L + PAD * 0.5f;
}
inline vec3 playerWorld(float yOff = 0.0f) { return segPos(*pSeg, pS, pLat, pY + yOff); }

// All segments that are close enough to matter (drawn / simulated)
inline void liveSegments(std::vector<Segment*>& out) {
    out.clear();
    auto add = [&](const SegPtr& s) { if (s && std::find(out.begin(), out.end(), s.get()) == out.end()) out.push_back(s.get()); };
    if (pPrev) { add(pPrev); add(pPrev->child[0]); add(pPrev->child[1]); }
    add(pSeg);
    if (pSeg) { add(pSeg->child[0]); add(pSeg->child[1]); }
}

// Where is the monkey? It follows the runner's path, even around corners.
inline vec3 monkeyWorld(float& yawOut) {
    float sm = pS - mGap;
    if (sm >= -PAD * 0.5f || !pPrev) { yawOut = pSeg->yaw; return segPos(*pSeg, sm, mLat, 0.0f); }
    yawOut = pPrev->yaw;
    return segPos(*pPrev, pPrev->L + PAD + sm, mLat, 0.0f);
}

// ---------------------------------------------------------------------
//  Starting / restarting
// ---------------------------------------------------------------------
int gSegStart = 0;                   // test builds: start at a higher difficulty
void resetGame() {
    gSegCounter = gSegStart;
    pPrev.reset();
    pSeg = makeSegment(vec3(0), 0.0f, true);
    ensureChildren(*pSeg);
    pS = 2.0f; pLat = 0; pY = 0; pVy = 0; pLane = 1; pGround = true; pFalling = false;
    pSlideT = 0; pPhase = 0; pSpeed = BASE_SPEED; pTurnPending = 0; pFacing = 0;
    pAirW = pSlideW = pStumbleW = pLieW = pSpin = 0; pStumbleSlow = 0; pInvuln = 0;
    gRunTime = 0; gDist = 0; gCoins = 0;
    mGap = GAP_NORMAL; mLat = 0; mPhase = 0; mYaw = 0; mLunge = 0;
    gStumbleTimer = 0; gMagnetT = gBoostT = gShieldT = 0; gShake = 0;
    gState = GS_PLAY; gDyingT = 0; gOrbit = 0;
    camYaw = 0; camLat = 0; camJump = 0;
    std::printf("New run started.\n");
}

void die(Cause c) {
    if (gState != GS_PLAY) return;
    gState = GS_DYING; gCause = c; gDyingT = 0;
    gShake = 0.6f;
    gBestScore = std::max(gBestScore, (int)gDist + gCoins * 10);
    std::printf("Game over (%s). Score %d\n", c == C_CAUGHT ? "caught by the monkey" : (c == C_FELL ? "fell" : "missed the turn"), (int)gDist + gCoins * 10);
}

// ---------------------------------------------------------------------
//  Rules: stumbling and the monkey
// ---------------------------------------------------------------------
void onHit(Obstacle& o) {
    if (pInvuln > 0.0f || gGod) return;
    o.hit = true;
    if (gShieldT > 0.0f) {                               // the shield absorbs one hit
        gShieldT = 0.0f; pInvuln = 0.9f; gShake = 0.3f;
        return;
    }
    if (gStumbleTimer > 0.0f) { die(C_CAUGHT); return; } // 2nd stumble inside the time window
    gStumbleTimer = STUMBLE_WINDOW;                      // 1st stumble: monkey comes close
    pStumbleSlow = 0.55f; pInvuln = 1.0f; pStumbleW = 1.0f; gShake = 0.5f;
}

void pickPowerUp(PowerType t) {
    if (t == PW_MAGNET) gMagnetT = MAGNET_TIME;
    if (t == PW_SHIELD) gShieldT = SHIELD_TIME;
    if (t == PW_BOOST) {
        gBoostT = BOOST_TIME;
        gStumbleTimer = 0.0f;                            // the monkey drops back to the normal distance
    }
}

void doTurn() {
    int idx = pTurnPending < 0 ? 0 : 1;
    SegPtr next = pSeg->child[idx];
    if (!next) { die(C_WALL); return; }
    pPrev = pSeg; pSeg = next;
    pS = -PAD * 0.5f; pTurnPending = 0; pLat = 0.0f; pLane = 1;
    ensureChildren(*pSeg);
}

// ---------------------------------------------------------------------
//  Per-frame game simulation
// ---------------------------------------------------------------------
void collideObstacles() {
    float yMin = pY, yMax = pY + (pSlideT > 0 ? 0.8f : 1.85f);
    for (Obstacle& o : pSeg->obs) {
        if (o.hit || std::fabs(o.s - pS) > 9.0f) continue;
        HitBox b[4];
        int n = obstacleBoxes(o, gGame, b);
        for (int i = 0; i < n; i++) {
            if (pS + 0.3f > b[i].s0 && pS - 0.3f < b[i].s1 && pLat + 0.32f > b[i].x0 && pLat - 0.32f < b[i].x1 &&
                yMax - 0.08f > b[i].y0 && yMin + 0.08f < b[i].y1) { onHit(o); break; }
        }
    }
}

void collectStuff(float dt) {
    std::vector<Segment*> segs; liveSegments(segs);
    vec3 chest = playerWorld(0.95f);
    for (Segment* sg : segs) {
        for (Coin& c : sg->coins) {
            if (c.taken) continue;
            vec3 w = c.attracted ? c.wpos : segPos(*sg, c.s, c.lat, c.y);
            float d = glm::length(w - chest);
            if (gMagnetT > 0.0f && !c.attracted && d < MAGNET_RADIUS) { c.attracted = true; c.wpos = w; }
            if (c.attracted) {
                c.wpos += (chest - c.wpos) * std::min(1.0f, 9.0f * dt) + glm::normalize(chest - c.wpos + vec3(1e-4f)) * 14.0f * dt;
                d = glm::length(c.wpos - chest);
            }
            if (d < 0.85f) { c.taken = true; gCoins++; }
        }
    }
    for (PowerUp& p : pSeg->pups) {
        if (p.taken) continue;
        vec3 w = segPos(*pSeg, p.s, LANE_X[p.lane], 1.2f);
        if (glm::length(w - chest) < 1.3f) { p.taken = true; pickPowerUp(p.type); }
    }
}

void updateFallingRocks(float dt) {
    for (Obstacle& o : pSeg->obs) {
        if (o.type != O_FALLROCK || o.landed) continue;
        if (o.fallT < 0) { if (o.s > pS && o.s - pS < pSpeed * 1.35f + 3.0f) o.fallT = 0.0f; }
        else { o.fallT += dt; if (fallRockY(o) <= 0.0f) { o.landed = true; gShake = std::max(gShake, 0.25f); } }
    }
}

void updatePlay(float dt) {
    gGame += dt; gRunTime += dt;

    // ---- speed ramps up slowly; boost / stumble modify it ----
    float base = std::min(BASE_SPEED + SPEED_RAMP * gRunTime, MAX_SPEED);
    float target = base * (gBoostT > 0 ? BOOST_MULT : 1.0f) * (pStumbleSlow > 0 ? 0.55f : 1.0f);
    pSpeed = approach(pSpeed, target, 28.0f * dt);
    float ds = pSpeed * dt;
    pS += ds; gDist += ds;
    pPhase += dt * (6.0f + pSpeed * 0.55f);

    // ---- timers ----
    gBoostT = std::max(0.0f, gBoostT - dt);
    gMagnetT = std::max(0.0f, gMagnetT - dt);
    gShieldT = std::max(0.0f, gShieldT - dt);
    pStumbleSlow = std::max(0.0f, pStumbleSlow - dt);
    pInvuln = std::max(0.0f, pInvuln - dt);
    gStumbleTimer = std::max(0.0f, gStumbleTimer - dt);
    pSlideT = std::max(0.0f, pSlideT - dt);
    gShake = std::max(0.0f, gShake - dt * 1.4f);

    // ---- sideways movement ----
    float tl = pTurnPending ? 0.0f : LANE_X[pLane];
    pLat = damp(pLat, tl, 14.0f, dt);

    // ---- vertical movement: jump, fall into pits ----
    if (pFalling) {
        pVy -= GRAVITY * dt; pY += pVy * dt;
        if (pY < -5.4f) { pY = -5.4f; die(C_FELL); }
    } else if (!pGround) {
        pVy -= GRAVITY * dt; pY += pVy * dt;
        if (pY <= 0.0f) {
            pY = 0.0f; pVy = 0.0f;
            if (hasGround(*pSeg, pS, pLat) || gGod) pGround = true; else pFalling = true;
        }
    } else if (!hasGround(*pSeg, pS, pLat) && !gGod) {
        pFalling = true; pGround = false; pVy = 0.0f;
    }

    // ---- junction: execute the turn, or crash into the temple wall ----
    if (pTurnPending && pS >= pSeg->L + PAD * 0.5f) doTurn();
    else if (pS >= pSeg->L + PAD - 0.6f) { if (gGod) { pTurnPending = pSeg->canLeft() ? -1 : 1; doTurn(); } else die(C_WALL); }
    if (pPrev && pS > 36.0f) pPrev.reset();

    // ---- obstacles, falling rocks, coins, power-ups ----
    updateFallingRocks(dt);
    collideObstacles();
    collectStuff(dt);

    // ---- the monkey ----
    float gapTarget = gStumbleTimer > 0.0f ? GAP_CLOSE : GAP_NORMAL;
    float rate = gapTarget < mGap ? 6.0f : (gBoostT > 0.0f ? 4.0f : 0.55f);
    mGap = approach(mGap, gapTarget, rate * dt);
    mLat = damp(mLat, pLat, 7.0f, dt);
    mPhase += dt * (6.0f + pSpeed * 0.55f) * 1.05f;
}

void updateDying(float dt) {
    gDyingT += dt;
    pSpeed = approach(pSpeed, 0.0f, 40.0f * dt);
    pS += pSpeed * dt;
    if (gCause == C_FELL) { pVy -= GRAVITY * dt; pY = std::max(-5.4f, pY + pVy * dt); pSpin += dt * 6.0f; }
    if (gCause == C_CAUGHT) {
        pLieW = damp(pLieW, 1.0f, 8.0f, dt);
        mGap = approach(mGap, CAUGHT_DIST, 9.0f * dt);
        mLunge = damp(mLunge, 1.0f, 10.0f, dt);
        pSlideT = 0;
    }
    if (gCause == C_WALL) pLieW = damp(pLieW, 1.0f, 7.0f, dt);
    gShake = std::max(0.0f, gShake - dt * 1.2f);
    if (gCause != C_FELL) gOrbit = approach(gOrbit, 1.15f, 0.9f * dt);
    mPhase += dt * 8.0f;
    if (gDyingT > 1.6f) gState = GS_OVER;
}

void updateGame(float dt) {
    if (gState == GS_PLAY) updatePlay(dt);
    else if (gState == GS_DYING) updateDying(dt);

    // smooth pose weights and headings (also while dying)
    pAirW = damp(pAirW, (pGround && !pFalling) ? 0.0f : 1.0f, 18.0f, dt);
    pSlideW = damp(pSlideW, pSlideT > 0 ? 1.0f : 0.0f, 20.0f, dt);
    pStumbleW = damp(pStumbleW, 0.0f, 3.2f, dt);
    pFacing += angleDiff(pFacing, pSeg->yaw) * (1.0f - std::exp(-14.0f * dt));
    float myaw; monkeyWorld(myaw);
    mYaw += angleDiff(mYaw, myaw) * (1.0f - std::exp(-14.0f * dt));
}

// ---------------------------------------------------------------------
//  Sky, sun / moon, ambient colours
// ---------------------------------------------------------------------
struct SkyState {
    vec3 sunDir, lightDir, lightCol, ambSky, ambGround, zenith, horizon, fog;
    float night;
};

SkyState computeSky(float phase) {
    SkyState s;
    float a = TAU * phase;
    s.sunDir = glm::normalize(vec3(std::cos(a) * 0.9f, std::sin(a) * 0.92f + 0.10f, 0.35f));
    float y = s.sunDir.y;
    float dayAmt = smoothf(-0.05f, 0.35f, y);
    float twil = smoothf(0.38f, 0.0f, std::fabs(y));
    s.night = smoothf(0.08f, -0.18f, y);

    s.zenith  = glm::mix(vec3(0.008f, 0.012f, 0.04f), vec3(0.22f, 0.46f, 0.85f), dayAmt);
    s.zenith  = glm::mix(s.zenith, vec3(0.30f, 0.22f, 0.45f), twil * 0.55f);
    s.horizon = glm::mix(vec3(0.03f, 0.04f, 0.09f), vec3(0.68f, 0.82f, 0.95f), dayAmt);
    s.horizon = glm::mix(s.horizon, vec3(1.0f, 0.50f, 0.25f), twil * 0.8f);
    s.fog = s.horizon * 0.92f;

    float sunI = smoothf(-0.08f, 0.30f, y);
    float moonI = (1.0f - sunI) * 0.30f;
    if (sunI >= moonI) {
        s.lightDir = s.sunDir;
        vec3 warm = glm::mix(vec3(1.0f, 0.52f, 0.26f), vec3(1.0f, 0.95f, 0.85f), smoothf(0.02f, 0.5f, y));
        s.lightCol = warm * sunI * 1.25f;
    } else {
        s.lightDir = -s.sunDir;
        s.lightCol = vec3(0.45f, 0.55f, 0.95f) * moonI * 1.2f;
    }
    s.ambSky = glm::mix(vec3(0.05f, 0.07f, 0.16f), vec3(0.52f, 0.60f, 0.74f), dayAmt) + vec3(0.12f, 0.05f, 0.0f) * twil * 0.5f;
    s.ambGround = glm::mix(vec3(0.025f, 0.03f, 0.045f), vec3(0.30f, 0.32f, 0.22f), dayAmt);
    return s;
}

// ---------------------------------------------------------------------
//  Cameras
// ---------------------------------------------------------------------
vec3 freeForward() {
    return vec3(-std::sin(gFreeYaw) * std::cos(gFreePitch), std::sin(gFreePitch), -std::cos(gFreeYaw) * std::cos(gFreePitch));
}

void updateCamera(float dt) {
    // --- chase camera (the default): behind and above the runner, looks ahead ---
    camYaw += angleDiff(camYaw, pSeg->yaw) * (1.0f - std::exp(-5.5f * dt));
    camLat = damp(camLat, pLat * 0.55f, 8.0f, dt);
    camJump = damp(camJump, clampf(pY, -2.0f, 3.0f), 6.0f, dt);
    vec3 focus = segPos(*pSeg, pS, camLat, 0.0f);
    vec3 fwd = dirFromYaw(camYaw);
    float on = gOrbit / 1.15f;                                  // 0..1 : how far the death orbit has progressed
    vec3 efwd = dirFromYaw(camYaw + gOrbit);
    vec3 eye = focus - efwd * (9.6f - 3.2f * on) + vec3(0, 4.1f + 0.5f * on + camJump * 0.45f, 0);
    vec3 tgt = focus + fwd * (8.0f * (1.0f - on)) + vec3(0, 1.1f - 0.1f * on + camJump * 0.5f, 0);
    if (gShake > 0.0f) {
        float k = gShake * 0.35f;
        eye += vec3(std::sin(gAnim * 61.0f), std::sin(gAnim * 73.0f + 1.0f), std::sin(gAnim * 53.0f + 2.0f)) * k;
    }
    float fovT = 60.0f + 8.0f * clampf((pSpeed - BASE_SPEED) / (MAX_SPEED - BASE_SPEED), 0, 1) + (gBoostT > 0 ? 7.0f : 0.0f);
    camFov = damp(camFov, fovT, 4.0f, dt);

    if (gPaused) {
        gEye = gFreePos; gTarget = gFreePos + freeForward();
    } else if (gBlendT > 0.0f) {                              // fly back from the free camera
        gBlendT = std::max(0.0f, gBlendT - dt / 0.8f);
        float k = gBlendT * gBlendT * (3.0f - 2.0f * gBlendT);
        gEye = glm::mix(eye, gBlendEye, k);
        gTarget = glm::mix(tgt, gBlendTarget, k);
    } else {
        gEye = eye; gTarget = tgt;
    }
}

void setPaused(bool p) {
    if (p == gPaused) return;
    gPaused = p;
    if (p) {                                                  // start the free camera exactly where the chase camera is
        vec3 d = glm::normalize(gTarget - gEye);
        gFreePos = gEye;
        gFreePitch = std::asin(clampf(d.y, -1.0f, 1.0f));
        gFreeYaw = std::atan2(-d.x, -d.z);
        gFirstMouse = true;
        if (gWin) glfwSetInputMode(gWin, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else {
        gBlendEye = gFreePos; gBlendTarget = gFreePos + freeForward(); gBlendT = 1.0f;
        if (gWin) glfwSetInputMode(gWin, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

// ---------------------------------------------------------------------
//  Input
// ---------------------------------------------------------------------
void tryTurn(int dir) {
    if (!pSeg) return;
    bool ok = dir < 0 ? pSeg->canLeft() : pSeg->canRight();
    if (!ok) return;
    pTurnPending = dir;
    pLane = 1;
}
void actionSide(int dir) {                    // -1 = left, +1 = right
    if (gState != GS_PLAY) return;
    if (inTurnWindow()) { tryTurn(dir); return; }
    pLane = std::max(0, std::min(2, pLane + dir));
}
void actionJump() {
    if (gState != GS_PLAY || !pGround || pFalling) return;
    pVy = JUMP_V; pGround = false; pSlideT = 0.0f;
}
void actionSlide() {
    if (gState != GS_PLAY || pFalling) return;
    if (pGround) pSlideT = SLIDE_TIME; else pVy = std::min(pVy, -18.0f);
}

void keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    switch (key) {
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, GLFW_TRUE); break;
    case GLFW_KEY_SPACE:  setPaused(!gPaused); break;
    case GLFW_KEY_R:      resetGame(); if (gPaused) setPaused(false); break;
    case GLFW_KEY_P:      gGouraud = false; std::printf("Shading: PHONG\n"); break;
    case GLFW_KEY_G:      gGouraud = true;  std::printf("Shading: GOURAUD\n"); break;
    case GLFW_KEY_1:      gUseSun = !gUseSun; break;
    case GLFW_KEY_2:      gUsePoint = !gUsePoint; break;
    case GLFW_KEY_3:      gUseSpot = !gUseSpot; break;
    case GLFW_KEY_4:      gShadows = !gShadows; break;
    case GLFW_KEY_5:      gTexturesOn = !gTexturesOn; break;
    case GLFW_KEY_6:      gFog = !gFog; break;
    case GLFW_KEY_H:      gShowHelp = !gShowHelp; break;
    case GLFW_KEY_N:      gDayPhase = std::fmod(gDayPhase + 0.0625f, 1.0f); break;      // skip time forward
    case GLFW_KEY_M:      gDayPhase = std::fmod(gDayPhase + 0.9375f, 1.0f); break;      // skip time back
    default: break;
    }
    if (gPaused) return;                                        // gameplay keys do nothing while paused
    switch (key) {
    case GLFW_KEY_A: case GLFW_KEY_LEFT:  actionSide(-1); break;
    case GLFW_KEY_D: case GLFW_KEY_RIGHT: actionSide(+1); break;
    case GLFW_KEY_W: case GLFW_KEY_UP:    actionJump();   break;
    case GLFW_KEY_S: case GLFW_KEY_DOWN:  actionSlide();  break;
    default: break;
    }
}

void cursorCallback(GLFWwindow*, double x, double y) {
    if (!gPaused) return;
    if (gFirstMouse) { gLastMx = x; gLastMy = y; gFirstMouse = false; return; }
    float dx = (float)(x - gLastMx), dy = (float)(y - gLastMy);
    gLastMx = x; gLastMy = y;
    gFreeYaw -= dx * 0.0025f;
    gFreePitch = clampf(gFreePitch - dy * 0.0025f, -1.55f, 1.55f);
}
void scrollCallback(GLFWwindow*, double, double yoff) {
    gFreeSpeed = clampf(gFreeSpeed * (yoff > 0 ? 1.25f : 0.8f), 1.0f, 120.0f);
}
void resizeCallback(GLFWwindow*, int w, int h) { gFbW = std::max(1, w); gFbH = std::max(1, h); }

void pollFreeCamera(float dt) {
    if (!gPaused) return;
    auto down = [&](int k) { return glfwGetKey(gWin, k) == GLFW_PRESS; };
    vec3 f = freeForward();
    vec3 r = vec3(std::cos(gFreeYaw), 0, -std::sin(gFreeYaw));
    float sp = gFreeSpeed * (down(GLFW_KEY_LEFT_SHIFT) ? 3.0f : 1.0f) * dt;
    vec3 mv(0);
    if (down(GLFW_KEY_W)) mv += f;
    if (down(GLFW_KEY_S)) mv -= f;
    if (down(GLFW_KEY_D)) mv += r;
    if (down(GLFW_KEY_A)) mv -= r;
    if (down(GLFW_KEY_E)) mv += vec3(0, 1, 0);
    if (down(GLFW_KEY_Q)) mv -= vec3(0, 1, 0);
    gFreePos += mv * sp;
    if (down(GLFW_KEY_LEFT))  gFreeYaw += 1.6f * dt;
    if (down(GLFW_KEY_RIGHT)) gFreeYaw -= 1.6f * dt;
    if (down(GLFW_KEY_UP))    gFreePitch = clampf(gFreePitch + 1.2f * dt, -1.55f, 1.55f);
    if (down(GLFW_KEY_DOWN))  gFreePitch = clampf(gFreePitch - 1.2f * dt, -1.55f, 1.55f);
}

// ---------------------------------------------------------------------
//  Rendering
// ---------------------------------------------------------------------
struct LightCand { vec3 pos, col; float d2; };

void drawSky(const SkyState& sky, const mat4& invVP, const vec3& eye) {
    gSky.use();
    gSky.m4("uInvViewProj", invVP);
    gSky.f3("uEyePos", eye);
    gSky.f3("uSunDir", sky.sunDir);
    gSky.f3("uZenith", sky.zenith);
    gSky.f3("uHorizon", sky.horizon);
    gSky.f1("uNight", sky.night);
    gSky.f1("uTime", gAnim);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(gSkyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

void renderFrame() {
    gDrawCalls = 0;
    glViewport(0, 0, gFbW, gFbH);
    SkyState sky = computeSky(gDayPhase);
    glClearColor(sky.horizon.r, sky.horizon.g, sky.horizon.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (float)gFbW / (float)gFbH;
    mat4 proj = glm::perspective(glm::radians(camFov), aspect, 0.3f, 420.0f);
    mat4 view = glm::lookAt(gEye, gTarget, vec3(0, 1, 0));
    mat4 VP = proj * view;
    gCull.view = view;
    gCull.tanY = std::tan(glm::radians(camFov) * 0.5f);
    gCull.tanX = gCull.tanY * aspect;
    gCull.farD = 230.0f;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    drawSky(sky, glm::inverse(VP), gEye);

    // ---------------- scene program + per-frame uniforms ----------------
    gCur = gGouraud ? &gProgGouraud : &gProgPhong;
    SceneProg& sp = *gCur;
    sp.p.use();
    glUniformMatrix4fv(sp.uViewProj, 1, GL_FALSE, glm::value_ptr(VP));
    glUniform1f(sp.uFlatten, 0.0f);
    sp.p.i1("uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    gBoundTex = 0xFFFFFFFFu;
    sp.p.f3("uEyePos", gEye);
    sp.p.f3("uAmbSky", sky.ambSky);
    sp.p.f3("uAmbGround", sky.ambGround);
    sp.p.f3("uSunDir", sky.lightDir);
    sp.p.f3("uSunCol", sky.lightCol);
    sp.p.i1("uUseSun", gUseSun ? 1 : 0);
    sp.p.i1("uUsePoint", gUsePoint ? 1 : 0);
    sp.p.i1("uUseSpot", gUseSpot ? 1 : 0);
    sp.p.i1("uShadowOn", gShadows ? 1 : 0);
    sp.p.i1("uFogOn", gFog ? 1 : 0);
    sp.p.f3("uFogColor", sky.fog);
    sp.p.f1("uFogStart", 40.0f);
    sp.p.f1("uFogEnd", 190.0f);

    std::vector<Segment*> segs; liveSegments(segs);
    vec3 focus = gPaused ? gEye : playerWorld(0.0f);

    // ---- point lights: the 8 closest torches / fires / power glows ----
    std::vector<LightCand> cands;
    for (Segment* sg : segs) {
        for (const Torch& t : sg->torches) {
            float fl = 1.0f + 0.18f * std::sin(gAnim * 11.0f + t.s) + 0.10f * std::sin(gAnim * 23.0f + t.s * 2.0f);
            vec3 p = segPos(*sg, t.s, t.side * 3.9f, 2.1f);
            cands.push_back({ p, vec3(1.0f, 0.62f, 0.28f) * 2.8f * fl, 0 });
        }
        for (const Obstacle& o : sg->obs) if (o.type == O_FIRE) {
            float fl = 1.0f + 0.2f * std::sin(gAnim * 9.0f + o.s);
            float x0[3], x1[3]; int n = laneRuns(o.mask, x0, x1);
            for (int i = 0; i < n; i++)
                cands.push_back({ segPos(*sg, o.s, (x0[i] + x1[i]) * 0.5f, 0.9f), vec3(1.0f, 0.45f, 0.12f) * 3.2f * fl, 0 });
        }
    }
    if (gBoostT > 0) cands.push_back({ playerWorld(1.0f), vec3(0.3f, 0.8f, 1.0f) * 2.4f, 0 });
    if (gMagnetT > 0) cands.push_back({ playerWorld(1.0f), vec3(1.0f, 0.25f, 0.25f) * 1.4f, 0 });
    for (LightCand& c : cands) c.d2 = glm::dot(c.pos - focus, c.pos - focus);
    std::sort(cands.begin(), cands.end(), [](const LightCand& a, const LightCand& b) { return a.d2 < b.d2; });
    int np = (int)std::min<size_t>(cands.size(), 8);
    vec3 pp[8], pc[8];
    for (int i = 0; i < np; i++) { pp[i] = cands[i].pos; pc[i] = cands[i].col; }
    sp.p.i1("uNumPoint", np);
    if (np) { sp.p.f3v("uPointPos", pp, np); sp.p.f3v("uPointCol", pc, np); }

    // ---- spot lights: runner's head lamp + lamp above the next junction ----
    vec3 sPos[2], sDir[2], sCol[2]; vec2 sCone[2];
    vec3 fwdDir = pSeg->dir;
    sPos[0] = playerWorld(1.7f) + fwdDir * 0.3f;
    sDir[0] = glm::normalize(fwdDir * std::cos(0.30f) - vec3(0, std::sin(0.30f), 0));
    sCol[0] = vec3(1.0f, 0.95f, 0.78f) * (1.1f + 2.2f * sky.night);
    sCone[0] = vec2(std::cos(glm::radians(11.0f)), std::cos(glm::radians(24.0f)));
    {
        vec3 lamp = segPos(*pSeg, pSeg->L + PAD + 0.5f, 0.0f, 11.5f);
        vec3 pad = segPos(*pSeg, pSeg->L + PAD * 0.5f, 0.0f, 0.0f);
        sPos[1] = lamp; sDir[1] = glm::normalize(pad - lamp);
        sCol[1] = vec3(1.0f, 0.85f, 0.55f) * (2.2f + 1.5f * sky.night);
        sCone[1] = vec2(std::cos(glm::radians(16.0f)), std::cos(glm::radians(34.0f)));
    }
    sp.p.i1("uNumSpot", 2);
    sp.p.f3v("uSpotPos", sPos, 2); sp.p.f3v("uSpotDir", sDir, 2); sp.p.f3v("uSpotCol", sCol, 2); sp.p.f2v("uSpotCone", sCone, 2);

    // ---- ray traced shadow casters (spheres): trees, rocks, runner, monkey ----
    struct Occ { vec4 s; float d2; };
    std::vector<Occ> occ;
    auto addOcc = [&](const vec3& c, float r) {
        float d2 = glm::dot(c - focus, c - focus);
        if (d2 < 75.0f * 75.0f) occ.push_back({ vec4(c, r), d2 });
    };
    for (Segment* sg : segs) {
        for (const Prop& p : sg->props) {
            vec3 b = segPos(*sg, p.s, p.lat, 0.0f);
            if (glm::dot(b - focus, b - focus) > 80.0f * 80.0f) continue;
            switch (p.kind) {
            case PR_TREE:   addOcc(b + vec3(0, 4.5f * p.scale, 0), 2.1f * p.scale); addOcc(b + vec3(0, 1.3f * p.scale, 0), 0.5f * p.scale); break;
            case PR_PINE:   addOcc(b + vec3(0, 3.6f * p.scale, 0), 1.9f * p.scale); addOcc(b + vec3(0, 1.0f * p.scale, 0), 0.45f * p.scale); break;
            case PR_ROCK:   addOcc(b + vec3(0, 0.5f, 0), 0.8f * p.scale); break;
            case PR_PILLAR: addOcc(b + vec3(0, 1.6f, 0), 0.75f); break;
            case PR_STATUE: addOcc(b + vec3(0, 1.4f, 0), 1.0f * p.scale); break;
            default: break;
            }
        }
        for (const Obstacle& o : sg->obs) {
            if (o.type == O_ROCK || (o.type == O_FALLROCK && o.landed))
                for (int l = 0; l < 3; l++) if ((o.mask >> l) & 1) addOcc(segPos(*sg, o.s, LANE_X[l], 1.2f), 1.1f);
            if (o.type == O_WALL) { HitBox b[4]; int n = obstacleBoxes(o, gGame, b);
                for (int i = 0; i < n; i++) addOcc(segPos(*sg, o.s, (b[i].x0 + b[i].x1) * 0.5f, 1.3f), 1.2f); }
        }
    }
    addOcc(playerWorld(1.15f), 0.48f); addOcc(playerWorld(1.75f), 0.3f);
    { float yw; vec3 mw = monkeyWorld(yw); addOcc(mw + vec3(0, 1.1f, 0), 0.6f); addOcc(mw + vec3(0, 1.6f, 0), 0.32f); }
    std::sort(occ.begin(), occ.end(), [](const Occ& a, const Occ& b) { return a.d2 < b.d2; });
    int no = (int)std::min<size_t>(occ.size(), 32);
    vec4 ov[32];
    for (int i = 0; i < no; i++) ov[i] = occ[i].s;
    sp.p.i1("uNumOcc", no);
    if (no) sp.p.f4v("uOcc", ov, no);

    // ---------------- opaque geometry ----------------
    {   // endless jungle floor: a big grid that follows the camera; texture coordinates come from
        // world position (uWorldUV) so the grass does not slide when the grid moves
        const float size = 420.0f, cell = size / 72.0f;
        float gx = std::floor(gEye.x / cell) * cell, gz = std::floor(gEye.z / cell) * cell;
        Mat m = mat(vec3(1), T_GRASS, vec2(1.0f / 6.0f), 0.02f, 10.0f);
        m.worldUV = true;
        drawMesh(G.ground, T(gx, -0.05f, gz) * S(size, 1.0f, size), m);
    }
    for (Segment* sg : segs) drawSegmentGround(*sg);
    for (Segment* sg : segs) drawSegmentPits(*sg, gAnim);
    DrawTimes tm{ gAnim, gGame };
    for (Segment* sg : segs) {
        drawProps(*sg, gAnim);
        for (const Obstacle& o : sg->obs) drawObstacle(*sg, o, tm);
        drawCollectibles(*sg, tm);
    }
    // glowing orb on top of every temple (the junction lamp)
    for (Segment* sg : segs)
        drawMesh(G.sphere, segFrame(*sg) * T(0, 11.4f, -(sg->L + PAD + 3.0f)) * S(0.4f), mat(vec3(1.0f, 0.85f, 0.45f), T_NONE, vec2(1), 0.0f, 8.0f, 1.0f));

    // runner + monkey
    {
        Pose pp;
        pp.phase = pPhase; pp.air = pAirW; pp.slide = pSlideW; pp.stumble = pStumbleW; pp.lie = pLieW; pp.spin = pSpin; pp.t = gAnim;
        drawPlayer(T(playerWorld(0.0f)) * RY(pFacing), pp);
        Pose mp; mp.phase = mPhase; mp.lunge = mLunge; mp.t = gAnim;
        float yw; vec3 mw = monkeyWorld(yw);
        drawMonkey(T(mw) * RY(mYaw), mp);
    }

    // ---------------- transparent: shield bubble ----------------
    if (gShieldT > 0.0f && gState != GS_OVER) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        Mat m = mat(vec3(0.35f, 0.65f, 1.0f), T_NONE, vec2(1), 0.6f, 40.0f, 0.7f);
        m.alpha = 0.22f + 0.06f * std::sin(gAnim * 6.0f);
        if (gShieldT < 3.0f && std::fmod(gAnim, 0.4f) < 0.2f) m.alpha = 0.06f;      // blinks when about to run out
        drawMesh(G.sphere, T(playerWorld(1.0f)) * S(1.25f), m);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}

// ---------------------------------------------------------------------
//  HUD
// ---------------------------------------------------------------------
void drawHud() {
    Hud& h = gHud;
    h.begin(gFbW, gFbH);
    float u = gFbH / 720.0f, sc = 2.0f * u;
    float W = (float)gFbW, Hh = (float)gFbH;
    int score = (int)gDist + gCoins * 10;
    vec4 white(1, 1, 1, 1), gold(1.0f, 0.85f, 0.25f, 1), red(1.0f, 0.25f, 0.2f, 1);

    // score block
    h.rect(10 * u, 10 * u, 250 * u, 92 * u, vec4(0, 0, 0, 0.35f));
    h.text(22 * u, 18 * u, "SCORE " + std::to_string(score), 3.0f * u, white);
    h.text(22 * u, 52 * u, "COINS " + std::to_string(gCoins), sc, gold);
    h.text(22 * u, 74 * u, std::to_string((int)gDist) + " m   " + std::to_string((int)(pSpeed * 3.6f)) + " km/h", sc, vec4(0.8f, 0.9f, 1, 1));

    // active power-ups
    float by = 112 * u;
    auto bar = [&](const char* name, float t, float tmax, const vec4& c) {
        if (t <= 0) return;
        h.rect(10 * u, by, 250 * u, 22 * u, vec4(0, 0, 0, 0.35f));
        h.rect(14 * u, by + 14 * u, 242 * u * (t / tmax), 5 * u, c);
        h.text(18 * u, by + 2 * u, name, 1.6f * u, c);
        by += 26 * u;
    };
    bar("MAGNET", gMagnetT, MAGNET_TIME, vec4(1.0f, 0.35f, 0.35f, 1));
    bar("SPEED BOOST", gBoostT, BOOST_TIME, vec4(0.3f, 0.9f, 1.0f, 1));
    bar("SHIELD", gShieldT, SHIELD_TIME, vec4(0.45f, 0.65f, 1.0f, 1));

    // right side: render state
    float hrs = std::fmod(6.0f + gDayPhase * 24.0f, 24.0f);
    char clock[64]; std::snprintf(clock, sizeof(clock), "%02d:%02d", (int)hrs, (int)((hrs - (int)hrs) * 60));
    SkyState sk = computeSky(gDayPhase);
    auto rline = [&](float y, const std::string& s, const vec4& c) { h.text(W - 12 * u - h.textWidth(s, 1.8f * u), y, s, 1.8f * u, c); };
    rline(12 * u, std::string("SHADING: ") + (gGouraud ? "GOURAUD" : "PHONG"), gold);
    rline(32 * u, std::string("TIME ") + clock + (sk.night > 0.5f ? "  NIGHT" : "  DAY"), vec4(0.8f, 0.9f, 1, 1));
    rline(52 * u, std::string("Sun ") + (gUseSun ? "ON" : "off") + "  Point " + (gUsePoint ? "ON" : "off") + "  Spot " + (gUseSpot ? "ON" : "off"), white);
    rline(72 * u, std::string("Shadows ") + (gShadows ? "ON" : "off") + "  Tex " + (gTexturesOn ? "ON" : "off") + "  Fog " + (gFog ? "ON" : "off"), white);

    // monkey warning
    if (gStumbleTimer > 0.0f && gState == GS_PLAY) {
        float a = 0.16f + 0.10f * std::sin(gAnim * 8.0f);
        vec4 rc(0.9f, 0.05f, 0.0f, a);
        float e = 38 * u;
        h.rect(0, 0, W, e, rc); h.rect(0, Hh - e, W, e, rc); h.rect(0, 0, e, Hh, rc); h.rect(W - e, 0, e, Hh, rc);
        char b[96]; std::snprintf(b, sizeof(b), "THE MONKEY IS CLOSE!  %.1f s", gStumbleTimer);
        h.textCentered(W * 0.5f, 18 * u, b, 3.0f * u, red);
        h.textCentered(W * 0.5f, 52 * u, "stumble again and it catches you", 1.8f * u, white);
    }
    // junction hint
    if (gState == GS_PLAY && !gPaused && inTurnWindow() && !pTurnPending) {
        bool fl = std::fmod(gAnim, 0.5f) < 0.3f;
        std::string t = pSeg->junction == J_FORK ? "<  TURN LEFT or RIGHT  >" : (pSeg->junction == J_LEFT ? "<  TURN LEFT" : "TURN RIGHT  >");
        if (fl) h.textCentered(W * 0.5f, Hh * 0.30f, t, 3.4f * u, vec4(1.0f, 0.9f, 0.2f, 1));
    }
    // pause banner
    if (gPaused) {
        h.rect(W * 0.5f - 360 * u, 8 * u, 720 * u, 64 * u, vec4(0, 0, 0, 0.5f));
        h.textCentered(W * 0.5f, 14 * u, "PAUSED  -  FREE CAMERA", 3.2f * u, gold);
        h.textCentered(W * 0.5f, 46 * u, "W A S D move   mouse / arrows look   Q E down/up   SHIFT fast   wheel speed   SPACE resume", 1.5f * u, white);
    }
    // help
    if (gShowHelp) {
        const char* lines[] = {
            "A/D or arrows : lane / turn   W : jump   S : slide",
            "SPACE : pause + free camera     R : restart     H : hide help",
            "P : Phong shading    G : Gouraud shading",
            "1 sun  2 point lights  3 spot lights  4 ray-traced shadows  5 textures  6 fog",
            "N / M : skip time of day forward / back" };
        float y = Hh - 14 * u - 5 * 17 * u;
        h.rect(10 * u, y - 6 * u, 560 * u, 5 * 17 * u + 10 * u, vec4(0, 0, 0, 0.35f));
        for (int i = 0; i < 5; i++) h.text(18 * u, y + i * 17 * u, lines[i], 1.45f * u, vec4(0.9f, 0.95f, 1, 1));
    }
    // game over
    if (gState == GS_OVER) {
        h.rect(0, 0, W, Hh, vec4(0, 0, 0, 0.55f));
        h.textCentered(W * 0.5f, Hh * 0.28f, "GAME OVER", 8.0f * u, red);
        const char* why = gCause == C_CAUGHT ? "The monkey caught you!" : (gCause == C_FELL ? "You fell into the pit!" : "You missed the turn!");
        h.textCentered(W * 0.5f, Hh * 0.28f + 90 * u, why, 3.0f * u, white);
        char b[128]; std::snprintf(b, sizeof(b), "Score %d    Coins %d    Distance %d m    Best %d", score, gCoins, (int)gDist, gBestScore);
        h.textCentered(W * 0.5f, Hh * 0.28f + 140 * u, b, 2.4f * u, gold);
        h.textCentered(W * 0.5f, Hh * 0.28f + 190 * u, "Press R to run again", 2.4f * u, white);
    }
    h.flush();
}

// ---------------------------------------------------------------------
//  Test hooks (only compiled with -DTR_TEST, used to verify the game
//  headlessly; a normal build ignores all of this)
// ---------------------------------------------------------------------
#ifdef TR_TEST
#include <set>
void saveShot(int frame) {
    std::vector<unsigned char> px((size_t)gFbW * gFbH * 3);
    glReadPixels(0, 0, gFbW, gFbH, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    char name[64]; std::snprintf(name, sizeof(name), "/tmp/shot_%04d.ppm", frame);
    FILE* f = std::fopen(name, "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", gFbW, gFbH);
    for (int y = gFbH - 1; y >= 0; y--) std::fwrite(&px[(size_t)y * gFbW * 3], 1, (size_t)gFbW * 3, f);
    std::fclose(f);
}
#endif

// ---------------------------------------------------------------------
//  main
// ---------------------------------------------------------------------
int main() {
    if (!glfwInit()) { std::fprintf(stderr, "Failed to start GLFW\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);                      // anti-aliasing
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    gWin = glfwCreateWindow(gFbW, gFbH, "Temple Run - Computer Graphics Project", nullptr, nullptr);
    if (!gWin) { std::fprintf(stderr, "Failed to create the window (needs OpenGL 3.3)\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(gWin);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::fprintf(stderr, "Failed to load OpenGL\n"); return 1; }
    glfwGetFramebufferSize(gWin, &gFbW, &gFbH);
    glfwSetKeyCallback(gWin, keyCallback);
    glfwSetCursorPosCallback(gWin, cursorCallback);
    glfwSetScrollCallback(gWin, scrollCallback);
    glfwSetFramebufferSizeCallback(gWin, resizeCallback);

    std::printf("OpenGL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);

    // ---- build everything once ----
    initTextures();
    buildMeshes();
    bool ok = buildProgram(gProgPhong.p, sceneVertexSource(false), sceneFragmentSource(false), "scene-phong");
    ok = buildProgram(gProgGouraud.p, sceneVertexSource(true), sceneFragmentSource(true), "scene-gouraud") && ok;
    ok = buildProgram(gSky, SKY_VS, SKY_FS, "sky") && ok;
    if (!ok) { std::fprintf(stderr, "Shader error - see messages above.\n"); return 1; }
    gProgPhong.fetch(); gProgGouraud.fetch();
    glGenVertexArrays(1, &gSkyVao);
    gHud.init();

    rngState() = 987654321u ^ (uint32_t)(glfwGetTime() * 100000.0 + 17.0);
    resetGame();
    setPaused(false);

#ifdef TR_TEST
    {   // ---------- headless test run ----------
        const char* seed = std::getenv("TR_SEED");   if (seed) { rngState() = (uint32_t)std::atoi(seed); }
        if (const char* df = std::getenv("TR_DIFF")) gSegStart = std::atoi(df);
        if (seed || std::getenv("TR_DIFF")) resetGame();
        const char* ph = std::getenv("TR_PHASE");    if (ph) gDayPhase = (float)std::atof(ph);
        const char* gr = std::getenv("TR_GOURAUD");  if (gr) gGouraud = std::atoi(gr) != 0;
        const char* so = std::getenv("TR_SHOTS");
        const char* fr = std::getenv("TR_FREE_AT");
        const char* sw = std::getenv("TR_SWITCH");   // "frame:phong/gouraud toggling" not needed
        (void)sw;
        gGod = std::getenv("TR_GOD") != nullptr;
        for (const Gap& g : pSeg->gaps) std::printf("  gap  s=%.1f..%.1f mask=%d lava=%d\n", g.s0, g.s1, g.mask, g.lava);
        for (const Obstacle& o : pSeg->obs) std::printf("  obs  type=%d s=%.1f mask=%d\n", (int)o.type, o.s, o.mask);
        if (std::getenv("TR_SPAWN_PUPS")) {            // one of each power-up right in front of the runner
            for (int i = 0; i < 3; i++) { PowerUp p; p.type = (PowerType)i; p.s = 9.0f + 2.5f * i; p.lane = i; pSeg->pups.push_back(p); }
            addCoinLine(*pSeg, 6.0f, 1, 8);
        }
        if (std::getenv("TR_MAGNET")) gMagnetT = 999.0f;
        const char* ats = std::getenv("TR_AT_S");    if (ats) pS = (float)std::atof(ats);
        std::set<int> shots; int last = 0;
        if (so) { std::string s = so; size_t p = 0; while (p < s.size()) { size_t q = s.find(',', p); if (q == std::string::npos) q = s.size(); int v = std::atoi(s.substr(p, q - p).c_str()); shots.insert(v); last = std::max(last, v); p = q + 1; } }
        int freeAt = fr ? std::atoi(fr) : -1;
        const float dt = 1.0f / 30.0f;
        for (int f = 0; f <= last; f++) {
            if (inTurnWindow() && !pTurnPending && gState == GS_PLAY) tryTurn(pSeg->canLeft() && (!pSeg->canRight() || chance(0.5f)) ? -1 : 1);
            if (f == freeAt) {
                setPaused(true);
                float ox = -6, oy = 7, oz = 14, pt = -0.45f, yw = 0, fwdProbe = 0;
                if (const char* fo = std::getenv("TR_FREE_OFF")) std::sscanf(fo, "%f,%f,%f,%f,%f", &ox, &oy, &oz, &pt, &yw);
                (void)fwdProbe;
                gFreePos = gEye + vec3(ox, oy, oz); gFreePitch = pt; gFreeYaw = yw + camYaw;
            }
            if (const char* fh = std::getenv("TR_HITS")) {          // force obstacle hits at given frames
                std::string s = fh; size_t p = 0;
                while (p < s.size()) { size_t q = s.find(',', p); if (q == std::string::npos) q = s.size();
                    if (std::atoi(s.substr(p, q - p).c_str()) == f) { Obstacle d; d.type = O_LOG; d.mask = 0; d.s = pS; d.depth = 0; d.yMin = 0; d.yMax = 0; onHit(d); }
                    p = q + 1; }
            }
            gAnim += dt;
            if (!gPaused) { updateGame(dt); gDayPhase = std::fmod(gDayPhase + dt / DAY_LENGTH, 1.0f); }
            updateCamera(dt);
            if (shots.count(f)) {
                renderFrame(); drawHud(); glFinish(); saveShot(f);
                std::printf("shot %d  draws=%d seg=%d dist=%.0f coins=%d state=%d\n", f, gDrawCalls, pSeg->id, gDist, gCoins, (int)gState);
            }
        }
        return 0;
    }
#endif

    double last = glfwGetTime();
    while (!glfwWindowShouldClose(gWin)) {
        double now = glfwGetTime();
        float dt = (float)std::min(0.05, now - last);
        last = now;
        glfwPollEvents();

        gAnim += dt;
        pollFreeCamera(dt);
        if (!gPaused) {
            updateGame(dt);
            gDayPhase = std::fmod(gDayPhase + dt / DAY_LENGTH, 1.0f);       // day <-> night
        }
        updateCamera(dt);

        renderFrame();
        drawHud();
        glfwSwapBuffers(gWin);
    }
    glfwTerminate();
    return 0;
}
