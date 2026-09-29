#ifndef SQW_TEXT_METRICS_H
#define SQW_TEXT_METRICS_H
/* Glyph-advance measurement, shared by the Vulkan text renderer
 * (text_renderer_vk.c, which also needs it to emit UV quads) and the
 * layout engine (layout.c, which needs it for real word-wrap -- see that
 * file's word-splitting loop). Deliberately has NO Vulkan/vk_context.h
 * dependency, unlike text_renderer_vk.h, so layout.c (and its standalone
 * test, tests/test_layout.c) can measure text without pulling in the
 * whole Vulkan stack. Relies on g_font_glyph_metrics[] already being
 * populated via sqw_font_atlas_decode() (called once at startup by
 * sqw_text_renderer_init(), before layout_compute() ever runs -- see
 * sqw_main.c's init order). */
#include "font_atlas.h"

static const SqwGlyphMetrics *sqw_glyph_lookup(char ch) {
    int idx = (unsigned char)ch - SQW_FONT_FIRST_CHAR;
    if (idx < 0 || idx >= SQW_FONT_GLYPH_COUNT) return NULL;
    return &g_font_glyph_metrics[idx];
}

static float sqw_text_glyph_advance(char ch, float scale) {
    const SqwGlyphMetrics *g = sqw_glyph_lookup(ch);
    if (!g) {
        const SqwGlyphMetrics *space = sqw_glyph_lookup(' ');
        return space ? (float)space->advance * scale : 8.0f * scale;
    }
    return (float)g->advance * scale;
}

static float sqw_text_measure(const char *s, int len, float scale) {
    float w = 0.0f;
    int i;
    /* NOT "w += sqw_text_glyph_advance(...)" -- a real, confirmed squash/
     * ARM64 codegen bug found while building the Android text-rendering
     * demo: accumulating a float via "+=" directly from a function call's
     * return value inside a loop silently drops every addition (w stayed
     * 0.0 for any real string, reproduced live on device), while hoisting
     * the call's result into its own local first and adding THAT works
     * correctly every time -- same bug shape, same fix, as this
     * project's other documented "never combine a function call with an
     * operation on its result in one statement" workarounds. */
    for (i = 0; i < len; i++) {
        float adv = sqw_text_glyph_advance(s[i], scale);
        w = w + adv;
    }
    return w;
}

#endif /* SQW_TEXT_METRICS_H */
