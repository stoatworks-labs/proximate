// SPDX-License-Identifier: MIT
//
// WebAssembly wrapper around Proximate's unmodified DSP.
//
// This file plays the role PluginProcessor.cpp plays in the plugin: it owns a
// dsp::Compensator, hands it the knob, runs it over the audio buffers and reads the
// readouts back out. The compensator is the plugin's own Source/DSP/Compensator.cpp,
// compiled verbatim — it depends on nothing but the C++ standard library, so no JUCE
// shim is needed.
//
// The one thing here that is NOT in the plugin is the proximity simulator. A browser
// demo cannot count on a directional microphone at a few centimetres, so the page has
// a Distance slider, and this stage puts a first-order low-frequency rise on the source
// shaped like a handheld dynamic's near-field response: a zero at c / (2 r) — the
// ideal-cardioid near-field corner — over a pole fixed at 90 Hz, where a real capsule's
// own roll-off flattens the rise, capped at +18 dB. Flat beyond 30 cm; +14 dB at 100 Hz,
// +9 dB at 250 Hz and +4 dB at 500 Hz by 4 cm. It runs BEFORE the compensator, as a microphone
// would, and the compensator never sees the slider — only the audio.

#include "DSP/Compensator.h"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cmath>

using namespace proximate::dsp;

namespace
{
constexpr int kMaxBlock = 2048;
constexpr double kSpeedOfSound = 343.0;
constexpr double kPi = 3.14159265358979323846;

/** The proximity simulator: y = hp + g * lp with a TPT one-pole at the pole, so the
    shelf is (s + wz) / (s + wp) with DC gain g = wz / wp. Same construction as the
    compensator's own shelf, in the other direction. */
struct ProximitySim
{
    static constexpr double poleHz = 90.0;   // wz == wp at c / (4 pi 90 Hz) = 30 cm: flat from there out
    static constexpr double maxGain = 7.943;  // +18 dB

    OnePole lp[Compensator::maxChannels];
    double gain = 1.0;
    bool enabled = false;

    void prepare (double sampleRate)
    {
        for (auto& p : lp)
        {
            p.setCutoff (poleHz, sampleRate);
            p.reset();
        }
    }

    /** Distance from the capsule, in metres. */
    void setDistance (double metres)
    {
        const double r = std::max (metres, 0.005);
        const double wz = kSpeedOfSound / (2.0 * r);
        const double wp = 2.0 * kPi * poleHz;
        // The ideal model keeps rising as r shrinks; a real capsule does not. +18 dB is
        // about where a handheld dynamic's published curves stop.
        gain = std::clamp (wz / wp, 1.0, maxGain);
    }

    double process (int ch, double x) noexcept
    {
        const double l = lp[ch].lowpass (x);
        return (x - l) + gain * l;
    }
};

struct WebProximate
{
    double sampleRate = 48000.0;
    Compensator compensator;
    ProximitySim sim;

    // In place: the page writes the source here, and reads the processed signal back
    // from the same buffers. `dry` keeps the post-simulator signal for the A/B and the
    // input spectrum.
    float bufL[kMaxBlock] = {}, bufR[kMaxBlock] = {};
    float dryL[kMaxBlock] = {}, dryR[kMaxBlock] = {};
};

WebProximate* W = nullptr;
} // namespace

