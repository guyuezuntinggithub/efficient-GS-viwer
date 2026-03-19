const canvas = document.getElementById('c');
const gl = canvas.getContext('webgl2', { antialias: true, alpha: false });
if (!gl) throw new Error('WebGL2 not supported');

const vs = `#version 300 es
precision highp float;
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
layout(location=2) in float aScale;

uniform mat4 uMVP;
uniform vec3 uCamPos;
uniform float uPointScale;

// --- tiny MLP in vertex shader ---
// input x = [pos(3), viewDir(3), scale(1), 1]
uniform mat4 uW1a; // hidden[0..3]
uniform mat4 uW1b; // hidden[4..7]
uniform vec4 uB1a;
uniform vec4 uB1b;
uniform vec4 uW2a; // output weights over hidden[0..3]
uniform vec4 uW2b; // output weights over hidden[4..7]
uniform float uB2;

out vec3 vColor;
out float vOpacity;

float relu(float x){ return max(0.0, x); }

void main() {
  vec4 clip = uMVP * vec4(aPos, 1.0);
  gl_Position = clip;

  float ps = max(2.0, uPointScale * aScale / max(0.05, clip.w));
  gl_PointSize = ps;

  vec3 vd = normalize(uCamPos - aPos);
  vec4 x0 = vec4(aPos, vd.x);
  vec4 x1 = vec4(vd.y, vd.z, aScale, 1.0);

  vec4 h0 = vec4(
    relu(dot(uW1a[0], x0) + dot(uW1b[0], x1) + uB1a.x),
    relu(dot(uW1a[1], x0) + dot(uW1b[1], x1) + uB1a.y),
    relu(dot(uW1a[2], x0) + dot(uW1b[2], x1) + uB1a.z),
    relu(dot(uW1a[3], x0) + dot(uW1b[3], x1) + uB1a.w)
  );

  // second half hidden (4..7) stored with another bias vec
  vec4 h1 = vec4(
    relu(dot(uW1a[0], x1) + dot(uW1b[0], x0) + uB1b.x),
    relu(dot(uW1a[1], x1) + dot(uW1b[1], x0) + uB1b.y),
    relu(dot(uW1a[2], x1) + dot(uW1b[2], x0) + uB1b.z),
    relu(dot(uW1a[3], x1) + dot(uW1b[3], x0) + uB1b.w)
  );

  float o = dot(uW2a, h0) + dot(uW2b, h1) + uB2;
  vOpacity = 1.0 / (1.0 + exp(-o));
  vColor = aColor;
}
`;

const fs = `#version 300 es
precision highp float;
in vec3 vColor;
in float vOpacity;
out vec4 outColor;

void main() {
  vec2 p = gl_PointCoord * 2.0 - 1.0;
  float r2 = dot(p, p);
  if (r2 > 1.0) discard;

  float g = exp(-2.5 * r2);
  float alpha = clamp(vOpacity * g, 0.0, 0.98);
  outColor = vec4(vColor * alpha, alpha);
}
`;

function compile(type, src) {
  const s = gl.createShader(type);
  gl.shaderSource(s, src);
  gl.compileShader(s);
  if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
    throw new Error(gl.getShaderInfoLog(s));
  }
  return s;
}

const prog = gl.createProgram();
gl.attachShader(prog, compile(gl.VERTEX_SHADER, vs));
gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, fs));
gl.linkProgram(prog);
if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
  throw new Error(gl.getProgramInfoLog(prog));
}
gl.useProgram(prog);

// --- scene points ---
const N = 12000;
const pos = new Float32Array(N * 3);
const col = new Float32Array(N * 3);
const sca = new Float32Array(N);
for (let i = 0; i < N; i++) {
  const t = i / N;
  const a = t * Math.PI * 10.0;
  const r = 0.2 + 1.2 * Math.sqrt(Math.random());
  const y = (Math.random() - 0.5) * 1.6;
  pos[i * 3 + 0] = Math.cos(a) * r;
  pos[i * 3 + 1] = y;
  pos[i * 3 + 2] = Math.sin(a) * r - 2.8;
  col[i * 3 + 0] = 0.3 + 0.7 * Math.abs(Math.cos(a));
  col[i * 3 + 1] = 0.3 + 0.7 * Math.abs(Math.sin(a * 0.7));
  col[i * 3 + 2] = 0.4 + 0.6 * Math.random();
  sca[i] = 0.015 + 0.03 * Math.random();
}

