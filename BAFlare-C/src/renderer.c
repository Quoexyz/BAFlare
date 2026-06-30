#include "renderer.h"

/* ---------- Shader sources ---------- */
const char *VS_SRC =
"#version 330 core\n"
"in vec2 a_pos;\n"
"in vec4 a_color;\n"
"in vec2 a_uv;\n"
"out vec4 v_color;\n"
"out vec2 v_uv;\n"
"uniform mat4 u_proj;\n"
"void main() {\n"
"    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
"    v_color = a_color;\n"
"    v_uv = a_uv;\n"
"}\n";

const char *FS_SRC =
"#version 330 core\n"
"in vec4 v_color;\n"
"in vec2 v_uv;\n"
"out vec4 frag;\n"
"uniform sampler2D u_tex;\n"
// 移除 u_use_tex
"void main() {\n"
"    frag = texture(u_tex, v_uv) * v_color;\n" // 统一采样
"}\n";


/* ---------- Globals ---------- */
GLuint g_prog;
GLint  g_u_proj    = -1;
GLint  g_u_use_tex = -1;
GLint  g_u_tex     = -1;
Batch g_batch;
GLuint g_white_tex;

/* ---------- Shader helpers ---------- */
GLuint compile_shader(const char *src, GLenum type) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "[Shader] compile error:\n%s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint make_program(const char *vs, const char *fs) {
    GLuint v = compile_shader(vs, GL_VERTEX_SHADER);
    GLuint f = compile_shader(fs, GL_FRAGMENT_SHADER);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "[Program] link error:\n%s\n", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

void make_ortho(float *m, float l, float r, float b, float t, float n, float f) {
    memset(m, 0, 16 * sizeof(float));
    m[0]  =  2.0f / (r - l);
    m[5]  =  2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] =  1.0f;
}