extern "C"
{

EMSCRIPTEN_KEEPALIVE
void prx_init (double sampleRate)
{
    delete W;
    W = new WebProximate();
    W->sampleRate = sampleRate;
    W->compensator.prepare (sampleRate);
    W->sim.prepare (sampleRate);
}

EMSCRIPTEN_KEEPALIVE
void prx_reset()
{
    if (W == nullptr)
        return;
    W->compensator.reset();
    W->sim.prepare (W->sampleRate);
}

/** The knob, 0..1. */
EMSCRIPTEN_KEEPALIVE
void prx_set_amount (double amount01)
{
    if (W != nullptr)
        W->compensator.setAmount (amount01);
}

/** The simulator: on/off and the talker's distance in metres. */
EMSCRIPTEN_KEEPALIVE
void prx_set_sim (int enabled, double metres)
{
    if (W == nullptr)
        return;
    W->sim.enabled = enabled != 0;
    W->sim.setDistance (metres);
}

EMSCRIPTEN_KEEPALIVE float* prx_buf_l() { return W ? W->bufL : nullptr; }
EMSCRIPTEN_KEEPALIVE float* prx_buf_r() { return W ? W->bufR : nullptr; }
EMSCRIPTEN_KEEPALIVE float* prx_dry_l() { return W ? W->dryL : nullptr; }
EMSCRIPTEN_KEEPALIVE float* prx_dry_r() { return W ? W->dryR : nullptr; }
EMSCRIPTEN_KEEPALIVE int prx_max_block() { return kMaxBlock; }

/** Run the simulator (if on) and then the compensator over @p n frames of both
    buffers, in place. `channels` is 1 or 2; with 1 only the left buffer is read. */
EMSCRIPTEN_KEEPALIVE
void prx_process (int n, int channels)
{
    if (W == nullptr)
        return;

    n = std::clamp (n, 0, kMaxBlock);
    channels = std::clamp (channels, 1, Compensator::maxChannels);

    float* bufs[Compensator::maxChannels] = { W->bufL, W->bufR };
    float* drys[Compensator::maxChannels] = { W->dryL, W->dryR };

    for (int ch = 0; ch < channels; ++ch)
    {
        for (int i = 0; i < n; ++i)
        {
            const double x = bufs[ch][i];
            const float y = (float) (W->sim.enabled ? W->sim.process (ch, x) : x);
            bufs[ch][i] = y;
            drys[ch][i] = y;
        }
    }

    W->compensator.process (bufs, channels, n);
}

// --- Readouts, as the plugin's editor reads them --------------------------------------

EMSCRIPTEN_KEEPALIVE double prx_balance_db() { return W ? W->compensator.balanceDb() : 0.0; }
EMSCRIPTEN_KEEPALIVE double prx_cut_db() { return W ? W->compensator.cutDb() : 0.0; }
EMSCRIPTEN_KEEPALIVE double prx_ceiling_db() { return W ? W->compensator.ceilingDb() : 0.0; }
EMSCRIPTEN_KEEPALIVE double prx_level_db() { return W ? W->compensator.levelDb() : -100.0; }
EMSCRIPTEN_KEEPALIVE int prx_holding() { return W && W->compensator.holding() ? 1 : 0; }

// --- The tuning, so the page can draw scales and the shelf from the same numbers ----

EMSCRIPTEN_KEEPALIVE double prx_tuning_split_hz() { return Tuning().splitHz; }
EMSCRIPTEN_KEEPALIVE double prx_tuning_shelf_floor_hz() { return Tuning().shelfFloorHz; }
EMSCRIPTEN_KEEPALIVE double prx_tuning_max_cut_db() { return Tuning().maxCutDb; }
EMSCRIPTEN_KEEPALIVE double prx_tuning_hold_below_db() { return Tuning().holdBelowDb; }

/** The compensator's shelf magnitude at @p hz for the cut it is applying now, in dB —
    H(s) = (s + wz) / (s + wz / g), the same expression the plugin's shelf implements. */
EMSCRIPTEN_KEEPALIVE
double prx_shelf_db (double hz)
{
    if (W == nullptr)
        return 0.0;
    const double cut = W->compensator.cutDb();
    const double g = std::pow (10.0, -cut / 20.0);
    const double fz = Tuning().shelfFloorHz;
    const double fp = fz / g;
    const double num = hz * hz + fz * fz;
    const double den = hz * hz + fp * fp;
    return 10.0 * std::log10 (num / den);
}

/** The simulator's rise at @p hz for the current distance, in dB. */
EMSCRIPTEN_KEEPALIVE
double prx_sim_db (double hz)
{
    if (W == nullptr || ! W->sim.enabled)
        return 0.0;
    const double fp = ProximitySim::poleHz;
    const double fz = fp * W->sim.gain;
    const double num = hz * hz + fz * fz;
    const double den = hz * hz + fp * fp;
    return 10.0 * std::log10 (num / den);
}

} // extern "C"
