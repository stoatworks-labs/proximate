// Proximate web demo — page logic. The DSP is the plugin's, in proximate.js (wasm) inside
// worklet.js; this file builds the audio graph, the sources, and the plugin face.

import createProximateModule from './proximate.js';

const $ = (id) => document.getElementById(id);
const SPEED_OF_SOUND = 343;
const SIM_POLE_HZ = 90;

// --- Tuning, read from the same module the worklet runs, so the scales cannot drift ---
const M = await createProximateModule();
const TUNING = {
  splitHz: M.cwrap('prx_tuning_split_hz', 'number', [])(),
  shelfFloorHz: M.cwrap('prx_tuning_shelf_floor_hz', 'number', [])(),
  maxCutDb: M.cwrap('prx_tuning_max_cut_db', 'number', [])(),
  holdBelowDb: M.cwrap('prx_tuning_hold_below_db', 'number', [])(),
};

// --- State ----------------------------------------------------------------------------
const state = {
  ctx: null, node: null, analyserIn: null, analyserOut: null, trim: null,
  source: null, micStream: null, fileBuffer: null, voices: {},
  playing: false, ready: false,
  amount: 0.5, wet: true,
  status: { balance: 0, cut: 0, ceiling: 2, level: -100, holding: true, inPeak: 0, outPeak: 0 },
  peakIn: 0, peakOut: 0,
};

// --- Distance slider: 3 cm at 0 to 100 cm at 1, logarithmic ---------------------------
function sliderToMetres(t) { return 0.03 * Math.pow(100 / 3, t); }
const SIM_MAX_GAIN = 7.943;   // +18 dB, the same cap as the wasm simulator
function simRiseDb(hz, metres) {
  const g = Math.min(SIM_MAX_GAIN, SPEED_OF_SOUND / (2 * metres) / (2 * Math.PI) / SIM_POLE_HZ);
  if (g <= 1) return 0;
  const fz = SIM_POLE_HZ * g;
  return 10 * Math.log10((hz * hz + fz * fz) / (hz * hz + SIM_POLE_HZ * SIM_POLE_HZ));
}
function shelfDb(hz, cutDb) {
  const g = Math.pow(10, -cutDb / 20);
  const fz = TUNING.shelfFloorHz, fp = fz / g;
  return 10 * Math.log10((hz * hz + fz * fz) / (hz * hz + fp * fp));
}
function currentMetres() { return sliderToMetres(parseFloat($('distance').value)); }

function updateDistanceRead() {
  const m = currentMetres();
  const cm = m * 100;
  $('distVal').textContent = cm < 10 ? `${cm.toFixed(1)} cm` : `${Math.round(cm)} cm`;
  const rise = $('simOn').checked ? simRiseDb(100, m) : 0;
  $('distDb').textContent = rise > 0.05 ? `+${rise.toFixed(0)} dB at 100 Hz` : 'flat — beyond the near field';
  sendSim();
}

function sendSim() {
  if (state.node) state.node.port.postMessage({ type: 'sim', on: $('simOn').checked, metres: currentMetres() });
}

// --- Audio graph ----------------------------------------------------------------------
async function ensureAudio() {
  if (state.ctx) return;
  const ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
  await ctx.audioWorklet.addModule('worklet.js');
  const node = new AudioWorkletNode(ctx, 'proximate', {
    numberOfInputs: 1, numberOfOutputs: 2, outputChannelCount: [2, 2],
  });
  const trim = ctx.createGain();
  trim.gain.value = dbToLin(parseFloat($('trim').value));
  const analyserIn = ctx.createAnalyser();
  const analyserOut = ctx.createAnalyser();
  for (const a of [analyserIn, analyserOut]) { a.fftSize = 4096; a.smoothingTimeConstant = 0.75; }

  trim.connect(node);
  node.connect(analyserOut, 0);
  node.connect(analyserIn, 1);
  analyserOut.connect(ctx.destination);

  node.port.onmessage = (e) => {
    const m = e.data;
    if (m.type === 'ready') {
      state.ready = true;
      node.port.postMessage({ type: 'amount', value: state.amount });
      node.port.postMessage({ type: 'wet', on: state.wet });
      node.port.postMessage({ type: 'safetyClip', on: $('safetyClip').checked });
      sendSim();
    } else if (m.type === 'status') {
      state.status = m;
      state.peakIn = Math.max(m.inPeak, state.peakIn * 0.92);
      state.peakOut = Math.max(m.outPeak, state.peakOut * 0.92);
    }
  };

  Object.assign(state, { ctx, node, trim, analyserIn, analyserOut });
}

