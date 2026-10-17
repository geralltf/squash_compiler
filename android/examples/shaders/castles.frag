#version 450
layout(location = 0) in vec3 vColor;
layout(location = 1) in float vFog;
layout(location = 0) out vec4 outColor;
void main() {
    vec3 skyColor = vec3(0.62, 0.78, 0.93);
    outColor = vec4(mix(vColor, skyColor, vFog * vFog), 1.0);
}
