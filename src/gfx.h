// =====================================================================
//  gfx.h  --  low level OpenGL helpers
//    * Program      : compile/link shaders, set uniforms by name
//    * MeshData     : a CPU-side list of vertices + builder functions that
//                     generate boxes, spheres, cylinders, cones, tori, grids
//    * Mesh         : the GPU copy (VAO + VBO)
//    * Textures     : procedural textures generated in code (no image files)
// =====================================================================
#pragma once
#include "common.h"

// ---------------------------------------------------------------------
//  Shader program
// ---------------------------------------------------------------------
struct Program {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> cache;   // uniform name -> location

    GLint loc(const char* n) {
        auto it = cache.find(n);
        if (it != cache.end()) return it->second;
        GLint l = glGetUniformLocation(id, n);
        cache[n] = l;
        return l;
    }
    void use() const { glUseProgram(id); }
    void i1(const char* n, int v)            { glUniform1i(loc(n), v); }
    void f1(const char* n, float v)          { glUniform1f(loc(n), v); }
    void f2(const char* n, const vec2& v)    { glUniform2f(loc(n), v.x, v.y); }
    void f3(const char* n, const vec3& v)    { glUniform3f(loc(n), v.x, v.y, v.z); }
    void f4(const char* n, const vec4& v)    { glUniform4f(loc(n), v.x, v.y, v.z, v.w); }
    void f2v(const char* n, const vec2* v, int c) { glUniform2fv(loc(n), c, &v[0].x); }
    void f3v(const char* n, const vec3* v, int c) { glUniform3fv(loc(n), c, &v[0].x); }
    void f4v(const char* n, const vec4* v, int c) { glUniform4fv(loc(n), c, &v[0].x); }
    void m4(const char* n, const mat4& m)    { glUniformMatrix4fv(loc(n), 1, GL_FALSE, glm::value_ptr(m)); }
};

inline GLuint compileStage(GLenum type, const std::string& src, const char* name) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, 2048, nullptr, log);
        std::fprintf(stderr, "[shader] %s failed to compile:\n%s\n", name, log);
    }
    return s;
}

inline bool buildProgram(Program& p, const std::string& vs, const std::string& fs, const char* name) {
    GLuint v = compileStage(GL_VERTEX_SHADER, vs, name);
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs, name);
    p.id = glCreateProgram();
    glAttachShader(p.id, v);
    glAttachShader(p.id, f);
    glLinkProgram(p.id);
    GLint ok = 0;
    glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p.id, 2048, nullptr, log);
        std::fprintf(stderr, "[shader] %s failed to link:\n%s\n", name, log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    p.cache.clear();
    return ok != 0;
}

// ---------------------------------------------------------------------
//  Mesh data.  Every vertex: position, normal, texture coordinate and a
//  vertex colour (lets one mesh contain several colours, e.g. a tree
//  with a brown trunk and green leaves).
// ---------------------------------------------------------------------
struct Vtx { vec3 p; vec3 n; vec2 uv; vec3 c; };

inline Vtx mkV(const vec3& p, const vec3& n, const vec2& uv, const vec3& c) {
    Vtx v; v.p = p; v.n = n; v.uv = uv; v.c = c; return v;
}

struct MeshData {
    std::vector<Vtx> v;
    void tri(const Vtx& a, const Vtx& b, const Vtx& c) { v.push_back(a); v.push_back(b); v.push_back(c); }
};

struct Mesh {
    GLuint vao = 0, vbo = 0;
    GLsizei count = 0;
};

inline Mesh uploadMesh(const MeshData& md) {
    Mesh m;
    m.count = (GLsizei)md.v.size();
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, md.v.size() * sizeof(Vtx), md.v.data(), GL_STATIC_DRAW);
    const GLsizei stride = 11 * sizeof(float);          // 3+3+2+3 floats
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);                    // position
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));  // normal
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));  // uv
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));  // colour
    for (int i = 0; i < 4; i++) glEnableVertexAttribArray(i);
    glBindVertexArray(0);
    return m;
}

// helper that applies a model matrix to positions and (properly) to normals
struct Xf {
    mat4 M; mat3 N;
    explicit Xf(const mat4& m) : M(m), N(glm::transpose(glm::inverse(mat3(m)))) {}
    vec3 P(const vec3& p) const { return vec3(M * vec4(p, 1.0f)); }
    vec3 Nrm(const vec3& n) const { return glm::normalize(N * n); }
};

