// Animated pink glow over the hero (see hero_shader() in main.c).
//
// The fragment shader reproduces #pink-overlay's CSS radial gradients exactly
// and, while animating, lets colour flow through them: travelling waves blend
// the pinks towards warm and cool tints and gently vary the glow, while the
// glow shapes themselves only drift slightly. On top of that, a sheen band
// sweeps across now and then, and every 8-15 s thin iridescent ridges run
// along the wave crests. Every load starts from random phases, and the sheen,
// the rainbow and the flow's strength and tempo are random events, so the
// motion never visibly repeats. The CSS gradient stays on #pink-overlay as the
// fallback and is switched off (data-shader) only once a frame has been
// drawn, so there's never a blank hero: until the shader is ready, or if WebGL
// is missing, the shader fails or the context is lost, the static gradient
// shows instead.

const canvas = document.getElementById('hero-shader');
const fallback = document.getElementById('pink-overlay');

// Drawing buffer size relative to CSS pixels: a soft gradient doesn't need
// full resolution, and CSS scales the canvas back up.
const RES_SCALE = 0.5;
const MAX_DPR = 1.5;
// The shader's motion repeats exactly every LOOP_S seconds (every frequency is
// a whole multiple of 2π / LOOP_S), so the clock can wrap without a jump and
// stays small enough for mediump floats.
const LOOP_S = 240;
// About 30 fps is plenty for motion this slow, and leaves headroom for the
// rest of the page (the cursor canvas, scrolling).
const FRAME_MS = 1000 / 30;
// Rainbow ridges: RAINBOW_S long, 3-10 s apart, so one starts every 8-15 s.
const RAINBOW_S = 5;
const RAINBOW_GAP_S = [3, 10];
// Sheen: a band crossing in 3.5-6 s, 2-8 s apart.
const SHEEN_S = [3.5, 6];
const SHEEN_GAP_S = [2, 8];
// The colour flow drifts towards a new random strength and tempo every 4-9 s.
const DRIFT_S = [4, 9];
// Motion fades in over this long after the shader takes over from the CSS.
const MOTION_IN_S = 1.5;
const FLOW_RANGE = [0.6, 1];
const RATE_RANGE = [0.75, 1.3];

const VERTEX = `
attribute vec2 a_pos;
void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
`;

