// SPDX-License-Identifier: MIT
//
// proxhost: load the BUILT plugin the way a host does and run a WAV through it.
//
//     cmake --build build --target proxhost
//     build/proxhost_artefacts/proxhost build/Proximate_artefacts/Release/VST3/Proximate.vst3 in.wav 0.65 out.wav
//     build/proxhost_artefacts/proxhost build/Proximate_artefacts/Release/AU/Proximate.component in.wav 0.65 out.wav
//
// The point is the glue, not the DSP: proxstat proves what `dsp::Compensator` does, and
// pluginval proves the plugin survives a host, but neither proves that the plugin the
// host loads *is* the compensator — that the parameter reaches it as 0..1, that both
// channels go through it, that the output is the same samples. So this writes the
// plugin's output and the check is that it matches proxstat's, bit for bit, for the same
// file and amount:
//
//     cmp <(proxstat in.wav 0.65 a.wav >/dev/null; cat a.wav) out.wav
//
// (proxstat writes 32-bit float WAV; so does this.)
//
// With `--show` after the output path it also opens the plugin's own editor in a window
// and streams the file through the plugin at real-time pace, so the editor can be
// watched — or captured — doing what it does on real audio, with no audio device
// involved. It closes itself when the file ends.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

namespace
{
/** A window around the plugin's editor, for `--show`. */
class EditorWindow final : public juce::DocumentWindow
{
public:
    EditorWindow (juce::AudioProcessorEditor* editor)
        : DocumentWindow ("Proximate (proxhost)", juce::Colours::black, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { juce::JUCEApplicationBase::quit(); }
};

/** Feeds the file to the plugin in real time on its own thread, as an audio device
    would, and stops the message loop when it runs out. */
void streamInRealTime (juce::AudioProcessor& plugin, juce::AudioBuffer<float>& audio,
                       double sampleRate, std::atomic<bool>& finished)
{
    const int channels = audio.getNumChannels();
    const int frames = audio.getNumSamples();
    juce::MidiBuffer midi;
    const auto start = std::chrono::steady_clock::now();

    for (int at = 0; at < frames; at += 512)
    {
        const int n = juce::jmin (512, frames - at);
        float* ptrs[2] = {};
        for (int ch = 0; ch < channels; ++ch)
            ptrs[ch] = audio.getWritePointer (ch, at);
        juce::AudioBuffer<float> block (ptrs, channels, n);
        plugin.processBlock (block, midi);

        const auto due = start + std::chrono::microseconds ((long long) ((at + n) * 1e6 / sampleRate));
        std::this_thread::sleep_until (due);
    }
    finished = true;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf (stderr, "usage: proxhost <plugin bundle> in.wav <amount 0..1> out.wav [--show]\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();

    juce::AudioPluginFormatManager formats;
    formats.addDefaultFormats();

    juce::OwnedArray<juce::PluginDescription> found;
    juce::KnownPluginList list;
    for (auto* format : formats.getFormats())
        list.scanAndAddFile (argv[1], true, found, *format);

    if (found.isEmpty())
    {
        std::fprintf (stderr, "no plugin found in %s\n", argv[1]);
        return 1;
    }

    juce::String error;
    auto plugin = formats.createPluginInstance (*found[0], 48000.0, 512, error);
    if (plugin == nullptr)
    {
        std::fprintf (stderr, "could not instantiate: %s\n", error.toRawUTF8());
        return 1;
    }
    std::printf ("loaded %s (%s) by %s, %d parameter(s)\n",
                 found[0]->name.toRawUTF8(), found[0]->pluginFormatName.toRawUTF8(),
                 found[0]->manufacturerName.toRawUTF8(), plugin->getParameters().size());

    // --- The input -------------------------------------------------------------------
    juce::AudioFormatManager audioFormats;
    audioFormats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (audioFormats.createReaderFor (juce::File (argv[2])));
    if (reader == nullptr)
    {
        std::fprintf (stderr, "cannot read %s\n", argv[2]);
        return 1;
    }

    const int channels = juce::jmin ((int) reader->numChannels, 2);
    const auto frames = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio (channels, frames);
    reader->read (&audio, 0, frames, 0, true, channels > 1);

    // --- The parameter, as a host sets it: normalised 0..1 ---------------------------
    const float amount = (float) std::atof (argv[3]);
    juce::AudioProcessorParameter* amountParam = nullptr;
    for (auto* p : plugin->getParameters())
        if (p->getName (32).equalsIgnoreCase ("Amount"))
            amountParam = p;
    if (amountParam == nullptr)
    {
        std::fprintf (stderr, "no Amount parameter\n");
        return 1;
    }
    amountParam->setValueNotifyingHost (amount);
    std::printf ("Amount set to %.2f -> host reads back \"%s\"\n", amount,
                 amountParam->getCurrentValueAsText().toRawUTF8());

    // --- Run it in host-sized blocks --------------------------------------------------
    plugin->setPlayConfigDetails (channels, channels, reader->sampleRate, 512);
    plugin->prepareToPlay (reader->sampleRate, 512);
    std::printf ("latency reported: %d samples\n", plugin->getLatencySamples());

    const bool show = argc > 5 && std::strcmp (argv[5], "--show") == 0;

    if (show)
    {
        // The editor on the message thread, the audio on its own, the way a host does
        // it; the message loop runs until the file has been streamed.
        std::unique_ptr<EditorWindow> window;
        if (auto* editor = plugin->createEditorIfNeeded())
            window = std::make_unique<EditorWindow> (editor);
        else
            std::fprintf (stderr, "the plugin has no editor\n");

        std::atomic<bool> finished { false };
        std::thread audioThread ([&] { streamInRealTime (*plugin, audio, reader->sampleRate, finished); });

        while (! finished)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        audioThread.join();
        // One more spin so the editor's last timer tick paints the final state, then
        // the editor goes away before the plugin does (its destructor tells the
        // plugin so).
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        window.reset();
    }
    else
    {
        juce::MidiBuffer midi;
        for (int start = 0; start < frames; start += 512)
        {
            const int n = juce::jmin (512, frames - start);
            float* ptrs[2] = {};
            for (int ch = 0; ch < channels; ++ch)
                ptrs[ch] = audio.getWritePointer (ch, start);
            juce::AudioBuffer<float> block (ptrs, channels, n);
            plugin->processBlock (block, midi);
        }
    }
    plugin->releaseResources();

    // --- The output, as 32-bit float --------------------------------------------------
    juce::File outFile (argv[4]);
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (
        new juce::FileOutputStream (outFile), reader->sampleRate, (unsigned) channels, 32, {}, 0));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, frames))
    {
        std::fprintf (stderr, "cannot write %s\n", argv[4]);
        return 1;
    }
    writer.reset();
    std::printf ("wrote %s\n", argv[4]);

    plugin.reset();
    return 0;
}
