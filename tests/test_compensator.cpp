// SPDX-License-Identifier: MIT
//
// The compensator, pinned: the split filters, the knob-to-ceiling mapping, the gain
// computer, and then the whole thing on signals whose right answer is known — a two-tone
// whose balance is arithmetic, a step in the low band, silence, a stereo pair, a NaN.
//
// Everything here links the standard library alone:
//
//     clang++ -std=c++20 -O2 -o /tmp/t tests/test_compensator.cpp Source/DSP/Compensator.cpp && /tmp/t
//
// The numbers are printed as well as checked. When tuning changes, read them.

#include "../Source/DSP/Compensator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{
int failures = 0;
int checks = 0;

void expectNear (double actual, double expected, double tolerance, const std::string& what)
{
    ++checks;
    if (! (std::abs (actual - expected) <= tolerance))
    {
        ++failures;
        std::printf ("  FAIL  %-56s got %9.4f  want %9.4f (tol %.4f)\n",
                     what.c_str(), actual, expected, tolerance);
    }
}

void expectTrue (bool condition, const std::string& what)
{
    ++checks;
    if (! condition)
    {
        ++failures;
        std::printf ("  FAIL  %s\n", what.c_str());
    }
}

void section (const char* name) { std::printf ("\n%s\n", name); }

constexpr double pi = 3.14159265358979323846;

/** Deterministic white noise, so a run is a run. */
struct Noise
{
    uint32_t state = 0x12345678u;
    double next()
    {
        state = state * 1664525u + 1013904223u;
        return ((double) (state >> 8) / (double) (1u << 24)) * 2.0 - 1.0;
    }
};

/** A sine at @p hz and @p dbfs (RMS), @p seconds long. */
std::vector<float> sine (double hz, double dbfs, double seconds, double sampleRate, double phase = 0.0)
{
    const auto n = (std::size_t) (seconds * sampleRate);
    std::vector<float> v (n);
    const double amp = std::pow (10.0, dbfs / 20.0) * std::sqrt (2.0);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = (float) (amp * std::sin (2.0 * pi * hz * (double) i / sampleRate + phase));
    return v;
}

void add (std::vector<float>& a, const std::vector<float>& b)
{
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        a[i] += b[i];
}

double rmsDb (const float* x, std::size_t n)
{
    double e = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        e += (double) x[i] * x[i];
    return 10.0 * std::log10 (e / (double) n + 1e-30);
}

/** What the detector should read for a set of tones, from the split filters' own
    magnitudes: the balance is arithmetic, leakage included — a 100 Hz tone is 16 dB
    down in the high band, not absent from it. */
double expectedBalanceDb (const std::vector<std::pair<double, double>>& tonesHzDb,
                          const proximate::dsp::Tuning& t, double sampleRate)
{
    proximate::dsp::Biquad lp, hp;
    lp.setButterworthLowpass (t.splitHz, sampleRate);
    hp.setButterworthHighpass (t.splitHz, sampleRate);
    double low = 0.0, high = 0.0;
    for (const auto& [hz, db] : tonesHzDb)
    {
        const double p = std::pow (10.0, db / 10.0);
        low += p * std::pow (10.0, lp.magnitudeDb (hz, sampleRate) / 10.0);
        high += p * std::pow (10.0, hp.magnitudeDb (hz, sampleRate) / 10.0);
    }
    return 10.0 * std::log10 (low / high);
}

/** Run a mono buffer through @p comp in host-sized blocks, in place. */
void run (proximate::dsp::Compensator& comp, std::vector<float>& x, int block = 256)
{
    for (std::size_t start = 0; start < x.size(); start += (std::size_t) block)
    {
        float* ptr = x.data() + start;
        const int n = (int) std::min<std::size_t> ((std::size_t) block, x.size() - start);
        comp.process (&ptr, 1, n);
    }
}

} // namespace

using namespace proximate::dsp;

