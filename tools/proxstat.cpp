// SPDX-License-Identifier: MIT
//
// proxstat: run a WAV file through the shipped compensator and print what it did.
//
//     clang++ -std=c++20 -O2 -o /tmp/proxstat tools/proxstat.cpp Source/DSP/Compensator.cpp
//     /tmp/proxstat in.wav [amount 0..1] [out.wav] [trace]
//
// Prints the low/high balance of the input and of the output (10th/50th/90th/99th
// percentiles over the samples where the detector is live), the cut's distribution, and
// how much of the time the detector was holding. This is the tool the numbers in the
// README and the guide came from, and the quickest way to hear a change: write the
// output and listen to it.
//
// With `trace` as the fourth argument it also prints a line every 50 ms — time, input
// balance, cut, output balance — which is how the attack and release were watched on a
// file that alternates between a far and a close talker.
//
// 16-bit and 32-bit-float PCM WAV, mono or stereo, any sample rate.

#include "../Source/DSP/Compensator.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
struct Wav
{
    int sampleRate = 0;
    int channels = 0;
    std::vector<std::vector<float>> data;   // [channel][sample]
};

bool readWav (const std::string& path, Wav& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f)
        return false;

    std::vector<char> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp (bytes.data(), "RIFF", 4) != 0
        || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
        return false;

    auto u16 = [&] (std::size_t at) { return (uint16_t) ((uint8_t) bytes[at] | ((uint8_t) bytes[at + 1] << 8)); };
    auto u32 = [&] (std::size_t at) { return (uint32_t) u16 (at) | ((uint32_t) u16 (at + 2) << 16); };

    int format = 0, bits = 0;
    std::size_t pos = 12;
    const char* pcm = nullptr;
    std::size_t pcmBytes = 0;

    while (pos + 8 <= bytes.size())
    {
        const std::string id (bytes.data() + pos, 4);
        const std::size_t size = u32 (pos + 4);
        const std::size_t body = pos + 8;

        if (id == "fmt ")
        {
            format = u16 (body);
            out.channels = u16 (body + 2);
            out.sampleRate = (int) u32 (body + 4);
            bits = u16 (body + 14);
        }
        else if (id == "data")
        {
            pcm = bytes.data() + body;
            pcmBytes = std::min (size, bytes.size() - body);
        }

        pos = body + size + (size & 1);
    }

    if (pcm == nullptr || out.channels <= 0)
        return false;

    const bool isFloat = format == 3 && bits == 32;
    const bool isPcm16 = format == 1 && bits == 16;
    if (! isFloat && ! isPcm16)
    {
        std::fprintf (stderr, "unsupported WAV: format %d, %d bits\n", format, bits);
        return false;
    }

    const std::size_t frameBytes = (std::size_t) out.channels * (isFloat ? 4 : 2);
    const std::size_t frames = pcmBytes / frameBytes;
    out.data.assign ((std::size_t) out.channels, std::vector<float> (frames));

    for (std::size_t i = 0; i < frames; ++i)
        for (int ch = 0; ch < out.channels; ++ch)
        {
            const char* p = pcm + i * frameBytes + (std::size_t) ch * (isFloat ? 4 : 2);
            if (isFloat)
            {
                float v;
                std::memcpy (&v, p, 4);
                out.data[(std::size_t) ch][i] = v;
            }
            else
            {
                const int16_t v = (int16_t) ((uint8_t) p[0] | ((uint8_t) p[1] << 8));
                out.data[(std::size_t) ch][i] = (float) v / 32768.0f;
            }
        }

    return true;
}

bool writeWav (const std::string& path, const Wav& w)
{
    std::ofstream f (path, std::ios::binary);
    if (! f)
        return false;

    const uint32_t frames = w.data.empty() ? 0 : (uint32_t) w.data[0].size();
    const uint32_t dataBytes = frames * (uint32_t) w.channels * 4;

    auto put16 = [&] (uint16_t v) { char b[2] = { (char) (v & 0xff), (char) (v >> 8) }; f.write (b, 2); };
    auto put32 = [&] (uint32_t v) { put16 ((uint16_t) (v & 0xffff)); put16 ((uint16_t) (v >> 16)); };

    f.write ("RIFF", 4); put32 (36 + dataBytes); f.write ("WAVE", 4);
    f.write ("fmt ", 4); put32 (16); put16 (3); put16 ((uint16_t) w.channels);
    put32 ((uint32_t) w.sampleRate); put32 ((uint32_t) w.sampleRate * (uint32_t) w.channels * 4);
    put16 ((uint16_t) (w.channels * 4)); put16 (32);
    f.write ("data", 4); put32 (dataBytes);

    for (uint32_t i = 0; i < frames; ++i)
        for (int ch = 0; ch < w.channels; ++ch)
        {
            const float v = w.data[(std::size_t) ch][i];
            f.write (reinterpret_cast<const char*> (&v), 4);
        }
    return true;
}