function dbToLin(db) { return Math.pow(10, db / 20); }

// --- Sources --------------------------------------------------------------------------

/** A talking voice with no recording behind it: a pulse train (a sawtooth, whose
    harmonics fall 6 dB per octave, through a gentle low-pass for the glottal tilt) into
    three vowel formants, with syllables, words, phrases and the pauses between them.
    Rendered offline once per voice and looped. */
async function renderVoice(kind) {
  const sr = 48000, seconds = 18;
  const off = new OfflineAudioContext(1, sr * seconds, sr);
  const low = kind === 'voiceLow';
  const f0base = low ? 112 : 205;
  const fscale = low ? 1.0 : 1.17;

  const osc = off.createOscillator();
  osc.type = 'sawtooth';
  const tilt = off.createBiquadFilter();
  tilt.type = 'lowpass'; tilt.frequency.value = 1400; tilt.Q.value = 0.5;
  const env = off.createGain(); env.gain.value = 0;
  osc.connect(tilt).connect(env);

  const vowels = {
    a: [730, 1090, 2440], e: [530, 1840, 2480], i: [270, 2290, 3010], o: [570, 840, 2410], u: [300, 870, 2240],
  };
  // The vocal tract as three resonances in series on top of the source, so the
  // fundamental and the low harmonics survive the way they do in a real voice (a
  // vocal tract is all-pole: unity at DC, peaks at the formants) rather than being
  // filtered away by band-passes.
  let stage = env;
  const formants = [0, 1, 2].map((i) => {
    const f = off.createBiquadFilter();
    f.type = 'peaking'; f.Q.value = [5, 7, 8][i]; f.gain.value = [14, 10, 7][i];
    f.frequency.value = vowels.a[i] * fscale;
    stage.connect(f); stage = f;
    return f;
  });
  stage.connect(off.destination);

  // Consonant-ish bursts: a little band-limited noise at syllable onsets.
  const noiseLen = sr;
  const noiseBuf = off.createBuffer(1, noiseLen, sr);
  const nd = noiseBuf.getChannelData(0);
  let seed = 0x2545F491;
  for (let i = 0; i < noiseLen; i++) { seed = (seed * 1664525 + 1013904223) >>> 0; nd[i] = (seed / 4294967296) * 2 - 1; }
  const noise = off.createBufferSource(); noise.buffer = noiseBuf; noise.loop = true;
  const noiseBp = off.createBiquadFilter(); noiseBp.type = 'bandpass'; noiseBp.frequency.value = 3800; noiseBp.Q.value = 0.9;
  const noiseEnv = off.createGain(); noiseEnv.gain.value = 0;
  noise.connect(noiseBp).connect(noiseEnv).connect(off.destination);

  // A deterministic schedule, so the loop is the same every time.
  let rnd = low ? 12345 : 67890;
  const rand = () => { rnd = (rnd * 1103515245 + 12345) & 0x7fffffff; return rnd / 0x7fffffff; };
  const vowelKeys = Object.keys(vowels);
  let t = 0.4;
  let sinceLongPause = 0;
  while (t < seconds - 2.5) {
    const phraseWords = 3 + Math.floor(rand() * 3);
    const phraseStart = t;
    const phraseLen = phraseWords * 0.55;
    for (let w = 0; w < phraseWords; w++) {
      const syllables = 1 + Math.floor(rand() * 3);
      for (let s = 0; s < syllables; s++) {
        const v = vowels[vowelKeys[Math.floor(rand() * vowelKeys.length)]];
        const dur = 0.13 + rand() * 0.1;
        const progress = Math.min(1, (t - phraseStart) / phraseLen);
        const f0 = f0base * (1.08 - 0.16 * progress) * (1 + (rand() - 0.5) * 0.06);
        osc.frequency.setValueAtTime(f0 * 1.03, t);
        osc.frequency.linearRampToValueAtTime(f0, t + dur);
        formants.forEach((f, i) => f.frequency.setTargetAtTime(v[i] * fscale, t, 0.015));
        env.gain.setValueAtTime(0, t);
        env.gain.linearRampToValueAtTime(0.9, t + 0.02);
        env.gain.setValueAtTime(0.9, t + dur - 0.04);
        env.gain.linearRampToValueAtTime(0, t + dur);
        if (rand() < 0.6) {
          noiseEnv.gain.setValueAtTime(0.25, t - 0.03 < 0 ? 0 : t - 0.03);
          noiseEnv.gain.linearRampToValueAtTime(0, t + 0.01);
        }
        t += dur + 0.04;
      }
      t += 0.14;
    }
    t += 0.5;
    sinceLongPause += phraseLen + 0.5;
    if (sinceLongPause > 5.5) { t += 1.4; sinceLongPause = 0; }
  }

  osc.start(0); noise.start(0);
  osc.stop(seconds); noise.stop(seconds);
  const buffer = await off.startRendering();

  // Normalise to -6 dBFS peak; the trim does the rest.
  const d = buffer.getChannelData(0);
  let peak = 0;
  for (let i = 0; i < d.length; i++) peak = Math.max(peak, Math.abs(d[i]));
  const scale = peak > 0 ? 0.5 / peak : 1;
  for (let i = 0; i < d.length; i++) d[i] *= scale;
  return buffer;
}

