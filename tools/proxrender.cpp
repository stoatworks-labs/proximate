// SPDX-License-Identifier: MIT
//
// proxrender: the plugin's own editor, painted frame by frame over a file, offline.
//
//     cmake --build build --target proxrender
//     build/proxrender_artefacts/Release/proxrender in.wav out.wav <dir> <fps> <editor height> [t:amount,t:amount,...]
//
// The file goes through ProximateProcessor block by block, and at every frame boundary
// the editor is painted into <dir>/frameNNNNN.png at the given height — so each frame
// shows exactly the readouts the plugin had at that sample, and out.wav is the audio that
// goes with it, sample-accurate. The optional list moves the Amount parameter over time
// the way a host's automation would, with a linear ramp between the points.
//
// This is how the demo video was made: no screen recording, no audio device — the real
// editor over the real output. It links the plugin's sources directly rather than
// hosting the built bundle (as proxhost does) because a hosted VST3's editor is a foreign
// native view that the host's JUCE cannot paint into an image; the editor has to be a
// component in this process. proxhost proves the bundle is the same code.

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
/** "t:amount,t:amount,..." — seconds and 0..1 — as sorted breakpoints. */
std::vector<std::pair<double, double>> parseAutomation (const char* text)
{
    std::vector<std::pair<double, double>> points;
    for (const auto& item : juce::StringArray::fromTokens (juce::String (text), ",", ""))
    {
        const auto pair = juce::StringArray::fromTokens (item.trim(), ":", "");
        if (pair.size() == 2)
            points.emplace_back (pair[0].getDoubleValue(), pair[1].getDoubleValue());
    }
    std::sort (points.begin(), points.end());
    return points;
}

/** The automated value at @p t: held before the first point and after the last, a
    straight line in between, the way a host draws a ramp. */
double automationAt (const std::vector<std::pair<double, double>>& points, double t)
{
    if (t <= points.front().first)
        return points.front().second;
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        const auto& [t0, v0] = points[i - 1];
        const auto& [t1, v1] = points[i];
        if (t <= t1)
            return t1 > t0 ? v0 + (v1 - v0) * (t - t0) / (t1 - t0) : v1;
    }
    return points.back().second;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "usage: proxrender in.wav out.wav <dir> <fps> <editor height> [t:amount,...]\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();

    juce::AudioFormatManager audioFormats;
    audioFormats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (audioFormats.createReaderFor (juce::File (argv[1])));
    if (reader == nullptr)
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    const int channels = juce::jmin ((int) reader->numChannels, 2);
    const auto frames = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio (channels, frames);
    reader->read (&audio, 0, frames, 0, true, channels > 1);

    const juce::File dir (argv[3]);
    const double fps = std::atof (argv[4]);
    const int editorHeight = std::atoi (argv[5]);
    const auto automation = parseAutomation (argc > 6 ? argv[6] : "0:0.5");
    dir.createDirectory();

    proximate::ProximateProcessor plugin;
    auto* amountParam = plugin.getState().getParameter (proximate::params::amount);
    plugin.setPlayConfigDetails (channels, channels, reader->sampleRate, 512);
    plugin.prepareToPlay (reader->sampleRate, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());
    editor->setSize (juce::roundToInt (editorHeight * (double) editor->getWidth() / editor->getHeight()), editorHeight);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    juce::MidiBuffer midi;
    juce::PNGImageFormat png;
    const double samplesPerFrame = reader->sampleRate / fps;
    int frameIndex = 0;
    int at = 0;

    while (at < frames)
    {
        const int next = juce::jmin (frames, (int) std::llround ((frameIndex + 1) * samplesPerFrame));
        amountParam->setValueNotifyingHost ((float) automationAt (automation, at / reader->sampleRate));

        for (int start = at; start < next; start += 512)
        {
            const int n = juce::jmin (512, next - start);
            float* ptrs[2] = {};
            for (int ch = 0; ch < channels; ++ch)
                ptrs[ch] = audio.getWritePointer (ch, start);
            juce::AudioBuffer<float> block (ptrs, channels, n);
            plugin.processBlock (block, midi);
        }
        at = next;

        // Deliver the parameter change to the knob, then paint what the editor shows for
        // the audio just processed.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);
        const auto file = dir.getChildFile (juce::String::formatted ("frame%05d.png", frameIndex));
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk() || ! png.writeImageToStream (image, out))
        {
            std::fprintf (stderr, "cannot write %s\n", file.getFullPathName().toRawUTF8());
            return 1;
        }
        ++frameIndex;
    }

    editor.reset();
    plugin.releaseResources();

    juce::File outFile (argv[2]);
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (
        new juce::FileOutputStream (outFile), reader->sampleRate, (unsigned) channels, 32, {}, 0));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, frames))
    {
        std::fprintf (stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    writer.reset();

    std::printf ("rendered %d frames at %.0f fps, %d px tall, into %s; wrote %s\n", frameIndex, fps,
                 editorHeight, dir.getFullPathName().toRawUTF8(), argv[2]);
    return 0;
}
