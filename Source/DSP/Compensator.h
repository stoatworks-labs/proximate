// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cmath>

/**
    Proximate's DSP: a single-band dynamic low shelf steered by the spectral balance of
    the input.

    Standard-library only, on purpose. Everything in this namespace builds and runs in a
    plain test executable with no JUCE, no audio device and no host, which is how the
    behaviour is pinned down. If something here starts needing juce_core it belongs in a
    different directory.

    ## How it works

    The proximity effect of a directional microphone is a first-order rise in the low
    frequencies that grows as the source gets closer. Proximate does not know where the
    talker is; what it can measure is the *balance* between the energy below a split
    frequency and the energy above it. When the talker leans in, that balance tips
    towards the low band by several dB, on top of whatever the voice does on its own. The
    compensator holds the balance at or under a ceiling: whatever the low band is over the
    ceiling by is taken off again with a first-order low shelf, the same shape as the
    effect it is undoing.

    One knob. It sets the ceiling — from "never" at zero to a deliberately thin balance at
    full — and that is the whole control surface. The time constants, the split, the knee
    and the depth limit are tuning, fixed in `Tuning` and pinned by the tests.

    ## What it does not do

    It never boosts anything: the shelf is cut-only, the high band passes at unity, and
    the output can only ever be the input with less low end. It adds no latency: there is
    no look-ahead and no buffering beyond the caller's block. And it does not react to
    level — the detector is a ratio, so a talker who simply gets louder is left alone.
*/
namespace proximate::dsp
{

/** The fixed numbers the product is built from. Not parameters: the tests measure the
    behaviour these give and the guide describes it. Change one here, and both. */
struct Tuning
{
    /** Where the detector splits low from high. */
    double splitHz = 250.0;

    /** The shelf's zero: below this the cut is the full amount, however deep. A
        handheld dynamic's proximity rise peaks around here and its own low-frequency
        roll-off takes over below, so this is where the correction flattens too. The
        pole moves up from it as the cut deepens — a deeper cut is a closer talker, and
        a closer talker's boost reaches further up the spectrum. At 12 dB the pole is
        at 480 Hz and the shelf reads -10 dB at 100 Hz, -6 at 250, -2.6 at 500 and
        -0.8 at 1 kHz, which is the inverse of a handheld dynamic's published
        proximity curve at a few centimetres to within a dB. */
    double shelfFloorHz = 120.0;

    /** The detector ignores anything under this; a plugin that reacts to DC offset or
        a subsonic thump is reacting to something it cannot fix. */
    double dcBlockHz = 15.0;

    /** RMS integration time of the two band detectors. Long enough that the balance
        follows where the talker is standing rather than which vowel they are on:
        speech at a fixed distance spreads about 10 dB between its 10th and 90th
        percentile with a 20 ms window, and about 3 dB with this one, while a
        proximity change of several dB survives intact. */
    double integrationMs = 200.0;

    /** How fast the cut deepens once the balance is over the ceiling. */
    double attackMs = 80.0;

    /** How fast it comes back off. */
    double releaseMs = 300.0;

    /** Width of the soft knee around the ceiling. */
    double kneeDb = 6.0;

    /** dB of shelf per dB of balance over the ceiling. Not 1: the balance the detector
        measures moves by only a quarter to a third of the shelf's nominal depth,
        because a voice's low-band energy sits near the split, where the shelf is
        shallow, and the high band loses a little too. Two matches the physics — a
        talker at a few centimetres tips the balance at the split by 4 to 7 dB and
        needs about 12 dB of shelf to undo — without turning the 3 dB of ordinary
        phonetic wobble in the balance into a pumping cut. */
    double ratio = 2.0;

    /** The shelf never cuts deeper than this, whatever the detector says. */
    double maxCutDb = 18.0;

    /** The ceiling at amount 0: high enough that speech never reaches it. */
    double ceilingOffDb = 12.0;

    /** The ceiling at amount 1: a deliberately thin balance. Linear in between. */
    double ceilingFullDb = -8.0;

    /** When the high band is under this (dBFS, RMS) nobody is talking, and the balance
        is the balance of the noise floor. The cut holds where it is rather than chasing
        that: a pause is not a change of distance. */
    double holdBelowDb = -50.0;

    /** The level that decides that is measured fast, so a pause is noticed in tens of
        milliseconds — not after the slow balance detectors have spent a second and a
        half decaying through the threshold, drifting the cut on the way down. */
    double gateMs = 10.0;
};

/** A one-pole low-pass in the topology-preserving (Zavalishin) form. Its state is the
    integrator's, not a function of the coefficient, so the cutoff can move every sample
    without a transient — the shelf relies on that. */
class OnePole
{
public:
    void reset() noexcept { s = 0.0; }