async function startSource(kind) {
  stopSource();
  const ctx = state.ctx;
  if (kind === 'mic') {
    const stream = await navigator.mediaDevices.getUserMedia({
      audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 1 },
    });
    state.micStream = stream;
    const src = ctx.createMediaStreamSource(stream);
    src.connect(state.trim);
    state.source = src;
    return;
  }
  let buffer;
  if (kind === 'file') {
    buffer = state.fileBuffer;
    if (!buffer) throw new Error('no file');
  } else {
    if (!state.voices[kind]) state.voices[kind] = await renderVoice(kind);
    buffer = state.voices[kind];
  }
  const src = ctx.createBufferSource();
  src.buffer = buffer; src.loop = true;
  src.connect(state.trim);
  src.start();
  state.source = src;
}

function stopSource() {
  if (state.source) {
    try { state.source.stop(); } catch (_) { /* a stream source has no stop */ }
    state.source.disconnect();
    state.source = null;
  }
  if (state.micStream) {
    for (const tr of state.micStream.getTracks()) tr.stop();
    state.micStream = null;
  }
}

async function play() {
  const kind = $('sourceSel').value;
  if (kind === 'file' && !state.fileBuffer) { $('fileInput').click(); return; }
  $('playBtn').disabled = true;
  try {
    await ensureAudio();
    if (state.ctx.state !== 'running') await state.ctx.resume();
    await startSource(kind);
    state.playing = true;
    $('playBtn').textContent = '■ Stop';
    $('playBtn').classList.add('playing');
  } catch (err) {
    console.error(err);
    $('status').textContent = kind === 'mic' ? 'Microphone access was refused.' : `Could not start: ${err.message}`;
  } finally {
    $('playBtn').disabled = false;
  }
}

function stop() {
  stopSource();
  state.playing = false;
  if (state.node) state.node.port.postMessage({ type: 'reset' });
  $('playBtn').textContent = '▶ Play';
  $('playBtn').classList.remove('playing');
}

