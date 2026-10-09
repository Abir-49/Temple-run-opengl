

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>

using glm::vec3;
using glm::mat4;

// --------------------------------------------------------------
// ------------------------  CONSTANTS  --------------------------
// --------------------------------------------------------------

const int   WIN_W = 900, WIN_H = 650;
const float LANE_X[3] = { -2.0f, 0.0f, 2.0f };
const float FORWARD_SPEED         = 9.0f;
const float LANE_SWITCH_SPEED     = 10.0f;
const float CHASE_DISTANCE_NORMAL = 4.0f;
const float CHASE_DISTANCE_HIT    = 1.3f;
const float CHASE_RECOVER_SPEED   = 1.2f;
const float GAME_OVER_DISTANCE    = 1.4f;
const float WORLD_RECYCLE_LENGTH  = 220.0f;
const float PLAYER_COLLIDE_RADIUS = 0.9f;
const float COIN_COLLIDE_RADIUS   = 0.9f;

// --------------------------------------------------------------
// ------------------------  MESH SYSTEM  --------------------------
// --------------------------------------------------------------
// Every mesh is a flat list of (position.xyz, normal.xyz) floats,
// drawn with glDrawArrays(GL_TRIANGLES, ...) -- no index buffer,
// kept simple on purpose for a first prototype.

struct Mesh {
    GLuint vao = 0, vbo = 0;
    GLsizei vertexCount = 0;
};

// Pushes one flat-shaded triangle (face normal computed automatically).
static void pushFlatTri(std::vector<float>& v, vec3 a, vec3 b, vec3 c) {
    vec3 n = glm::normalize(glm::cross(b - a, c - a));
    for (vec3 p : { a, b, c }) {
        v.push_back(p.x); v.push_back(p.y); v.push_back(p.z);
        v.push_back(n.x); v.push_back(n.y); v.push_back(n.z);
    }
}

// Pushes one triangle with explicit per-vertex normals (for smooth shapes).
static void pushSmoothTri(std::vector<float>& v, vec3 a, vec3 na, vec3 b, vec3 nb, vec3 c, vec3 nc) {
    v.insert(v.end(), { a.x,a.y,a.z, na.x,na.y,na.z });
    v.insert(v.end(), { b.x,b.y,b.z, nb.x,nb.y,nb.z });
    v.insert(v.end(), { c.x,c.y,c.z, nc.x,nc.y,nc.z });
}

Mesh uploadMesh(const std::vector<float>& data) {
    Mesh m;
    m.vertexCount = static_cast<GLsizei>(data.size() / 6);
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    // layout(location=0) = position, layout(location=1) = normal
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return m;
}

// Unit cube, centered at origin, extents -0.5..0.5
Mesh makeCube() {
    std::vector<float> v;
    vec3 p[8] = {
        {-.5f,-.5f,-.5f}, {.5f,-.5f,-.5f}, {.5f,.5f,-.5f}, {-.5f,.5f,-.5f},
        {-.5f,-.5f, .5f}, {.5f,-.5f, .5f}, {.5f,.5f, .5f}, {-.5f,.5f, .5f}
    };
    int faces[6][4] = { {0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0} };
    for (auto& f : faces) {
        pushFlatTri(v, p[f[0]], p[f[1]], p[f[2]]);
        pushFlatTri(v, p[f[0]], p[f[2]], p[f[3]]);
    }
    return uploadMesh(v);
}

