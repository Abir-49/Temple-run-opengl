// =====================================================================
//  scene.h  --  draws one Segment (path, junction, scenery, hazards ...)
//  Everything is drawn with the segment's frame matrix F, so objects are
//  placed with simple local numbers: x = lateral, y = height, z = -s.
// =====================================================================
#pragma once
#include "world.h"

struct DrawTimes { float anim; float game; };      // anim = real time, game = frozen while paused

// ---- one rectangular stretch of floor ---------------------------------
inline void strip(const mat4& F, float s0, float s1, float x0, float x1, int tex, float y = 0.0f,
                  const vec3& tint = vec3(1.0f)) {
    if (s1 - s0 < 0.01f) return;
    mat4 M = F * T((x0 + x1) * 0.5f, y, -(s0 + s1) * 0.5f) * S(x1 - x0, 1.0f, s1 - s0);
    drawMesh(G.grid, M, mat(tint, tex, vec2((x1 - x0) / 2.0f, (s1 - s0) / 2.0f), 0.06f, 14.0f));
}

inline void sortedGaps(const Segment& sg, std::vector<Gap>& out) {
    out = sg.gaps;
    std::sort(out.begin(), out.end(), [](const Gap& a, const Gap& b) { return a.s0 < b.s0; });
}

// ---- floor, curbs, junction pad, facade, closed-side wall -------------
inline void drawSegmentGround(const Segment& sg) {
    mat4 F = segFrame(sg);
    float L = sg.L;
    std::vector<Gap> gaps; sortedGaps(sg, gaps);

    // path strips (one per lane when there are gaps, otherwise one wide strip)
    if (gaps.empty()) {
        strip(F, 0, L, -3.0f, 3.0f, T_STONE);
    } else {
        for (int lane = 0; lane < 3; lane++) {
            float x0 = LANE_X[lane] - 1.0f, x1 = LANE_X[lane] + 1.0f, cur = 0;
            for (const Gap& g : gaps) {
                strip(F, cur, g.s0, x0, x1, T_STONE);
                if (!((g.mask >> lane) & 1)) strip(F, g.s0, g.s1, x0, x1, T_WOOD);      // rope-bridge planks
                cur = g.s1;
            }
            strip(F, cur, L, x0, x1, T_STONE);
        }
    }
    // curbs (raised stones along both sides, interrupted where the lane is missing)
    for (int side = -1; side <= 1; side += 2) {
        int lane = side < 0 ? 0 : 2;
        float cur = 0;
        auto curb = [&](float a, float b) {
            if (b - a < 0.05f) return;
            drawMesh(G.cube, F * T(side * 3.25f, 0.17f, -(a + b) * 0.5f) * S(0.5f, 0.34f, b - a),
                     mat(vec3(0.85f), T_RUIN, vec2(0.25f, (b - a) / 2.0f), 0.05f));
        };
        for (const Gap& g : gaps) if ((g.mask >> lane) & 1) { curb(cur, g.s0); cur = g.s1; }
        curb(cur, L);
    }
    // junction pad
    strip(F, L, L + PAD, -3.5f, 3.5f, T_STONE, 0.0f, vec3(0.92f, 0.88f, 0.8f));
    drawMesh(G.ring, F * T(0, 0.03f, -(L + PAD * 0.5f)), mat(vec3(1.0f, 0.85f, 0.4f), T_NONE, vec2(1), 0.6f, 40.0f, 0.55f));
    // closed side of the pad = stone wall
    for (int side = -1; side <= 1; side += 2) {
        bool open = side < 0 ? sg.canLeft() : sg.canRight();
        if (open) continue;
        drawMesh(G.cube, F * T(side * 3.8f, 1.2f, -(L + PAD * 0.5f)) * S(0.7f, 2.4f, PAD + 0.2f),
                 mat(vec3(0.9f), T_RUIN, vec2(PAD / 2.0f, 1.2f), 0.05f));
    }
    // temple facade at the end of the pad
    drawMesh(G.temple, F * T(0, 0, -(L + PAD + 3.0f)), mat(vec3(1.0f), T_BRICK, vec2(1), 0.05f, 12.0f));
    // start area behind the player in the very first segment
    if (sg.first) {
        strip(F, -14.0f, 0.0f, -3.5f, 3.5f, T_STONE, 0.0f, vec3(0.92f, 0.88f, 0.8f));
        for (int side = -1; side <= 1; side += 2)
            drawMesh(G.cube, F * T(side * 3.8f, 0.5f, 7.0f) * S(0.7f, 1.0f, 14.0f), mat(vec3(0.9f), T_RUIN, vec2(7.0f, 0.5f), 0.05f));
        drawMesh(G.temple, F * T(0, 0, 17.0f) * RY(PI), mat(vec3(1.0f), T_BRICK, vec2(1), 0.05f, 12.0f));
    }
}

