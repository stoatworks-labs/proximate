// Verification harness for the Proximate wasm core. Node-only; not shipped.
//
// Same discipline as the plugin's own suite: real numbers out of the actual compiled
// module, checked against independently derived expectations. Then the whole thing is
// pinned against a NATIVE clang++ build of the same sources — tools/proxstat.cpp over the
// same stimulus — so this catches wasm-vs-native drift, not just self-consistency. Audio
// goes through in 128-sample chunks, matching the AudioWorklet render quantum.
//
// Run from the repo root: node web/test/harness.mjs

import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import createProximateModule from '../public/proximate.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..', '..');
const SR = 48000;
const BLOCK = 128;

const M = await createProximateModule();
const C = {
  init: M.cwrap('prx_init', null, ['number']),
  reset: M.cwrap('prx_reset', null, []),
  setAmount: M.cwrap('prx_set_amount', null, ['number']),
  setSim: M.cwrap('prx_set_sim', null, ['number', 'number']),
  bufL: M.cwrap('prx_buf_l', 'number', []),
  bufR: M.cwrap('prx_buf_r', 'number', []),
  process: M.cwrap('prx_process', null, ['number', 'number']),
  balance: M.cwrap('prx_balance_db', 'number', []),
  cut: M.cwrap('prx_cut_db', 'number', []),
  ceiling: M.cwrap('prx_ceiling_db', 'number', []),
  level: M.cwrap('prx_level_db', 'number', []),
  holding: M.cwrap('prx_holding', 'number', []),
  shelfDb: M.cwrap('prx_shelf_db', 'number', ['number']),
  simDb: M.cwrap('prx_sim_db', 'number', ['number']),
  maxCut: M.cwrap('prx_tuning_max_cut_db', 'number', [])(),
};

let failures = 0;
function check(name, cond, detail) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!cond) failures++;
}

function sine(hz, dbfs, seconds, phase = 0) {
  const n = Math.round(seconds * SR);
  const amp = Math.pow(10, dbfs / 20) * Math.SQRT2;
  const v = new Float32Array(n);
  for (let i = 0; i < n; i++) v[i] = amp * Math.sin((2 * Math.PI * hz * i) / SR + phase);
  return v;
}
function add(a, b) { for (let i = 0; i < a.length; i++) a[i] += b[i]; return a; }

/** Push a mono buffer through the module in worklet-sized chunks; returns the output. */
function run(x, channels = 1, right = null) {
  const pL = C.bufL() >> 2, pR = C.bufR() >> 2;
  const out = new Float32Array(x.length);
  const outR = right ? new Float32Array(x.length) : null;
  for (let at = 0; at < x.length; at += BLOCK) {
    const n = Math.min(BLOCK, x.length - at);
    const H = M.HEAPF32;
    for (let i = 0; i < n; i++) { H[pL + i] = x[at + i]; if (right) H[pR + i] = right[at + i]; }
    C.process(n, channels);
    const H2 = M.HEAPF32;   // the heap may have grown
    for (let i = 0; i < n; i++) { out[at + i] = H2[pL + i]; if (outR) outR[at + i] = H2[pR + i]; }
  }
  return outR ? [out, outR] : out;
}
function rmsDb(v, from = 0) {
  let e = 0; let n = 0;
  for (let i = from; i < v.length; i++) { e += v[i] * v[i]; n++; }
  return 10 * Math.log10(e / n + 1e-30);
}

// ---------------------------------------------------------------------------------------
console.log('\nAmount 0 is bit-exact transparency');
{
  C.init(SR); C.setAmount(0); C.setSim(0, 1);
  const x = add(sine(60, -6, 1.0), sine(3000, -20, 1.0));
  const y = run(Float32Array.from(x));
  let same = true;
  for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) { same = false; break; }
  check('output equals input sample for sample', same);
  check('no cut', C.cut() === 0, `cut ${C.cut()}`);
}

