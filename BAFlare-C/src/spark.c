#include "spark.h"
#include "renderer.h"

float randf(void) { return (float)rand() / (float)RAND_MAX; }

void apply_color(MouseSpark *s, int r, int g, int b) {
    s->color[0] = r / 255.0f;
    s->color[1] = g / 255.0f;
    s->color[2] = b / 255.0f;
}

void spark_init(MouseSpark *s) {
    // 设置默认颜色为蓝色 (45, 175, 255) 后面可能会考虑不使用配置程序就默认原版
    apply_color(s, 45, 175, 255);

    s->scale   = 1.575f;
    s->opacity = 1.0f;
    s->speed   = 1.0f;
    s->max_trail = 16;
    s->wave_count = 0;
    s->spark_count = 0;
    s->trail_count = 0;
    s->is_down = 0;
    s->has_last_pos = 0;
    s->last_frame_time = SDL_GetTicks();
    s->base_frame_ms = 1000.0f / 60.0f;
}

float clampf01(float a) { return a < 0 ? 0 : (a > 1 ? 1 : a); }
float spark_alpha(const MouseSpark *s, float a) {
    return clampf01(a * s->opacity);
}

void spark_create_move_sparks(MouseSpark *s, float x, float y) {
    if (!s->has_last_pos) { s->last_pos[0] = x; s->last_pos[1] = y; s->has_last_pos = 1; }
    float dx = x - s->last_pos[0], dy = y - s->last_pos[1];
    if (hypotf(dx, dy) > 2.0f) {
        if (s->trail_count < MAX_TRAIL) {
            s->trail[s->trail_count].x = x;
            s->trail[s->trail_count].y = y;
            s->trail[s->trail_count].life = 1.0f;
            s->trail_count++;
        } else {
            memmove(&s->trail[0], &s->trail[1], (MAX_TRAIL - 1) * sizeof(TrailPoint));
            s->trail[MAX_TRAIL - 1].x = x;
            s->trail[MAX_TRAIL - 1].y = y;
            s->trail[MAX_TRAIL - 1].life = 1.0f;
        }
        s->last_pos[0] = x;
        s->last_pos[1] = y;

        if (randf() < 0.3f && s->spark_count < MAX_SPARKS) {
            float a = randf() * 2.0f * (float)M_PI;
            float spd = (s->scale / 1.5f) * 0.7f;
            float bs = 13.5f * s->scale * (0.8f + randf() * 0.2f);
            Spark *sp = &s->sparks[s->spark_count++];
            sp->x  = x + cosf(a) * 20.0f * s->scale;
            sp->y  = y + sinf(a) * 20.0f * s->scale;
            sp->vx = cosf(a) * spd;
            sp->vy = sinf(a) * spd;
            sp->rot = (randf() < 0.5f) ? 0.0f : (float)M_PI;
            sp->base_size = bs;
            sp->s = bs;
            sp->a = 0.7f;
            sp->f = 0.95f;
            sp->start_time = SDL_GetTicks();
            sp->phase_offset = randf() * 2.0f * (float)M_PI;
        }
    }
}

void spark_boom(MouseSpark *s, float x, float y) {
    if (s->wave_count < MAX_WAVES) {
        Wave *w = &s->waves[s->wave_count++];
        w->x = x; w->y = y;
        w->life = 0.0f; w->max_life = 18.0f; w->r = 0.0f;
        w->ring_ang = randf() * 2.0f * (float)M_PI;
        w->ring_life = 0.0f; w->ring_max_life = 30.0f; w->ring_rs = 0.08f;
    }
    int cnt = 5;
    float spd_adj = (s->scale / 1.5f) * 0.4f;
    float rad = 18.0f * s->scale;
    for (int i = 0; i < cnt; i++) {
        if (s->spark_count >= MAX_SPARKS) break;
        float a = randf() * 2.0f * (float)M_PI;
        float v = (4.0f + randf() * 3.0f) * spd_adj;
        float bs = (6.0f + randf() * 4.5f) * s->scale * (0.8f + randf() * 0.2f);
        Spark *sp = &s->sparks[s->spark_count++];
        sp->x  = x + cosf(a) * rad;
        sp->y  = y + sinf(a) * rad;
        sp->vx = cosf(a) * v;
        sp->vy = sinf(a) * v;
        sp->rot = (randf() < 0.5f) ? 0.0f : (float)M_PI;
        sp->base_size = bs;
        sp->s = bs;
        sp->a = 1.0f;
        sp->f = 0.93f;
        sp->start_time = SDL_GetTicks();
        sp->phase_offset = randf() * 2.0f * (float)M_PI;
    }
}

