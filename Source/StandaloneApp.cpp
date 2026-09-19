// SPDX-License-Identifier: MIT
//
// A custom standalone application, replacing JUCE's default one.
//
// ## Why this file exists
//
// JUCE's stock standalone wrapper opens the system's default audio input *and* its
// default output before it creates the window. When those are two different CoreAudio
// devices — and on Apple Silicon the built-in microphone and the built-in speakers are
// two different devices — it wraps them in an `AudioIODeviceCombiner`. On a machine
// with a few virtual audio devices installed (NDI, Dante-style bridges, conferencing
// drivers) that combiner can block indefinitely inside `AudioDeviceCreateIOProcID`, on
// a `mach_msg` to coreaudiod, on the message thread, with the window not yet created.
//
// The result is an application that launches, appears in the Dock, uses no CPU, writes
// nothing to stderr, produces no crash report and never shows a window. It was first
// found on Contourtonist, and this plugin's first launch on the same machine sat in
// exactly that stack.
//
// So this standalone opens an output and *no input* on its first run. One device, no
// combiner, a window every time. The microphone is then picked in Options > Audio
// Settings, where the operator can also choose the right one — a stage microphone is
// rarely the system default — and that choice is saved and restored on every later
// launch, like any other. Nothing here removes a capability; it stops one being taken
// without being asked for.
//
// A single device that has both inputs and outputs (a USB interface, an aggregate
// device) never needs the combiner, and is the setup that works everywhere.

#include "PluginProcessor.h"

// These four, in this order, before the standalone header — it is written to be included
// by JUCE's own wrapper translation unit, which pulls them in first and does not include
// them itself. Without them the header fails to compile on its own base classes.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace proximate
{

class StandaloneApp final : public juce::JUCEApplication
{
public:
    StandaloneApp()
    {
        juce::PropertiesFile::Options options;

        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif

        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override    { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            jassertfalse;
            return;
        }

        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel()
                .findColour (juce::ResizableWindow::backgroundColourId),
            createPluginHolder());

        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    /** Write a first-run audio setup that opens an output but no input.

        Saved device state is the one path JUCE honours verbatim: `initialiseFromXML`
        takes the device names straight out of the XML and never calls
        `insertDefaultDeviceNames`, which would otherwise fill in the default input for
        any processor with input channels. So an `audioInputDeviceName` of "" really
        does mean no input. (The other routes — `preferredSetupOptions`, constraining
        `channelConfiguration`, overriding `StandalonePluginHolder::init` — were tried on
        Contourtonist and each one either gets overwritten, cripples the settings dialog,
        or is not virtual.)

        A default, not a constraint: the settings dialog still lists every input on the
        system, and once the operator picks one JUCE overwrites this file with their
        choice. Seeding only happens when there is no saved state at all.
    */
    void seedFirstRunAudioSetup()
    {
        auto* settings = appProperties.getUserSettings();

        if (settings == nullptr || settings->getXmlValue ("audioSetup") != nullptr)
            return;   // the operator has been here before; their choice wins

        // Enumerating device types scans for devices but opens none of them — no IOProc
        // is created, so this cannot hit the CoreAudio stall described at the top of
        // this file.
        juce::AudioDeviceManager probe;
        auto& types = probe.getAvailableDeviceTypes();

        if (types.isEmpty())
            return;

        auto* type = types.getFirst();
        type->scanForDevices();

        const auto outputs = type->getDeviceNames (false);
        const auto defaultOutput = outputs[type->getDefaultDeviceIndex (false)];

        if (defaultOutput.isEmpty())
            return;   // nothing sensible to write; let JUCE do whatever it would do

        juce::XmlElement setup ("DEVICESETUP");
        setup.setAttribute ("deviceType", type->getTypeName());
        setup.setAttribute ("audioOutputDeviceName", defaultOutput);
        setup.setAttribute ("audioInputDeviceName", "");

        settings->setValue ("audioSetup", &setup);
        settings->saveIfNeeded();
    }

    std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
        seedFirstRunAudioSetup();

        return std::make_unique<juce::StandalonePluginHolder> (
            appProperties.getUserSettings(),
            false,
            juce::String {},
            nullptr,
            juce::Array<juce::StandalonePluginHolder::PluginInOuts> {},
            false);   // don't auto-open MIDI devices; this plugin has no MIDI
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};

} // namespace proximate

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new proximate::StandaloneApp();
}