// ---- unit box centred on the origin (size 1) -------------------------
inline void addBox(MeshData& md, const mat4& m, const vec3& col, const vec2& uvs = vec2(1.0f)) {
    Xf x(m);
    struct F { vec3 n, u, v; };
    static const F faces[6] = {
        { vec3(0, 0, 1),  vec3(1, 0, 0),  vec3(0, 1, 0) },
        { vec3(0, 0, -1), vec3(-1, 0, 0), vec3(0, 1, 0) },
        { vec3(1, 0, 0),  vec3(0, 0, -1), vec3(0, 1, 0) },
        { vec3(-1, 0, 0), vec3(0, 0, 1),  vec3(0, 1, 0) },
        { vec3(0, 1, 0),  vec3(1, 0, 0),  vec3(0, 0, -1) },
        { vec3(0, -1, 0), vec3(1, 0, 0),  vec3(0, 0, 1) },
    };
    for (const F& f : faces) {
        vec3 c = f.n * 0.5f;
        vec3 p00 = c - f.u * 0.5f - f.v * 0.5f, p10 = c + f.u * 0.5f - f.v * 0.5f;
        vec3 p11 = c + f.u * 0.5f + f.v * 0.5f, p01 = c - f.u * 0.5f + f.v * 0.5f;
        vec3 nn = x.Nrm(f.n);
        Vtx a = mkV(x.P(p00), nn, vec2(0, 0), col), b = mkV(x.P(p10), nn, vec2(uvs.x, 0), col);
        Vtx cc = mkV(x.P(p11), nn, vec2(uvs.x, uvs.y), col), d = mkV(x.P(p01), nn, vec2(0, uvs.y), col);
        md.tri(a, b, cc);
        md.tri(a, cc, d);
    }
}

// ---- unit sphere (radius 1). lump > 0 makes it a bumpy boulder -------
inline void addSphere(MeshData& md, const mat4& m, const vec3& col,
                      int stacks = 12, int slices = 18, float lump = 0.0f, float seed = 0.0f) {
    Xf x(m);
    auto radiusAt = [&](const vec3& d) {
        if (lump <= 0.0f) return 1.0f;
        float a = std::sin(d.x * 3.1f + seed) * std::sin(d.y * 2.7f + seed * 1.3f) * std::sin(d.z * 3.5f + seed * 0.7f);
        float b = std::sin(d.x * 7.3f + seed * 2.1f) * std::sin(d.z * 6.1f + seed) * 0.5f;
        return 1.0f + lump * (a + b);
    };
    auto pos = [&](float lat, float lon) {
        vec3 d(std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon));
        return d * radiusAt(d);
    };
    std::vector<Vtx> g((stacks + 1) * (slices + 1));
    for (int i = 0; i <= stacks; i++) {
        float lat = -PI / 2 + PI * i / stacks;
        for (int j = 0; j <= slices; j++) {
            float lon = TAU * j / slices;
            vec3 d(std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon));
            vec3 p = pos(lat, lon);
            vec3 n = d;
            if (lump > 0.0f) {                      // numerical normal of the bumpy surface
                const float e = 0.01f;
                vec3 dLat = pos(lat + e, lon) - pos(lat - e, lon);
                vec3 dLon = pos(lat, lon + e) - pos(lat, lon - e);
                vec3 c = glm::cross(dLat, dLon);
                if (glm::length(c) > 1e-6f) n = glm::normalize(c);
            }
            g[i * (slices + 1) + j] = mkV(x.P(p), x.Nrm(n), vec2((float)j / slices, (float)i / stacks), col);
        }
    }
    for (int i = 0; i < stacks; i++)
        for (int j = 0; j < slices; j++) {
            const Vtx& a = g[i * (slices + 1) + j];
            const Vtx& b = g[(i + 1) * (slices + 1) + j];
            const Vtx& c = g[(i + 1) * (slices + 1) + j + 1];
            const Vtx& d = g[i * (slices + 1) + j + 1];
            md.tri(a, b, c);
            md.tri(a, c, d);
        }
}