// --- Controls -------------------------------------------------------------------------
$('playBtn').addEventListener('click', () => (state.playing ? stop() : play()));

$('sourceSel').addEventListener('change', async () => {
  const kind = $('sourceSel').value;
  $('micNote').classList.toggle('show', kind === 'mic');
  if (kind === 'file' && !state.fileBuffer) { $('fileInput').click(); return; }
  if (state.playing) { await startSource(kind); }
});

$('fileInput').addEventListener('change', async () => {
  const f = $('fileInput').files[0];
  if (!f) return;
  await ensureAudio();
  const data = await f.arrayBuffer();
  try {
    state.fileBuffer = await state.ctx.decodeAudioData(data);
  } catch (err) {
    $('status').textContent = 'That file could not be decoded.';
    return;
  }
  $('sourceSel').value = 'file';
  if (state.playing) await startSource('file'); else play();
});

$('trim').addEventListener('input', () => {
  const db = parseFloat($('trim').value);
  $('trimVal').textContent = `${db.toFixed(1)} dB`;
  if (state.trim) state.trim.gain.setTargetAtTime(dbToLin(db), state.ctx.currentTime, 0.02);
});

$('safetyClip').addEventListener('change', () => {
  if (state.node) state.node.port.postMessage({ type: 'safetyClip', on: $('safetyClip').checked });
});

$('distance').addEventListener('input', updateDistanceRead);
$('simOn').addEventListener('change', updateDistanceRead);

$('abBtn').addEventListener('click', () => {
  state.wet = !state.wet;
  $('abBtn').classList.toggle('on', state.wet);
  $('abBtn').textContent = state.wet ? 'Proximate ON — click for before' : 'BEFORE — the microphone alone';
  if (state.node) state.node.port.postMessage({ type: 'wet', on: state.wet });
});

// --- The knob -------------------------------------------------------------------------
const knob = $('knob');
const kctx = knob.getContext('2d');
const START = Math.PI * 1.2, END = Math.PI * 2.8;   // JUCE's angles: clockwise from 12 o'clock

function setAmount(a, fromUser = true) {
  state.amount = Math.min(1, Math.max(0, a));
  $('amountRead').textContent = `${Math.round(state.amount * 100)} %`;
  if (state.node && fromUser) state.node.port.postMessage({ type: 'amount', value: state.amount });
  drawKnob();
}

function drawKnob() {
  const W = knob.width, cx = W / 2, cy = W / 2;
  const radius = W / 2 - 20;
  const arcWidth = Math.max(8, radius * 0.11);
  const arcRadius = radius - arcWidth / 2;
  const angle = START + state.amount * (END - START);
  const toCanvas = (a) => a - Math.PI / 2;

  kctx.clearRect(0, 0, W, W);
  kctx.fillStyle = '#14161a';
  kctx.beginPath(); kctx.arc(cx, cy, radius * 0.8, 0, Math.PI * 2); kctx.fill();

  kctx.lineCap = 'round'; kctx.lineWidth = arcWidth;
  kctx.strokeStyle = '#23272e';
  kctx.beginPath(); kctx.arc(cx, cy, arcRadius, toCanvas(START), toCanvas(END)); kctx.stroke();
  if (state.amount > 0) {
    kctx.strokeStyle = '#45b0e8';
    kctx.beginPath(); kctx.arc(cx, cy, arcRadius, toCanvas(START), toCanvas(angle)); kctx.stroke();
  }

  // The pointer.
  const len = radius * 0.42, inner = radius * 0.30;
  kctx.save();
  kctx.translate(cx, cy); kctx.rotate(angle);
  kctx.fillStyle = '#d4d8de';
  const w = arcWidth * 0.6;
  roundRect(kctx, -w / 2, -len - inner, w, len, w / 2);
  kctx.fill();
  kctx.restore();
}