// Unit sphere (radius 1) centered at origin, smooth-shaded.
Mesh makeSphere(int stacks = 14, int slices = 20) {
    std::vector<float> v;
    for (int i = 0; i < stacks; i++) {
        float lat0 = glm::pi<float>() * (-0.5f + (float)i / stacks);
        float lat1 = glm::pi<float>() * (-0.5f + (float)(i + 1) / stacks);
        for (int j = 0; j < slices; j++) {
            float lon0 = 2.0f * glm::pi<float>() * (float)j / slices;
            float lon1 = 2.0f * glm::pi<float>() * (float)(j + 1) / slices;

            auto sph = [](float lat, float lon) {
                return vec3(cosf(lat) * cosf(lon), sinf(lat), cosf(lat) * sinf(lon));
            };
            vec3 p00 = sph(lat0, lon0), p01 = sph(lat0, lon1);
            vec3 p10 = sph(lat1, lon0), p11 = sph(lat1, lon1);
            // normals of a unit sphere == its position
            pushSmoothTri(v, p00, p00, p11, p11, p01, p01);
            pushSmoothTri(v, p00, p00, p10, p10, p11, p11);
        }
    }
    return uploadMesh(v);
}

// Unit cylinder: radius 1, running from y=0 (base) to y=1 (top), capped.
Mesh makeCylinder(int segments = 16) {
    std::vector<float> v;
    for (int i = 0; i < segments; i++) {
        float a0 = 2.0f * glm::pi<float>() * i / segments;
        float a1 = 2.0f * glm::pi<float>() * (i + 1) / segments;
        vec3 b0(cosf(a0), 0.0f, sinf(a0)), b1(cosf(a1), 0.0f, sinf(a1));
        vec3 t0(cosf(a0), 1.0f, sinf(a0)), t1(cosf(a1), 1.0f, sinf(a1));
        // side (flat-shaded quad -> 2 triangles; close enough for a low-poly look)
        pushFlatTri(v, b0, b1, t1);
        pushFlatTri(v, b0, t1, t0);
        // caps
        pushFlatTri(v, vec3(0, 0, 0), b1, b0);
        pushFlatTri(v, vec3(0, 1, 0), t0, t1);
    }
    return uploadMesh(v);
}

// Unit cone: base radius 1 at y=0, apex point at y=1. `segments`=4 gives a
// pyramid (used for the temple roof); a higher count gives a smooth cone.
Mesh makeCone(int segments = 4) {
    std::vector<float> v;
    vec3 apex(0, 1, 0);
    for (int i = 0; i < segments; i++) {
        float a0 = 2.0f * glm::pi<float>() * i / segments;
        float a1 = 2.0f * glm::pi<float>() * (i + 1) / segments;
        vec3 b0(cosf(a0), 0.0f, sinf(a0)), b1(cosf(a1), 0.0f, sinf(a1));
        pushFlatTri(v, b0, b1, apex);
        pushFlatTri(v, vec3(0, 0, 0), b1, b0); // base cap
    }
    return uploadMesh(v);
}

// --------------------------------------------------------------
// ---------------------------  SHADERS  ------------------------------
// --------------------------------------------------------------

const char* VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec4 world = model * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    // NOTE: for a beginner prototype we approximate the normal matrix
    // with mat3(model). This is only exact for uniform scale, but is
    // a common simplification for course projects.
    vNormal = mat3(model) * aNormal;
    gl_Position = proj * view * world;
}
)";

const char* FRAG_SRC = R"(
#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
out vec4 FragColor;

uniform vec3 objectColor;
uniform vec3 lightPos0;   // main light
uniform vec3 lightColor0;
uniform vec3 lightPos1;   // fill light
uniform vec3 lightColor1;
uniform vec3 viewPos;

void main() {
    vec3 N = normalize(vNormal);
    vec3 ambient = 0.30 * objectColor;

    vec3 L0 = normalize(lightPos0 - vWorldPos);
    float diff0 = max(dot(N, L0), 0.0);

    vec3 L1 = normalize(lightPos1 - vWorldPos);
    float diff1 = max(dot(N, L1), 0.0);

    vec3 V = normalize(viewPos - vWorldPos);
    vec3 R0 = reflect(-L0, N);
    float spec0 = pow(max(dot(V, R0), 0.0), 24.0) * 0.25;

    vec3 result = ambient
                + diff0 * lightColor0 * objectColor
                + diff1 * lightColor1 * objectColor * 0.5
                + spec0 * lightColor0;

    FragColor = vec4(result, 1.0);
}
)";

