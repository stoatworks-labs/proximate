// SPDX-License-Identifier: MIT
#include "Compensator.h"

#include <algorithm>

namespace proximate::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;

/** Energies are floored here before the logs, which puts the level floor at
    -100 dBFS and keeps log10 away from zero. */
constexpr double energyFloor = 1e-10;

/** Under this the shelf is bypassed exactly rather than applied at a gain of
    0.99999: an untouched sample should be untouched. */
constexpr double transparentCutDb = 1e-4;

double onePoleCoefficient (double hz, double sampleRate) noexcept
{
    // tan() runs away at Nyquist; nothing here is asked for anywhere near it, but a
    // clamp is cheaper than a NaN in somebody's mix.
    const double limited = std::clamp (hz, 1.0, 0.45 * sampleRate);
    const double g = std::tan (pi * limited / sampleRate);
    return g / (1.0 + g);
}

double smoothingCoefficient (double milliseconds, double sampleRate) noexcept
{
    const double seconds = std::max (milliseconds, 0.01) * 1e-3;
    return 1.0 - std::exp (-1.0 / (seconds * sampleRate));
}

double toDb (double power) noexcept
{
    return 10.0 * std::log10 (power);
}
} // namespace

// ---------------------------------------------------------------------------------------

void OnePole::setCutoff (double hz, double sampleRate) noexcept
{
    a = onePoleCoefficient (hz, sampleRate);
}

// ---------------------------------------------------------------------------------------

void Biquad::setButterworthLowpass (double hz, double sampleRate) noexcept
{
    const double w0 = 2.0 * pi * hz / sampleRate;
    const double cosw = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::sqrt (0.5));
    const double a0 = 1.0 + alpha;

    b0 = (1.0 - cosw) * 0.5 / a0;
    b1 = (1.0 - cosw) / a0;
    b2 = b0;
    a1 = -2.0 * cosw / a0;
    a2 = (1.0 - alpha) / a0;
}

void Biquad::setButterworthHighpass (double hz, double sampleRate) noexcept
{
    const double w0 = 2.0 * pi * hz / sampleRate;
    const double cosw = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::sqrt (0.5));
    const double a0 = 1.0 + alpha;

    b0 = (1.0 + cosw) * 0.5 / a0;
    b1 = -(1.0 + cosw) / a0;
    b2 = b0;
    a1 = -2.0 * cosw / a0;
    a2 = (1.0 - alpha) / a0;
}

double Biquad::magnitudeDb (double hz, double sampleRate) const noexcept
{
    // Evaluate H(z) on the unit circle, as a pair of real/imaginary sums.
    const double w = 2.0 * pi * hz / sampleRate;
    const double c1 = std::cos (w), s1 = std::sin (w);
    const double c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);

    const double numRe = b0 + b1 * c1 + b2 * c2;
    const double numIm = -(b1 * s1 + b2 * s2);
    const double denRe = 1.0 + a1 * c1 + a2 * c2;
    const double denIm = -(a1 * s1 + a2 * s2);

    const double num = numRe * numRe + numIm * numIm;
    const double den = denRe * denRe + denIm * denIm;
    return toDb (num / std::max (den, 1e-30));
}

// ---------------------------------------------------------------------------------------

void Detector::prepare (double sampleRate)
{
    dcBlock.setCutoff (tuning.dcBlockHz, sampleRate);
    low.setButterworthLowpass (tuning.splitHz, sampleRate);
    high.setButterworthHighpass (tuning.splitHz, sampleRate);
    smoothing = smoothingCoefficient (tuning.integrationMs, sampleRate);
    gateSmoothing = smoothingCoefficient (tuning.gateMs, sampleRate);
    reset();
}

void Detector::reset()
{
    dcBlock.reset();
    low.reset();
    high.reset();
    lowEnergy = highEnergy = gateEnergy = 0.0;
    balance = 0.0;
    level = toDb (energyFloor);
}

void Detector::push (double x) noexcept
{
    x -= dcBlock.lowpass (x);

    const double lo = low.process (x);
    const double hi = high.process (x);

    lowEnergy += (lo * lo - lowEnergy) * smoothing;
    highEnergy += (hi * hi - highEnergy) * smoothing;
    gateEnergy += (hi * hi - gateEnergy) * gateSmoothing;

    balance = toDb ((lowEnergy + energyFloor) / (highEnergy + energyFloor));
    level = toDb (gateEnergy + energyFloor);
}

