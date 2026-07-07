#ifndef SPARK_H
#define SPARK_H

#include "common.h"

typedef struct {
    float   x, y;
    float   vx, vy;
    float   rot;
    float   base_size, s, a, a0, f;
    Uint32  start_time;
    float   phase_offset;
} Spark;

typedef struct {
    float   x, y;
    float   life, max_life, r;
    float   ring_ang, ring_life, ring_max_life, ring_rs;
} Wave;

typedef struct {
    float   x, y;
    float   life;
} TrailPoint;

typedef struct {
    float color[3];
    float scale;
    float opacity;
    float speed;
    int   max_trail;

    Wave       waves[MAX_WAVES];
    int        wave_count;
    Spark      sparks[MAX_SPARKS];
    int        spark_count;
    TrailPoint trail[MAX_TRAIL];
    int        trail_count;

    int     is_down;
    int     has_last_pos;
    float   last_pos[2];

    Uint32  last_frame_time;
    float   base_frame_ms;
} MouseSpark;

float randf(void);
void apply_color(MouseSpark *s, int r, int g, int b);
void spark_init(MouseSpark *s);
float clampf01(float a);
float spark_alpha(const MouseSpark *s, float a);
void spark_create_move_sparks(MouseSpark *s, float x, float y);
void spark_boom(MouseSpark *s, float x, float y);
void spark_update_and_draw(MouseSpark *s, Uint32 now);

#endif /* SPARK_H */
