// SPDX-License-Identifier: MIT
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace proximate
{

ProximateProcessor::ProximateProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "Proximate", params::createLayout())
{
    amountParam = state.getRawParameterValue (params::amount);
}

void ProximateProcessor::prepareToPlay (double sampleRate, int)
{
    compensator.prepare (sampleRate > 0.0 ? sampleRate : 48000.0);

    // No look-ahead, no buffering: nothing for the host to compensate.
    setLatencySamples (0);
}

void ProximateProcessor::releaseResources()
{
    compensator.reset();
}

bool ProximateProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void ProximateProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const int channels = juce::jmin (buffer.getNumChannels(), dsp::Compensator::maxChannels);
    if (channels <= 0 || numSamples <= 0)
        return;

    const float amount = amountParam != nullptr ? amountParam->load (std::memory_order_relaxed) : 0.0f;
    compensator.setAmount (amount * 0.01);

    float* ptrs[dsp::Compensator::maxChannels] = {};
    for (int ch = 0; ch < channels; ++ch)
        ptrs[ch] = buffer.getWritePointer (ch);

    compensator.process (ptrs, channels, numSamples);

    readoutBalance.store ((float) compensator.balanceDb(), std::memory_order_relaxed);
    readoutCut.store ((float) compensator.cutDb(), std::memory_order_relaxed);
    readoutCeiling.store ((float) compensator.ceilingDb(), std::memory_order_relaxed);
    readoutLevel.store ((float) compensator.levelDb(), std::memory_order_relaxed);
    readoutHolding.store (compensator.holding(), std::memory_order_relaxed);
    readoutOff.store (amount <= 0.0f, std::memory_order_relaxed);
}

ProximateProcessor::Readout ProximateProcessor::getReadout() const
{
    Readout r;
    r.balanceDb = readoutBalance.load (std::memory_order_relaxed);
    r.cutDb = readoutCut.load (std::memory_order_relaxed);
    r.ceilingDb = readoutCeiling.load (std::memory_order_relaxed);
    r.levelDb = readoutLevel.load (std::memory_order_relaxed);
    r.holding = readoutHolding.load (std::memory_order_relaxed);
    r.off = readoutOff.load (std::memory_order_relaxed);
    return r;
}

juce::AudioProcessorEditor* ProximateProcessor::createEditor()
{
    return new ProximateEditor (*this);
}

void ProximateProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ProximateProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace proximate

// This is the one symbol the host looks for.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new proximate::ProximateProcessor();
}
