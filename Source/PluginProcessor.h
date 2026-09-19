// SPDX-License-Identifier: MIT
#pragma once

#include "DSP/Compensator.h"
#include "PluginParameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace proximate
{

/**
    The host-facing plugin.

    Thin on purpose. The audio thread reads the knob, runs the compensator over the block
    in place, and publishes four numbers for the editor through atomics. There is no
    timer, no message-thread work and no state beyond the parameter tree: everything
    that matters lives in `dsp::Compensator`, which is where the tests point.
*/
class ProximateProcessor final : public juce::AudioProcessor
{
public:
    ProximateProcessor();
    ~ProximateProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Proximate"; }

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getState() { return state; }

    /** What the audio thread last saw, for the editor. Written once per block. */
    struct Readout
    {
        float balanceDb = 0.0f;   // low over high, dB
        float cutDb = 0.0f;       // what the shelf is taking off, dB
        float ceilingDb = 0.0f;   // where the knob has put the ceiling, dB
        float levelDb = -100.0f;  // high-band level, dBFS
        bool holding = false;     // under the gate: the cut is frozen
        bool off = false;         // amount is zero
    };

    Readout getReadout() const;

    const dsp::Tuning& getTuning() const { return compensator.getTuning(); }

private:
    juce::AudioProcessorValueTreeState state;
    std::atomic<float>* amountParam = nullptr;

    dsp::Compensator compensator;

    std::atomic<float> readoutBalance { 0.0f }, readoutCut { 0.0f },
                       readoutCeiling { 0.0f }, readoutLevel { -100.0f };
    std::atomic<bool> readoutHolding { false }, readoutOff { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProximateProcessor)
};

} // namespace proximate
