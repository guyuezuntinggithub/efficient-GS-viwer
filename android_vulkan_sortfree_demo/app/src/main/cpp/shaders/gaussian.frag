#version 450
layout(location=0) in vec4 vColorOpacity;
layout(location=1) in float vViewDepth;

layout(location=0) out vec4 outAccum;   // rgb accum + weight sum
layout(location=1) out float outReveal; // optional revealage/extra weight

void main() {
    // Point sprite Gaussian kernel
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float power = -dot(uv, uv) * 2.0;
    float alpha = clamp(vColorOpacity.a * exp(power), 0.0, 0.99);
    if (alpha < (1.0 / 255.0)) discard;

    // Sort-free weighted accumulation (mobile-friendly weighted blend variant)
    float phi = 1.0; // TODO: replace with MLP-predicted phi from compute/inference pass
    float weight = exp(0.05 / vViewDepth) + phi / (vViewDepth * vViewDepth) + phi * phi;

    vec3 c = vColorOpacity.rgb * alpha * weight;
    float w = alpha * weight;

    outAccum = vec4(c, w);
    outReveal = w;
}
