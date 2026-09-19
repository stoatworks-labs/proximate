# Tests

One suite, `test_compensator.cpp`, and it links nothing but the standard library, so it
builds and runs without CMake, JUCE, an audio device, a plugin host or a microphone:

```bash
clang++ -std=c++20 -O2 -o /tmp/t tests/test_compensator.cpp Source/DSP/Compensator.cpp && /tmp/t
```

Or through CMake:

```bash
ctest --test-dir build --output-on-failure
```

| Section | What it is really checking |
|---|---|
| The split | That the Butterworth pair is power-complementary, so the balance loses nothing |
| The knob | That amount maps to the ceiling linearly and monotonically, clamped |
| The gain computer | Nothing under the knee, `ratio` times the excess over it, capped, continuous |
| The detector | Its reading on a two-tone against the arithmetic of the filters' own magnitudes, leakage included; that it is level-independent |
| Amount 0 | Bit-for-bit transparency on a bass-heavy input |
| The shelf | At its maximum depth: never boosts at any of eight frequencies, full cut under the floor, unity above 5 kHz, and the impulse comes out on its own sample |
| A step | The cut follows a step in the low band in and out within a second, settles on the static answer, does not overshoot |
| Level | The same signal 20 dB quieter gets the same cut |
| Silence | The cut freezes through digital silence and through quiet rumble, within 150 ms of the pause, and a bright signal releases it |
| Stereo | One detector on the mean, and both channels get the identical filter |
| Block size, sample rate | The output does not depend on either |
| NaN | A non-finite sample from the host is dropped, not remembered |

## It prints numbers

Every section reports the figures behind its assertions — the shelf at each probe
frequency, the cut at nine points along the step, the detector's ripple — because the
interesting question when changing DSP is "by how much did that move", not "did it pass".
Read the output.

## Assertions are set to measured reality

Where a limit is not a physical law, the threshold is set just outside what the code
currently achieves, with a comment saying so. If a change makes something substantially
better, tighten the assertion; if it makes something worse, the suite says so before a
release does.

## Beyond the suite

`tools/proxstat` runs a WAV through the compensator and prints the input and output
balance, the cut's distribution, and (with `trace`) a line every 50 ms. `tools/proxhost`
does the same through the built VST3 or AU via JUCE's hosting code, and the check is that
its output is identical, sample for sample, to `proxstat`'s. `pluginval` and `auval` cover
the host contract.