const FRAGMENT = `
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform vec2 u_res;
uniform float u_time;     // seconds, wrapped at LOOP_S
uniform float u_motion;   // 0 = the static CSS gradient, 1 = animated
uniform vec4 u_seed;      // random phases, per page load
uniform float u_flow;     // colour flow strength, drifting randomly
uniform vec4 u_sheen;     // xy: direction, z: band position, w: strength
uniform vec2 u_rainbow;   // x: strength, y: hue offset
uniform float u_wide;     // 1 at min-width: 601px
uniform float u_white;    // alpha of the white highlight (wide screens only)
uniform float u_fx;       // sheen strength (lower in dark mode)
uniform vec3 u_a;         // top-right pink
uniform vec3 u_b;         // left pink
uniform vec3 u_warm;      // tints the colour flow drifts towards
uniform vec3 u_cool;

const float TAU = 6.2831853;

// Alpha of CSS radial-gradient(at c, color 0px, transparent 50%) in a unit box:
// a farthest-corner ellipse, which for these centres is the farthest-side
// ellipse scaled by sqrt(2).
float glow(vec2 uv, vec2 c) {
  vec2 side = max(c, 1.0 - c);
  return clamp(1.0 - 1.4142136 * length((uv - c) / side), 0.0, 1.0);
}

vec4 over(vec4 top, vec4 under) { return top + under * (1.0 - top.a); }

// Smooth field in -1..1: three domain-warped travelling waves, so colour
// flows across instead of pulsing in place. Periods 15-20 s.
float flow(vec2 q, float t, float s) {
  float a = sin(q.x * 1.9 + q.y * 1.1 + t * 12.0 + s);
  float b = sin(q.y * 2.3 - q.x * 0.7 - t * 15.0 + a * 1.1 + s * 1.7);
  return sin(q.x * 1.3 - q.y * 1.7 + t * 16.0 + b * 1.3 + s * 2.3);
}

// Oklab (perceptual lightness L, chroma ab) from linear sRGB and back.
vec3 toOklab(vec3 c) {
  vec3 lms = pow(vec3(dot(c, vec3(0.4122214708, 0.5363325363, 0.0514459929)),
                      dot(c, vec3(0.2119034982, 0.6806995451, 0.1073969566)),
                      dot(c, vec3(0.0883024619, 0.2817188376, 0.6299787005))), vec3(1.0 / 3.0));
  return vec3(dot(lms, vec3(0.2104542553, 0.7936177850, -0.0040720468)),
              dot(lms, vec3(1.9779984951, -2.4285922050, 0.4505937099)),
              dot(lms, vec3(0.0259040371, 0.7827717662, -0.8086757660)));
}
vec3 fromOklab(vec3 c) {
  vec3 lms = vec3(dot(c, vec3(1.0, 0.3963377774, 0.2158037573)),
                  dot(c, vec3(1.0, -0.1055613458, -0.0638541728)),
                  dot(c, vec3(1.0, -0.0894841775, -1.2914855480)));
  lms = lms * lms * lms;
  return vec3(dot(lms, vec3(4.0767416621, -3.3077115913, 0.2309699292)),
              dot(lms, vec3(-1.2684380046, 2.6097574011, -0.3413193965)),
              dot(lms, vec3(-0.0041960863, -0.7034186147, 1.7076147010)));
}

// Iridescence: swings the hue from pink through gold/orange one way and
// magenta, violet and blue to cyan the other (m * (2.4 sin(phase) - 1.1)),
// skipping yellow-green, which turns olive over the purple base. Lightness
// (towards light) and chroma are lifted so every hue reads as a bright
// pastel. Done in Oklab so hues stay saturated instead of passing through
// grey, then fitted into the screen's gamut by desaturating towards the grey
// of the same lightness: no channel is ever clipped, so nothing turns into
// hard-edged patches. At m = 0 it returns rgb unchanged, and every term fades
// with m, so a ribbon dissolves smoothly instead of leaving a haze behind.
vec3 iridescent(vec3 rgb, float m, float phase, float light) {
  vec3 lab = toOklab(pow(rgb, vec3(2.2)));
  // Rotate the colour's own chroma, so nothing changes until m does.
  float turn = m * (2.4 * sin(TAU * phase) - 1.1);
  mat2 rotate = mat2(cos(turn), sin(turn), -sin(turn), cos(turn));
  vec2 ab = rotate * lab.yz;
  // Extra chroma, in the turned direction; white has no hue of its own, so
  // it is leant towards pink (+a) for this.
  float chroma = length(lab.yz);
  vec2 dir = rotate * normalize(lab.yz + vec2(0.015, 0.0));
  ab += dir * (m * (sqrt(chroma * chroma + 0.022) - chroma));
  // Lightness rises a little ahead of the hue (2m at first) so a hue never
  // shows at a lightness where it would look muddy, but not so far ahead that
  // a pale band lingers after the colour: both return to zero with m.
  float lift = 0.5 * (lab.x + light + sqrt((lab.x - light) * (lab.x - light) + 0.003));
  float l = min(mix(lab.x, lift, m * (2.0 - m)), 1.0);
  vec3 lin = fromOklab(vec3(l, ab));
  vec3 grey = vec3(l * l * l);
  vec3 d = lin - grey;
  vec3 room = mix(grey / max(-d, 1e-5), (1.0 - grey) / max(d, 1e-5), step(0.0, d));
  lin = grey + d * min(1.0, min(room.x, min(room.y, room.z)));
  return pow(clamp(lin, 0.0, 1.0), vec3(1.0 / 2.2));
}

// Ordered noise of +-0.5/255 so slow gradients don't band in 8 bits.
float dither(vec2 xy) {
  return (fract(52.9829189 * fract(dot(xy, vec2(0.06711056, 0.00583715)))) - 0.5) / 255.0;
}

void main() {
  vec2 uv = gl_FragCoord.xy / u_res;
  uv.y = 1.0 - uv.y; // CSS y grows downward
  float t = u_time * (TAU / ${LOOP_S}.0);
  vec2 q = vec2(uv.x * u_res.x / u_res.y, uv.y) * 1.2; // square units

  float f1 = flow(q, t, u_seed.x);
  float f2 = flow(q.yx * 0.9 + 1.7, t, u_seed.z);
  float k = u_flow * u_motion;

  // Layer colours flow between the theme's pinks and a warm and a cool tint.
  vec3 ca = mix(u_a, u_warm, smoothstep(-0.2, 1.0, f1) * 0.55 * k);
  ca = mix(ca, u_cool, smoothstep(-0.2, 1.0, -f1) * 0.5 * k);
  vec3 cb = mix(u_b, u_cool, smoothstep(-0.2, 1.0, f2) * 0.5 * k);
  cb = mix(cb, u_warm, smoothstep(-0.2, 1.0, -f2) * 0.4 * k);
  vec3 cw = mix(vec3(1.0), mix(u_warm, u_cool, 0.5 + 0.5 * f2), 0.3 * k);

  // Glow centres drift a little (0..0.03, always inwards from the corners)
  // along slow loops; the glow strength flows.
  vec2 drift = 0.015 * u_motion * (1.0 + vec2(sin(t * 12.0 + u_seed.y), cos(t * 15.0 + u_seed.w)));
  float swell = 1.0 + 0.12 * f2 * k;

  // The CSS layers, bottom to top, premultiplied.
  vec2 cbPos = mix(vec2(0.2, 0.2), vec2(0.3, 0.0), u_wide) + drift.yx - drift;
  vec4 col = vec4(cb, 1.0) * min(1.0, glow(uv, cbPos) * swell);
  col = over(vec4(cw, 1.0) * (u_white * u_wide * glow(uv, drift.yx * 0.5)), col);
  col = over(vec4(ca, 1.0) * min(1.0, glow(uv, vec2(1.0 - drift.x, drift.y)) * swell), col);
  if (col.a < 0.002) { gl_FragColor = vec4(0.0); return; }

  vec3 rgb = clamp(col.rgb / col.a, 0.0, 1.0);
  // Sheen and rainbow fade out with the overlay itself.
  float presence = smoothstep(0.0, 0.3, col.a) * u_motion;
  float crestArg = dot(q, vec2(3.1, 1.9)) + f1 * 1.1 - t * 16.0 + u_seed.y;
  float wave = sin(crestArg);
  float ridge = pow(0.5 + 0.5 * wave, 14.0) * presence;

  // Sheen: a soft band sweeping across along u_sheen.xy.
  float band = (dot(uv - 0.5, u_sheen.xy) + f2 * 0.03 - u_sheen.z) / 0.14;
  float sheen = exp(-band * band) * (0.4 + 0.6 * ridge) * u_sheen.w * presence;
  rgb = mix(rgb, vec3(1.0), sheen * 0.3 * u_fx);

  // Rainbow: iridescent ridges along the wave crests. The hue changes along a
  // ridge and across it (cos is opposite on either side of the crest), like a
  // thin film. The overlay turns more opaque there: a translucent yellow or
  // green over the purple base would average out to grey.
  float m = u_rainbow.x * ridge;
  float alpha = col.a;
  if (m > 0.002) {
    float phase = q.x * 0.5 + q.y * 0.35 + cos(crestArg) * 0.3 + u_rainbow.y;
    rgb = iridescent(rgb, m, phase, 0.72 + 0.16 * u_fx);
    alpha += (1.0 - alpha) * 0.5 * m;
  }

  gl_FragColor = vec4(clamp(rgb + dither(gl_FragCoord.xy), 0.0, 1.0) * alpha, alpha);
}
`;

