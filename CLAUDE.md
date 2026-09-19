# CLAUDE.md — Proximate command reference

Read [AGENTS.md](AGENTS.md) first for what this project is and where its traps are. This
file is just the commands.

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Reuse a local JUCE checkout instead of cloning:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/Projects/audio/contourtonist/build-release/_deps/juce-src
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

The DSP suite also builds standalone with no CMake and no JUCE, which is much faster when
iterating on the maths:

```bash
clang++ -std=c++20 -O2 -o /tmp/t tests/test_compensator.cpp Source/DSP/Compensator.cpp && /tmp/t
```

It prints its measured numbers (the shelf at every probe frequency, the cut against the
clock, the detector against arithmetic), not just pass/fail. Read the output when changing DSP.

## Measure it on speech

```bash
say -v Daniel -o speech.wav --data-format=LEI16@48000 "Good evening everyone, and welcome."
build/proxstat speech.wav 0.65 out.wav          # statistics of balance in/out and cut
build/proxstat speech.wav 0.65 - trace          # a line every 50 ms
```

## Prove the built plugin is the compensator

```bash
build/proxhost_artefacts/Release/proxhost build/Proximate_artefacts/Release/VST3/Proximate.vst3 speech.wav 0.65 host.wav
build/proxstat speech.wav 0.65 stat.wav
# host.wav and stat.wav hold identical samples (the WAV headers differ; compare the data)
```

Add `--show` to `proxhost` to open the editor and stream the file through the plugin in
real time — the way to watch (or screenshot) the editor on real audio without an audio
device.

## Validate the plugin

```bash
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate build/Proximate_artefacts/Release/VST3/Proximate.vst3
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate build/Proximate_artefacts/Release/AU/Proximate.component
auval -v aufx Prx1 Alsg
```

## Verify the build is actually universal

```bash
lipo -archs build/Proximate_artefacts/Release/VST3/Proximate.vst3/Contents/MacOS/Proximate
```

## Run the standalone

```bash
open build/Proximate_artefacts/Release/Standalone/Proximate.app
```

It opens no audio input on its first run (AGENTS.md §4.4); pick the microphone under
Options > Audio Settings and untick *Mute audio input*. Its settings live in
`~/Library/Application Support/Proximate.settings`.

## Rules

- Never add a gain path above unity, never add look-ahead, never expose the tuning as
  parameters. All three are the product.
- `Source/StoatworksAbout*.h` and `scripts/release-lib.sh` are vendored/generated from
  stoatworks-backend; edit the masters and re-sync.
- `docs/USER-GUIDE.md` is the only copy of the guide anyone edits; the PDF and site page
  are built from it.
- "Commit" = commit **and** push.
