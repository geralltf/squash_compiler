#ifndef SQW_SVG_RENDER_H
#define SQW_SVG_RENDER_H

/* Hand-written SVG rasterizer -- no external dependency (no librsvg/
 * cairo), matching this project's PNG/GIF/JPEG decoders (see
 * img_decode_png.h's own top comment for why this project hand-rolls its
 * own image formats rather than binding external C libraries). Renders a
 * real SVG document (the raw bytes of a fetched/read .svg file) into a
 * flat RGBA8 raster at the document's own natural pixel size, exactly
 * like sqw_png_decode()/sqw_jpeg_decode()/sqw_gif_decode() -- from the
 * image cache's own point of view an SVG is just a fourth raster format
 * it can now decode, with no other code path (layout/draw/caching)
 * needing to know or care that the source was vector.
 *
 * Scope -- a real, hand-rolled 2D rasterizer covering enough of the SVG
 * 1.1 static-rendering subset to render real-world icon/logo SVGs
 * recognizably (this project's own motivating case: Wikipedia's sister-
 * project footer icons and page logos, all plain shape/path artwork):
 *   - Shapes: <rect> (incl. rounded corners), <circle>, <ellipse>,
 *     <line>, <polyline>, <polygon>, <path> (M/L/H/V/C/S/Q/T/A/Z, both
 *     absolute and relative -- the full path-data command set).
 *   - <g> grouping with inherited fill/stroke/opacity down the tree.
 *   - transform="..." (translate/scale/rotate/skewX/skewY/matrix, and
 *     matrix multiplication composing through nested <g>s) on <svg>,
 *     <g>, and individual shapes.
 *   - fill/stroke as #rgb/#rrggbb hex, rgb()/rgba(), a real named-color
 *     table (CSS's own, the common subset actually seen in real SVGs),
 *     "none", and "currentColor" (treated as black -- no real CSS
 *     cascade to resolve it against). fill-opacity/stroke-opacity/
 *     opacity, and the same via a "style='fill:...;opacity:...'"
 *     attribute (a real, common alternative spelling).
 *   - <linearGradient>/<radialGradient> fill references
 *     ("fill='url(#id)'") -- resolved to a single flat, mid-stop-
 *     weighted average color, NOT a real multi-color gradient
 *     rasterization (a deliberate scope cut: real gradient rendering is
 *     a substantial separate rasterizer feature on its own; a flat
 *     representative color still puts recognizable, correctly-hued
 *     artwork on screen instead of the blank/black a totally-unhandled
 *     fill would produce).
 *   - viewBox + width/height (with unit suffixes stripped) sizing.
 *
 * Explicitly OUT of scope, same "returns 0, safe-degrades to FAILED,
 * never silently wrong" convention as this project's other decoders'
 * own unsupported-feature cases: <text> (would need this project's own
 * glyph-metrics/shaping machinery layered into a vector rasterizer, a
 * separate feature), <use>/<symbol> (shape references/reuse), CSS
 * class-based styling via a <style> block (only inline "fill"/"style"
 * attributes are read), clipPath/mask/filter (drawn unclipped/
 * unfiltered rather than not at all -- a visible-but-imperfect
 * approximation, not a blank result), and true multi-stop gradients
 * (see above). None of these cause a decode FAILURE on their own -- an
 * SVG using them still renders everything this scope DOES cover;
 * they're just not applied on top of it. */

/* Decodes `len` bytes at `data` (a full .svg file's bytes) into a
 * freshly malloc'd RGBA8 buffer (`*out_rgba`, width*height*4 bytes,
 * row-major top-to-bottom, no padding) at the document's own natural
 * pixel size (`*out_w`/`*out_h`, from its viewBox/width/height -- see
 * sqw_svg_natural_size()'s own comment for the exact precedence rule),
 * capped to SQW_SVG_MAX_DIM per axis so a pathological/huge declared
 * canvas can't allocate an unbounded buffer. Returns 1 on success
 * (caller owns *out_rgba, must free() it) or 0 on any parse failure /
 * missing root <svg> element (out_rgba/out_w/out_h left untouched). */
int sqw_svg_decode(const unsigned char *data, long len,
                    unsigned char **out_rgba, int *out_w, int *out_h);

#endif /* SQW_SVG_RENDER_H */