console.log('\nThe ceiling follows the knob');
{
  C.init(SR);
  C.setAmount(0); const c0 = C.ceiling();
  C.setAmount(0.5); const c5 = C.ceiling();
  C.setAmount(1); const c1 = C.ceiling();
  check('amount 0 -> +12 dB', Math.abs(c0 - 12) < 1e-9, c0.toFixed(2));
  check('amount 0.5 -> +2 dB', Math.abs(c5 - 2) < 1e-9, c5.toFixed(2));
  check('amount 1 -> -8 dB', Math.abs(c1 + 8) < 1e-9, c1.toFixed(2));
}

console.log('\nA bass-heavy signal is cut to the maximum, a bright one is not touched');
{
  C.init(SR); C.setAmount(1); C.setSim(0, 1);
  run(add(sine(80, -12, 3.0), sine(2000, -40, 3.0)));
  check('cut reaches the maximum', Math.abs(C.cut() - C.maxCut) < 0.05, `cut ${C.cut().toFixed(2)} of ${C.maxCut}`);
  check('shelf at 40 Hz is the full cut', Math.abs(C.shelfDb(40) + C.maxCut) < 1.0, `${C.shelfDb(40).toFixed(2)} dB`);
  check('shelf at 8 kHz is unity', Math.abs(C.shelfDb(8000)) < 0.2, `${C.shelfDb(8000).toFixed(3)} dB`);
  C.reset(); C.setAmount(1);
  run(sine(2000, -20, 3.0));
  check('bright signal: no cut', C.cut() < 0.01, `cut ${C.cut().toFixed(3)}`);
}

console.log('\nThe simulator adds low end, and the compensator takes it back off');
{
  C.init(SR); C.setAmount(0); C.setSim(1, 0.04);
  const x = add(sine(100, -20, 2.0), sine(1000, -20, 2.0));
  const y = run(Float32Array.from(x));
  // The 100 Hz tone should come out roughly 14 dB hotter than it went in at 4 cm, the
  // 1 kHz tone about 1.6 dB (the simulator's shelf, from the same formula).
  const rise100 = C.simDb(100), rise1k = C.simDb(1000);
  check('simulator rise at 100 Hz is about +14 dB', rise100 > 12 && rise100 < 16, `${rise100.toFixed(2)} dB`);
  check('simulator rise at 1 kHz is small', rise1k > 0.5 && rise1k < 3, `${rise1k.toFixed(2)} dB`);
  const gainDb = rmsDb(y, SR) - rmsDb(x, SR);
  check('the two-tone came out louder overall', gainDb > 6, `${gainDb.toFixed(2)} dB`);

  C.reset(); C.setAmount(0.8); C.setSim(1, 0.04);
  run(Float32Array.from(x));
  const cutNear = C.cut();
  check('at 80 % the compensator answers with a real cut', cutNear > 6, `cut ${cutNear.toFixed(2)} dB, balance ${C.balance().toFixed(2)} dB`);
  C.reset(); C.setAmount(0.8); C.setSim(1, 1.0);
  run(Float32Array.from(x));
  // The bare two-tone balances near 0 dB, which is over a -4 dB ceiling on its own; the
  // point is that the simulator at 1 m adds nothing to that.
  check('at 1 m the simulator is flat and the cut is well under the 4 cm one',
    C.simDb(100) === 0 && C.cut() < cutNear - 6, `cut ${C.cut().toFixed(2)} dB vs ${cutNear.toFixed(2)} dB at 4 cm`);
}

console.log('\nStereo is linked');
{
  C.init(SR); C.setAmount(1); C.setSim(0, 1);
  const l = add(sine(100, -12, 2.0), sine(1000, -24, 2.0));
  const r = Float32Array.from(l, (v) => v * 0.25);
  const [ol, or_] = run(Float32Array.from(l), 2, r);
  let worst = 0;
  for (let i = 0; i < ol.length; i++) worst = Math.max(worst, Math.abs(or_[i] - 0.25 * ol[i]));
  check('right is the same filter over a quarter of left', worst < 1e-6, `worst ${worst.toExponential(2)}, cut ${C.cut().toFixed(2)}`);
}