// ---------------------------------------------------------------------------------------

Compensator::Compensator (const Tuning& t)
    : tuning (t), detector (t)
{
    prepare (sampleRate);
}

void Compensator::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    detector.prepare (sampleRate);
    attackCoeff = smoothingCoefficient (tuning.attackMs, sampleRate);
    releaseCoeff = smoothingCoefficient (tuning.releaseMs, sampleRate);
    reset();
}

void Compensator::reset()
{
    detector.reset();
    for (auto& s : shelf)
        s.reset();
    target = cut = 0.0;
    held = false;
    setShelf (0.0);
}

double Compensator::ceilingDb (double amount01, const Tuning& t) noexcept
{
    const double a = clamp01 (amount01);
    return t.ceilingOffDb + (t.ceilingFullDb - t.ceilingOffDb) * a;
}

double Compensator::staticCutDb (double balanceDb, double amount01, const Tuning& t) noexcept
{
    const double over = balanceDb - ceilingDb (amount01, t);
    const double half = 0.5 * t.kneeDb;

    double excess;
    if (over <= -half)
        excess = 0.0;
    else if (over >= half || t.kneeDb <= 0.0)
        excess = over;
    else
        excess = (over + half) * (over + half) / (2.0 * t.kneeDb);

    return std::min (excess * t.ratio, t.maxCutDb);
}

void Compensator::setShelf (double cutDb) noexcept
{
    // A first-order shelf, H(s) = (s + wz) / (s + wz / g): DC gain g, unity at the
    // top, the zero fixed at the floor frequency and the pole g times higher. As the
    // cut deepens the pole climbs and the shelf reaches further up — the inverse of a
    // proximity rise, whose corner climbs as the talker gets closer.
    shelfGain = std::pow (10.0, -cutDb / 20.0);
    const double pole = tuning.shelfFloorHz / shelfGain;
    const double a = onePoleCoefficient (pole, sampleRate);

    for (auto& s : shelf)
        s.setCoefficientDirect (a);
}

void Compensator::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    const int used = std::clamp (numChannels, 0, maxChannels);
    if (used == 0 || numSamples <= 0)
        return;

    const double amountNow = amountValue;
    const double channelScale = 1.0 / used;

    for (int i = 0; i < numSamples; ++i)
    {
        // A NaN or infinity from the host must not poison a filter state for the rest
        // of the show: the sample is dropped, the channel's shelf restarted, and the
        // detector sees silence for that sample.
        double mono = 0.0;
        for (int ch = 0; ch < used; ++ch)
        {
            const float x = channels[ch][i];
            if (std::isfinite (x))
                mono += x;
            else
            {
                channels[ch][i] = 0.0f;
                shelf[(std::size_t) ch].reset();
            }
        }
        mono *= channelScale;

        detector.push (mono);

        // The gain computer. At amount zero the plugin is off, however bass-heavy the
        // input; otherwise it answers only while somebody is talking, and holds its
        // last answer through the pauses.
        if (amountNow <= 0.0)
        {
            target = 0.0;
            held = false;
        }
        else if (detector.levelDb() > tuning.holdBelowDb)
        {
            target = staticCutDb (detector.balanceDb(), amountNow, tuning);
            held = false;
        }
        else
        {
            held = true;
        }

        cut += (target - cut) * (target > cut ? attackCoeff : releaseCoeff);
        setShelf (cut);

        const bool transparent = cut < transparentCutDb;

        for (int ch = 0; ch < used; ++ch)
        {
            const double x = channels[ch][i];
            const double lp = shelf[(std::size_t) ch].lowpass (x);

            // Below the transparency threshold the sample goes through untouched rather
            // than as (x - lp) + lp, which is not always x to the last bit. The filter
            // state is still advanced so the shelf is warm when it is next needed.
            if (! transparent)
                channels[ch][i] = (float) ((x - lp) + shelfGain * lp);
        }
    }
}

} // namespace proximate::dsp