// Simple flat-color overlay shader for the 2D "game over" screen tint.
const char* OVERLAY_VERT = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";
const char* OVERLAY_FRAG = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 tint;
void main() { FragColor = tint; }
)";

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log);
        fprintf(stderr, "Shader compile error:\n%s\n", log);
    }
    return s;
}

GLuint makeProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
    }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// --------------------------------------------------------------
// ------------------------  GAME STATE  --------------------------
// --------------------------------------------------------------

int   currentLane = 1;
float playerX = LANE_X[1];
float playerZ = 0.0f;
float runAnimAngle = 0.0f;
bool  isSlowed = false;
float slowTimer = 0.0f;

float monkeyX = LANE_X[1];
float chaseGap = CHASE_DISTANCE_NORMAL;
float monkeyAnimAngle = 0.0f;

bool gameOver = false;
int  score = 0;
int  lastPrintedScore = -1;

struct Prop { float z; int lane; bool active; };
std::vector<Prop> obstacles, coins;

float randRange(float lo, float hi) { return lo + (float)rand() / RAND_MAX * (hi - lo); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }

void initProps() {
    obstacles.clear(); coins.clear();
    for (int i = 0; i < 10; i++)
        obstacles.push_back({ -15.0f - i * 14.0f + randRange(-2,2), rand() % 3, true });
    for (int i = 0; i < 25; i++)
        coins.push_back({ -8.0f - i * 6.0f + randRange(-1,1), rand() % 3, true });
}

void resetGame() {
    gameOver = false; score = 0; lastPrintedScore = -1;
    playerZ = 0.0f; currentLane = 1; playerX = LANE_X[1];
    chaseGap = CHASE_DISTANCE_NORMAL; isSlowed = false;
    initProps();
    printf("New game started. Score: 0\n");
}

float dist2D(float x1, float z1, float x2, float z2) {
    float dx = x1 - x2, dz = z1 - z2;
    return sqrtf(dx * dx + dz * dz);
}

void recycleProps() {
    for (auto& o : obstacles) if (o.z > playerZ + 10.0f) { o.z -= WORLD_RECYCLE_LENGTH; o.lane = rand() % 3; }
    for (auto& c : coins)     if (c.z > playerZ + 10.0f) { c.z -= WORLD_RECYCLE_LENGTH; c.lane = rand() % 3; c.active = true; }
}

void checkCollisions() {
    for (auto& o : obstacles) {
        if (dist2D(playerX, playerZ, LANE_X[o.lane], o.z) < PLAYER_COLLIDE_RADIUS) {
            isSlowed = true; slowTimer = 0.6f;
            chaseGap = CHASE_DISTANCE_HIT;
            o.z -= WORLD_RECYCLE_LENGTH;
            printf("Hit an obstacle! Monkey is closing in.\n");
        }
    }
    for (auto& c : coins) {
        if (!c.active) continue;
        if (dist2D(playerX, playerZ, LANE_X[c.lane], c.z) < COIN_COLLIDE_RADIUS) {
            c.active = false; score += 10;
        }
    }
    if (chaseGap <= GAME_OVER_DISTANCE && !gameOver) {
        gameOver = true;
        printf("GAME OVER -- final score: %d (press R to restart)\n", score);
    }
}