/* ---------- Batch Renderer ---------- */
void batch_init(Batch *b, int max_verts) {
    glGenVertexArrays(1, &b->vao);
    glGenBuffers(1, &b->vbo);
    glBindVertexArray(b->vao);
    glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
    glBufferData(GL_ARRAY_BUFFER, max_verts * (GLsizei)sizeof(Vertex), NULL, GL_DYNAMIC_DRAW);
    GLsizei stride = (GLsizei)sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float) + 4));
    glBindVertexArray(0);

    b->buf = (Vertex*)malloc((size_t)max_verts * sizeof(Vertex));
    b->idx = 0;
    b->cmd_count = 0;
    b->cur_mode = -1;
    b->cur_tex = 0;
    b->cur_start = 0;

    // 创建 1x1 白纹理
    unsigned char white_px[] = { 255, 255, 255, 255 };
    glGenTextures(1, &g_white_tex);
    glBindTexture(GL_TEXTURE_2D, g_white_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void batch_begin(Batch *b) {
    b->idx = 0;
    b->cmd_count = 0;
    b->cur_mode = -1;
    b->cur_tex = 0;
    b->cur_start = 0;
}

static void batch_flush(Batch *b) {
    int cnt = b->idx - b->cur_start;
    if (cnt > 0 && b->cmd_count < MAX_CMDS) {
        DrawCmd *c = &b->cmds[b->cmd_count++];
        c->mode  = (GLenum)b->cur_mode;
        c->start = b->cur_start;
        c->count = cnt;
        c->tex   = b->cur_tex;
    }
    b->cur_start = b->idx;
}

void batch_set_mode(Batch *b, GLenum mode, GLuint tex) {
    if ((int)mode != b->cur_mode || tex != b->cur_tex) {
        batch_flush(b);
        b->cur_mode = (int)mode;
        b->cur_tex  = tex;
        b->cur_start = b->idx;
    }
}

void batch_v(Batch *b, float x, float y,
                     float r, float g, float bl, float a,
                     float u, float v) {
    if (b->idx >= MAX_VERTS) return;
    Vertex *vx = &b->buf[b->idx++];
    vx->x = x; vx->y = y;

    // 将 0.0-1.0 的浮点数乘以 255 转换为 byte
    vx->r = (uint8_t)(r * 255.0f);
    vx->g = (uint8_t)(g * 255.0f);
    vx->b = (uint8_t)(bl * 255.0f);
    vx->a = (uint8_t)(a * 255.0f);

    vx->u = u; vx->v = v;
}

void batch_end(Batch *b) {
    batch_flush(b);
    if (b->idx == 0) return;

    glBindVertexArray(b->vao);
    glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
    glBufferData(GL_ARRAY_BUFFER, MAX_VERTS * sizeof(Vertex), NULL, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, b->idx * sizeof(Vertex), b->buf);

    for (int i = 0; i < b->cmd_count; i++) {
        DrawCmd *c = &b->cmds[i];
        glActiveTexture(GL_TEXTURE0);
        // 如果 cmd 没有指定纹理，绑定白纹理
        glBindTexture(GL_TEXTURE_2D, c->tex ? c->tex : g_white_tex);
        glDrawArrays(c->mode, c->start, c->count);
    }
    glBindVertexArray(0);
}


/* ---- primitive helpers ---- */
void batch_triangle(Batch *b, const float p1[2], const float p2[2],
                           const float p3[2], const float col[4]) {
    batch_set_mode(b, GL_TRIANGLES, 0);
    batch_v(b, p1[0], p1[1], col[0], col[1], col[2], col[3], 0, 0);
    batch_v(b, p2[0], p2[1], col[0], col[1], col[2], col[3], 0, 0);
    batch_v(b, p3[0], p3[1], col[0], col[1], col[2], col[3], 0, 0);
}

void batch_thick_line(Batch *b, const float p0[2], const float p1[2],
                             const float col[4], float thickness) {
    batch_set_mode(b, GL_TRIANGLES, 0);
    float dx = p1[0] - p0[0], dy = p1[1] - p0[1];
    float L = hypotf(dx, dy);
    if (L < 1e-6f) return;
    float nx = -dy / L * thickness * 0.5f;
    float ny =  dx / L * thickness * 0.5f;
    float a0x = p0[0] + nx, a0y = p0[1] + ny;
    float b0x = p0[0] - nx, b0y = p0[1] - ny;
    float a1x = p1[0] + nx, a1y = p1[1] + ny;
    float b1x = p1[0] - nx, b1y = p1[1] - ny;
    batch_v(b, a0x, a0y, col[0], col[1], col[2], col[3], 0, 0);
    batch_v(b, b0x, b0y, col[0], col[1], col[2], col[3], 0, 0);
    batch_v(b, a1x, a1y, col[0], col[1], col[2], col[3], 0, 0);
    batch_v(b, a1x, a1y, col[0], col[1], col[2], col[3], 0, 0);
    batch_v(b, b0x, b0y, col[0], col[1], col[2], col[3], 0, 0);
    batch_v(b, b1x, b1y, col[0], col[1], col[2], col[3], 0, 0);
}

void batch_thick_arc(Batch *b, float cx, float cy, float radius,
                            float sa, float ea, const float col[4],
                            float thickness, int segments) {
    batch_set_mode(b, GL_TRIANGLES, 0);
    if (segments <= 0) segments = (int)(fabsf(ea - sa) / 0.1f);
    if (segments < 4) segments = 4;
    float ro = radius + thickness * 0.5f;
    float ri = radius - thickness * 0.5f;
    if (ri < 0.0f) ri = 0.0f;
    float step = (ea - sa) / segments;
    float pox = cx + cosf(sa) * ro, poy = cy + sinf(sa) * ro;
    float pix = cx + cosf(sa) * ri, piy = cy + sinf(sa) * ri;
    for (int i = 1; i <= segments; i++) {
        float ang = sa + step * i;
        float cox = cx + cosf(ang) * ro, coy = cy + sinf(ang) * ro;
        float cix = cx + cosf(ang) * ri, ciy = cy + sinf(ang) * ri;
        batch_v(b, pox, poy, col[0], col[1], col[2], col[3], 0, 0);
        batch_v(b, pix, piy, col[0], col[1], col[2], col[3], 0, 0);
        batch_v(b, cox, coy, col[0], col[1], col[2], col[3], 0, 0);
        batch_v(b, cox, coy, col[0], col[1], col[2], col[3], 0, 0);
        batch_v(b, pix, piy, col[0], col[1], col[2], col[3], 0, 0);
        batch_v(b, cix, ciy, col[0], col[1], col[2], col[3], 0, 0);
        pox = cox; poy = coy;
        pix = cix; piy = ciy;
    }
}

void batch_filled_circle(Batch *b, float cx, float cy, float radius,
                                const float col[4], int segments) {
    batch_set_mode(b, GL_TRIANGLES, 0);
    if (segments < 3) segments = 3;
    for (int i = 0; i < segments; i++) {
        float a1 = (float)i       * 2.0f * (float)M_PI / segments;
        float a2 = (float)(i + 1) * 2.0f * (float)M_PI / segments;
        batch_v(b, cx, cy, col[0], col[1], col[2], col[3], 0, 0);
        batch_v(b, cx + cosf(a1) * radius, cy + sinf(a1) * radius, col[0], col[1], col[2], col[3], 0, 0);
        batch_v(b, cx + cosf(a2) * radius, cy + sinf(a2) * radius, col[0], col[1], col[2], col[3], 0, 0);
    }
}
