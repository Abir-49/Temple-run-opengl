// =====================================================================
//  world.h  --  the endless level
//
//  The level is a chain of STRAIGHT SEGMENTS. Every segment ends in a
//  junction pad (a square) where the path turns left, right or forks
//  (both).  Segments are created lazily in front of the player and
//  forgotten once the player has left them -- so the world never ends
//  and never uses much memory.
//
//  Every object (obstacle, coin, tree ...) belongs to a segment and is
//  stored in that segment's LOCAL coordinates:
//        s   = distance along the segment (forward)
//        lat = sideways offset (+ = right of the running direction)
//        y   = height
//  A segment's frame converts local -> world:  (lat, y, -s) in a frame
//  that is moved to seg.start and rotated by seg.yaw.
// =====================================================================
#pragma once
#include "models.h"

constexpr float LANE_X[3]   = { -2.0f, 0.0f, 2.0f };   // lane centres
constexpr float LANE_W      = 2.0f;
constexpr float PAD         = 7.0f;                    // junction square size
constexpr float TURN_WINDOW = 20.0f;                   // how early a turn key counts

enum Junction  { J_FORK, J_LEFT, J_RIGHT };
enum ObsType   { O_LOG, O_ROCK, O_WALL, O_LOWBAR, O_OVERHEAD, O_SPIKES, O_FIRE, O_BLADE, O_MOVWALL, O_FALLROCK, O_GAP };
enum PowerType { PW_MAGNET, PW_BOOST, PW_SHIELD };
enum PropKind  { PR_TREE, PR_PINE, PR_BUSH, PR_ROCK, PR_PILLAR, PR_STATUE, PR_GATE };

struct Obstacle {
    ObsType type;
    int   mask;               // which lanes it occupies (bit0 left, bit1 centre, bit2 right)
    float s;                  // position along the segment
    float depth;              // thickness along the running direction
    float yMin, yMax;         // vertical extent of the hit box
    float phase = 0;          // animation phase (moving obstacles)
    float fallT = -1;         // falling rock: <0 = waiting, >=0 = time since it started to fall
    bool  landed = false;
    bool  hit = false;        // already hit the player (never hit twice)
    int   variant = 0;
};
struct Gap     { float s0, s1; int mask; bool lava; };         // missing floor tiles
struct Coin    { float s, lat, y; bool taken = false, attracted = false; vec3 wpos = vec3(0); };
struct PowerUp { PowerType type; float s; int lane; bool taken = false; };
struct Prop    { PropKind kind; float s, lat, scale, rot; int variant; };
struct Torch   { float s; int side; };

struct Segment {
    int      id = 0;
    vec3     start = vec3(0);
    float    yaw = 0;
    float    L = 100;
    Junction junction = J_FORK;
    bool     first = false;
    vec3     dir = vec3(0, 0, -1), right = vec3(1, 0, 0);
    std::vector<Obstacle> obs;
    std::vector<Gap>      gaps;
    std::vector<Coin>     coins;
    std::vector<PowerUp>  pups;
    std::vector<Prop>     props;
    std::vector<Torch>    torches;
    std::shared_ptr<Segment> child[2];     // [0] = left arm, [1] = right arm
    bool childrenMade = false;
    bool canLeft()  const { return junction != J_RIGHT; }
    bool canRight() const { return junction != J_LEFT;  }
};
using SegPtr = std::shared_ptr<Segment>;

inline int gSegCounter = 0;
inline float difficulty() { return clampf(gSegCounter / 14.0f, 0.0f, 1.0f); }

// ---- coordinate helpers ---------------------------------------------
inline vec3 segPos(const Segment& s, float sPos, float lat, float y = 0.0f) {
    return s.start + s.dir * sPos + s.right * lat + vec3(0, y, 0);
}
inline mat4 segFrame(const Segment& s) { return T(s.start) * RY(s.yaw); }   // local (lat, y, -s) -> world