void update(float dt) {
    if (gameOver) return;
    float speed = FORWARD_SPEED;
    if (isSlowed) {
        speed *= 0.25f;
        slowTimer -= dt;
        if (slowTimer <= 0.0f) isSlowed = false;
    }
    playerZ -= speed * dt;
    playerX = lerp(playerX, LANE_X[currentLane], std::min(1.0f, LANE_SWITCH_SPEED * dt));
    runAnimAngle += dt * (isSlowed ? 4.0f : 10.0f);

    monkeyX = lerp(monkeyX, playerX, std::min(1.0f, 6.0f * dt));
    if (chaseGap < CHASE_DISTANCE_NORMAL) {
        chaseGap = std::min(CHASE_DISTANCE_NORMAL, chaseGap + CHASE_RECOVER_SPEED * dt);
    }
    monkeyAnimAngle += dt * 11.0f;

    recycleProps();
    checkCollisions();

    if (score != lastPrintedScore) { printf("Score: %d\n", score); lastPrintedScore = score; }
}

// --------------------------------------------------------------
// ---------------------------  RENDERING  -----------------------------
// --------------------------------------------------------------

Mesh cubeMesh, sphereMesh, cylMesh, coneMesh;
GLuint sceneProgram, overlayProgram;
GLint u_model, u_view, u_proj, u_color, u_lightPos0, u_lightColor0, u_lightPos1, u_lightColor1, u_viewPos;

void drawMesh(const Mesh& m, const mat4& model, vec3 color) {
    glUniformMatrix4fv(u_model, 1, GL_FALSE, glm::value_ptr(model));
    glUniform3fv(u_color, 1, glm::value_ptr(color));
    glBindVertexArray(m.vao);
    glDrawArrays(GL_TRIANGLES, 0, m.vertexCount);
}

// ---- Humanoid used for both the player and (with different proportions) the monkey ----
void drawHumanoid(vec3 basePos, float legSwing, float armSwing, vec3 bodyColor, vec3 headColor) {
    mat4 base = glm::translate(mat4(1.0f), basePos);

    // torso
    drawMesh(cubeMesh, base * glm::translate(mat4(1.0f), vec3(0, 1.1f, 0))
                             * glm::scale(mat4(1.0f), vec3(0.5f, 0.7f, 0.3f)), bodyColor);
    // head
    drawMesh(sphereMesh, base * glm::translate(mat4(1.0f), vec3(0, 1.75f, 0))
                               * glm::scale(mat4(1.0f), vec3(0.28f)), headColor);

    // legs (cylinder mesh runs 0..1 along +Y, so translate down after rotating at the hip)
    for (int s = -1; s <= 1; s += 2) {
        float swing = (s < 0) ? legSwing : -legSwing;
        mat4 hip = base * glm::translate(mat4(1.0f), vec3(0.18f * s, 0.75f, 0))
                         * glm::rotate(mat4(1.0f), glm::radians(swing), vec3(1, 0, 0));
        mat4 leg = hip * glm::translate(mat4(1.0f), vec3(0, -0.7f, 0))
                        * glm::scale(mat4(1.0f), vec3(0.11f, 0.7f, 0.11f));
        drawMesh(cylMesh, leg, vec3(0.15f, 0.15f, 0.4f));
    }
    // arms
    for (int s = -1; s <= 1; s += 2) {
        float swing = (s < 0) ? -armSwing : armSwing;
        mat4 shoulder = base * glm::translate(mat4(1.0f), vec3(0.42f * s, 1.35f, 0))
                              * glm::rotate(mat4(1.0f), glm::radians(swing), vec3(1, 0, 0));
        mat4 arm = shoulder * glm::translate(mat4(1.0f), vec3(0, -0.6f, 0))
                             * glm::scale(mat4(1.0f), vec3(0.08f, 0.6f, 0.08f));
        drawMesh(cylMesh, arm, bodyColor);
    }
}

void drawPlayer() {
    float swing = sinf(runAnimAngle) * 35.0f;
    drawHumanoid(vec3(playerX, 0, playerZ), swing, swing, vec3(0.85f, 0.35f, 0.1f), vec3(1.0f, 0.5f, 0.35f));
}

