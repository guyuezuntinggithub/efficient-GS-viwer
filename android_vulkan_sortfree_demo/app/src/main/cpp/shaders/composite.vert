#version 450
layout(location=0) out vec2 uv;
vec2 pos[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main(){
    gl_Position = vec4(pos[gl_VertexIndex],0,1);
    uv = gl_Position.xy*0.5+0.5;
}
