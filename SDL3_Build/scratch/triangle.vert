#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(push_constant) uniform PushConstants {
    float angle;
} pc;

layout(location = 0) out vec3 fragColor;

void main() {
    float s = sin(pc.angle);
    float c = cos(pc.angle);
    vec2 rotated = vec2(
        inPosition.x * c - inPosition.y * s,
        inPosition.x * s + inPosition.y * c
    );
    gl_Position = vec4(rotated, 0.0, 1.0);
    fragColor = inColor;
}
