# Proximate

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. The compensator is pinned by a
> dependency-free test suite that measures rather than asserts — the shelf's shape at every
> frequency, the detector's reading against arithmetic, the attack and the release against
> the clock — and the built VST3 and AU, loaded the way a host loads them, produce the same
> samples as the bare DSP. `pluginval` at strictness 10 and `auval` both pass. It has
> **not yet been used on a real microphone in a real room.** Review before use on live gear.

One knob against the proximity effect. VST3, AU and standalone, built with JUCE.

**Video:** [What it does, in 44 seconds](https://www.youtube.com/watch?v=VNhoTlwiVx4) —
the real editor over a presenter who leans in and steps back, rendered from the plugin's
own harness with the plugin's output as the soundtrack.

Put it on a speech or vocal channel. When the talker leans into the microphone and the low
end swells, Proximate takes the swell off again: a first-order low shelf that deepens as
they get closer and comes back off as they step away, so the voice keeps the same tonal
balance wherever the mic is. It never boosts, it adds no latency, and it does not react to
level — only to balance.

![The Proximate window: one large Amount knob reading 65 %, a balance meter with the
ceiling marked and the balance sitting 6 dB over it, a cut meter reading −10.5 dB, and
the status line Listening.](docs/screenshots/plugin.png)

*Synthesised speech run through a model of a cardioid microphone at 4 cm, at 65 %: the
balance reads +5.2 dB against a ceiling of −1 dB and the shelf is taking 10.5 dB off. The plugin is running
inside a host, on real audio; only the window it is drawn in is a test harness.*

**[Try it in your browser](https://proximate-demo.stoatworks-labs.com)** — the plugin's own
DSP compiled to WebAssembly, with a Distance slider that stands in for a microphone's
proximity effect, two synthesised voices, your microphone or a file. Audio never leaves the
page. See [`web/`](web/README.md) for how the demo relates to the real thing.

![The browser demo: a Distance slider at 6 cm over a spectrum plot of the voice going into
Proximate and coming out of it with the low end taken down, and beside it the plugin's own
knob, balance meter and cut meter reading −8.2 dB.](docs/screenshots/demo.png)

## The idea

A directional microphone picks up more bass the closer the source is — a handheld dynamic
at a few centimetres reads something like +10 dB at 100 Hz against the same voice at arm's
length. A static EQ is right at one distance and wrong at every other, and the usual
answer, a dynamic EQ with a low shelf keyed off the low band, has a threshold, a ratio, an
attack and a release to set, and reacts to a talker who simply gets louder.

Proximate reacts to **balance**: the energy under 250 Hz against the energy over it. Speech
at a steady distance keeps that within about 3 dB of itself from phrase to phrase, measured
over 200 ms; a talker who leans in tips it towards the low end by 4 to 7 dB on top. The
plugin holds the balance at or under a **ceiling** and takes whatever is over it off with a
shelf shaped like the effect it is undoing — flattening at 120 Hz, its corner climbing as
the cut deepens, the way the proximity rise climbs as the talker gets closer. The one knob
sets the ceiling. Everything else is tuning, fixed, and written down in the
[user guide](docs/USER-GUIDE.md).

- **Cut only.** The output is the input with less low end, or the input untouched. At 0 %
  it is transparent to the bit.
- **Zero latency.** No look-ahead, no buffering beyond the host's block. Safe to monitor
  through.
- **Level-blind.** The same signal 20 dB quieter gets the same cut.
- **Holds through pauses.** Under −50 dBFS in the high band nobody is talking, and the cut
  freezes where it is rather than chasing the noise floor's balance.
- **Stereo-linked.** One detector on the mean, one shelf on both channels.

## Status

Beta, at its first release. What has been checked:

- `tests/test_compensator.cpp` — 85 checks over the standard-library-only DSP: the split
  filters are power-complementary; the knob-to-ceiling mapping; the gain computer's knee,
  ratio and cap; the detector's reading against arithmetic; the shelf at its maximum depth
  never boosts at any frequency and is unity above 5 kHz; an impulse comes out on its own
  sample; a step in the low band is followed in and out within a second; level independence;
  the hold through digital silence and through quiet rumble; stereo linking to the last
  bit; block-size and sample-rate independence; a NaN from the host is dropped rather than
  remembered.
- `tools/proxstat` — the compensator over synthesised speech (four `say` voices) run
  through a model of an ideal cardioid at 1 m and at 4 cm, which adds 15 dB at 100 Hz. At
  65 % the 4 cm files get 8 to 16 dB of shelf and about 40 % of the modelled boost comes
  off at every third-octave band from 63 to 630 Hz; at 80 %, about two thirds. The 1 m
  files are barely touched at 65 %.
- `tools/proxhost` — the built VST3 and AU, loaded through JUCE's hosting code with the
  parameter set as a host sets it, produce output identical sample for sample to
  `proxstat` on the same file, mono and stereo.
- `pluginval --strictness-level 10` passes on the VST3 clean and on the AU with the usual
  "current program is -1" note; `auval` passes; both report a latency of 0.

What has not: a real microphone, a real talker, a real room, most hosts. The standalone
opens no audio input on its first run (see [the guide](docs/USER-GUIDE.md#the-standalone-app)
for why) and has been launched, not used on a mic.

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/proximate/releases/tag/v0.1.0)** — prebuilt for macOS, Windows and Linux. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`proximate-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/proximate/releases/download/v0.1.0/proximate-0.1.0-macos-universal.dmg) | 9.7 MB |
| Universal (Apple Silicon + Intel) · .pkg installer | [`proximate-0.1.0-macos-universal.pkg`](https://github.com/stoatworks-labs/proximate/releases/download/v0.1.0/proximate-0.1.0-macos-universal.pkg) | 9.7 MB |
| Universal (Apple Silicon + Intel) · .zip archive | [`proximate-macos-universal.zip`](https://github.com/stoatworks-labs/proximate/releases/latest/download/proximate-macos-universal.zip) | 9.7 MB |

</details>

<details>
<summary><b>Windows</b> — x64, ARM64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`proximate-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/proximate/releases/download/v0.1.0/proximate-0.1.0-windows-x86_64-setup.exe) | 2.8 MB |
| ARM64 · .exe installer | [`proximate-0.1.0-windows-aarch64-setup.exe`](https://github.com/stoatworks-labs/proximate/releases/download/v0.1.0/proximate-0.1.0-windows-aarch64-setup.exe) | 2.5 MB |
| x64 · .zip archive | [`proximate-windows-x86_64.zip`](https://github.com/stoatworks-labs/proximate/releases/latest/download/proximate-windows-x86_64.zip) | 5.0 MB |
| ARM64 · .zip archive | [`proximate-windows-aarch64.zip`](https://github.com/stoatworks-labs/proximate/releases/latest/download/proximate-windows-aarch64.zip) | 4.9 MB |

</details>

<details>
<summary><b>Linux</b> — x64, ARM64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .deb package (Debian/Ubuntu) | [`proximate_0.1.0_amd64.deb`](https://github.com/stoatworks-labs/proximate/releases/download/v0.1.0/proximate_0.1.0_amd64.deb) | 2.1 MB |
| ARM64 · .deb package (Debian/Ubuntu) | [`proximate_0.1.0_arm64.deb`](https://github.com/stoatworks-labs/proximate/releases/download/v0.1.0/proximate_0.1.0_arm64.deb) | 2.1 MB |
| x64 · .rpm package (Fedora/RHEL) | [`proximate-0.1.0-1.x86_64.rpm`](https://github.com/stoatworks-labs/proximate/releases/download/v0.1.0/proximate-0.1.0-1.x86_64.rpm) | 2.1 MB |
| ARM64 · .rpm package (Fedora/RHEL) | [`proximate-0.1.0-1.aarch64.rpm`](https://github.com/stoatworks-labs/proximate/releases/download/v0.1.0/proximate-0.1.0-1.aarch64.rpm) | 2.2 MB |
| x64 · .zip archive | [`proximate-linux-x86_64.zip`](https://github.com/stoatworks-labs/proximate/releases/latest/download/proximate-linux-x86_64.zip) | 4.1 MB |
| ARM64 · .zip archive | [`proximate-linux-aarch64.zip`](https://github.com/stoatworks-labs/proximate/releases/latest/download/proximate-linux-aarch64.zip) | 4.2 MB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/proximate/releases](https://github.com/stoatworks-labs/proximate/releases).

macOS builds are signed and notarised and open normally. The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

## Building

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

That fetches JUCE 8.0.6 and produces `build/Proximate_artefacts/Release/{VST3,AU,Standalone}/`,
universal on macOS (check with `lipo -archs`, not the build log). Then the tests:

```bash
ctest --test-dir build --output-on-failure
```

The DSP suite also builds on its own, with no CMake and no JUCE, which is the quick way to
iterate on the maths:

```bash
clang++ -std=c++20 -O2 -o /tmp/t tests/test_compensator.cpp Source/DSP/Compensator.cpp && /tmp/t
```

`build/proxstat in.wav 0.65 out.wav` runs a file through the compensator and prints what
it did; `build/proxhost_artefacts/Release/proxhost <bundle> in.wav 0.65 out.wav --show` does
the same through the built plugin, with its editor on screen.

## Known limits

- **It cannot tell a low note from a close mic.** The detector sees balance, and a sung low
  note is more low end. The 200 ms integration and the soft knee keep this small on speech;
  on a bass singer, set the knob so that only real leaning-in crosses the ceiling.
- **It cuts plosives and handling noise too.** They are low end. A high-pass filter before
  it is the right tool for those.
- **The shelf's shape is one microphone's.** The 120 Hz floor and the rising corner are the
  inverse of a handheld dynamic's published proximity curve; a condenser that extends lower
  is under-corrected below 100 Hz. The knob covers the difference in depth, not in shape.
- **The tuning is fixed.** That is the product, not an omission: a version with a
  threshold, a ratio, an attack and a release is a dynamic EQ, and there are plenty of those.

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

**Licensing:** the source here is MIT, but the released binaries link JUCE 8 and are conveyed under the **AGPLv3** — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md) before redistributing them.
<!-- attributions:end -->

## Licence

MIT. See [LICENSE](LICENSE).