// ---- lane helpers ---------------------------------------------------
inline int randomMask(int minLanes, int maxLanes) {
    int n = irange(minLanes, maxLanes);
    if (n >= 3) return 7;
    if (n == 2) { int r = irange(0, 2); return r == 0 ? 3 : (r == 1 ? 6 : 5); }
    return 1 << irange(0, 2);
}
// contiguous groups of lanes -> lateral extents
inline int laneRuns(int mask, float x0[3], float x1[3]) {
    int n = 0;
    for (int i = 0; i < 3;) {
        if (!((mask >> i) & 1)) { i++; continue; }
        int j = i;
        while (j + 1 < 3 && ((mask >> (j + 1)) & 1)) j++;
        x0[n] = LANE_X[i] - LANE_W * 0.5f;
        x1[n] = LANE_X[j] + LANE_W * 0.5f;
        n++; i = j + 1;
    }
    return n;
}
inline int lowestLane(int mask) { for (int i = 0; i < 3; i++) if ((mask >> i) & 1) return i; return 1; }
inline int freeLane(int mask) {
    int c[3], n = 0;
    for (int i = 0; i < 3; i++) if (!((mask >> i) & 1)) c[n++] = i;
    return n ? c[irange(0, n - 1)] : irange(0, 2);
}

// ---- moving obstacle formulas (shared by drawing and collision) ------
constexpr float BLADE_LEN = 4.4f, BLADE_PIVOT = 5.5f;
inline float bladeAngle(const Obstacle& o, float t)  { return 0.85f * std::sin(t * 2.2f + o.phase); }
inline float movWallX(const Obstacle& o, float t)    { return 2.0f * std::sin(t * 1.5f + o.phase); }
inline float spikeExtend(const Obstacle& o, float t) { return 0.5f + 0.5f * std::sin(t * 2.6f + o.phase); }   // 0..1
inline float fallRockY(const Obstacle& o)            { return std::max(0.0f, 16.0f - 14.0f * o.fallT * o.fallT); }

// Hit boxes (in segment coordinates) of an obstacle at game time t.
struct HitBox { float x0, x1, y0, y1, s0, s1; };
inline int obstacleBoxes(const Obstacle& o, float t, HitBox out[4]) {
    int n = 0;
    float hd = o.depth * 0.5f;
    switch (o.type) {
    case O_BLADE: {
        float a = bladeAngle(o, t);
        float bx = BLADE_LEN * std::sin(a), by = BLADE_PIVOT - BLADE_LEN * std::cos(a);
        out[n++] = { bx - 0.75f, bx + 0.75f, by - 0.75f, by + 0.75f, o.s - 0.35f, o.s + 0.35f };
        return n;
    }
    case O_MOVWALL: {
        float c = movWallX(o, t);
        out[n++] = { c - 1.0f, c + 1.0f, 0.0f, 2.4f, o.s - 0.45f, o.s + 0.45f };
        return n;
    }
    case O_FALLROCK: {
        if (!o.landed && o.fallT < 0) return 0;
        float fy = o.landed ? 0.0f : fallRockY(o);
        float x0[3], x1[3]; int r = laneRuns(o.mask, x0, x1);
        for (int i = 0; i < r; i++) out[n++] = { x0[i] + 0.2f, x1[i] - 0.2f, fy, fy + 2.4f, o.s - 0.9f, o.s + 0.9f };
        return n;
    }
    case O_SPIKES: {
        if (spikeExtend(o, t) < 0.3f) return 0;       // spikes are retracted
        break;
    }
    default: break;
    }
    float x0[3], x1[3]; int r = laneRuns(o.mask, x0, x1);
    for (int i = 0; i < r; i++) {
        float shrink = (o.type == O_ROCK || o.type == O_FALLROCK) ? 0.2f : 0.0f;
        out[n++] = { x0[i] + shrink, x1[i] - shrink, o.yMin, o.yMax, o.s - hd, o.s + hd };
    }
    return n;
}

// is there floor under (sPos, lat) ?
inline bool hasGround(const Segment& sg, float sPos, float lat) {
    if (sPos < 0 || sPos > sg.L) return true;
    int lane = lat < -1.0f ? 0 : (lat > 1.0f ? 2 : 1);
    for (const Gap& g : sg.gaps)
        if (((g.mask >> lane) & 1) && sPos > g.s0 + 0.2f && sPos < g.s1 - 0.2f) return false;
    return true;
}

// ---------------------------------------------------------------------
//  Level generator
// ---------------------------------------------------------------------
inline void addCoinLine(Segment& sg, float s0, int lane, int n, float spacing = 1.6f, float y = 0.9f) {
    for (int i = 0; i < n; i++) { Coin c; c.s = s0 + i * spacing; c.lat = LANE_X[lane]; c.y = y; sg.coins.push_back(c); }
}
// a hump of coins that arcs over an obstacle (invites the player to jump)
inline void addCoinArc(Segment& sg, float sCenter, int lane, float halfLen, float peak) {
    int n = 7;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (n - 1) * 2.0f - 1.0f;       // -1 .. 1
        Coin c; c.s = sCenter + t * halfLen; c.lat = LANE_X[lane]; c.y = 0.9f + peak * (1.0f - t * t);
        sg.coins.push_back(c);
    }
}

