// SPDX-License-Identifier: MIT
#pragma once

#include "PluginProcessor.h"
#include "StoatworksAboutPanel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace proximate
{

/** The knob's look: an arc for the track, a brighter arc for the value, a pointer. */
class KnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

/**
    The window: one knob, and underneath it what the knob is doing.

    The readouts are the point of the lower half. A single-knob plugin whose only
    feedback is the knob's own position leaves the operator guessing whether it is
    working, working too hard, or waiting for someone to talk. So the balance the
    detector measures is drawn against the ceiling the knob has set, the cut the shelf
    is applying is drawn as a gain-reduction bar with its number, and the gate says when
    it is holding rather than listening.

    Everything is laid out at a fixed base size and scaled with the window, so the
    plugin is legible at whatever size the host gives it and keeps its proportions.
*/
class ProximateEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit ProximateEditor (ProximateProcessor&);
    ~ProximateEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /** The lower half: balance against the ceiling, the cut, and the gate. */
    class Readouts final : public juce::Component
    {
    public:
        explicit Readouts (const ProximateProcessor& p) : processor (p) {}
        void paint (juce::Graphics&) override;
    private:
        const ProximateProcessor& processor;
    };

    ProximateProcessor& plugin;

    /* Everything but the About panel lives in here, at the base size, and the
       whole thing is scaled to fit the window. */
    juce::Component content;

    KnobLookAndFeel knobLook;
    juce::Slider knob;
    juce::Label title, subtitle, knobCaption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttachment;
    Readouts readouts;

    /* Vendored from stoatworks-backend/about - see StoatworksAboutPanel.h.
       A child of the editor rather than a window of its own: a plugin must not
       put a second top-level window on a host's screen. */
    juce::TextButton aboutButton { "i" };
    stoatworks::AboutPanel aboutPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProximateEditor)
};

} // namespace proximate
