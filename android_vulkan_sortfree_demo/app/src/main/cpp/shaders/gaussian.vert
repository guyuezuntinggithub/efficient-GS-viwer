#version 450
layout(location=0) in vec4 inPosScale; // xyz + scale
layout(location=1) in vec4 inColorOpacity; // rgb + opacity

layout(push_constant) uniform Push {
    mat4 uMVP;
} pc;

layout(location=0) out vec4 vColorOpacity;
layout(location=1) out float vViewDepth;

void main() {
    vec4 p = pc.uMVP * vec4(inPosScale.xyz, 1.0);
    gl_Position = p;
    gl_PointSize = max(1.0, 800.0 * inPosScale.w / max(1e-4, p.w));
    vColorOpacity = inColorOpacity;
    vViewDepth = max(1e-4, p.w);
}
