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
} pc;

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
