# Proximate — Web Demo

Try Proximate in the browser: **https://proximate-demo.stoatworks-labs.com**

This is the plugin's own DSP — the unmodified [`../Source/DSP/Compensator.cpp`](../Source/DSP)
— compiled to WebAssembly and run inside an AudioWorklet. The knob, the ceiling, the balance
and cut meters and the status line are the plugin's; the readouts come from the same
functions the plugin's editor reads.

What a browser cannot count on is a directional microphone at a few centimetres, so the
page adds a **Distance** slider: a simulator that puts a handheld dynamic's near-field rise
on the source before Proximate hears it — a zero at c / (2r), the ideal-cardioid corner,
over a pole fixed at 90 Hz where a capsule's own roll-off flattens the rise, capped at
+18 dB; flat beyond 30 cm, +14 dB at 100 Hz by 4 cm. It runs before the compensator,
exactly as a microphone would, and the compensator only ever sees the audio.

Sources: two synthesised voices (a pulse train through three vowel formants, with
syllables, words, phrases and pauses — built in the browser, no recordings), the
microphone (headphones!), or a file. Audio never leaves the page.

## How it works

- No JUCE shim is needed: `Source/DSP` depends on nothing but the C++ standard library,
  by design.
- [`wasm/proximate_web.cpp`](wasm/proximate_web.cpp) plays PluginProcessor's role — it owns
  a `dsp::Compensator`, hands it the knob and the audio, and exposes the readouts — and
  carries the simulator, which is the one thing here that is not in the plugin.
- The worklet has two outputs: what you hear (compensated, or the post-simulator signal
  when the A/B says *before*), and the post-simulator signal for the input side of the
  spectrum. The compensator runs either way, so the A/B is click-free.
- The shelf curve on the spectrum is `H(s) = (s + ωz) / (s + ωz / g)`, the expression the
  plugin's shelf implements, evaluated for the cut the readout reports.
- A web-only "safety clip" stage (hard ceiling at −0.1 dBFS, on by default) protects ears
  and speakers: the simulator can add 17 dB of low end, and the plugin itself has no
  such stage.

## Build

Requires Emscripten (`brew install emscripten`) and Node.

```bash
./wasm/build.sh          # → public/proximate.js (single self-contained ES module)
node test/harness.mjs    # verification — must pass before deploying
```

The harness checks the module's behaviour against independently derived expectations
(bit-exact transparency at 0 %, the ceiling mapping, the shelf's shape at its maximum,
the simulator's rise, stereo linking) and then pins the wasm output against a native
`clang++` build of `tools/proxstat.cpp` over the same stimulus: the two agree sample for
sample.

## Deploy

A push to `main` that touches `web/` deploys through `.github/workflows/deploy.yml`.
By hand, from `web/`:

```bash
cf-run npx wrangler deploy
```