void spark_update_and_draw(MouseSpark *s, Uint32 now) {
    Uint32 delta = now - s->last_frame_time;
    if (delta > 100) delta = 100;
    s->last_frame_time = now;
    float fs = (delta / s->base_frame_ms) * s->speed;

    /* ---- Trail ---- */
    for (int i = s->trail_count - 1; i >= 0; i--) {
        s->trail[i].life -= (s->is_down ? 0.08f : 0.15f) * fs;
        if (s->trail[i].life <= 0.0f) {
            memmove(&s->trail[i], &s->trail[i + 1],
                    (s->trail_count - i - 1) * sizeof(TrailPoint));
            s->trail_count--;
        }
    }
    if (s->trail_count > 1) {
        int n = s->trail_count - 1;
        float cr = s->color[0], cg = s->color[1], cb = s->color[2];
        float thickness = 5.0f * s->scale;
        for (int i = 0; i < n; i++) {
            float p0[2] = { s->trail[i].x,   s->trail[i].y   };
            float p1[2] = { s->trail[i+1].x, s->trail[i+1].y };
            float alpha = spark_alpha(s, (float)i / (float)n);
            float col[4] = { cr, cg, cb, alpha };
            batch_thick_line(&g_batch, p0, p1, col, thickness);
        }
    }

    /* ---- Shock waves ---- */
    float wc[3] = {
        fminf(1.0f, s->color[0] * 1.3f),
        fminf(1.0f, s->color[1] * 1.3f),
        fminf(1.0f, s->color[2] * 1.3f)
    };
    float segs[3][2] = {
        { -0.25f * (float)M_PI, 1.15f * (float)M_PI },
        {  0.00f,               1.15f * (float)M_PI },
        {  0.25f * (float)M_PI, 1.15f * (float)M_PI }
    };
    for (int i = s->wave_count - 1; i >= 0; i--) {
        Wave *w = &s->waves[i];
        w->life += fs;
        float p = w->life / w->max_life;
        float p_clamped = p < 1.0f ? p : 1.0f;
        w->r = 26.0f * s->scale * (1.0f - powf(1.0f - p_clamped, 3.0f));
        if (p < 1.0f) {
            float alpha = spark_alpha(s, 1.0f - powf(p, 3.0f));
            float col[4] = { wc[0], wc[1], wc[2], alpha };
            batch_filled_circle(&g_batch, w->x, w->y, w->r, col, 32);
        }
        w->ring_life += fs;
        float rp = w->ring_life / w->ring_max_life;
        if (rp > 1.0f) rp = 1.0f;
        w->ring_ang -= w->ring_rs * fs;
        for (int k = 0; k < 3; k++) {
            float sa = w->ring_ang + segs[k][0];
            float ea = sa + segs[k][1] * (1.0f - rp);
            float alpha = spark_alpha(s, 1.0f - rp);
            if (alpha <= 0.0f) continue;
            float radius = w->r + 3.0f * s->scale;
            float col[4] = { 1.0f, 1.0f, 1.0f, alpha };
            batch_thick_arc(&g_batch, w->x, w->y, radius, sa, ea, col,
                            2.0f * s->scale, 0);
        }
        if (p >= 1.0f && rp >= 1.0f) {
            s->waves[i] = s->waves[s->wave_count - 1];
            s->wave_count--;
        }
    }

    /* ---- Sparks ---- */
    float cr = s->color[0], cg = s->color[1], cb = s->color[2];
    for (int i = s->spark_count - 1; i >= 0; i--) {
        Spark *sp = &s->sparks[i];
        sp->x  += sp->vx * fs;
        sp->y  += sp->vy * fs;
        sp->vx *= powf(sp->f, fs);
        sp->vy *= powf(sp->f, fs);
        sp->a  -= 0.023f * fs;
        sp->s  = sp->base_size * sp->a;
        if (sp->s < 0.0f) sp->s = 0.0f;
        if (sp->a <= 0.0f || sp->s <= 0.0f) {
            s->sparks[i] = s->sparks[s->spark_count - 1];
            s->spark_count--;
            continue;
        }
        Uint32 elapsed = now - sp->start_time;
        float cp = (sinf(elapsed * (2.0f * (float)M_PI / 500.0f) + sp->phase_offset) + 1.0f) * 0.5f;
        float alpha = spark_alpha(s, sp->a) * (0.6f + cp * 0.4f);
        float col[4];
        if (cp < 0.5f) { col[0]=1; col[1]=1; col[2]=1; }
        else           { col[0]=cr; col[1]=cg; col[2]=cb; }
        col[3] = alpha;

        float half = sp->s * 0.6f;
        float cos_r = cosf(sp->rot), sin_r = sinf(sp->rot);
        float sx = sp->x, sy = sp->y;
        float local[3][2] = {
            { 0.0f, -sp->s },
            { half,  half  },
            { -half, half  }
        };
        float tp[3][2];
        for (int k = 0; k < 3; k++) {
            tp[k][0] = local[k][0] * cos_r - local[k][1] * sin_r + sx;
            tp[k][1] = local[k][0] * sin_r + local[k][1] * cos_r + sy;
        }
        batch_triangle(&g_batch, tp[0], tp[1], tp[2], col);
    }
}
