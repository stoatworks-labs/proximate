// SPDX-License-Identifier: MIT
#include "PluginEditor.h"

namespace proximate
{
namespace
{
/** The base layout. The window is this shape at any size. */
constexpr int baseWidth  = 320;
constexpr int baseHeight = 440;

constexpr int headerHeight  = 60;
constexpr int knobSize      = 190;
constexpr int knobTop       = 66;
constexpr int captionHeight = 18;
constexpr int readoutTop    = 304;

const juce::Colour background { 0xff0d0f12 };
const juce::Colour panel      { 0xff14161a };
const juce::Colour track      { 0xff23272e };
const juce::Colour text       { 0xffd4d8de };
const juce::Colour dim        { 0xff8b929e };
const juce::Colour accent     { 0xff45b0e8 };
const juce::Colour warn       { 0xffe0a03a };

/** The balance meter's span. */
constexpr float meterMinDb = -12.0f;
constexpr float meterMaxDb = 12.0f;

float meterX (float db, juce::Rectangle<float> r)
{
    const float t = juce::jlimit (0.0f, 1.0f, (db - meterMinDb) / (meterMaxDb - meterMinDb));
    return r.getX() + t * r.getWidth();
}

juce::String dbText (float db, int decimals = 1)
{
    // A cut of 0.0 is "0.0 dB", not "-0.0 dB".
    if (std::abs (db) < 0.05f)
        db = 0.0f;
    return (db > 0.0f ? "+" : "") + juce::String (db, decimals) + " dB";
}
} // namespace

// --- KnobLookAndFeel -------------------------------------------------------------------

KnobLookAndFeel::KnobLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, track);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));
    setColour (juce::Label::textWhenEditingColourId, text);
    setColour (juce::TextEditor::highlightedTextColourId, text);
    setColour (juce::CaretComponent::caretColourId, accent);
}

void KnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float pos, float startAngle, float endAngle,
                                        juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (10.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcWidth = juce::jmax (4.0f, radius * 0.11f);
    const float arcRadius = radius - arcWidth * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    // The dial face.
    g.setColour (panel);
    g.fillEllipse (juce::Rectangle<float> (radius * 1.6f, radius * 1.6f).withCentre (centre));

    // The track, and the value on top of it.
    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (trackArc, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (pos > 0.0f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (valueArc, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // The pointer.
    const float pointerLength = radius * 0.42f;
    const float pointerInner = radius * 0.30f;
    juce::Path pointer;
    pointer.addRoundedRectangle (-arcWidth * 0.3f, -pointerLength - pointerInner,
                                 arcWidth * 0.6f, pointerLength, arcWidth * 0.3f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.fillPath (pointer);
}

juce::Label* KnobLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::FontOptions (22.0f, juce::Font::bold));
    label->setJustificationType (juce::Justification::centred);
    return label;
}

// --- Readouts --------------------------------------------------------------------------

void ProximateEditor::Readouts::paint (juce::Graphics& g)
{
    const auto r = processor.getReadout();
    const auto& tuning = processor.getTuning();
    auto area = getLocalBounds().toFloat().reduced (20.0f, 0.0f);

    auto row = [&] (float height) { return area.removeFromTop (height); };
    auto caption = [&] (juce::Rectangle<float> rect, const juce::String& left, const juce::String& right,
                        juce::Colour rightColour)
    {
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (dim);
        g.drawText (left, rect, juce::Justification::centredLeft);
        g.setColour (rightColour);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (right, rect, juce::Justification::centredRight);
    };

    // --- Balance against the ceiling ---------------------------------------------
    const bool live = ! r.off && ! r.holding && r.levelDb > tuning.holdBelowDb;
    caption (row (18.0f), "Balance, low over high",
             r.off ? juce::String ("off") : dbText (r.balanceDb),
             live ? text : dim);

    auto meter = row (16.0f).reduced (0.0f, 4.0f);
    g.setColour (track);
    g.fillRoundedRectangle (meter, 3.0f);

    // The zone over the ceiling, dimly, so the knob's position is visible even when
    // nothing is happening.
    const float ceilingX = meterX (r.ceilingDb, meter);
    if (! r.off)
    {
        g.setColour (accent.withAlpha (0.15f));
        g.fillRoundedRectangle (meter.withLeft (ceilingX), 3.0f);
    }

    // The excess, brightly, when there is any.
    const float balanceX = meterX (r.balanceDb, meter);
    if (live && balanceX > ceilingX)
    {
        g.setColour (accent.withAlpha (0.7f));
        g.fillRoundedRectangle (meter.withLeft (ceilingX).withRight (balanceX), 3.0f);
    }

    // The ceiling line and the balance marker.
    if (! r.off)
    {
        g.setColour (accent);
        g.fillRect (juce::Rectangle<float> (ceilingX - 1.0f, meter.getY() - 3.0f, 2.0f, meter.getHeight() + 6.0f));
    }
    if (live)
    {
        g.setColour (text);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ balanceX, meter.getCentreY() }));
    }

    // Scale ticks under the meter.
    auto scale = row (12.0f);
    g.setFont (juce::FontOptions (9.0f));
    g.setColour (dim.withAlpha (0.7f));
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float x = meterX (db, meter);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (x - 14.0f, scale.getY(), 28.0f, scale.getHeight()),
                    juce::Justification::centred);
    }

    area.removeFromTop (8.0f);

    // --- The cut -------------------------------------------------------------------
    caption (row (18.0f), "Cut", r.off ? juce::String ("0.0 dB") : dbText (-r.cutDb), r.cutDb > 0.05f ? text : dim);

    auto cutMeter = row (16.0f).reduced (0.0f, 4.0f);
    g.setColour (track);
    g.fillRoundedRectangle (cutMeter, 3.0f);
    const float cutFraction = juce::jlimit (0.0f, 1.0f, r.cutDb / (float) tuning.maxCutDb);
    if (cutFraction > 0.0f && ! r.off)
    {
        // Grows from the right, the way a gain-reduction meter does.
        g.setColour (warn);
        g.fillRoundedRectangle (cutMeter.withLeft (cutMeter.getRight() - cutFraction * cutMeter.getWidth()), 3.0f);
    }

    auto cutScale = row (12.0f);
    g.setFont (juce::FontOptions (9.0f));
    g.setColour (dim.withAlpha (0.7f));
    for (float db : { 0.0f, 6.0f, 12.0f, 18.0f })
    {
        const float x = cutMeter.getRight() - (db / (float) tuning.maxCutDb) * cutMeter.getWidth();
        g.drawText ("-" + juce::String ((int) db), juce::Rectangle<float> (x - 14.0f, cutScale.getY(), 28.0f, cutScale.getHeight()),
                    juce::Justification::centred);
    }

    area.removeFromTop (6.0f);

    // --- The gate ------------------------------------------------------------------
    juce::String status;
    juce::Colour statusColour = dim;
    if (r.off)
        status = "Off: the amount is at zero, the audio is untouched.";
    else if (r.holding)
        status = "Holding: nobody is talking, so the cut stays where it is.";
    else
    {
        status = "Listening.";
        statusColour = accent;
    }
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (statusColour);
    g.drawFittedText (status, row (18.0f).toNearestInt(), juce::Justification::centredLeft, 1);
}