inline void generateScenery(Segment& sg) {
    float L = sg.L;
    float sStart = sg.first ? -16.0f : 3.0f;
    float sEnd = L - 20.0f;                     // keep the junction area clear
    for (int side = -1; side <= 1; side += 2) {
        for (int row = 0; row < 3; row++) {
            float s = sStart + frange(0.0f, 4.0f);
            while (s < sEnd) {
                Prop p;
                float r = frand();
                p.kind = r < 0.50f ? PR_TREE : (r < 0.72f ? PR_PINE : (r < 0.88f ? PR_BUSH : PR_ROCK));
                if (row == 0 && p.kind == PR_TREE && chance(0.25f)) p.kind = PR_BUSH;
                // big trees keep their canopy off the path; bushes and rocks may sit close to it
                bool big = (p.kind == PR_TREE || p.kind == PR_PINE);
                float latMin = (big ? 7.2f : 4.4f) + row * (big ? 5.5f : 3.5f), latMax = latMin + 4.5f;
                p.s = s;
                p.lat = side * frange(latMin, latMax);
                p.scale = frange(0.85f, 1.35f) * (p.kind == PR_BUSH ? 0.9f : 1.0f);
                p.rot = frange(0.0f, TAU);
                p.variant = irange(0, 2);
                sg.props.push_back(p);
                s += frange(4.0f, 8.0f) + row * 2.5f;
            }
        }
    }
    // ancient ruins right next to the path
    int ruins = irange(2, 4);
    for (int i = 0; i < ruins; i++) {
        Prop p; p.s = frange(sStart + 4, sEnd - 4); p.lat = (chance(0.5f) ? -1 : 1) * frange(4.5f, 5.3f);
        p.scale = frange(0.9f, 1.2f); p.rot = frange(0, TAU); p.variant = irange(0, 1);
        p.kind = chance(0.25f) ? PR_STATUE : PR_PILLAR;
        if (p.kind == PR_STATUE) p.rot = (p.lat > 0 ? PI / 2 : -PI / 2) * -1.0f;      // face the path
        sg.props.push_back(p);
    }
    // a gate arching over the path
    if (!sg.first && chance(0.55f)) {
        Prop g; g.kind = PR_GATE; g.s = frange(24.0f, std::max(26.0f, L - 36.0f)); g.lat = 0; g.scale = 1; g.rot = 0; g.variant = 0;
        sg.props.push_back(g);
    }
    // torches along both edges
    int side = chance(0.5f) ? -1 : 1;
    for (float s = 9.0f; s < L - 12.0f; s += 17.0f) { sg.torches.push_back({ s, side }); side = -side; }
}