// ---- cylinder / cone along +Y from y=0 to y=1 ------------------------
inline void addCyl(MeshData& md, const mat4& m, const vec3& col, int slices = 16,
                   float rBot = 1.0f, float rTop = 1.0f, bool capBot = true, bool capTop = true,
                   float uRepeat = 1.0f) {
    Xf x(m);
    for (int j = 0; j < slices; j++) {
        float a0 = TAU * j / slices, a1 = TAU * (j + 1) / slices;
        float u0 = uRepeat * j / slices, u1 = uRepeat * (j + 1) / slices;
        vec3 b0(rBot * std::cos(a0), 0, rBot * std::sin(a0)), b1(rBot * std::cos(a1), 0, rBot * std::sin(a1));
        vec3 t0(rTop * std::cos(a0), 1, rTop * std::sin(a0)), t1(rTop * std::cos(a1), 1, rTop * std::sin(a1));
        float slope = rBot - rTop;
        vec3 n0 = x.Nrm(vec3(std::cos(a0), slope, std::sin(a0)));
        vec3 n1 = x.Nrm(vec3(std::cos(a1), slope, std::sin(a1)));
        Vtx vb0 = mkV(x.P(b0), n0, vec2(u0, 0), col), vb1 = mkV(x.P(b1), n1, vec2(u1, 0), col);
        Vtx vt0 = mkV(x.P(t0), n0, vec2(u0, 1), col), vt1 = mkV(x.P(t1), n1, vec2(u1, 1), col);
        if (rTop > 1e-4f) {                       // cylinder / truncated cone: a quad = 2 triangles
            md.tri(vb0, vt0, vt1);
            md.tri(vb0, vt1, vb1);
        } else {                                  // true cone: top is a single point
            md.tri(vb0, vt0, vb1);
        }
        vec3 cb = x.Nrm(vec3(0, -1, 0)), ct = x.Nrm(vec3(0, 1, 0));
        if (capBot) md.tri(mkV(x.P(vec3(0, 0, 0)), cb, vec2(0.5f, 0.5f), col),
                           mkV(x.P(b0), cb, vec2(0.5f + 0.5f * std::cos(a0), 0.5f + 0.5f * std::sin(a0)), col),
                           mkV(x.P(b1), cb, vec2(0.5f + 0.5f * std::cos(a1), 0.5f + 0.5f * std::sin(a1)), col));
        if (capTop) md.tri(mkV(x.P(vec3(0, 1, 0)), ct, vec2(0.5f, 0.5f), col),
                           mkV(x.P(t1), ct, vec2(0.5f + 0.5f * std::cos(a1), 0.5f + 0.5f * std::sin(a1)), col),
                           mkV(x.P(t0), ct, vec2(0.5f + 0.5f * std::cos(a0), 0.5f + 0.5f * std::sin(a0)), col));
    }
}

// ---- torus arc in the XY plane (arc = TAU gives a full ring) ---------
//  colorFn(t) lets the colour change along the arc (t = 0..1)
inline void addTorus(MeshData& md, const mat4& m, float R, float r, float arc,
                     int segMaj, int segMin, const std::function<vec3(float)>& colorFn) {
    Xf x(m);
    std::vector<Vtx> g((segMaj + 1) * (segMin + 1));
    for (int i = 0; i <= segMaj; i++) {
        float th = arc * i / segMaj;
        vec3 rad(std::cos(th), std::sin(th), 0);
        vec3 col = colorFn(arc > 0 ? th / arc : 0.0f);
        for (int j = 0; j <= segMin; j++) {
            float ph = TAU * j / segMin;
            vec3 n = std::cos(ph) * rad + std::sin(ph) * vec3(0, 0, 1);
            vec3 p = R * rad + r * n;
            g[i * (segMin + 1) + j] = mkV(x.P(p), x.Nrm(n), vec2(2.0f * i / segMaj, (float)j / segMin), col);
        }
    }
    for (int i = 0; i < segMaj; i++)
        for (int j = 0; j < segMin; j++) {
            const Vtx& a = g[i * (segMin + 1) + j];
            const Vtx& b = g[(i + 1) * (segMin + 1) + j];
            const Vtx& c = g[(i + 1) * (segMin + 1) + j + 1];
            const Vtx& d = g[i * (segMin + 1) + j + 1];
            md.tri(a, b, c);
            md.tri(a, c, d);
        }
    if (arc < TAU - 0.01f) {                         // close the open ends with flat caps
        for (int end = 0; end < 2; end++) {
            float th = end == 0 ? 0.0f : arc;
            vec3 rad(std::cos(th), std::sin(th), 0);
            vec3 tangent(-std::sin(th), std::cos(th), 0);
            vec3 nrm = x.Nrm(end == 0 ? -tangent : tangent);
            vec3 col = colorFn(end == 0 ? 0.0f : 1.0f);
            vec3 c = R * rad;
            for (int j = 0; j < segMin; j++) {
                float p0 = TAU * j / segMin, p1 = TAU * (j + 1) / segMin;
                vec3 a = c + r * (std::cos(p0) * rad + std::sin(p0) * vec3(0, 0, 1));
                vec3 b = c + r * (std::cos(p1) * rad + std::sin(p1) * vec3(0, 0, 1));
                if (end == 0) md.tri(mkV(x.P(c), nrm, vec2(0.5f), col), mkV(x.P(a), nrm, vec2(0.5f), col), mkV(x.P(b), nrm, vec2(0.5f), col));
                else          md.tri(mkV(x.P(c), nrm, vec2(0.5f), col), mkV(x.P(b), nrm, vec2(0.5f), col), mkV(x.P(a), nrm, vec2(0.5f), col));
            }
        }
    }
}