// --- ProximateEditor -------------------------------------------------------------------

ProximateEditor::ProximateEditor (ProximateProcessor& p)
    : AudioProcessorEditor (&p), plugin (p), readouts (p)
{
    auto styleLabel = [] (juce::Label& l, float size, juce::Colour colour,
                          juce::Justification just = juce::Justification::centredLeft,
                          bool bold = false)
    {
        l.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        l.setColour (juce::Label::textColourId, colour);
        l.setJustificationType (just);
    };

    addAndMakeVisible (content);

    content.addAndMakeVisible (title);
    title.setText ("Proximate", juce::dontSendNotification);
    styleLabel (title, 20.0f, text, juce::Justification::centredLeft, true);

    content.addAndMakeVisible (subtitle);
    subtitle.setText ("proximity effect compensation", juce::dontSendNotification);
    styleLabel (subtitle, 11.0f, dim);

    content.addAndMakeVisible (knob);
    knob.setLookAndFeel (&knobLook);
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 28);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                              juce::MathConstants<float>::pi * 2.8f, true);
    knob.setDoubleClickReturnValue (true, 50.0);
    knob.setTooltip ("How far down the ceiling sits. Zero is off; turn it up until leaning in stops sounding boomy.");
    knobAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        plugin.getState(), params::amount, knob);

    content.addAndMakeVisible (knobCaption);
    knobCaption.setText ("Amount", juce::dontSendNotification);
    styleLabel (knobCaption, 12.0f, dim, juce::Justification::centred);

    content.addAndMakeVisible (readouts);

    content.addAndMakeVisible (aboutButton);
    aboutButton.setTooltip ("About Proximate");
    aboutButton.setColour (juce::TextButton::buttonColourId, panel);
    aboutButton.setColour (juce::TextButton::textColourOffId, dim);
    aboutButton.onClick = [this] { aboutPanel.setVisible (true); };

    // Hidden until asked for, and on top of everything when it is shown.
    addChildComponent (aboutPanel);
    aboutPanel.setAlwaysOnTop (true);

    // One shape at any size: the host can resize it, and it scales rather than reflows.
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 3, baseHeight * 3);
    setSize (baseWidth, baseHeight);

    startTimerHz (30);
}

ProximateEditor::~ProximateEditor()
{
    stopTimer();
    knob.setLookAndFeel (nullptr);
}

void ProximateEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void ProximateEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, baseWidth, baseHeight);

    auto area = content.getLocalBounds();

    auto header = area.removeFromTop (headerHeight).reduced (20, 0);
    aboutButton.setBounds (header.removeFromRight (26).withSizeKeepingCentre (26, 26));
    title.setBounds (header.removeFromTop (34).withTrimmedTop (10));
    subtitle.setBounds (header.removeFromTop (18));

    // The slider's own text box sits under the dial, so its bounds are the dial plus
    // one row; the caption goes under that.
    knob.setBounds (juce::Rectangle<int> (knobSize, knobSize + 28).withCentre ({ baseWidth / 2, knobTop + knobSize / 2 + 14 }));
    knobCaption.setBounds (0, knobTop + knobSize + 30, baseWidth, captionHeight);

    readouts.setBounds (0, readoutTop, baseWidth, baseHeight - readoutTop);

    aboutPanel.setBounds (getLocalBounds());
}

void ProximateEditor::timerCallback()
{
    readouts.repaint();
}

} // namespace proximate
