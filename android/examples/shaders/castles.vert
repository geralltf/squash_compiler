#version 450
// castles_android.c world shader: flat-shaded low-poly vertex colors,
// one sun + ambient, distance fog, animated water (normal.y == 2 marks a
// water vertex), and an unlit mode for the HUD (params.w == 1).
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 cam;     // xyz = camera position
    vec4 params;  // x = time, y = fog start, z = fog end, w = 1 for unlit HUD
    vec4 worm0;   // wormhole mouth A: x, z, opening strength (0..1), throat radius b
    vec4 worm1;   // wormhole mouth B (castles_wormhole.h)
} pc;

// Bent space around a wormhole mouth: everything within the rim falls
// down the Ellis wormhole's embedding funnel z(r) = b acosh(r / b)
// (depth below the rim: z(R) - z(r), flat across the throat r < b),
// twisted into a vortex that tightens toward the throat. Scaled by the
// mouth's opening strength, which castles_wormhole.h interpolates.
const float WORM_RIM = 16.0;
float g_depthFrac;
vec3 bend(vec3 p, vec4 w, float t) {
    if (w.z <= 0.001) return p;
    vec2 d = p.xz - w.xy;
    float r = length(d);
    if (r >= WORM_RIM) return p;
    float b = max(w.w, 0.05);
    float zR = b * acosh(WORM_RIM / b);
    float z = b * acosh(max(r, b) / b);
    float depth = (zR - z) * w.z;
    float u = 1.0 - r / WORM_RIM;
    float tw = w.z * (2.6 * u * u + 0.25 * u * sin(t * 1.9 + r * 0.6));
    float ca = cos(tw), sa = sin(tw);
    d = vec2(ca * d.x - sa * d.y, sa * d.x + ca * d.y);
    p.xz = w.xy + d;
    p.y -= depth;
    g_depthFrac = max(g_depthFrac, depth / max(zR, 0.001));
    return p;
}

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vFog;

void main() {
    vec3 p = inPos;
    vec3 n = inNormal;
    vec3 c = inColor;
    float t = pc.params.x;
    if (n.y > 1.5) {
        float a1 = p.x * 0.23 + t * 1.3;
        float a2 = p.z * 0.19 + t * 0.9;
        p.y += sin(a1) * 0.16 + sin(a2) * 0.14;
        n = normalize(vec3(-cos(a1) * 0.07, 1.0, -cos(a2) * 0.05));
        c *= 0.92 + 0.12 * sin(p.x * 0.05 + p.z * 0.07 + t * 0.6);
    }
    g_depthFrac = 0.0;
    if (pc.params.w < 0.5) {
        p = bend(p, pc.worm0, t);
        p = bend(p, pc.worm1, t);
        // down the funnel: darker, shifted toward a violet glow at the throat
        c = mix(c * (1.0 - 0.55 * g_depthFrac), vec3(0.55, 0.30, 1.0), 0.55 * g_depthFrac * g_depthFrac);
    }
    gl_Position = pc.mvp * vec4(p, 1.0);
    if (pc.params.w > 0.5) {
        vColor = c;
        vFog = 0.0;
        return;
    }
    vec3 L = normalize(vec3(0.45, 0.80, 0.30));
    float diff = max(dot(normalize(n), L), 0.0);
    float sky = 0.5 + 0.5 * normalize(n).y;
    vColor = c * (0.30 + 0.16 * sky + 0.70 * diff);
    float d = length(p - pc.cam.xyz);
    vFog = clamp((d - pc.params.y) / (pc.params.z - pc.params.y), 0.0, 1.0);
}