// ---- flat grid on the XZ plane (x,z in -0.5..0.5, normal +Y) ---------
//  Subdivided on purpose: with Gouraud shading light is computed only at
//  the vertices, so a finely divided floor still shows the lighting.
inline void addGrid(MeshData& md, int cols, int rows, const vec3& col) {
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < cols; i++) {
            auto V = [&](int a, int b) {
                return mkV(vec3((float)a / cols - 0.5f, 0, (float)b / rows - 0.5f), vec3(0, 1, 0),
                           vec2((float)a / cols, (float)b / rows), col);
            };
            md.tri(V(i, j), V(i, j + 1), V(i + 1, j + 1));
            md.tri(V(i, j), V(i + 1, j + 1), V(i + 1, j));
        }
}

// ---------------------------------------------------------------------
//  Procedural textures  (value noise + a few recipes)
// ---------------------------------------------------------------------
enum TexId { T_NONE = 0, T_STONE, T_GRASS, T_BARK, T_LEAF, T_BRICK, T_WOOD, T_ROCK, T_LAVA, T_RUIN, T_COUNT };
inline GLuint gTex[T_COUNT];

inline float hash21(int x, int y, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / 16777215.0f;
}
// tileable value noise: the lattice wraps every px / py cells
inline float vnoise(float x, float y, int px, int py, int seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    auto h = [&](int a, int b) { return hash21(((a % px) + px) % px, ((b % py) + py) % py, seed); };
    float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    return lerpf(lerpf(h(xi, yi), h(xi + 1, yi), u), lerpf(h(xi, yi + 1), h(xi + 1, yi + 1), u), v);
}
inline float fbm(float u, float v, int freq, int oct, int seed) {
    float a = 0.5f, s = 0.0f, tot = 0.0f;
    int f = freq;
    for (int o = 0; o < oct; o++) {
        s += a * vnoise(u * f, v * f, f, f, seed + o * 17);
        tot += a; a *= 0.5f; f *= 2;
    }
    return s / tot;
}

inline GLuint makeTexture(int N, const std::function<vec3(float, float)>& fn) {
    std::vector<unsigned char> px((size_t)N * N * 4);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            vec3 c = fn((x + 0.5f) / N, (y + 0.5f) / N);
            c = glm::clamp(c, vec3(0.0f), vec3(1.0f));
            size_t i = ((size_t)y * N + x) * 4;
            px[i] = (unsigned char)(c.r * 255); px[i + 1] = (unsigned char)(c.g * 255);
            px[i + 2] = (unsigned char)(c.b * 255); px[i + 3] = 255;
        }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (glfwExtensionSupported("GL_EXT_texture_filter_anisotropic"))
        glTexParameterf(GL_TEXTURE_2D, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY_EXT*/, 8.0f);
    return t;
}