// ---- pits: punch a hole in the endless ground and draw the pit inside --
inline void drawSegmentPits(const Segment& sg, float animTime) {
    if (sg.gaps.empty()) return;
    mat4 F = segFrame(sg);
    struct Run { float x0, x1, s0, s1; bool lava; };
    std::vector<Run> runs;
    for (const Gap& g : sg.gaps) {
        float x0[3], x1[3]; int n = laneRuns(g.mask, x0, x1);
        for (int i = 0; i < n; i++) runs.push_back({ x0[i], x1[i], g.s0, g.s1, g.lava });
    }
    // 1) reset the depth buffer inside the hole outlines (colour writes off)
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthFunc(GL_ALWAYS);
    glUniform1f(gCur->uFlatten, 1.0f);
    for (const Run& r : runs)
        drawMesh(G.grid, F * T((r.x0 + r.x1) * 0.5f, 0.0f, -(r.s0 + r.s1) * 0.5f) * S(r.x1 - r.x0, 1, r.s1 - r.s0), mat(vec3(0)));
    glUniform1f(gCur->uFlatten, 0.0f);
    glDepthFunc(GL_LESS);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // 2) draw walls + floor of the pit
    const float D = 7.0f;
    for (const Run& r : runs) {
        float w = r.x1 - r.x0, len = r.s1 - r.s0, cx = (r.x0 + r.x1) * 0.5f, cs = (r.s0 + r.s1) * 0.5f;
        Mat wall = mat(vec3(0.55f), T_RUIN, vec2(len / 2.0f, 3.0f), 0.0f);
        drawMesh(G.cube, F * T(cx, -D * 0.5f, -(r.s0 - 0.2f)) * S(w + 0.4f, D, 0.4f), wall);            // front wall
        drawMesh(G.cube, F * T(cx, -D * 0.5f, -(r.s1 + 0.2f)) * S(w + 0.4f, D, 0.4f), wall);            // back wall
        drawMesh(G.cube, F * T(r.x0 - 0.2f, -D * 0.5f, -cs) * S(0.4f, D, len), wall);                   // side walls
        drawMesh(G.cube, F * T(r.x1 + 0.2f, -D * 0.5f, -cs) * S(0.4f, D, len), wall);
        if (r.lava) {
            Mat lv = mat(vec3(1.0f), T_LAVA, vec2(w / 3.0f + 0.01f * std::sin(animTime), len / 3.0f), 0.0f, 8.0f, 0.7f);
            drawMesh(G.grid, F * T(cx, -D + 0.6f, -cs) * S(w, 1, len), lv);
        } else {
            drawMesh(G.grid, F * T(cx, -D + 0.6f, -cs) * S(w, 1, len), mat(vec3(0.02f, 0.02f, 0.03f), T_NONE, vec2(1), 0.0f));
        }
    }
    // rope-bridge rails for narrow bridges
    for (const Gap& g : sg.gaps) {
        if (g.mask == 7) continue;
        float len = g.s1 - g.s0, cs = (g.s0 + g.s1) * 0.5f;
        for (int side = -1; side <= 1; side += 2) {
            drawMesh(G.cyl, F * T(side * 1.0f, 0.95f, -g.s0) * RX(-PI / 2) * S(0.04f, len, 0.04f), mat(vec3(0.7f, 0.6f, 0.4f), T_NONE, vec2(1), 0.05f));
            for (int k = 0; k <= 3; k++)
                drawMesh(G.cyl, F * T(side * 1.0f, 0.0f, -(g.s0 + len * k / 3.0f)) * S(0.07f, 1.0f, 0.07f), mat(vec3(0.5f), T_WOOD, vec2(1), 0.05f));
        }
        (void)cs;
    }
}