    /** Set the cutoff. @p sampleRate must be positive. */
    void setCutoff (double hz, double sampleRate) noexcept;

    /** Set the coefficient a cutoff was already turned into — the shelf computes one
        and hands it to every channel rather than taking the tangent twice. */
    void setCoefficientDirect (double coefficient) noexcept { a = coefficient; }

    /** Returns the low-pass output; the high-pass is `input - lowpass`, exactly. */
    double lowpass (double x) noexcept
    {
        const double v = (x - s) * a;
        const double lp = v + s;
        s = lp + v;
        return lp;
    }

private:
    double a = 0.0;
    double s = 0.0;
};

/** A second-order section, transposed direct form II. Coefficients here never move, so
    the topology's transient-on-retune does not matter. */
class Biquad
{
public:
    void reset() noexcept { z1 = z2 = 0.0; }

    /** Butterworth (Q = 1/sqrt 2) low-pass. */
    void setButterworthLowpass (double hz, double sampleRate) noexcept;

    /** Butterworth high-pass at the same frequency. The two are power-complementary:
        |L|^2 + |H|^2 = 1 at every frequency, so the split loses nothing. */
    void setButterworthHighpass (double hz, double sampleRate) noexcept;

    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    /** Magnitude at @p hz, in dB, for the tests. */
    double magnitudeDb (double hz, double sampleRate) const noexcept;

private:
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
};

/** Measures the low/high balance of a signal. Also used by the tests to look at the
    output, which is how "the balance is held at the ceiling" is checked rather than
    assumed. */
class Detector
{
public:
    explicit Detector (const Tuning& t = {}) : tuning (t) {}

    void prepare (double sampleRate);
    void reset();

    /** Push one sample. Afterwards `balanceDb()` and `levelDb()` describe the signal
        up to and including it. */
    void push (double x) noexcept;

    /** Low-band energy over high-band energy, in dB, over the integration time. */
    double balanceDb() const noexcept { return balance; }

    /** High-band RMS level, in dBFS, over the fast gate time. */
    double levelDb() const noexcept { return level; }

private:
    Tuning tuning;
    OnePole dcBlock;
    Biquad low, high;
    double smoothing = 0.0, gateSmoothing = 0.0;
    double lowEnergy = 0.0, highEnergy = 0.0, gateEnergy = 0.0;
    double balance = 0.0, level = -100.0;
};

/** The compensator: detector, gain computer, ballistics and the shelf. */
class Compensator
{
public:
    static constexpr int maxChannels = 2;

    explicit Compensator (const Tuning& t = {});

    void prepare (double sampleRate);
    void reset();

    /** The knob, 0..1. Safe to call from any thread between blocks; read once per block. */
    void setAmount (double amount01) noexcept { amountValue = clamp01 (amount01); }
    double amount() const noexcept { return amountValue; }

    /** Where the ceiling sits for a knob position. */
    static double ceilingDb (double amount01, const Tuning& t) noexcept;

    /** How deep a cut a balance of @p balanceDb asks for at @p amount01, before
        ballistics: zero under the knee, `ratio` times the excess above it, capped. */
    static double staticCutDb (double balanceDb, double amount01, const Tuning& t) noexcept;

    /** Process @p numSamples of up to `maxChannels` channels in place. Channels are
        linked: one detector on their mean, one cut applied to all of them. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    // --- Readouts for a display, describing the last sample processed --------------

    double balanceDb() const noexcept { return detector.balanceDb(); }
    double levelDb() const noexcept { return detector.levelDb(); }
    double cutDb() const noexcept { return cut; }
    double ceilingDb() const noexcept { return ceilingDb (amountValue, tuning); }
    bool holding() const noexcept { return held; }

    const Tuning& getTuning() const noexcept { return tuning; }

private:
    static double clamp01 (double v) noexcept { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
    void setShelf (double cutDb) noexcept;

    Tuning tuning;
    double sampleRate = 48000.0;
    double amountValue = 0.5;

    Detector detector;
    double attackCoeff = 0.0, releaseCoeff = 0.0;
    double target = 0.0;   // the gain computer's answer, held through pauses
    double cut = 0.0;      // after ballistics; what the shelf applies
    bool held = false;

    std::array<OnePole, maxChannels> shelf;
    double shelfGain = 1.0;
};

} // namespace proximate::dsp
