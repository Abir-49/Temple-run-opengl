// =====================================================================
//  hud.h  --  2D overlay (score, bars, messages)
//  Text comes from the tiny single-header library stb_easy_font.h: it
//  turns a string into quads; we convert them to triangles and draw
//  everything in ONE draw call at the end of the frame.
// =====================================================================
#pragma once
#include "gfx.h"
#include "shaders.h"
#include "stb_easy_font.h"

struct Hud {
    Program prog;
    GLuint vao = 0, vbo = 0;
    std::vector<float> v;           // x, y, r, g, b, a  per vertex
    int W = 1, H = 1;

    void init() {
        buildProgram(prog, HUD_VS, HUD_FS, "hud");
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }
    void begin(int w, int h) { v.clear(); W = w; H = h; }

    void vert(float x, float y, const vec4& c) { v.insert(v.end(), { x, y, c.r, c.g, c.b, c.a }); }
    void rect(float x, float y, float w, float h, const vec4& c) {
        vert(x, y, c); vert(x + w, y, c); vert(x + w, y + h, c);
        vert(x, y, c); vert(x + w, y + h, c); vert(x, y + h, c);
    }
    float textWidth(const std::string& s, float scale) {
        return (float)stb_easy_font_width((char*)s.c_str()) * scale;
    }
    void text(float x, float y, const std::string& s, float scale, const vec4& c, bool shadow = true) {
        static char buf[65536];
        int quads = stb_easy_font_print(0, 0, (char*)s.c_str(), nullptr, buf, sizeof(buf));
        for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
            float ox = pass == 0 ? scale * 0.8f : 0.0f, oy = pass == 0 ? scale * 0.8f : 0.0f;
            vec4 col = pass == 0 ? vec4(0, 0, 0, c.a * 0.75f) : c;
            for (int q = 0; q < quads; q++) {
                float px[4], py[4];
                for (int i = 0; i < 4; i++) {
                    const char* b = buf + (q * 4 + i) * 16;
                    px[i] = x + ox + *(const float*)b * scale;
                    py[i] = y + oy + *(const float*)(b + 4) * scale;
                }
                vert(px[0], py[0], col); vert(px[1], py[1], col); vert(px[2], py[2], col);
                vert(px[0], py[0], col); vert(px[2], py[2], col); vert(px[3], py[3], col);
            }
        }
    }
    void textCentered(float cx, float y, const std::string& s, float scale, const vec4& c) {
        text(cx - textWidth(s, scale) * 0.5f, y, s, scale, c);
    }
    void flush() {
        if (v.empty()) return;
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        prog.use();
        prog.f2("uScreen", vec2((float)W, (float)H));
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 6));
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
    }
};
