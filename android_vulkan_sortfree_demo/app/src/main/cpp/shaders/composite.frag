#version 450
layout(set=0,binding=0) uniform sampler2D uAccum;
layout(set=0,binding=1) uniform sampler2D uReveal;
layout(push_constant) uniform Bg { vec3 bg; } bg;

layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;

void main(){
    vec4 a = texture(uAccum, uv);
    float w = max(a.a, 1e-6);
    vec3 fg = a.rgb / w;
    // simplified transmittance approximation
    float T = exp(-texture(uReveal, uv).r);
    outColor = vec4(fg * (1.0 - T) + bg.bg * T, 1.0);
}