int main()
{
    const Tuning tuning;
    const double fs = 48000.0;

    // -----------------------------------------------------------------------------------
    section ("The split is a power-complementary Butterworth pair");
    {
        Biquad lp, hp;
        lp.setButterworthLowpass (tuning.splitHz, fs);
        hp.setButterworthHighpass (tuning.splitHz, fs);

        expectNear (lp.magnitudeDb (tuning.splitHz, fs), -3.0103, 0.01, "low-pass at the split is -3 dB");
        expectNear (hp.magnitudeDb (tuning.splitHz, fs), -3.0103, 0.01, "high-pass at the split is -3 dB");

        for (double hz : { 30.0, 100.0, 250.0, 500.0, 1000.0, 4000.0, 12000.0 })
        {
            const double l = std::pow (10.0, lp.magnitudeDb (hz, fs) / 10.0);
            const double h = std::pow (10.0, hp.magnitudeDb (hz, fs) / 10.0);
            std::printf ("  %7.0f Hz  |L|^2 %.4f  |H|^2 %.4f  sum %.6f\n", hz, l, h, l + h);
            expectNear (l + h, 1.0, 1e-6, "|L|^2 + |H|^2 = 1 at " + std::to_string ((int) hz) + " Hz");
        }
    }

    // -----------------------------------------------------------------------------------
    section ("The knob sets the ceiling, linearly, from 'never' to thin");
    {
        expectNear (Compensator::ceilingDb (0.0, tuning), tuning.ceilingOffDb, 1e-9, "amount 0");
        expectNear (Compensator::ceilingDb (1.0, tuning), tuning.ceilingFullDb, 1e-9, "amount 1");
        expectNear (Compensator::ceilingDb (0.5, tuning),
                    0.5 * (tuning.ceilingOffDb + tuning.ceilingFullDb), 1e-9, "amount 0.5 is halfway");
        expectNear (Compensator::ceilingDb (-1.0, tuning), tuning.ceilingOffDb, 1e-9, "clamped below 0");
        expectNear (Compensator::ceilingDb (2.0, tuning), tuning.ceilingFullDb, 1e-9, "clamped above 1");

        double last = 1e9;
        bool monotonic = true;
        for (int i = 0; i <= 100; ++i)
        {
            const double c = Compensator::ceilingDb (i / 100.0, tuning);
            monotonic = monotonic && c <= last;
            last = c;
        }
        expectTrue (monotonic, "ceiling falls monotonically with the knob");
        std::printf ("  amount 0 -> %+.1f dB, 0.5 -> %+.1f dB, 1 -> %+.1f dB\n",
                     Compensator::ceilingDb (0.0, tuning), Compensator::ceilingDb (0.5, tuning),
                     Compensator::ceilingDb (1.0, tuning));
    }

    // -----------------------------------------------------------------------------------
    section ("The gain computer: nothing under the knee, ratio times the excess over it, capped");
    {
        const double amount = 1.0;
        const double ceiling = Compensator::ceilingDb (amount, tuning);
        const double half = 0.5 * tuning.kneeDb;

        expectNear (Compensator::staticCutDb (ceiling - half - 5.0, amount, tuning), 0.0, 1e-12, "well under the knee");
        expectNear (Compensator::staticCutDb (ceiling - half, amount, tuning), 0.0, 1e-12, "at the bottom of the knee");
        expectNear (Compensator::staticCutDb (ceiling, amount, tuning),
                    tuning.ratio * half * half / (2.0 * tuning.kneeDb), 1e-12, "at the ceiling, mid-knee");
        expectNear (Compensator::staticCutDb (ceiling + half, amount, tuning),
                    tuning.ratio * half, 1e-12, "at the top of the knee");
        expectNear (Compensator::staticCutDb (ceiling + 5.0, amount, tuning),
                    tuning.ratio * 5.0, 1e-12, "5 dB over: ratio x 5");
        expectNear (Compensator::staticCutDb (ceiling + 100.0, amount, tuning),
                    tuning.maxCutDb, 1e-12, "capped at the maximum cut");

        // Continuous and monotonic across the whole range, in 0.01 dB steps.
        double prev = -1.0;
        double worstJump = 0.0;
        bool monotonic = true;
        for (double b = ceiling - 10.0; b <= ceiling + 30.0; b += 0.01)
        {
            const double c = Compensator::staticCutDb (b, amount, tuning);
            if (prev >= 0.0)
            {
                worstJump = std::max (worstJump, std::abs (c - prev));
                monotonic = monotonic && c >= prev - 1e-12;
            }
            prev = c;
        }
        std::printf ("  worst step between 0.01 dB neighbours: %.5f dB\n", worstJump);
        expectTrue (monotonic, "cut is monotonic in balance");
        expectTrue (worstJump < 0.03, "cut is continuous through the knee");
    }

    // -----------------------------------------------------------------------------------
    section ("The detector reads a two-tone's balance as arithmetic says it should");
    {
        for (double lowDb : { -6.0, 0.0, 6.0 })
        {
            Detector d (tuning);
            d.prepare (fs);
            auto x = sine (100.0, -20.0 + lowDb, 2.0, fs);
            add (x, sine (1000.0, -20.0, 2.0, fs));
            for (float v : x)
                d.push (v);
            const double expected = expectedBalanceDb ({ { 100.0, -20.0 + lowDb }, { 1000.0, -20.0 } }, tuning, fs);
            std::printf ("  100 Hz at %+.0f dB against 1 kHz: balance %+.2f dB (arithmetic %+.2f), level %.1f dBFS\n",
                         lowDb, d.balanceDb(), expected, d.levelDb());
            expectNear (d.balanceDb(), expected, 0.1, "balance for low tone at " + std::to_string ((int) lowDb) + " dB");
        }

        // The same two-tone 20 dB quieter reads the same balance: it is a ratio.
        Detector loud (tuning), quiet (tuning);
        loud.prepare (fs);
        quiet.prepare (fs);
        auto a = sine (100.0, -14.0, 2.0, fs);
        add (a, sine (1000.0, -20.0, 2.0, fs));
        for (float v : a)
        {
            loud.push (v);
            quiet.push (v * 0.1f);
        }
        expectNear (quiet.balanceDb(), loud.balanceDb(), 0.01, "balance is independent of level");
        expectNear (quiet.levelDb(), loud.levelDb() - 20.0, 0.05, "level follows the signal");
    }

    // -----------------------------------------------------------------------------------
    section ("Amount 0 is bit-exact transparency, whatever the input");
    {
        Compensator comp (tuning);
        comp.prepare (fs);
        comp.setAmount (0.0);

        Noise noise;
        std::vector<float> x ((std::size_t) fs * 2);
        for (auto& v : x)
            v = (float) (0.5 * noise.next());
        add (x, sine (60.0, -6.0, 2.0, fs));   // very bass-heavy
        auto y = x;
        run (comp, y);

        bool identical = true;
        for (std::size_t i = 0; i < x.size(); ++i)
            identical = identical && x[i] == y[i];
        expectTrue (identical, "output equals input sample for sample");
        expectNear (comp.cutDb(), 0.0, 1e-12, "no cut");
    }

    // -----------------------------------------------------------------------------------
    section ("A bass-heavy input is cut; the cut never boosts; the top is untouched; latency is 0");
    {
        Compensator comp (tuning);
        comp.prepare (fs);
        comp.setAmount (1.0);

        // Prime the detector with something far over the ceiling until the cut is at
        // its maximum.
        auto prime = sine (80.0, -12.0, 3.0, fs);
        add (prime, sine (2000.0, -40.0, 3.0, fs));
        run (comp, prime);
        std::printf ("  cut after 3 s of bass-heavy input: %.2f dB (max %.0f)\n", comp.cutDb(), tuning.maxCutDb);
        expectNear (comp.cutDb(), tuning.maxCutDb, 0.05, "cut reaches the maximum");

        // Now measure the shelf with quiet sines. Quiet on purpose: under the hold
        // threshold the cut stays where it is, which is the only way to look at the
        // filter at a known depth from the outside — and it is the hold test too.
        std::printf ("  shelf at %.0f dB:", tuning.maxCutDb);
        for (double hz : { 40.0, 80.0, 120.0, 250.0, 500.0, 1000.0, 5000.0, 15000.0 })
        {
            auto probe = sine (hz, -70.0, 1.0, fs);
            const double inDb = rmsDb (probe.data() + probe.size() / 2, probe.size() / 2);
            run (comp, probe);
            const double outDb = rmsDb (probe.data() + probe.size() / 2, probe.size() / 2);
            const double gain = outDb - inDb;
            std::printf ("  %.0f:%+.1f", hz, gain);
            expectTrue (gain <= 0.001, "never boosts at " + std::to_string ((int) hz) + " Hz");
            if (hz <= tuning.shelfFloorHz * 0.5)
                expectNear (gain, -tuning.maxCutDb, 1.0, "full cut well under the floor, at " + std::to_string ((int) hz) + " Hz");
            if (hz >= 5000.0)
                expectNear (gain, 0.0, 0.2, "unity at " + std::to_string ((int) hz) + " Hz");
            expectNear (comp.cutDb(), tuning.maxCutDb, 0.05, "hold keeps the cut through a quiet probe");
        }
        std::printf ("\n");
        expectTrue (comp.holding(), "detector reports holding under the threshold");

        // Zero latency: the first output sample of an impulse is the impulse, scaled.
        // A second of silence first, so the probes' tails have died out of the shelf.
        std::vector<float> gap ((std::size_t) fs, 0.0f);
        run (comp, gap);
        std::vector<float> impulse (64, 0.0f);
        impulse[10] = 1e-4f;   // quiet enough to keep the hold
        run (comp, impulse);
        bool silentBefore = true;
        for (std::size_t i = 0; i < 10; ++i)
            silentBefore = silentBefore && std::abs (impulse[i]) < 1e-12f;
        expectTrue (silentBefore, "nothing before the impulse");
        expectTrue (impulse[10] > 0.0f, "the impulse comes out on its own sample: zero latency");
        std::printf ("  impulse: out[10] / in[10] = %.3f (the direct path through the shelf)\n", impulse[10] / 1e-4f);
    }

    // -----------------------------------------------------------------------------------
    section ("A step in the low band is followed within a second, in and out");
    {
        Compensator comp (tuning);
        comp.prepare (fs);
        comp.setAmount (0.75);   // ceiling -3 dB
        const double ceiling = comp.ceilingDb();

        // 2 s with the low tone 6 dB under the high one (a balance well under the
        // knee), 3 s with it 3 dB over (a balance well over it, but short of the cap),
        // 3 s back down.
        auto x = sine (1000.0, -20.0, 8.0, fs);
        auto low = sine (100.0, -26.0, 8.0, fs);
        const auto n = low.size();
        const auto t1 = (std::size_t) (2.0 * fs), t2 = (std::size_t) (5.0 * fs);
        const float up = (float) std::pow (10.0, 9.0 / 20.0);
        for (std::size_t i = t1; i < t2 && i < n; ++i)
            low[i] *= up;
        add (x, low);

        const int block = 480;   // 10 ms
        std::vector<double> trace, balances;
        for (std::size_t start = 0; start < x.size(); start += (std::size_t) block)
        {
            float* ptr = x.data() + start;
            comp.process (&ptr, 1, (int) std::min<std::size_t> ((std::size_t) block, x.size() - start));
            trace.push_back (comp.cutDb());
            balances.push_back (comp.balanceDb());
        }
        auto at = [&] (double seconds) { return trace[(std::size_t) (seconds * fs / block)]; };
        auto balanceAt = [&] (double seconds) { return balances[(std::size_t) (seconds * fs / block)]; };

        const double expected = Compensator::staticCutDb (balanceAt (4.9), 0.75, tuning);
        std::printf ("  ceiling %+.1f dB; balance during the step %+.2f dB, static answer %.2f dB\n",
                     ceiling, balanceAt (4.9), expected);
        std::printf ("  cut at 1.9 s %.2f | 2.2 s %.2f | 2.5 s %.2f | 3.0 s %.2f | 4.9 s %.2f | 5.3 s %.2f | 5.6 s %.2f | 6.0 s %.2f | 7.0 s %.2f\n",
                     at (1.9), at (2.2), at (2.5), at (3.0), at (4.9), at (5.3), at (5.6), at (6.0), at (7.0));

        expectTrue (expected > 6.0 && expected < tuning.maxCutDb - 1.0, "the step asks for a real cut short of the cap");
        expectNear (at (1.9), 0.0, 0.01, "no cut while under the knee");
        expectTrue (at (2.2) > 0.3 * expected, "moving 200 ms after the step");
        expectTrue (at (2.5) > 0.8 * expected, "80 % there 500 ms after the step");
        expectNear (at (4.9), expected, 0.15, "settled on the static answer");
        expectTrue (at (5.3) < 0.85 * expected, "coming off 300 ms after the step back");
        expectTrue (at (5.6) < 0.5 * expected, "under half 600 ms after the step back");
        expectTrue (at (6.0) < 0.15 * expected, "under 15 % a second after the step back");
        expectTrue (at (7.0) < 0.1, "back to nothing two seconds after");

        // Nothing in between overshoots the static answer by more than the detector's
        // ripple can explain.
        double peak = 0.0;
        for (double c : trace)
            peak = std::max (peak, c);
        std::printf ("  peak cut %.2f dB\n", peak);
        expectTrue (peak <= expected + 0.3, "no overshoot");
    }

    // -----------------------------------------------------------------------------------
    section ("The same signal 20 dB quieter gets the same cut: it reacts to balance, not level");
    {
        double cuts[2] = {};
        int k = 0;
        for (double level : { -12.0, -32.0 })
        {
            Compensator comp (tuning);
            comp.prepare (fs);
            comp.setAmount (0.75);
            auto x = sine (100.0, level, 3.0, fs);
            add (x, sine (1000.0, level - 6.0, 3.0, fs));
            run (comp, x);
            cuts[k++] = comp.cutDb();
        }
        std::printf ("  at -12 dBFS: %.3f dB   at -32 dBFS: %.3f dB\n", cuts[0], cuts[1]);
        expectNear (cuts[1], cuts[0], 0.02, "same cut at both levels");
        expectTrue (cuts[0] > 3.0, "and it is a real cut");
    }

    // -----------------------------------------------------------------------------------
    section ("Silence holds the cut; a quiet room does not creep it");
    {
        Compensator comp (tuning);
        comp.prepare (fs);
        comp.setAmount (0.75);
        auto x = sine (100.0, -12.0, 3.0, fs);
        add (x, sine (1000.0, -18.0, 3.0, fs));
        run (comp, x);
        const double before = comp.cutDb();

        // The gate closes within tens of milliseconds, the target freezes at the last
        // live reading, the cut finishes settling onto it (it was within the detector's
        // ripple of it already) and then stays put.
        // (Processing is in place, so the silence is refilled between runs: the shelf's
        // tail from the tone would otherwise be fed straight back in as input.)
        std::vector<float> silence ((std::size_t) fs, 0.0f);
        run (comp, silence);
        const double afterOne = comp.cutDb();
        std::fill (silence.begin(), silence.end(), 0.0f);
        run (comp, silence);
        std::fill (silence.begin(), silence.end(), 0.0f);
        run (comp, silence);
        std::printf ("  cut before silence %.3f, after 1 s %.4f, after 3 s %.4f\n", before, afterOne, comp.cutDb());
        expectNear (afterOne, before, 0.1, "no more than the ripple moves it");
        expectNear (comp.cutDb(), afterOne, 1e-6, "frozen from then on");
        expectTrue (comp.holding(), "reports holding");

        // And it was holding almost at once, not once the slow detectors had decayed.
        Compensator quick (tuning);
        quick.prepare (fs);
        quick.setAmount (0.75);
        auto y = sine (100.0, -12.0, 2.0, fs);
        add (y, sine (1000.0, -18.0, 2.0, fs));
        run (quick, y);
        std::vector<float> beat ((std::size_t) (0.15 * fs), 0.0f);
        run (quick, beat);
        expectTrue (quick.holding(), "holding 150 ms into a pause");

        // Bass-heavy room noise under the threshold: also held, even though its own
        // balance would ask for the maximum cut.
        Noise noise;
        std::vector<float> rumble ((std::size_t) fs * 3);
        for (auto& v : rumble)
            v = (float) (1e-4 * noise.next());
        add (rumble, sine (50.0, -62.0, 3.0, fs));
        run (comp, rumble);
        std::printf ("  after 3 s of quiet rumble %.4f\n", comp.cutDb());
        expectNear (comp.cutDb(), afterOne, 1e-6, "held through quiet rumble");

        // Speech-level treble releases it.
        auto treble = sine (2000.0, -20.0, 3.0, fs);
        run (comp, treble);
        std::printf ("  after 3 s of treble at -20 dBFS %.3f\n", comp.cutDb());
        expectNear (comp.cutDb(), 0.0, 0.01, "released by a bright signal");
        expectTrue (! comp.holding(), "no longer holding");
    }

    // -----------------------------------------------------------------------------------
    section ("Stereo is linked: one detector on the mean, one cut on both channels");
    {
        // Left is pure treble, right is pure bass. Together they balance near 0 dB,
        // under the knee at amount 0.5, so neither channel is touched. A per-channel detector
        // would have cut the right channel by the maximum.
        Compensator comp (tuning);
        comp.prepare (fs);
        comp.setAmount (0.5);
        auto left = sine (3000.0, -20.0, 3.0, fs);
        auto right = sine (150.0, -20.0, 3.0, fs);
        const auto rightRef = right;
        float* ptrs[2] = { left.data(), right.data() };
        for (std::size_t start = 0; start < left.size(); start += 256)
        {
            float* blk[2] = { ptrs[0] + start, ptrs[1] + start };
            comp.process (blk, 2, (int) std::min<std::size_t> (256, left.size() - start));
        }
        const auto half = right.size() / 2;
        const double gainRight = rmsDb (right.data() + half, half) - rmsDb (rightRef.data() + half, half);
        std::printf ("  balance of the pair %+.2f dB, cut %.3f dB, right channel gain %+.3f dB\n",
                     comp.balanceDb(), comp.cutDb(), gainRight);
        const double pairExpected = expectedBalanceDb ({ { 3000.0, -20.0 }, { 150.0, -20.0 } }, tuning, fs);
        expectNear (comp.balanceDb(), pairExpected, 0.1, "the pair balances where arithmetic says");
        expectNear (comp.cutDb(), 0.0, 1e-6, "no cut");
        expectNear (gainRight, 0.0, 0.01, "the bass channel is untouched");

        // And when there is a cut, both channels get exactly the same filter: a channel
        // that is a scaled copy of the other comes out as the same scaled copy.
        Compensator linked (tuning);
        linked.prepare (fs);
        linked.setAmount (1.0);
        auto a = sine (100.0, -12.0, 2.0, fs);
        add (a, sine (1000.0, -24.0, 2.0, fs));
        auto b = a;
        for (auto& v : b)
            v *= 0.25f;
        for (std::size_t start = 0; start < a.size(); start += 256)
        {
            float* blk[2] = { a.data() + start, b.data() + start };
            linked.process (blk, 2, (int) std::min<std::size_t> (256, a.size() - start));
        }
        double worst = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
            worst = std::max (worst, std::abs ((double) b[i] - 0.25 * (double) a[i]));
        std::printf ("  cut %.2f dB; worst deviation of the scaled channel: %.2e\n", linked.cutDb(), worst);
        expectTrue (linked.cutDb() > 6.0, "a real cut");
        expectTrue (worst < 1e-6, "both channels got the identical filter");
    }

    // -----------------------------------------------------------------------------------
    section ("Block size does not change the output");
    {
        Noise noise;
        std::vector<float> x ((std::size_t) fs * 2);
        for (auto& v : x)
            v = (float) (0.2 * noise.next());
        add (x, sine (90.0, -10.0, 2.0, fs));

        auto a = x, b = x;
        Compensator ca (tuning), cb (tuning);
        ca.prepare (fs);
        cb.prepare (fs);
        ca.setAmount (0.8);
        cb.setAmount (0.8);
        run (ca, a, 1);
        run (cb, b, 1024);
        bool same = true;
        for (std::size_t i = 0; i < x.size(); ++i)
            same = same && a[i] == b[i];
        expectTrue (same, "1-sample blocks and 1024-sample blocks agree bit for bit");
        expectNear (cb.cutDb(), ca.cutDb(), 1e-12, "and so does the cut");
    }

    // -----------------------------------------------------------------------------------
    section ("Sample rate does not change the behaviour");
    {
        double cuts[3] = {};
        int k = 0;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            Compensator comp (tuning);
            comp.prepare (rate);
            comp.setAmount (0.75);
            auto x = sine (100.0, -12.0, 2.5, rate);
            add (x, sine (1000.0, -20.0, 2.5, rate));
            // Stop 400 ms in so the ballistics are mid-flight, which is where a
            // rate-dependent coefficient would show.
            x.resize ((std::size_t) (0.4 * rate));
            run (comp, x);
            cuts[k++] = comp.cutDb();
        }
        std::printf ("  cut 400 ms into a step: 44.1k %.3f  48k %.3f  96k %.3f\n", cuts[0], cuts[1], cuts[2]);
        expectNear (cuts[0], cuts[1], 0.05, "44.1 kHz matches 48 kHz");
        expectNear (cuts[2], cuts[1], 0.05, "96 kHz matches 48 kHz");
        expectTrue (cuts[1] > 1.0, "and something was happening");
    }

    // -----------------------------------------------------------------------------------
    section ("A NaN from the host is dropped, not remembered");
    {
        Compensator comp (tuning);
        comp.prepare (fs);
        comp.setAmount (0.75);
        auto x = sine (100.0, -12.0, 1.0, fs);
        add (x, sine (1000.0, -20.0, 1.0, fs));
        x[1000] = std::numeric_limits<float>::quiet_NaN();
        x[1001] = std::numeric_limits<float>::infinity();
        run (comp, x);
        bool finite = true;
        for (float v : x)
            finite = finite && std::isfinite (v);
        expectTrue (finite, "every output sample is finite");
        expectTrue (std::isfinite (comp.cutDb()) && std::isfinite (comp.balanceDb()), "readouts are finite");
        expectTrue (comp.cutDb() > 1.0, "and the compensator is still working afterwards");
        std::printf ("  cut after the NaN: %.2f dB\n", comp.cutDb());
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