void drawMonkey() {
    float monkeyZ = playerZ + chaseGap;
    float swing = sinf(monkeyAnimAngle) * 40.0f;
    vec3 base = vec3(monkeyX, 0, monkeyZ);
    drawHumanoid(base, swing, swing * 0.6f, vec3(0.42f, 0.25f, 0.12f), vec3(0.48f, 0.3f, 0.15f));

    // ears + eyes, positioned relative to the monkey's head
    mat4 head = glm::translate(mat4(1.0f), base + vec3(0, 1.55f, 0.05f));
    for (int s = -1; s <= 1; s += 2)
        drawMesh(sphereMesh, head * glm::translate(mat4(1.0f), vec3(0.28f * s, 0.18f, 0))
                                   * glm::scale(mat4(1.0f), vec3(0.11f)), vec3(0.55f, 0.35f, 0.18f));
    for (int s = -1; s <= 1; s += 2)
        drawMesh(sphereMesh, head * glm::translate(mat4(1.0f), vec3(0.11f * s, 0, 0.3f))
                                   * glm::scale(mat4(1.0f), vec3(0.045f)), vec3(0.03f, 0.03f, 0.03f));
}

void drawGround() {
    mat4 m = glm::translate(mat4(1.0f), vec3(0, -0.01f, playerZ - 80.0f))
           * glm::scale(mat4(1.0f), vec3(80.0f, 0.02f, 240.0f));
    drawMesh(cubeMesh, m, vec3(0.13f, 0.35f, 0.13f));
}

void drawPath() {
    drawMesh(cubeMesh, glm::translate(mat4(1.0f), vec3(0, 0, playerZ - 80.0f))
                      * glm::scale(mat4(1.0f), vec3(6.4f, 0.02f, 240.0f)), vec3(0.55f, 0.52f, 0.48f));
    for (int i = -1; i <= 1; i += 2)
        drawMesh(cubeMesh, glm::translate(mat4(1.0f), vec3(i * 1.0f, 0.01f, playerZ - 80.0f))
                          * glm::scale(mat4(1.0f), vec3(0.06f, 0.03f, 240.0f)), vec3(0.35f, 0.33f, 0.30f));
    for (int side = -1; side <= 1; side += 2)
        drawMesh(cubeMesh, glm::translate(mat4(1.0f), vec3(side * 3.4f, 0.02f, playerZ - 80.0f))
                          * glm::scale(mat4(1.0f), vec3(0.5f, 0.05f, 240.0f)), vec3(0.30f, 0.27f, 0.24f));
}

void drawTree(float x, float z) {
    drawMesh(cylMesh, glm::translate(mat4(1.0f), vec3(x, 0, z)) * glm::scale(mat4(1.0f), vec3(0.2f, 2.2f, 0.2f)),
             vec3(0.35f, 0.22f, 0.1f));
    vec3 top = vec3(x, 2.2f, z);
    drawMesh(sphereMesh, glm::translate(mat4(1.0f), top + vec3(0, 0.5f, 0)) * glm::scale(mat4(1.0f), vec3(0.9f)), vec3(0.1f, 0.45f, 0.12f));
    drawMesh(sphereMesh, glm::translate(mat4(1.0f), top + vec3(0.4f, 0.1f, 0.3f)) * glm::scale(mat4(1.0f), vec3(0.6f)), vec3(0.1f, 0.45f, 0.12f));
    drawMesh(sphereMesh, glm::translate(mat4(1.0f), top + vec3(-0.4f, 0.0f, -0.2f)) * glm::scale(mat4(1.0f), vec3(0.55f)), vec3(0.1f, 0.45f, 0.12f));
}