double percentile (std::vector<double> v, double p)
{
    if (v.empty())
        return 0.0;
    std::sort (v.begin(), v.end());
    const double idx = p / 100.0 * (double) (v.size() - 1);
    const auto lo = (std::size_t) idx;
    const auto hi = std::min (lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (idx - (double) lo);
}

void printStats (const char* name, const std::vector<double>& v)
{
    std::printf ("%-16s n=%7zu  p10 %6.1f  p50 %6.1f  p90 %6.1f  p99 %6.1f  max %6.1f\n",
                 name, v.size(), percentile (v, 10), percentile (v, 50), percentile (v, 90),
                 percentile (v, 99), v.empty() ? 0.0 : *std::max_element (v.begin(), v.end()));
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: proxstat in.wav [amount 0..1] [out.wav]\n");
        return 2;
    }

    Wav wav;
    if (! readWav (argv[1], wav))
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    const double amount = argc > 2 ? std::atof (argv[2]) : 0.5;
    const std::string outPath = argc > 3 && std::strcmp (argv[3], "-") != 0 ? argv[3] : "";
    const bool trace = argc > 4 && std::strcmp (argv[4], "trace") == 0;
    const std::size_t traceEvery = (std::size_t) (wav.sampleRate / 20);

    using namespace proximate::dsp;
    Compensator comp;
    comp.prepare (wav.sampleRate);
    comp.setAmount (amount);

    Detector after;
    after.prepare (wav.sampleRate);

    const int channels = std::min (wav.channels, Compensator::maxChannels);
    const std::size_t frames = wav.data[0].size();

    std::vector<double> balanceIn, balanceOut, cuts;
    std::size_t heldSamples = 0;

    // Block by block, as a host would, so any block-boundary bug shows up here too.
    constexpr int block = 512;
    for (std::size_t start = 0; start < frames; start += block)
    {
        const int n = (int) std::min<std::size_t> (block, frames - start);
        float* ptrs[Compensator::maxChannels] = {};
        for (int ch = 0; ch < channels; ++ch)
            ptrs[ch] = wav.data[(std::size_t) ch].data() + start;

        // The input's own balance, sample by sample, before it is changed in place.
        std::vector<double> mono ((std::size_t) n);
        for (int i = 0; i < n; ++i)
        {
            double m = 0.0;
            for (int ch = 0; ch < channels; ++ch)
                m += ptrs[ch][i];
            mono[(std::size_t) i] = m / channels;
        }

        // Run one sample at a time so the readouts can be sampled per sample.
        for (int i = 0; i < n; ++i)
        {
            float* one[Compensator::maxChannels] = {};
            for (int ch = 0; ch < channels; ++ch)
                one[ch] = ptrs[ch] + i;
            comp.process (one, channels, 1);

            double m = 0.0;
            for (int ch = 0; ch < channels; ++ch)
                m += one[ch][0];
            after.push (m / channels);

            if (! comp.holding())
            {
                balanceIn.push_back (comp.balanceDb());
                balanceOut.push_back (after.balanceDb());
                cuts.push_back (comp.cutDb());
            }
            else
                ++heldSamples;

            const std::size_t at = start + (std::size_t) i;
            if (trace && at % traceEvery == 0)
                std::printf ("t %6.2f  in %6.1f  cut %5.1f  out %6.1f%s\n",
                             (double) at / wav.sampleRate, comp.balanceDb(), comp.cutDb(),
                             after.balanceDb(), comp.holding() ? "  hold" : "");
        }
    }

    std::printf ("%s: %d Hz, %d ch, %.1f s, amount %.2f -> ceiling %+.1f dB\n",
                 argv[1], wav.sampleRate, wav.channels, (double) frames / wav.sampleRate,
                 amount, comp.ceilingDb());
    printStats ("balance in", balanceIn);
    printStats ("balance out", balanceOut);
    printStats ("cut", cuts);
    std::printf ("holding %.0f%% of the time\n", 100.0 * (double) heldSamples / (double) frames);

    if (! outPath.empty())
    {
        Wav out = wav;
        out.channels = channels;
        out.data.resize ((std::size_t) channels);
        if (! writeWav (outPath, out))
        {
            std::fprintf (stderr, "cannot write %s\n", outPath.c_str());
            return 1;
        }
        std::printf ("wrote %s\n", outPath.c_str());
    }
    return 0;
}
