#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    const char *name;
    int r, g, b;
} ColorPreset;

static ColorPreset g_presets[] = {
    { "蓝色",   45, 175, 255 },
    { "红色",   255, 69, 69 },
    { "绿色",   69, 255, 137 },
    { "黄色",   255, 223, 69 },
    { "紫色",   195, 69, 255 },
    { "白色",   255, 255, 255 },
    { "青色",   0, 255, 255 },
    { "橙色",   255, 140, 0 },
    { "粉色",   255, 105, 180 },
};
#define NUM_PRESETS (sizeof(g_presets)/sizeof(g_presets[0]))

#endif /* CONFIG_H */