void drawTemple() {
    mat4 base = glm::translate(mat4(1.0f), vec3(0, 0, -180.0f));
    for (int i = 0; i < 4; i++)
        drawMesh(cubeMesh, base * glm::translate(mat4(1.0f), vec3(0, 0.15f * i, 2.0f - i * 0.6f))
                                 * glm::scale(mat4(1.0f), vec3(9.0f - i * 0.6f, 0.3f, 1.2f)), vec3(0.6f, 0.58f, 0.5f));
    drawMesh(cubeMesh, base * glm::translate(mat4(1.0f), vec3(0, 3.0f, -3.0f)) * glm::scale(mat4(1.0f), vec3(8.0f, 5.0f, 1.0f)), vec3(0.75f, 0.7f, 0.55f));
    for (int s = -1; s <= 1; s += 2)
        drawMesh(cylMesh, base * glm::translate(mat4(1.0f), vec3(3.0f * s, 0, -1.0f)) * glm::scale(mat4(1.0f), vec3(0.5f, 5.5f, 0.5f)), vec3(0.8f, 0.76f, 0.6f));
    drawMesh(coneMesh, base * glm::translate(mat4(1.0f), vec3(0, 5.5f, -3.0f)) * glm::scale(mat4(1.0f), vec3(5.5f, 2.5f, 5.5f)), vec3(0.5f, 0.2f, 0.15f));
    drawMesh(cubeMesh, base * glm::translate(mat4(1.0f), vec3(0, 2.0f, -2.4f)) * glm::scale(mat4(1.0f), vec3(1.6f, 3.0f, 0.3f)), vec3(0.05f, 0.05f, 0.05f));
}

void drawObstacle(float x, float z, int variant) {
    if (variant % 2 == 0)
        drawMesh(sphereMesh, glm::translate(mat4(1.0f), vec3(x, 0.4f, z)) * glm::scale(mat4(1.0f), vec3(0.5f, 0.4f, 0.5f)), vec3(0.45f, 0.43f, 0.4f));
    else
        drawMesh(cylMesh, glm::translate(mat4(1.0f), vec3(x, 0.35f, z))
                         * glm::rotate(mat4(1.0f), glm::radians(90.0f), vec3(0, 0, 1))
                         * glm::scale(mat4(1.0f), vec3(0.35f, 1.6f, 0.35f)), vec3(0.4f, 0.25f, 0.12f));
}

void drawCoin(float x, float z) {
    mat4 m = glm::translate(mat4(1.0f), vec3(x, 1.0f, z))
           * glm::rotate(mat4(1.0f), runAnimAngle * 2.5f, vec3(0, 1, 0))
           * glm::rotate(mat4(1.0f), glm::radians(90.0f), vec3(1, 0, 0))
           * glm::translate(mat4(1.0f), vec3(0, -0.04f, 0))
           * glm::scale(mat4(1.0f), vec3(0.3f, 0.08f, 0.3f));
    drawMesh(cylMesh, m, vec3(1.0f, 0.85f, 0.1f));
}

void drawSceneryAlongPath() {
    for (int i = 0; i < 20; i++) {
        float z = playerZ + 20.0f - i * 12.0f;
        drawTree(-6.0f - (i % 3), z);
        drawTree(6.0f + (i % 3), z + 4.0f);
    }
}