function roundRect(c, x, y, w, h, r) {
  c.beginPath();
  c.moveTo(x + r, y); c.lineTo(x + w - r, y); c.arcTo(x + w, y, x + w, y + r, r);
  c.lineTo(x + w, y + h - r); c.arcTo(x + w, y + h, x + w - r, y + h, r);
  c.lineTo(x + r, y + h); c.arcTo(x, y + h, x, y + h - r, r);
  c.lineTo(x, y + r); c.arcTo(x, y, x + r, y, r); c.closePath();
}

let drag = null;
knob.addEventListener('pointerdown', (e) => {
  knob.setPointerCapture(e.pointerId);
  drag = { y: e.clientY, x: e.clientX, amount: state.amount };
});
knob.addEventListener('pointermove', (e) => {
  if (!drag) return;
  const delta = (drag.y - e.clientY) + (e.clientX - drag.x);
  setAmount(drag.amount + delta / 250);
});
knob.addEventListener('pointerup', () => { drag = null; });
knob.addEventListener('pointercancel', () => { drag = null; });
knob.addEventListener('dblclick', () => setAmount(0.5));
knob.addEventListener('wheel', (e) => { e.preventDefault(); setAmount(state.amount - Math.sign(e.deltaY) * 0.02); }, { passive: false });
knob.tabIndex = 0;
knob.addEventListener('keydown', (e) => {
  if (e.key === 'ArrowUp' || e.key === 'ArrowRight') setAmount(state.amount + 0.01);
  if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') setAmount(state.amount - 0.01);
});

// --- Meters, as the plugin draws them -------------------------------------------------
const METER_MIN = -12, METER_MAX = 12;
function meterX(db, x0, w) {
  const t = Math.min(1, Math.max(0, (db - METER_MIN) / (METER_MAX - METER_MIN)));
  return x0 + t * w;
}

/** Size a meter canvas to its CSS box at device resolution; returns (ctx, W, H, dpr). */
function meterCanvas(id) {
  const c = $(id);
  const dpr = window.devicePixelRatio || 1;
  const w = Math.round(c.clientWidth * dpr), h = Math.round(c.clientHeight * dpr);
  if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
  const g = c.getContext('2d');
  g.clearRect(0, 0, w, h);
  g.font = `${9 * dpr}px -apple-system, Segoe UI, Roboto, sans-serif`;
  return [g, w, h, dpr];
}

function drawBalance() {
  const [g, W, H, dpr] = meterCanvas('balanceMeter');
  const s = state.status;
  const off = state.amount <= 0;
  const live = state.playing && !off && !s.holding && s.level > TUNING.holdBelowDb;
  const x0 = 6 * dpr, w = W - 12 * dpr, y = 4 * dpr, h = 8 * dpr;
  g.fillStyle = '#23272e'; roundRect(g, x0, y, w, h, 3 * dpr); g.fill();
  const ceilingX = meterX(s.ceiling, x0, w);
  if (!off) {
    g.fillStyle = 'rgba(69,176,232,0.15)'; roundRect(g, ceilingX, y, x0 + w - ceilingX, h, 3 * dpr); g.fill();
  }
  const balanceX = meterX(s.balance, x0, w);
  if (live && balanceX > ceilingX) {
    g.fillStyle = 'rgba(69,176,232,0.7)'; roundRect(g, ceilingX, y, balanceX - ceilingX, h, 3 * dpr); g.fill();
  }
  if (!off) { g.fillStyle = '#45b0e8'; g.fillRect(ceilingX - dpr, y - 3 * dpr, 2 * dpr, h + 6 * dpr); }
  if (live) {
    g.fillStyle = '#d4d8de';
    g.beginPath(); g.arc(balanceX, y + h / 2, 4 * dpr, 0, Math.PI * 2); g.fill();
  }
  g.fillStyle = 'rgba(139,146,158,0.75)'; g.textAlign = 'center';
  for (const db of [-12, -6, 0, 6, 12]) g.fillText(String(db), meterX(db, x0, w), H - 3 * dpr);
  $('balanceRead').textContent = off ? 'off' : fmtDb(s.balance);
  $('balanceRead').classList.toggle('dim', !live);
}