inline void generateObstacles(Segment& sg) {
    float d = difficulty();
    float cur = sg.first ? 38.0f : 16.0f;
    float end = sg.L - 30.0f;
    int powerCool = irange(1, 3);
    struct W { ObsType t; float w; };
    while (cur < end) {
        float r = frand();
        if (r < 0.13f) {                                  // a plain line of coins
            int lane = irange(0, 2), n = irange(6, 10);
            addCoinLine(sg, cur, lane, n);
            cur += n * 1.6f + 7.0f;
            continue;
        }
        W table[] = { { O_LOG, 14 }, { O_ROCK, 11 }, { O_WALL, 8 }, { O_LOWBAR, 10 }, { O_OVERHEAD, 10 },
                      { O_SPIKES, 8 }, { O_FIRE, 8 }, { O_GAP, 9 },
                      { O_BLADE, d > 0.20f ? 6.0f : 0.0f }, { O_MOVWALL, d > 0.30f ? 5.0f : 0.0f },
                      { O_FALLROCK, d > 0.15f ? 6.0f : 0.0f } };
        float total = 0; for (const W& w : table) total += w.w;
        float pick = frand() * total;
        ObsType type = O_LOG;
        for (const W& w : table) { pick -= w.w; if (pick <= 0) { type = w.t; break; } }

        Obstacle o; o.type = type; o.s = cur; o.mask = 7; o.depth = 1.0f; o.yMin = 0; o.yMax = 1.0f;
        o.phase = frange(0, TAU); o.variant = irange(0, 2);
        float used = 1.0f;
        switch (type) {
        case O_LOG:      o.mask = randomMask(1, 3); o.depth = 1.0f; o.yMax = 0.8f;
                         addCoinArc(sg, cur, lowestLane(o.mask), 3.2f, 1.7f); break;
        case O_ROCK:     o.mask = randomMask(1, 2); o.depth = 1.8f; o.yMax = 2.4f; used = 1.8f;
                         addCoinLine(sg, cur - 3.0f, freeLane(o.mask), 6, 1.4f); break;
        case O_WALL:     o.mask = chance(0.5f) ? 3 : 6; o.depth = 1.0f; o.yMax = 2.6f;
                         addCoinLine(sg, cur - 3.0f, freeLane(o.mask), 6, 1.4f); break;
        case O_LOWBAR:   o.mask = randomMask(1, 3); o.depth = 0.7f; o.yMax = 0.95f;
                         addCoinArc(sg, cur, lowestLane(o.mask), 3.2f, 1.7f); break;
        case O_OVERHEAD: o.mask = randomMask(1, 3); o.depth = 0.9f; o.yMin = 1.15f; o.yMax = 2.6f;
                         addCoinLine(sg, cur - 1.6f, lowestLane(o.mask), 3, 1.6f, 0.55f); break;
        case O_SPIKES:   o.mask = randomMask(1, 3); o.depth = 2.2f; o.yMax = 0.5f; used = 2.2f;
                         addCoinArc(sg, cur, lowestLane(o.mask), 3.4f, 1.6f); break;
        case O_FIRE:     o.mask = randomMask(1, 2); o.depth = 1.6f; o.yMax = 1.0f; used = 1.6f;
                         addCoinArc(sg, cur, lowestLane(o.mask), 3.2f, 1.8f); break;
        case O_BLADE:    o.mask = 7; o.depth = 1.0f; o.yMax = 6.0f; break;
        case O_MOVWALL:  o.mask = 7; o.depth = 0.9f; o.yMax = 2.4f; break;
        case O_FALLROCK: o.mask = 1 << irange(0, 2); o.depth = 1.8f; o.yMax = 2.4f; used = 1.8f; break;
        case O_GAP: {
            float len = frange(3.8f, 5.0f);
            bool narrow = chance(0.30f);
            sg.gaps.push_back({ cur, cur + len, narrow ? 5 : 7, chance(0.45f) });
            addCoinArc(sg, cur + len * 0.5f, 1, len * 0.75f, 1.5f);
            used = len;
            break;
        }
        default: break;
        }
        if (type != O_GAP) sg.obs.push_back(o);
        cur += used + frange(11.0f, 17.0f) * (1.0f - 0.3f * d);

        if (--powerCool <= 0 && chance(0.55f)) {          // power-up between obstacles
            PowerUp p; p.type = (PowerType)irange(0, 2); p.s = cur - 6.0f; p.lane = irange(0, 2);
            if (p.s < end) sg.pups.push_back(p);
            powerCool = irange(4, 7);
        }
    }
}

inline SegPtr makeSegment(const vec3& start, float yaw, bool first) {
    SegPtr sg = std::make_shared<Segment>();
    sg->id = gSegCounter++;
    sg->start = start;
    sg->yaw = yaw;
    sg->dir = dirFromYaw(yaw);
    sg->right = rightFromYaw(yaw);
    sg->first = first;
    sg->L = first ? 120.0f : (float)irange(74, 112);
    float r = frand();
    sg->junction = r < 0.5f ? J_FORK : (r < 0.75f ? J_LEFT : J_RIGHT);
    generateScenery(*sg);
    generateObstacles(*sg);
    return sg;
}

// Create the arms that leave the junction at the end of `s` (done lazily).
inline void ensureChildren(Segment& s) {
    if (s.childrenMade) return;
    s.childrenMade = true;
    vec3 C = s.start + s.dir * (s.L + PAD * 0.5f);          // centre of the junction pad
    auto mk = [&](float yawDelta) {
        float yaw2 = s.yaw + yawDelta;
        return makeSegment(C + dirFromYaw(yaw2) * (PAD * 0.5f), yaw2, false);
    };
    if (s.canLeft())  s.child[0] = mk(+PI / 2);
    if (s.canRight()) s.child[1] = mk(-PI / 2);
}
