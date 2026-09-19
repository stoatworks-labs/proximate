// AudioWorklet host for the Proximate wasm core. All DSP happens inside the wasm module
// (the plugin's own Source/DSP code, plus the demo's proximity simulator); this file
// only shuttles samples and messages.
//
// Two outputs: 0 is what you hear (the compensated signal, or the post-simulator dry
// signal when the A/B is on "before"); 1 is always the post-simulator dry signal, for
// the input side of the spectrum display. The compensator runs whatever the A/B says,
// so its detector and shelf stay warm and switching is click-free — the same reason
// the plugin's own transparency path keeps its filter state moving.

import createProximateModule from './proximate.js';

const STATUS_INTERVAL_BLOCKS = 4;   // ~11 ms at 48 kHz

class ProximateProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ready = false;
    this.wet = true;
    this.safetyClip = true;
    this.pending = [];

    createProximateModule().then((M) => {
      this.M = M;
      this.C = {
        init: M.cwrap('prx_init', null, ['number']),
        reset: M.cwrap('prx_reset', null, []),
        setAmount: M.cwrap('prx_set_amount', null, ['number']),
        setSim: M.cwrap('prx_set_sim', null, ['number', 'number']),
        bufL: M.cwrap('prx_buf_l', 'number', []),
        bufR: M.cwrap('prx_buf_r', 'number', []),
        dryL: M.cwrap('prx_dry_l', 'number', []),
        dryR: M.cwrap('prx_dry_r', 'number', []),
        process: M.cwrap('prx_process', null, ['number', 'number']),
        balance: M.cwrap('prx_balance_db', 'number', []),
        cut: M.cwrap('prx_cut_db', 'number', []),
        ceiling: M.cwrap('prx_ceiling_db', 'number', []),
        level: M.cwrap('prx_level_db', 'number', []),
        holding: M.cwrap('prx_holding', 'number', []),
      };
      this.C.init(sampleRate);
      this.pL = this.C.bufL() >> 2;
      this.pR = this.C.bufR() >> 2;
      this.dL = this.C.dryL() >> 2;
      this.dR = this.C.dryR() >> 2;
      this.blockCount = 0;
      this.inPeak = 0;
      this.outPeak = 0;
      this.ready = true;
      for (const m of this.pending) this.handle(m);
      this.pending = [];
      this.port.postMessage({ type: 'ready' });
    });

    this.port.onmessage = (e) => {
      if (this.ready) this.handle(e.data); else this.pending.push(e.data);
    };
  }

  handle(msg) {
    switch (msg.type) {
      case 'amount': this.C.setAmount(msg.value); break;
      case 'sim': this.C.setSim(msg.on ? 1 : 0, msg.metres); break;
      case 'wet': this.wet = !!msg.on; break;
      case 'safetyClip': this.safetyClip = !!msg.on; break;
      case 'reset': this.C.reset(); break;
      default: break;
    }
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const tap = outputs[1];
    if (!this.ready || out.length === 0) return true;

    const input = inputs[0];
    const n = out[0].length;             // the render quantum, 128
    const H = this.M.HEAPF32;
    const inL = input.length > 0 ? input[0] : null;
    const inR = input.length > 1 ? input[1] : null;
    const channels = inR ? 2 : 1;

    for (let i = 0; i < n; i++) {
      H[this.pL + i] = inL ? inL[i] : 0;
      if (inR) H[this.pR + i] = inR[i];
    }

    this.C.process(n, channels);         // always: the detector and the shelf stay warm

    const ceil = 0.988553;               // -0.1 dBFS, web-only safety stage
    const oL = out[0];
    const oR = out.length > 1 ? out[1] : null;
    const tL = tap && tap.length > 0 ? tap[0] : null;
    const tR = tap && tap.length > 1 ? tap[1] : null;
    let ip = this.inPeak, op = this.outPeak;

    for (let i = 0; i < n; i++) {
      const dl = H[this.dL + i];
      const dr = inR ? H[this.dR + i] : dl;
      let l = this.wet ? H[this.pL + i] : dl;
      let r = this.wet ? (inR ? H[this.pR + i] : l) : dr;
      if (this.safetyClip) {
        l = l > ceil ? ceil : (l < -ceil ? -ceil : l);
        r = r > ceil ? ceil : (r < -ceil ? -ceil : r);
      }
      oL[i] = l;
      if (oR) oR[i] = r;
      if (tL) tL[i] = dl;
      if (tR) tR[i] = dr;
      const ai = Math.max(Math.abs(dl), Math.abs(dr));
      const ao = Math.max(Math.abs(l), Math.abs(r));
      if (ai > ip) ip = ai;
      if (ao > op) op = ao;
    }
    this.inPeak = ip;
    this.outPeak = op;

    if (++this.blockCount >= STATUS_INTERVAL_BLOCKS) {
      this.blockCount = 0;
      this.port.postMessage({
        type: 'status',
        balance: this.C.balance(),
        cut: this.C.cut(),
        ceiling: this.C.ceiling(),
        level: this.C.level(),
        holding: this.C.holding() === 1,
        inPeak: this.inPeak,
        outPeak: this.outPeak,
      });
      this.inPeak = 0;
      this.outPeak = 0;
    }

    return true;
  }
}

registerProcessor('proximate', ProximateProcessor);
