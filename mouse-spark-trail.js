(function () {
    const MAX_TRAIL = 32;
    const MAX_SPARKS = 180;
    const MAX_WAVES = 12;
    const SUBDIV = 8;
    const BASE_FRAME_MS = 1000.0 / 60.0;
    const M_PI = Math.PI;
    const PULSE_PERIOD = 350.0;
    const canvas = document.createElement('canvas');
    canvas.id = 'sparkCanvas';
    canvas.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;z-index:9999;pointer-events:none;';
    document.body.appendChild(canvas);
    const ctx = canvas.getContext('2d');
    const state = {
        color: [45 / 255, 175 / 255, 255 / 255],
        scale: 1.575,
        opacity: 1.0,
        speed: 1.0,
        max_trail: MAX_TRAIL,
        wave_count: 0,
        spark_count: 0,
        trail_count: 0,
        is_down: false,
        has_last_pos: false,
        last_pos: [0, 0],
        last_frame_time: performance.now(),
        base_frame_ms: BASE_FRAME_MS,
        trail: [],
        sparks: [],
        waves: []
    };
    function randf() { return Math.random(); }
    function clampf01(a) { return a < 0 ? 0 : (a > 1 ? 1 : a); }
    function spark_alpha(a) { return clampf01(a * state.opacity); }
    function catmullRom(p0, p1, p2, p3, t) {
        const t2 = t * t;
        const t3 = t2 * t;
        return 0.5 * ((2.0 * p1) + (-p0 + p2) * t +
            (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
            (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
    }
    function createMoveSparks(x, y) {
        if (!state.has_last_pos) {
            state.last_pos[0] = x;
            state.last_pos[1] = y;
            state.has_last_pos = true;
            return;
        }
        const dx = x - state.last_pos[0];
        const dy = y - state.last_pos[1];
        if (Math.hypot(dx, dy) > 2.0) {
            if (state.trail.length < MAX_TRAIL) {
                state.trail.push({ x, y, life: 1.0 });
            } else {
                state.trail.shift();
                state.trail.push({ x, y, life: 1.0 });
            }
            state.trail_count = state.trail.length;
            state.last_pos[0] = x;
            state.last_pos[1] = y;
            if (randf() < 0.3 && state.sparks.length < MAX_SPARKS) {
                const a = randf() * 2.0 * M_PI;
                const spd = (state.scale / 1.5) * 0.7;
                const bs = 10.5 * state.scale * (0.8 + randf() * 0.2);
                state.sparks.push({
                    x: x + Math.cos(a) * 20.0 * state.scale,
                    y: y + Math.sin(a) * 20.0 * state.scale,
                    vx: Math.cos(a) * spd,
                    vy: Math.sin(a) * spd,
                    rot: randf() < 0.5 ? 0.0 : M_PI,
                    base_size: bs,
                    s: bs,
                    a: 0.7,
                    a0: 0.7,
                    f: 0.95,
                    start_time: performance.now(),
                    phase_offset: -0.5 * M_PI
                });
                state.spark_count = state.sparks.length;
            }
        }
    }
    function createBoom(x, y) {
        if (state.waves.length < MAX_WAVES) {
            state.waves.push({
                x, y,
                life: 0.0,
                max_life: 18.0,
                r: 0.0,
                ring_ang: randf() * 2.0 * M_PI,
                ring_life: 0.0,
                ring_max_life: 30.0,
                ring_rs: 0.08
            });
            state.wave_count = state.waves.length;
        }
        const cnt = 5;
        const spd_adj = (state.scale / 1.5) * 0.4;
        const rad = 18.0 * state.scale;
        for (let i = 0; i < cnt; i++) {
            if (state.sparks.length >= MAX_SPARKS) break;
            const a = randf() * 2.0 * M_PI;
            const v = (4.0 + randf() * 3.0) * spd_adj;
            const bs = (6.0 + randf() * 4.5) * state.scale * (0.8 + randf() * 0.2);
            state.sparks.push({
                x: x + Math.cos(a) * rad,
                y: y + Math.sin(a) * rad,
                vx: Math.cos(a) * v,
                vy: Math.sin(a) * v,
                rot: randf() < 0.5 ? 0.0 : M_PI,
                base_size: bs,
                s: bs,
                a: 0.8,
                a0: 0.8,
                f: 0.93,
                start_time: performance.now(),
                phase_offset: -0.5 * M_PI
            });
        }
        state.spark_count = state.sparks.length;
    }
    function updateAndDraw(now) {
        let delta = now - state.last_frame_time;
        if (delta > 100) delta = 100;
        if (delta <= 0) delta = 1;
        state.last_frame_time = now;
        const fs = (delta / state.base_frame_ms) * state.speed;
        const cr = state.color[0];
        const cg = state.color[1];
        const cb = state.color[2];
        for (let i = state.trail.length - 1; i >= 0; i--) {
            state.trail[i].life -= (state.is_down ? 0.08 : 0.15) * fs;
            if (state.trail[i].life <= 0.0) state.trail.splice(i, 1);
        }
        state.trail_count = state.trail.length;
        if (state.trail.length > 1) {
            const n = state.trail.length - 1;
            const thickness = 5.0 * state.scale;
            for (let i = 0; i < n; i++) {
                const i0 = (i > 0) ? i - 1 : 0;
                const i1 = i;
                const i2 = i + 1;
                const i3 = (i + 2 <= n) ? i + 2 : n;
                const p0x = state.trail[i0].x;
                const p0y = state.trail[i0].y;
                const p1x = state.trail[i1].x;
                const p1y = state.trail[i1].y;
                const p2x = state.trail[i2].x;
                const p2y = state.trail[i2].y;
                const p3x = state.trail[i3].x;
                const p3y = state.trail[i3].y;
                let prev_x = p1x;
                let prev_y = p1y;
                for (let step = 1; step <= SUBDIV; step++) {
                    const t = step / SUBDIV;
                    const cx = catmullRom(p0x, p1x, p2x, p3x, t);
                    const cy = catmullRom(p0y, p1y, p2y, p3y, t);
                    const frac = (i + t) / n;
                    const alpha = spark_alpha(frac);
                    if (alpha > 0.001) {
                        ctx.beginPath();
                        ctx.moveTo(prev_x, prev_y);
                        ctx.lineTo(cx, cy);
                        ctx.lineWidth = thickness;
                        ctx.strokeStyle = `rgba(${Math.round(cr * 255)},${Math.round(cg * 255)},${Math.round(cb * 255)},${alpha})`;
                        ctx.lineCap = 'round';
                        ctx.stroke();
                    }
                    prev_x = cx;
                    prev_y = cy;
                }
            }
        }
        const wc = [Math.min(1.0, cr * 1.3), Math.min(1.0, cg * 1.3), Math.min(1.0, cb * 1.3)];
        const segs = [[-0.25 * M_PI, 1.15 * M_PI], [0.0, 1.15 * M_PI], [0.25 * M_PI, 1.15 * M_PI]];
        for (let i = state.waves.length - 1; i >= 0; i--) {
            const w = state.waves[i];
            w.life += fs;
            const p = w.life / w.max_life;
            const p_clamped = p < 1.0 ? p : 1.0;
            w.r = 26.0 * state.scale * (1.0 - Math.pow(1.0 - p_clamped, 3.0));
            if (p < 1.0) {
                const alpha = spark_alpha(1.0 - Math.pow(p, 3.0));
                if (alpha > 0.001) {
                    ctx.beginPath();
                    ctx.arc(w.x, w.y, w.r, 0, M_PI * 2);
                    ctx.fillStyle = `rgba(${Math.round(wc[0] * 255)},${Math.round(wc[1] * 255)},${Math.round(wc[2] * 255)},${alpha})`;
                    ctx.fill();
                }
            }
            w.ring_life += fs;
            let rp = w.ring_life / w.ring_max_life;
            if (rp > 1.0) rp = 1.0;
            w.ring_ang -= w.ring_rs * fs;
            for (let k = 0; k < 3; k++) {
                const sa = w.ring_ang + segs[k][0];
                const ea = sa + segs[k][1] * (1.0 - rp);
                const alpha = spark_alpha(1.0 - rp);
                if (alpha <= 0.001) continue;
                const radius = w.r + 3.0 * state.scale;
                ctx.beginPath();
                ctx.arc(w.x, w.y, radius, sa, ea);
                ctx.lineWidth = 2.0 * state.scale;
                ctx.strokeStyle = `rgba(255,255,255,${alpha})`;
                ctx.lineCap = 'butt';
                ctx.stroke();
            }
            if (p >= 1.0 && rp >= 1.0) state.waves.splice(i, 1);
        }
        state.wave_count = state.waves.length;
        for (let i = state.sparks.length - 1; i >= 0; i--) {
            const sp = state.sparks[i];
            sp.x += sp.vx * fs;
            sp.y += sp.vy * fs;
            sp.vx *= Math.pow(sp.f, fs);
            sp.vy *= Math.pow(sp.f, fs);
            sp.a -= 0.023 * fs;
            let p = 1.0 - sp.a / sp.a0;
            if (p < 0.0) p = 0.0;
            sp.s = sp.base_size * Math.sin(Math.pow(p, 0.65) * M_PI);
            if (sp.s < 0.0) sp.s = 0.0;
            if (sp.a <= 0.0 || sp.s <= 0.05) {
                state.sparks.splice(i, 1);
                continue;
            }
            const elapsed = now - sp.start_time;
            const cp = (Math.sin(elapsed * (2.0 * M_PI / PULSE_PERIOD) + sp.phase_offset) + 1.0) * 0.5;
            const alpha = spark_alpha(sp.a) * (0.6 + cp * 0.4);
            if (alpha <= 0.001) continue;
            let r_col, g_col, b_col;
            if (cp < 0.5) {
                r_col = 1; g_col = 1; b_col = 1;
            } else {
                r_col = cr; g_col = cg; b_col = cb;
            }
            const half = sp.s * 0.6;
            const h = half * 1.7320508;
            const cos_r = Math.cos(sp.rot);
            const sin_r = Math.sin(sp.rot);
            const sx = sp.x;
            const sy = sp.y;
            const local = [[0.0, -2.0 * h / 3.0], [half, h / 3.0], [-half, h / 3.0]];
            const tp = local.map(([lx, ly]) => [
                lx * cos_r - ly * sin_r + sx,
                lx * sin_r + ly * cos_r + sy
            ]);
            ctx.beginPath();
            ctx.moveTo(tp[0][0], tp[0][1]);
            ctx.lineTo(tp[1][0], tp[1][1]);
            ctx.lineTo(tp[2][0], tp[2][1]);
            ctx.closePath();
            ctx.fillStyle = `rgba(${Math.round(r_col * 255)},${Math.round(g_col * 255)},${Math.round(b_col * 255)},${alpha})`;
            ctx.fill();
        }
        state.spark_count = state.sparks.length;
    }
    function resizeCanvas() {
        const dpr = Math.min(window.devicePixelRatio || 1, 2);
        canvas.width = window.innerWidth * dpr;
        canvas.height = window.innerHeight * dpr;
        canvas.style.width = window.innerWidth + 'px';
        canvas.style.height = window.innerHeight + 'px';
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.scale(dpr, dpr);
    }
    function mainLoop(ts) {
        const dpr = Math.min(window.devicePixelRatio || 1, 2);
        ctx.clearRect(0, 0, canvas.width / dpr, canvas.height / dpr);
        updateAndDraw(ts);
        requestAnimationFrame(mainLoop);
    }
    function onMouseMove(e) {
        createMoveSparks(e.clientX, e.clientY);
    }
    function onMouseDown(e) {
        state.is_down = true;
        createBoom(e.clientX, e.clientY);
        state.last_pos[0] = e.clientX;
        state.last_pos[1] = e.clientY;
        state.has_last_pos = true;
    }
    function onMouseUp() {
        state.is_down = false;
    }
    function onMouseLeave() {
        state.is_down = false;
        state.has_last_pos = false;
    }
    function onMouseEnter(e) {
        state.has_last_pos = false;
        state.last_pos[0] = e.clientX;
        state.last_pos[1] = e.clientY;
        state.has_last_pos = true;
    }
    function onTouchMove(e) {
        if (e.touches.length > 0) {
            const x = e.touches[0].clientX;
            const y = e.touches[0].clientY;
            createMoveSparks(x, y);
        }
    }
    function onTouchStart(e) {
        if (e.touches.length > 0) {
            state.is_down = true;
            const x = e.touches[0].clientX;
            const y = e.touches[0].clientY;
            createBoom(x, y);
            state.last_pos[0] = x;
            state.last_pos[1] = y;
            state.has_last_pos = true;
        }
    }
    function onTouchEnd() {
        state.is_down = false;
    }
    function init() {
        resizeCanvas();
        window.addEventListener('mousemove', onMouseMove, { passive: true });
        window.addEventListener('mousedown', onMouseDown);
        window.addEventListener('mouseup', onMouseUp);
        window.addEventListener('mouseleave', onMouseLeave);
        window.addEventListener('mouseenter', onMouseEnter);
        window.addEventListener('touchmove', onTouchMove, { passive: true }); 
        window.addEventListener('touchstart', onTouchStart, { passive: true });
        window.addEventListener('touchend', onTouchEnd);
        window.addEventListener('touchcancel', onTouchEnd);
        window.addEventListener('resize', () => {
            resizeCanvas();
            state.trail.length = 0;
            state.trail_count = 0;
            state.has_last_pos = false;
        });
        requestAnimationFrame(mainLoop);
    }
    init();
})();