inline void initTextures() {
    // plain white (used when an object has no texture)
    {
        unsigned char white[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &gTex[T_NONE]);
        glBindTexture(GL_TEXTURE_2D, gTex[T_NONE]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    }
    const int N = 256;
    // Stone slabs with mortar grooves (covers 2x2 world units = 2x2 slabs)
    gTex[T_STONE] = makeTexture(N, [](float u, float v) {
        float cu = u * 2, cv = v * 2;
        int ci = (int)std::floor(cu), cj = (int)std::floor(cv);
        float fu = cu - ci, fv = cv - cj;
        float edge = std::min(std::min(fu, 1 - fu), std::min(fv, 1 - fv));
        float groove = smoothf(0.0f, 0.05f, edge);
        float n = fbm(u, v, 8, 4, 1), n2 = fbm(u, v, 32, 2, 7);
        vec3 base = glm::mix(vec3(0.46f, 0.42f, 0.36f), vec3(0.66f, 0.60f, 0.50f), n);
        base *= (0.82f + 0.3f * hash21(ci, cj, 3)) * (0.9f + 0.2f * n2);
        return base * glm::mix(0.30f, 1.0f, groove);
    });
    // Jungle floor
    gTex[T_GRASS] = makeTexture(N, [](float u, float v) {
        float n = fbm(u, v, 6, 5, 11), b = fbm(u, v, 48, 2, 5), p = fbm(u, v, 3, 3, 12);
        vec3 c = glm::mix(vec3(0.05f, 0.17f, 0.04f), vec3(0.22f, 0.42f, 0.09f), n);
        c = glm::mix(c, vec3(0.30f, 0.26f, 0.10f), smoothf(0.62f, 0.8f, p) * 0.5f);   // dry dirt patches
        return c * (0.8f + 0.4f * b);
    });
    // Bark: vertical streaks
    gTex[T_BARK] = makeTexture(N, [](float u, float v) {
        float n = vnoise(u * 14, v * 3, 14, 3, 21) * 0.6f + vnoise(u * 30, v * 6, 30, 6, 22) * 0.4f;
        return glm::mix(vec3(0.20f, 0.12f, 0.06f), vec3(0.46f, 0.31f, 0.18f), n);
    });
    // Leaves
    gTex[T_LEAF] = makeTexture(N, [](float u, float v) {
        float n = fbm(u, v, 10, 4, 31), s = fbm(u, v, 40, 2, 32);
        vec3 c = glm::mix(vec3(0.03f, 0.18f, 0.04f), vec3(0.20f, 0.50f, 0.10f), n);
        return c + std::pow(s, 3.0f) * vec3(0.30f, 0.40f, 0.05f);
    });
    // Sandstone temple bricks
    gTex[T_BRICK] = makeTexture(N, [](float u, float v) {
        float rv = v * 6; int row = (int)std::floor(rv); float fv = rv - row;
        float ru = u * 3 + (row & 1) * 0.5f; int col = (int)std::floor(ru); float fu = ru - col;
        float e = std::min(std::min(fu, 1 - fu) * 2.0f, std::min(fv, 1 - fv));
        float groove = smoothf(0.0f, 0.07f, e);
        float n = fbm(u, v, 8, 4, 41);
        vec3 base = glm::mix(vec3(0.62f, 0.50f, 0.34f), vec3(0.82f, 0.70f, 0.50f), n);
        base *= 0.85f + 0.3f * hash21(((col % 3) + 3) % 3, row, 5);
        return base * glm::mix(0.35f, 1.0f, groove);
    });
    // Wooden planks
    gTex[T_WOOD] = makeTexture(N, [](float u, float v) {
        float pv = v * 5; int pl = (int)std::floor(pv); float fv = pv - pl;
        float groove = smoothf(0.0f, 0.06f, std::min(fv, 1 - fv));
        float grain = vnoise(u * 3, v * 60, 3, 60, 51) * 0.7f + vnoise(u * 8, v * 90, 8, 90, 52) * 0.3f;
        vec3 base = glm::mix(vec3(0.38f, 0.24f, 0.11f), vec3(0.64f, 0.44f, 0.24f), grain);
        base *= 0.85f + 0.3f * hash21(pl, 0, 9);
        return base * glm::mix(0.3f, 1.0f, groove);
    });
    // Boulder rock
    gTex[T_ROCK] = makeTexture(N, [](float u, float v) {
        float n = fbm(u, v, 5, 5, 61);
        float ridge = std::abs(fbm(u, v, 8, 3, 62) - 0.5f) * 2.0f;
        vec3 c = glm::mix(vec3(0.28f, 0.28f, 0.30f), vec3(0.60f, 0.58f, 0.55f), n);
        return c * glm::mix(0.5f, 1.0f, smoothf(0.0f, 0.25f, ridge));
    });
    // Lava
    gTex[T_LAVA] = makeTexture(N, [](float u, float v) {
        float n = fbm(u, v, 6, 5, 71);
        float k = smoothf(0.38f, 0.58f, n);
        return glm::mix(vec3(0.10f, 0.02f, 0.01f), vec3(1.0f, 0.48f, 0.08f), k) + std::pow(k, 4.0f) * vec3(0.4f, 0.35f, 0.1f);
    });
    // Mossy ruin stone
    gTex[T_RUIN] = makeTexture(N, [](float u, float v) {
        float cu = u * 2, cv = v * 2;
        int ci = (int)std::floor(cu), cj = (int)std::floor(cv);
        float fu = cu - ci, fv = cv - cj;
        float groove = smoothf(0.0f, 0.05f, std::min(std::min(fu, 1 - fu), std::min(fv, 1 - fv)));
        float n = fbm(u, v, 5, 5, 81), moss = smoothf(0.48f, 0.66f, fbm(u, v, 7, 4, 82));
        vec3 stone = glm::mix(vec3(0.34f, 0.34f, 0.35f), vec3(0.58f, 0.57f, 0.54f), n) * (0.85f + 0.3f * hash21(ci, cj, 8));
        vec3 c = glm::mix(stone, vec3(0.16f, 0.34f, 0.10f), moss * 0.8f);
        return c * glm::mix(0.35f, 1.0f, groove);
    });
}
