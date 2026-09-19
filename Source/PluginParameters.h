// SPDX-License-Identifier: MIT
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    The one parameter Proximate exposes.

    Everything else the plugin does — where it splits the band, how fast it moves, how
    deep it will go — is tuning in `dsp::Tuning`, pinned by the tests and described in the
    guide. It is deliberately not a parameter list: a proximity compensator with a
    threshold, a ratio, an attack and a release is a dynamic EQ, and there are plenty of
    those. The product is the knob.
*/
namespace proximate::params
{

/** 0 to 100 %. Zero is off, bit for bit. */
inline constexpr const char* amount = "amount";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace proximate::params