// ---- scenery props ---------------------------------------------------
inline void drawProps(const Segment& sg, float animTime) {
    mat4 F = segFrame(sg);
    for (const Prop& p : sg.props) {
        vec3 w = segPos(sg, p.s, p.lat, 0.0f);
        float rad = (p.kind == PR_GATE ? 9.0f : 6.0f) * p.scale;
        if (!inView(w + vec3(0, 3, 0), rad)) continue;
        mat4 base = F * T(p.lat, 0, -p.s) * RY(p.rot);
        float sway = std::sin(animTime * 0.9f + p.s * 0.37f) * 0.022f;
        switch (p.kind) {
        case PR_TREE:
            drawMesh(G.trunk, base * S(p.scale), mat(vec3(1), T_BARK, vec2(1, 1), 0.02f));
            drawMesh(G.canopy, base * S(p.scale) * RX(sway) * RZ(sway * 0.7f), mat(vec3(1), T_LEAF, vec2(3, 3), 0.04f));
            break;
        case PR_PINE:
            drawMesh(G.pine, base * S(p.scale * 1.15f) * RX(sway) * RZ(sway), mat(vec3(1), T_LEAF, vec2(5, 2), 0.03f));
            break;
        case PR_BUSH:
            drawMesh(G.bush, base * S(p.scale) * RZ(sway * 2.0f), mat(vec3(1), T_LEAF, vec2(2, 2), 0.03f));
            break;
        case PR_ROCK:
            drawMesh(G.rockDec[p.variant], base * S(p.scale), mat(vec3(1), T_ROCK, vec2(1.5f, 1.5f), 0.12f));
            break;
        case PR_PILLAR:
            drawMesh(p.variant ? G.pillarBroken : G.pillarFull, base * S(p.scale), mat(vec3(1), T_RUIN, vec2(1, 1), 0.05f));
            break;
        case PR_STATUE:
            drawMesh(G.statue, base * S(p.scale), mat(vec3(1), T_RUIN, vec2(1, 1), 0.08f));
            break;
        case PR_GATE:
            drawMesh(G.gate, F * T(0, 0, -p.s), mat(vec3(1), T_RUIN, vec2(1, 1), 0.05f));
            break;
        }
    }
    // torches
    for (const Torch& t : sg.torches) {
        vec3 w = segPos(sg, t.s, t.side * 3.9f, 1.8f);
        if (!inView(w, 2.5f)) continue;
        drawMesh(G.torch, F * T(t.side * 3.9f, 0, -t.s), mat(vec3(1), T_NONE, vec2(1), 0.1f));
        float fl = 1.0f + 0.25f * std::sin(animTime * 11.0f + t.s) + 0.15f * std::sin(animTime * 23.0f + t.s * 2.0f);
        drawMesh(G.flame, F * T(t.side * 3.9f, 1.95f, -t.s) * S(0.17f, 0.55f * fl, 0.17f), mat(vec3(1.0f, 0.62f, 0.12f), T_NONE, vec2(1), 0.0f, 8.0f, 1.0f));
        drawMesh(G.flame, F * T(t.side * 3.9f, 1.97f, -t.s) * S(0.09f, 0.38f * fl, 0.09f), mat(vec3(1.0f, 0.95f, 0.55f), T_NONE, vec2(1), 0.0f, 8.0f, 1.0f));
    }
}

