#ifndef RENDERER_H
#define RENDERER_H

#include "common.h"

extern GLuint g_prog;
extern GLint  g_u_proj;
extern GLint  g_u_use_tex;
extern GLint  g_u_tex;

extern const char *VS_SRC;
extern const char *FS_SRC;

/* Shader helpers */
GLuint compile_shader(const char *src, GLenum type);
GLuint make_program(const char *vs, const char *fs);
void make_ortho(float *m, float l, float r, float b, float t, float n, float f);

/* Batch Renderer */
typedef struct {
    float x, y;
    uint8_t r, g, b, a;
    float u, v;
} Vertex;

typedef struct {
    GLenum mode;
    int    start;
    int    count;
    GLuint tex;
} DrawCmd;

typedef struct {
    GLuint  vao, vbo;
    Vertex *buf;
    int     idx;
    DrawCmd cmds[MAX_CMDS];
    int     cmd_count;
    int     cur_mode;
    GLuint  cur_tex;
    int     cur_start;
} Batch;

extern Batch g_batch;

void batch_init(Batch *b, int max_verts);
void batch_begin(Batch *b);
void batch_end(Batch *b);
void batch_set_mode(Batch *b, GLenum mode, GLuint tex);
void batch_v(Batch *b, float x, float y, float r, float g, float bl, float a, float u, float v);

/* Primitive helpers */
void batch_triangle(Batch *b, const float p1[2], const float p2[2], const float p3[2], const float col[4]);
void batch_thick_line(Batch *b, const float p0[2], const float p1[2], const float col[4], float thickness);
void batch_thick_arc(Batch *b, float cx, float cy, float radius, float sa, float ea, const float col[4], float thickness, int segments);
void batch_filled_circle(Batch *b, float cx, float cy, float radius, const float col[4], int segments);

#endif /* RENDERER_H */