// Per theme: top-right pink, left pink, white highlight alpha, sheen strength
// (the first three are the CSS fallback's), and the warm and cool tints the
// colour flow drifts towards.
const THEMES = {
  light: { a: 0xffabbc, b: 0xeda4b2, white: 0xd3 / 255, fx: 1.0, warm: 0xffc3a6, cool: 0xd8b2ff },
  dark: { a: 0xcf6e81, b: 0xcf7789, white: 0x40 / 255, fx: 0.7, warm: 0xd98a76, cool: 0xa47ad8 },
};
const rgb = hex => [hex >> 16, (hex >> 8) & 0xff, hex & 0xff].map(c => c / 255);

const wideQuery = matchMedia('(min-width: 601px)');
const reducedQuery = matchMedia('(prefers-reduced-motion: reduce)');

let gl = null;
let uniforms = null;
let ready = false;
let rafId = 0;
let timerId = 0;
let onScreen = true;
let lastFrame = 0;
const rand = ([min, max]) => min + Math.random() * (max - min);

let clock = 0; // seconds of animation actually shown
let easeFrom = 0; // clock at the last start(): motion eases in from there
let phase = rand([0, LOOP_S]); // the shader's time, advancing at `rate`
const seed = [0, 1, 2, 3].map(() => rand([0, 2 * Math.PI]));
// Each drifting value eases towards a target re-rolled every DRIFT_S.
const flowStrength = { value: 0.8, target: 0.8, range: FLOW_RANGE, next: 0 };
const rate = { value: 1, target: 1, range: RATE_RANGE, next: 0 };
// Events run from `start` for `length` seconds; the rest is re-rolled each time.
const sheen = { start: rand([1, 4]), length: rand(SHEEN_S), dir: [0.85, -0.53], strength: 1 };
const rainbow = { start: rand([3, 6]), length: RAINBOW_S, hue: Math.random(), strength: 0.65 };