function drawCut() {
  const [g, W, H, dpr] = meterCanvas('cutMeter');
  const s = state.status;
  const off = state.amount <= 0;
  const x0 = 6 * dpr, w = W - 12 * dpr, y = 4 * dpr, h = 8 * dpr;
  g.fillStyle = '#23272e'; roundRect(g, x0, y, w, h, 3 * dpr); g.fill();
  const frac = Math.min(1, Math.max(0, s.cut / TUNING.maxCutDb));
  if (frac > 0 && !off) {
    g.fillStyle = '#e0a03a'; roundRect(g, x0 + w - frac * w, y, frac * w, h, 3 * dpr); g.fill();
  }
  g.fillStyle = 'rgba(139,146,158,0.75)'; g.textAlign = 'center';
  for (const db of [0, 6, 12, 18]) g.fillText(`-${db}`, x0 + w - (db / TUNING.maxCutDb) * w, H - 3 * dpr);
  $('cutRead').textContent = off ? '0.0 dB' : fmtDb(-s.cut);
  $('cutRead').classList.toggle('dim', !(s.cut > 0.05) || off);
}

function fmtDb(db) {
  if (Math.abs(db) < 0.05) db = 0;
  return `${db > 0 ? '+' : ''}${db.toFixed(1)} dB`;
}

function drawPeak(id, peak) {
  const c = $(id), g = c.getContext('2d');
  const W = c.width, H = c.height;
  g.clearRect(0, 0, W, H);
  const db = 20 * Math.log10(Math.max(peak, 1e-5));
  const t = Math.min(1, Math.max(0, (db + 60) / 60));
  const hgt = Math.round(t * (H - 4));
  g.fillStyle = db > -1 ? '#ff5d5d' : (db > -10 ? '#e0a03a' : '#45b0e8');
  g.fillRect(2, H - 2 - hgt, W - 4, hgt);
}

function drawStatus() {
  const s = state.status;
  const el = $('status');
  el.classList.remove('live');
  if (!state.playing) { el.textContent = 'Press Play.'; return; }
  if (state.amount <= 0) { el.textContent = 'Off: the amount is at zero, the audio is untouched.'; return; }
  if (s.holding) { el.textContent = 'Holding: nobody is talking, so the cut stays where it is.'; return; }
  el.textContent = 'Listening.';
  el.classList.add('live');
}

// --- The spectrum ---------------------------------------------------------------------
const spec = $('spectrum');
const sctx = spec.getContext('2d');
const F_LO = 30, F_HI = 12000;
let specIn = null, specOut = null;

function fx(hz, W) { return (Math.log(hz / F_LO) / Math.log(F_HI / F_LO)) * W; }