// ---------------------------------------------------------------------------------------
console.log('\nPinned against the native build (tools/proxstat over the same file)');
{
  // A deterministic stimulus with content on both sides of the split and a step in the
  // low band, written as a WAV for the native tool. 32-bit float, mono, 2 s.
  const n = SR * 2;
  const x = new Float32Array(n);
  let seed = 0x12345678;
  for (let i = 0; i < n; i++) {
    seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
    const noise = (seed / 4294967296) * 2 - 1;
    const low = Math.sin((2 * Math.PI * 100 * i) / SR) * (i > n / 2 ? 0.4 : 0.1);
    x[i] = 0.05 * noise + low + 0.1 * Math.sin((2 * Math.PI * 1500 * i) / SR);
  }
  const dir = mkdtempSync(join(tmpdir(), 'proximate-harness-'));
  const wavIn = join(dir, 'in.wav'), wavOut = join(dir, 'native.wav');
  writeFileSync(wavIn, wav32(x));

  let native = null;
  try {
    const exe = join(dir, 'proxstat');
    execFileSync('clang++', ['-std=c++20', '-O2', '-o', exe,
      join(ROOT, 'tools/proxstat.cpp'), join(ROOT, 'Source/DSP/Compensator.cpp')], { stdio: 'pipe' });
    execFileSync(exe, [wavIn, '0.75', wavOut], { stdio: 'pipe' });
    native = readWav32(readFileSync(wavOut));
  } catch (err) {
    console.log(`SKIP  native reference (${err.message.split('\n')[0]})`);
  }

  if (native) {
    C.init(SR); C.setAmount(0.75); C.setSim(0, 1);
    const y = run(Float32Array.from(x));
    let worst = 0, at = -1;
    for (let i = 0; i < n; i++) {
      const d = Math.abs(y[i] - native[i]);
      if (d > worst) { worst = d; at = i; }
    }
    // Both are float32 through identical arithmetic; the only room for difference is
    // libm (tan, pow, log10) and it shows up at the 1e-7 level.
    check('wasm output matches the native build sample for sample', worst < 1e-5,
      `worst |diff| ${worst.toExponential(2)} at sample ${at}`);
    check('and the cut ended up somewhere real', C.cut() > 1, `cut ${C.cut().toFixed(2)} dB`);
  }
}

console.log(`\n${failures === 0 ? 'ALL PASS' : `${failures} FAILURE(S)`}`);
process.exit(failures === 0 ? 0 : 1);

// --- WAV helpers ---------------------------------------------------------------------
function wav32(samples) {
  const bytes = samples.length * 4;
  const buf = Buffer.alloc(44 + bytes);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + bytes, 4); buf.write('WAVE', 8);
  buf.write('fmt ', 12); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(3, 20); buf.writeUInt16LE(1, 22);
  buf.writeUInt32LE(SR, 24); buf.writeUInt32LE(SR * 4, 28); buf.writeUInt16LE(4, 32); buf.writeUInt16LE(32, 34);
  buf.write('data', 36); buf.writeUInt32LE(bytes, 40);
  for (let i = 0; i < samples.length; i++) buf.writeFloatLE(samples[i], 44 + i * 4);
  return buf;
}
function readWav32(buf) {
  let pos = 12;
  while (pos + 8 <= buf.length) {
    const id = buf.toString('ascii', pos, pos + 4);
    const size = buf.readUInt32LE(pos + 4);
    if (id === 'data') {
      const out = new Float32Array(size / 4);
      for (let i = 0; i < out.length; i++) out[i] = buf.readFloatLE(pos + 8 + i * 4);
      return out;
    }
    pos += 8 + size + (size & 1);
  }
  throw new Error('no data chunk');
}