// ---- obstacles --------------------------------------------------------
inline void drawObstacle(const Segment& sg, const Obstacle& o, const DrawTimes& tm) {
    mat4 F = segFrame(sg);
    vec3 wc = segPos(sg, o.s, 0.0f, 1.0f);
    if (!inView(wc, 7.0f)) return;
    float x0[3], x1[3];
    int nr = laneRuns(o.mask, x0, x1);
    switch (o.type) {
    case O_LOG:
        for (int i = 0; i < nr; i++) {
            float w = x1[i] - x0[i];
            drawMesh(G.cyl, F * T(x0[i], 0.40f, -o.s) * RZ(-PI / 2) * S(0.40f, w, 0.40f), mat(vec3(1), T_BARK, vec2(3, w / 1.5f), 0.03f));
            drawMesh(G.cyl, F * T(x0[i] + w * 0.3f, 0.65f, -o.s - 0.15f) * RZ(-PI / 2 + 0.2f) * S(0.14f, 0.7f, 0.14f), mat(vec3(1), T_BARK, vec2(1, 1), 0.03f));
        }
        break;
    case O_ROCK:
        for (int lane = 0; lane < 3; lane++) if ((o.mask >> lane) & 1)
            drawMesh(G.rockObs[o.variant], F * T(LANE_X[lane], 0, -o.s) * RY(o.phase), mat(vec3(1), T_ROCK, vec2(1.5f, 1.5f), 0.12f));
        break;
    case O_WALL:
        for (int i = 0; i < nr; i++) {
            float w = x1[i] - x0[i], cx = (x0[i] + x1[i]) * 0.5f;
            drawMesh(G.cube, F * T(cx, 1.3f, -o.s) * S(w - 0.1f, 2.6f, 0.9f), mat(vec3(1), T_RUIN, vec2(w / 2.0f, 1.3f), 0.05f));
            drawMesh(G.cube, F * T(cx, 2.68f, -o.s) * S(w + 0.1f, 0.22f, 1.1f), mat(vec3(0.9f), T_BRICK, vec2(w / 2.0f, 0.3f), 0.05f));
        }
        break;
    case O_LOWBAR:
        for (int i = 0; i < nr; i++) {
            float w = x1[i] - x0[i], cx = (x0[i] + x1[i]) * 0.5f;
            drawMesh(G.cube, F * T(cx, 0.72f, -o.s) * S(w - 0.2f, 0.34f, 0.55f), mat(vec3(1), T_RUIN, vec2(w / 2.0f, 0.3f), 0.05f));
            drawMesh(G.cube, F * T(x0[i] + 0.25f, 0.5f, -o.s) * S(0.4f, 1.0f, 0.55f), mat(vec3(0.9f), T_RUIN, vec2(0.4f, 0.5f), 0.05f));
            drawMesh(G.cube, F * T(x1[i] - 0.25f, 0.5f, -o.s) * S(0.4f, 1.0f, 0.55f), mat(vec3(0.9f), T_RUIN, vec2(0.4f, 0.5f), 0.05f));
        }
        break;
    case O_OVERHEAD:
        for (int i = 0; i < nr; i++) {
            float w = x1[i] - x0[i], cx = (x0[i] + x1[i]) * 0.5f;
            drawMesh(G.cube, F * T(cx, 1.875f, -o.s) * S(w, 1.45f, 0.9f), mat(vec3(1), T_BRICK, vec2(w / 2.0f, 0.7f), 0.05f));
            drawMesh(G.cube, F * T(x0[i] + 0.2f, 1.3f, -o.s) * S(0.4f, 2.6f, 0.9f), mat(vec3(0.9f), T_RUIN, vec2(0.4f, 1.3f), 0.05f));
            drawMesh(G.cube, F * T(x1[i] - 0.2f, 1.3f, -o.s) * S(0.4f, 2.6f, 0.9f), mat(vec3(0.9f), T_RUIN, vec2(0.4f, 1.3f), 0.05f));
            for (int k = 0; k < 3; k++)           // hanging vines under the beam
                drawMesh(G.cyl, F * T(cx - w * 0.3f + k * w * 0.3f, 0.75f + 0.1f * k, -o.s + 0.46f) * S(0.03f, 0.5f - 0.05f * k, 0.03f), mat(vec3(0.15f, 0.4f, 0.1f), T_NONE, vec2(1), 0.0f));
        }
        break;
    case O_SPIKES: {
        float ext = clampf((spikeExtend(o, tm.game) - 0.15f) / 0.5f, 0.0f, 1.0f);
        for (int lane = 0; lane < 3; lane++) if ((o.mask >> lane) & 1) {
            drawMesh(G.cube, F * T(LANE_X[lane], 0.05f, -o.s) * S(1.9f, 0.1f, 2.2f), mat(vec3(0.22f, 0.22f, 0.25f), T_NONE, vec2(1), 0.3f));
            for (int a = 0; a < 3; a++) for (int b = 0; b < 4; b++)
                drawMesh(G.cone, F * T(LANE_X[lane] - 0.6f + a * 0.6f, 0.08f, -(o.s - 0.8f + b * 0.53f)) * S(0.12f, 0.08f + 0.5f * ext, 0.12f),
                         mat(vec3(0.75f, 0.77f, 0.82f), T_NONE, vec2(1), 0.7f, 50.0f));
        }
        break;
    }
    case O_FIRE:
        for (int lane = 0; lane < 3; lane++) if ((o.mask >> lane) & 1) {
            float cx = LANE_X[lane];
            drawMesh(G.cube, F * T(cx, 0.08f, -o.s) * S(1.9f, 0.16f, 1.5f), mat(vec3(0.12f, 0.1f, 0.1f), T_NONE, vec2(1), 0.2f));
            drawMesh(G.cube, F * T(cx, 0.17f, -o.s) * S(1.6f, 0.03f, 1.2f), mat(vec3(1.0f, 0.3f, 0.05f), T_NONE, vec2(1), 0.0f, 8.0f, 1.0f));
            for (int k = 0; k < 4; k++) {
                float fx = (k % 2 ? 0.45f : -0.45f), fz = (k < 2 ? -0.35f : 0.35f);
                float h = 1.1f + 0.45f * std::sin(tm.anim * 9.0f + k * 2.1f + lane);
                drawMesh(G.flame, F * T(cx + fx, 0.16f, -(o.s + fz)) * S(0.34f, h, 0.34f), mat(vec3(1.0f, 0.30f, 0.03f), T_NONE, vec2(1), 0.0f, 8.0f, 0.75f));
                drawMesh(G.flame, F * T(cx + fx, 0.17f, -(o.s + fz)) * S(0.2f, h * 0.7f, 0.2f), mat(vec3(1.0f, 0.75f, 0.2f), T_NONE, vec2(1), 0.0f, 8.0f, 0.8f));
            }
        }
        break;
    case O_BLADE: {
        float a = bladeAngle(o, tm.game);
        for (int side = -1; side <= 1; side += 2)
            drawMesh(G.cyl, F * T(side * 3.7f, 0, -o.s) * S(0.42f, 6.0f, 0.42f), mat(vec3(1), T_RUIN, vec2(2, 3), 0.05f));
        drawMesh(G.cube, F * T(0, 6.1f, -o.s) * S(8.2f, 0.6f, 0.8f), mat(vec3(1), T_BRICK, vec2(4, 0.5f), 0.05f));
        mat4 pivot = F * T(0, BLADE_PIVOT, -o.s) * RZ(a);
        drawMesh(G.cyl, pivot * T(0, -BLADE_LEN, 0) * S(0.07f, BLADE_LEN, 0.07f), mat(vec3(0.35f, 0.3f, 0.28f), T_NONE, vec2(1), 0.3f));
        drawMesh(G.cyl, pivot * T(0, -BLADE_LEN, 0) * RX(PI / 2) * T(0, -0.06f, 0) * S(0.75f, 0.12f, 0.75f), mat(vec3(0.7f, 0.72f, 0.78f), T_NONE, vec2(1), 0.9f, 60.0f));
        drawMesh(G.torus_unit, pivot * T(0, -BLADE_LEN, 0) * S(0.75f), mat(vec3(0.9f, 0.2f, 0.1f), T_NONE, vec2(1), 0.3f, 30.0f, 0.4f));
        for (int k = 0; k < 8; k++)                     // spinning saw teeth
            drawMesh(G.cone, pivot * T(0, -BLADE_LEN, 0) * RZ(k * PI / 4 + tm.anim * 5.0f) * T(0, 0.62f, 0) * S(0.13f, 0.3f, 0.13f),
                     mat(vec3(0.8f, 0.82f, 0.88f), T_NONE, vec2(1), 0.9f, 60.0f));
        break;
    }
    case O_MOVWALL: {
        float c = movWallX(o, tm.game);
        drawMesh(G.cube, F * T(0, 0.1f, -o.s) * S(7.0f, 0.2f, 0.6f), mat(vec3(0.3f, 0.3f, 0.32f), T_NONE, vec2(1), 0.3f));
        drawMesh(G.cube, F * T(0, 2.7f, -o.s) * S(7.0f, 0.2f, 0.6f), mat(vec3(0.3f, 0.3f, 0.32f), T_NONE, vec2(1), 0.3f));
        drawMesh(G.cube, F * T(c, 1.2f, -o.s) * S(1.9f, 2.4f, 0.8f), mat(vec3(1), T_BRICK, vec2(1, 1.2f), 0.05f));
        break;
    }
    case O_FALLROCK: {
        float x = LANE_X[lowestLane(o.mask)];
        if (o.landed) {
            drawMesh(G.rockObs[o.variant], F * T(x, 0, -o.s) * RY(o.phase), mat(vec3(1), T_ROCK, vec2(1.5f, 1.5f), 0.12f));
        } else {
            float pulse = 1.0f + 0.12f * std::sin(tm.anim * 8.0f);
            drawMesh(G.cyl, F * T(x, 0.04f, -o.s) * S(1.0f * pulse, 0.02f, 1.0f * pulse), mat(vec3(0.9f, 0.1f, 0.05f), T_NONE, vec2(1), 0.0f, 8.0f, 0.9f));
            if (o.fallT >= 0)
                drawMesh(G.rockObs[o.variant], F * T(x, fallRockY(o), -o.s) * RY(o.phase + o.fallT * 3.0f), mat(vec3(1), T_ROCK, vec2(1.5f, 1.5f), 0.12f));
        }
        break;
    }
    default: break;
    }
}