function compile(type, source) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  return shader;
}

// Starts compiling and linking the program. Compile errors surface as a
// failed link, checked in setup(): querying status before then would block.
function build() {
  const program = gl.createProgram();
  gl.attachShader(program, compile(gl.VERTEX_SHADER, VERTEX));
  gl.attachShader(program, compile(gl.FRAGMENT_SHADER, FRAGMENT));
  gl.linkProgram(program);
  return program;
}

// Uses the linked program and sets up the full-screen triangle; false if the
// shaders didn't compile or link.
function setup(program) {
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) return false;
  gl.useProgram(program);

  gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
  const pos = gl.getAttribLocation(program, 'a_pos');
  gl.enableVertexAttribArray(pos);
  gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 0, 0);

  uniforms = {};
  for (const name of ['res', 'time', 'motion', 'seed', 'flow', 'sheen', 'rainbow', 'wide', 'white', 'fx', 'a', 'b', 'warm', 'cool'])
    uniforms[name] = gl.getUniformLocation(program, 'u_' + name);
  return true;
}

function setTheme() {
  const theme = THEMES[document.documentElement.classList.contains('dark') ? 'dark' : 'light'];
  for (const name of ['a', 'b', 'warm', 'cool'])
    gl.uniform3fv(uniforms[name], rgb(theme[name]));
  gl.uniform1f(uniforms.white, theme.white);
  gl.uniform1f(uniforms.fx, theme.fx);
  gl.uniform1f(uniforms.wide, wideQuery.matches ? 1 : 0);
}

function resize() {
  const scale = RES_SCALE * Math.min(devicePixelRatio || 1, MAX_DPR);
  const width = Math.max(1, Math.round(canvas.clientWidth * scale));
  const height = Math.max(1, Math.round(canvas.clientHeight * scale));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  gl.viewport(0, 0, width, height);
  gl.uniform2f(uniforms.res, width, height);
}

// 0..1 through an event, or -1 outside it. Once it has ended, the next one is
// scheduled after a random gap and re-rolled.
function progress(event, gap, reroll) {
  if (clock >= event.start + event.length) {
    event.start = clock + rand(gap);
    reroll(event);
  }
  const x = (clock - event.start) / event.length;
  return x > 0 && x < 1 ? x : -1;
}

// Smooth 0 → 1 → 0 bump over an event's progress.
const bump = x => (x < 0 ? 0 : 0.5 - 0.5 * Math.cos(x * 2 * Math.PI));

function drift(value, dt) {
  if (clock >= value.next) {
    value.target = rand(value.range);
    value.next = clock + rand(DRIFT_S);
  }
  value.value += (value.target - value.value) * (1 - Math.exp(-dt / 2.5));
}

function rerollSheen(event) {
  event.length = rand(SHEEN_S);
  event.strength = rand([0.6, 1]);
  // Roughly diagonal, in either direction.
  const angle = rand([-0.9, 0.9]) - 0.55 + (Math.random() < 0.5 ? Math.PI : 0);
  event.dir = [Math.cos(angle), Math.sin(angle)];
}

function rerollRainbow(event) {
  event.strength = rand([0.5, 0.75]);
  event.hue = Math.random();
}

// Advances the animation by dt seconds.
function step(dt) {
  clock += dt;
  drift(flowStrength, dt);
  drift(rate, dt);
  phase = (phase + dt * rate.value) % LOOP_S;
  progress(sheen, SHEEN_GAP_S, rerollSheen);
  progress(rainbow, RAINBOW_GAP_S, rerollRainbow);
}