GLuint overlayVAO, overlayVBO;
void initOverlayQuad() {
    float verts[] = { -1,-1,  1,-1,  1,1,  -1,-1,  1,1,  -1,1 };
    glGenVertexArrays(1, &overlayVAO);
    glGenBuffers(1, &overlayVBO);
    glBindVertexArray(overlayVAO);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void drawGameOverOverlay() {
    glUseProgram(overlayProgram);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    GLint tintLoc = glGetUniformLocation(overlayProgram, "tint");
    glUniform4f(tintLoc, 0.6f, 0.0f, 0.0f, 0.35f);
    glBindVertexArray(overlayVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

// --------------------------------------------------------------
// ---------------------------  INPUT  --------------------------------
// --------------------------------------------------------------

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_A || key == GLFW_KEY_LEFT) {
        if (currentLane > 0) currentLane--;
    } else if (key == GLFW_KEY_D || key == GLFW_KEY_RIGHT) {
        if (currentLane < 2) currentLane++;
    } else if (key == GLFW_KEY_R) {
        resetGame();
    } else if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

// --------------------------------------------------------------
// -----------------------------  MAIN  -------------------------------
// --------------------------------------------------------------

int main() {
    srand((unsigned)time(nullptr));

    if (!glfwInit()) { fprintf(stderr, "Failed to init GLFW\n"); return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(WIN_W, WIN_H, "Temple Run Prototype (GLFW+GLAD)", nullptr, nullptr);
    if (!window) { fprintf(stderr, "Failed to create window\n"); glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, keyCallback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "Failed to init GLAD\n"); return -1;
    }

    glViewport(0, 0, WIN_W, WIN_H);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    // ---- build meshes once ----
    cubeMesh = makeCube();
    sphereMesh = makeSphere();
    cylMesh = makeCylinder();
    coneMesh = makeCone(4); // 4 segments -> pyramid roof
    initOverlayQuad();

    sceneProgram = makeProgram(VERT_SRC, FRAG_SRC);
    overlayProgram = makeProgram(OVERLAY_VERT, OVERLAY_FRAG);

    u_model = glGetUniformLocation(sceneProgram, "model");
    u_view = glGetUniformLocation(sceneProgram, "view");
    u_proj = glGetUniformLocation(sceneProgram, "proj");
    u_color = glGetUniformLocation(sceneProgram, "objectColor");
    u_lightPos0 = glGetUniformLocation(sceneProgram, "lightPos0");
    u_lightColor0 = glGetUniformLocation(sceneProgram, "lightColor0");
    u_lightPos1 = glGetUniformLocation(sceneProgram, "lightPos1");
    u_lightColor1 = glGetUniformLocation(sceneProgram, "lightColor1");
    u_viewPos = glGetUniformLocation(sceneProgram, "viewPos");

    initProps();

    printf("=========================================\n");
    printf(" TEMPLE RUN PROTOTYPE (GLFW + GLAD + OpenGL)\n");
    printf(" Controls: A/D or LEFT/RIGHT arrows = change lane\n");
    printf("           R = restart after Game Over\n");
    printf("           ESC = quit\n");
    printf(" Score and Game Over messages print to this console.\n");
    printf("=========================================\n");

    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        if (dt > 0.1f) dt = 0.1f;
        lastTime = now;

        glfwPollEvents();
        update(dt);

        glClearColor(0.55f, 0.75f, 0.92f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ---- camera (third person, behind + above the player) ----
        vec3 eye(playerX * 0.6f, 3.2f, playerZ + 7.5f);
        vec3 target(playerX * 0.6f, 1.2f, playerZ - 10.0f);
        mat4 view = glm::lookAt(eye, target, vec3(0, 1, 0));
        mat4 proj = glm::perspective(glm::radians(60.0f), (float)WIN_W / WIN_H, 0.1f, 300.0f);

        glUseProgram(sceneProgram);
        glUniformMatrix4fv(u_view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(u_proj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3f(u_viewPos, eye.x, eye.y, eye.z);
        glUniform3f(u_lightPos0, 0.0f, 15.0f, playerZ + 8.0f);
        glUniform3f(u_lightColor0, 0.95f, 0.92f, 0.85f);
        glUniform3f(u_lightPos1, -10.0f, 6.0f, playerZ - 20.0f);
        glUniform3f(u_lightColor1, 0.4f, 0.45f, 0.6f);

        drawGround();
        drawPath();
        drawSceneryAlongPath();
        drawTemple();
        for (auto& o : obstacles) drawObstacle(LANE_X[o.lane], o.z, o.lane);
        for (auto& c : coins) if (c.active) drawCoin(LANE_X[c.lane], c.z);
        drawPlayer();
        drawMonkey();

        if (gameOver) drawGameOverOverlay();

        glfwSwapBuffers(window);
    }

    glfwTerminate();
    return 0;
}