function bindAttrib(loc, data, size) {
  const b = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, b);
  gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
  gl.enableVertexAttribArray(loc);
  gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 0, 0);
}
bindAttrib(0, pos, 3);
bindAttrib(1, col, 3);
bindAttrib(2, sca, 1);

// uniforms
const uMVP = gl.getUniformLocation(prog, 'uMVP');
const uCamPos = gl.getUniformLocation(prog, 'uCamPos');
const uPointScale = gl.getUniformLocation(prog, 'uPointScale');
const uW1a = gl.getUniformLocation(prog, 'uW1a');
const uW1b = gl.getUniformLocation(prog, 'uW1b');
const uB1a = gl.getUniformLocation(prog, 'uB1a');
const uB1b = gl.getUniformLocation(prog, 'uB1b');
const uW2a = gl.getUniformLocation(prog, 'uW2a');
const uW2b = gl.getUniformLocation(prog, 'uW2b');
const uB2 = gl.getUniformLocation(prog, 'uB2');

// random tiny MLP weights (demo purpose)
const rand = (n, s=0.7)=> Float32Array.from({length:n}, ()=> (Math.random()*2-1)*s);
gl.uniformMatrix4fv(uW1a, false, rand(16));
gl.uniformMatrix4fv(uW1b, false, rand(16));
gl.uniform4fv(uB1a, rand(4, 0.2));
gl.uniform4fv(uB1b, rand(4, 0.2));
gl.uniform4fv(uW2a, rand(4, 0.8));
gl.uniform4fv(uW2b, rand(4, 0.8));
gl.uniform1f(uB2, 0.0);

let yaw = 0.0, pitch = 0.1, dist = 3.0;
let down = false, lx = 0, ly = 0;
canvas.addEventListener('pointerdown', e=>{down=true; lx=e.clientX; ly=e.clientY;});
window.addEventListener('pointerup', ()=> down=false);
window.addEventListener('pointermove', e=>{
  if(!down) return;
  const dx = e.clientX-lx, dy = e.clientY-ly;
  yaw += dx*0.005;
  pitch += dy*0.005;
  pitch = Math.max(-1.2, Math.min(1.2, pitch));
  lx=e.clientX; ly=e.clientY;
});
canvas.addEventListener('wheel', e=>{
  dist *= Math.exp(e.deltaY*0.001);
  dist = Math.max(1.3, Math.min(8.0, dist));
});

function resize() {
  const dpr = Math.min(2, devicePixelRatio || 1);
  const w = Math.floor(canvas.clientWidth * dpr);
  const h = Math.floor(canvas.clientHeight * dpr);
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w; canvas.height = h;
    gl.viewport(0, 0, w, h);
  }
}

function mvp() {
  const cx = Math.cos(yaw), sx = Math.sin(yaw);
  const cy = Math.cos(pitch), sy = Math.sin(pitch);
  const cam = [dist * sx * cy, dist * sy, dist * cx * cy];
  const tx = -cam[0], ty = -cam[1], tz = -cam[2];

  // crude view-proj (enough for demo)
  const fov = 60 * Math.PI/180;
  const a = canvas.width / Math.max(1, canvas.height);
  const f = 1/Math.tan(fov/2), zn=0.01, zf=100;
  const P = [
    f/a,0,0,0,
    0,f,0,0,
    0,0,(zf+zn)/(zn-zf),-1,
    0,0,(2*zf*zn)/(zn-zf),0
  ];
  const V = [
    cx, 0, -sx, 0,
    sx*sy, cy, cx*sy, 0,
    sx*cy, -sy, cx*cy, 0,
    tx, ty, tz, 1
  ];

  const M = new Float32Array(16);
  for (let r=0;r<4;r++) for(let c=0;c<4;c++) {
    let s=0;
    for(let k=0;k<4;k++) s += V[r*4+k]*P[k*4+c];
    M[r*4+c]=s;
  }
  return {M, cam};
}

gl.enable(gl.BLEND);
gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);

gl.disable(gl.DEPTH_TEST);

function frame() {
  resize();
  const {M, cam} = mvp();
  gl.clearColor(0.06, 0.07, 0.09, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);

  gl.uniformMatrix4fv(uMVP, false, M);
  gl.uniform3f(uCamPos, cam[0], cam[1], cam[2]);
  gl.uniform1f(uPointScale, 1200.0);

  gl.drawArrays(gl.POINTS, 0, N);
  requestAnimationFrame(frame);
}
frame();