function draw() {
  if (!ready) return;
  const animate = !reducedQuery.matches;
  const sweep = animate ? progress(sheen, SHEEN_GAP_S, rerollSheen) : -1;
  const shimmer = animate ? progress(rainbow, RAINBOW_GAP_S, rerollRainbow) : -1;
  gl.uniform1f(uniforms.time, phase);
  // Motion eases in, so the shader's first frames match the CSS gradient it
  // replaces and the swap can't be seen.
  const ease = Math.min(1, (clock - easeFrom) / MOTION_IN_S);
  gl.uniform1f(uniforms.motion, animate ? ease * ease * (3 - 2 * ease) : 0);
  gl.uniform4fv(uniforms.seed, seed);
  gl.uniform1f(uniforms.flow, flowStrength.value);
  gl.uniform4f(uniforms.sheen, sheen.dir[0], sheen.dir[1], sweep * 2.2 - 1.1, bump(sweep) * sheen.strength);
  gl.uniform2f(uniforms.rainbow, bump(shimmer) * rainbow.strength, rainbow.hue + clock * 0.05);
  gl.drawArrays(gl.TRIANGLES, 0, 3);
  // The first drawn frame replaces the CSS gradient underneath.
  if (!('shader' in fallback.dataset)) fallback.dataset.shader = '';
}

function frame(now) {
  // Clamp so a long stall (debugger, slow device) doesn't jump the motion.
  step(Math.min(now - lastFrame, 100) / 1000);
  lastFrame = now;
  draw();
  // Wait most of a frame before asking for the next one: a requestAnimationFrame
  // that just skipped frames would still wake the page at the display's full
  // refresh rate (a main frame every vsync on a 144 Hz screen).
  rafId = 0;
  timerId = setTimeout(() => {
    timerId = 0;
    rafId = requestAnimationFrame(frame);
  }, FRAME_MS - 8);
}

// Runs the loop only while it can be seen and motion is allowed.
function updateLoop() {
  const run = ready && onScreen && !document.hidden && !reducedQuery.matches;
  const running = rafId || timerId;
  if (run && !running) {
    lastFrame = performance.now();
    rafId = requestAnimationFrame(frame);
  } else if (!run && running) {
    cancelAnimationFrame(rafId);
    clearTimeout(timerId);
    rafId = timerId = 0;
  }
}

function fail() {
  ready = false;
  updateLoop();
  delete fallback.dataset.shader;
}

function start() {
  const program = build();
  // Where supported, the driver compiles in the background: poll for it
  // instead of blocking the main thread on the first status query.
  const parallel = gl.getExtension('KHR_parallel_shader_compile');
  const finish = () => {
    if (gl.isContextLost()) return;
    if (parallel && !gl.getProgramParameter(program, parallel.COMPLETION_STATUS_KHR)) {
      setTimeout(finish, 16);
      return;
    }
    if (!setup(program)) {
      fail();
      return;
    }
    ready = true;
    easeFrom = clock;
    setTheme();
    resize();
    draw();
    updateLoop();
  };
  finish();
}

function boot() {
  gl = canvas.getContext('webgl', {
    alpha: true,
    premultipliedAlpha: true,
    antialias: false,
    depth: false,
    stencil: false,
    powerPreference: 'low-power',
    // Software GL (no usable GPU) would draw and read back every frame on the
    // CPU: keep the static CSS gradient there instead.
    failIfMajorPerformanceCaveat: true,
  });

  if (gl) {
    start();

    // Resizing clears the canvas; observer callbacks run before paint, so the
    // redraw lands in the same frame.
    new ResizeObserver(() => { if (ready) { resize(); draw(); } }).observe(canvas);
    new IntersectionObserver(entries => {
      onScreen = entries[entries.length - 1].isIntersecting;
      updateLoop();
    }).observe(canvas);
    document.addEventListener('visibilitychange', updateLoop);
    reducedQuery.addEventListener('change', () => { updateLoop(); draw(); });
    wideQuery.addEventListener('change', () => { if (ready) { setTheme(); draw(); } });
    // The theme toggle flips <html class="dark">.
    new MutationObserver(() => { if (ready) { setTheme(); draw(); } })
      .observe(document.documentElement, { attributes: true, attributeFilter: ['class'] });

    canvas.addEventListener('webglcontextlost', e => {
      e.preventDefault(); // allow webglcontextrestored
      fail();
    });
    canvas.addEventListener('webglcontextrestored', start);
  }
}

// WebGL setup (context, shader compile) waits for the page to load, so it
// stays off the critical path of the first paint and the page-transition
// cover. Until the first frame, the CSS gradient shows, and the shader starts
// out identical to it.
if (document.readyState === 'complete') boot();
else addEventListener('load', boot, { once: true });