// ---- coins and power-ups -----------------------------------------------
inline void drawCollectibles(const Segment& sg, const DrawTimes& tm) {
    for (const Coin& c : sg.coins) {
        if (c.taken) continue;
        vec3 w = c.attracted ? c.wpos : segPos(sg, c.s, c.lat, c.y);
        if (!inView(w, 0.8f)) continue;
        drawMesh(G.coin, T(w) * RY(tm.anim * 3.2f + c.s * 0.7f), mat(vec3(1), T_NONE, vec2(1), 0.8f, 40.0f, 0.35f));
    }
    for (const PowerUp& p : sg.pups) {
        if (p.taken) continue;
        vec3 w = segPos(sg, p.s, LANE_X[p.lane], 1.25f + 0.12f * std::sin(tm.anim * 3.0f));
        if (!inView(w, 1.5f)) continue;
        const Mesh& me = p.type == PW_MAGNET ? G.magnet : (p.type == PW_BOOST ? G.boost : G.shield);
        mat4 M = T(w) * RY(tm.anim * 2.0f) * S(1.35f);
        drawMesh(me, M, mat(vec3(1), T_NONE, vec2(1), 0.8f, 40.0f, 0.55f));
        // soft glow ring under it so power-ups stand out from coins
        vec3 gc = p.type == PW_MAGNET ? vec3(1.0f, 0.3f, 0.3f) : (p.type == PW_BOOST ? vec3(0.3f, 0.9f, 1.0f) : vec3(0.4f, 0.6f, 1.0f));
        drawMesh(G.ring, T(segPos(sg, p.s, LANE_X[p.lane], 0.06f)) * RY(tm.anim) * S(0.55f + 0.05f * std::sin(tm.anim * 4.0f)), mat(gc, T_NONE, vec2(1), 0.0f, 8.0f, 1.0f));
    }
}