function drawSpectrum() {
  const dpr = window.devicePixelRatio || 1;
  const cssW = spec.clientWidth, cssH = spec.clientHeight;
  if (spec.width !== Math.round(cssW * dpr) || spec.height !== Math.round(cssH * dpr)) {
    spec.width = Math.round(cssW * dpr); spec.height = Math.round(cssH * dpr);
  }
  const W = spec.width, H = spec.height;
  sctx.clearRect(0, 0, W, H);
  sctx.font = `${11 * dpr}px -apple-system, Segoe UI, Roboto, sans-serif`;

  // Grid.
  sctx.strokeStyle = '#1b1e24'; sctx.lineWidth = 1; sctx.fillStyle = '#5a6470'; sctx.textAlign = 'center';
  for (const hz of [50, 100, 250, 500, 1000, 2000, 5000, 10000]) {
    const x = fx(hz, W);
    sctx.beginPath(); sctx.moveTo(x, 0); sctx.lineTo(x, H); sctx.stroke();
    sctx.fillText(hz >= 1000 ? `${hz / 1000}k` : String(hz), x, H - 6 * dpr);
  }
  const splitX = fx(TUNING.splitHz, W);
  sctx.strokeStyle = 'rgba(69,176,232,0.35)'; sctx.setLineDash([4 * dpr, 4 * dpr]);
  sctx.beginPath(); sctx.moveTo(splitX, 0); sctx.lineTo(splitX, H); sctx.stroke(); sctx.setLineDash([]);
  sctx.fillStyle = 'rgba(69,176,232,0.6)'; sctx.textAlign = 'left';
  sctx.fillText(`split ${TUNING.splitHz} Hz`, splitX + 4 * dpr, 14 * dpr);

  // The spectra: -90..0 dB over the lower 70 % of the plot.
  const specTop = H * 0.3, specBottom = H - 18 * dpr;
  const yOf = (db) => specBottom - Math.min(1, Math.max(0, (db + 90) / 90)) * (specBottom - specTop);

  if (state.analyserIn && state.playing) {
    const n = state.analyserIn.frequencyBinCount;
    if (!specIn || specIn.length !== n) { specIn = new Float32Array(n); specOut = new Float32Array(n); }
    state.analyserIn.getFloatFrequencyData(specIn);
    state.analyserOut.getFloatFrequencyData(specOut);
    const sr = state.ctx.sampleRate;
    const plot = (data, stroke, fill) => {
      sctx.beginPath();
      let started = false;
      for (let i = 1; i < n; i++) {
        const hz = (i * sr) / (2 * n);
        if (hz < F_LO || hz > F_HI) continue;
        const x = fx(hz, W), y = yOf(data[i]);
        if (!started) { sctx.moveTo(x, y); started = true; } else sctx.lineTo(x, y);
      }
      if (fill) {
        sctx.lineTo(W, specBottom); sctx.lineTo(0, specBottom); sctx.closePath();
        sctx.fillStyle = fill; sctx.fill();
      } else {
        sctx.strokeStyle = stroke; sctx.lineWidth = 1.5 * dpr; sctx.stroke();
      }
    };
    plot(specIn, null, 'rgba(90,100,112,0.45)');
    plot(specOut, '#45b0e8', null);
  }

  // The curves: the simulated rise and the compensator's shelf, +-24 dB around a line
  // at 20 % height.
  const zeroY = H * 0.2, perDb = (H * 0.16) / 24;
  sctx.strokeStyle = '#2a2e36'; sctx.beginPath(); sctx.moveTo(0, zeroY); sctx.lineTo(W, zeroY); sctx.stroke();
  sctx.fillStyle = '#5a6470'; sctx.textAlign = 'right';
  sctx.fillText('0 dB', W - 4 * dpr, zeroY - 3 * dpr);
  const curve = (fn, stroke, width) => {
    sctx.beginPath();
    for (let px = 0; px <= W; px += 2 * dpr) {
      const hz = F_LO * Math.pow(F_HI / F_LO, px / W);
      const y = zeroY - fn(hz) * perDb;
      if (px === 0) sctx.moveTo(px, y); else sctx.lineTo(px, y);
    }
    sctx.strokeStyle = stroke; sctx.lineWidth = width * dpr; sctx.stroke();
  };
  const metres = currentMetres();
  if ($('simOn').checked) curve((hz) => simRiseDb(hz, metres), '#7a5a2a', 1.5);
  const cut = state.playing && state.amount > 0 ? state.status.cut : 0;
  curve((hz) => shelfDb(hz, cut), '#e0a03a', 2);
}

// --- Frame loop -----------------------------------------------------------------------
function frame() {
  drawBalance(); drawCut(); drawStatus();
  drawPeak('inMeter', state.playing ? state.peakIn : 0);
  drawPeak('outMeter', state.playing ? state.peakOut : 0);
  drawSpectrum();
  requestAnimationFrame(frame);
}

setAmount(0.5, false);
updateDistanceRead();
$('trimVal').textContent = `${parseFloat($('trim').value).toFixed(1)} dB`;
frame();
