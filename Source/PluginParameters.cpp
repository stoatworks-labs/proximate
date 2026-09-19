// SPDX-License-Identifier: MIT
#include "PluginParameters.h"

namespace proximate::params
{

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { amount, 1 }, "Amount",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " %"; })
            .withValueFromStringFunction ([] (const String& s) { return s.trimCharactersAtEnd (" %").getFloatValue(); })));

    return layout;
}

} // namespace proximate::params
