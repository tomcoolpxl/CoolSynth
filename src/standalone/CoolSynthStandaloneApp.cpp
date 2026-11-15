#if JucePlugin_Build_Standalone

#include "StandaloneAudioSupport.h"
#include "SettingsStore.h"
#include "MiniLab3Driver.h"
#include "plugin/PanelEditor.h"
#include "plugin/SynthAudioProcessor.h"
#include "synth/v3/AudioWorkers.h"
#include "synth/v3/SampleLibrary.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{

class CoolSynthStandaloneWindow final : public juce::StandaloneFilterWindow
{
public:
    CoolSynthStandaloneWindow(const juce::String& title, juce::Colour bgColour, std::unique_ptr<juce::StandalonePluginHolder> holder)
        : juce::StandaloneFilterWindow(title, bgColour, std::move(holder))
    {
        // Intercept the default JUCE StandaloneFilterWindow "Options" button
        // by hiding it and overlaying our own
        for (auto* child : getChildren())
        {
            if (auto* button = dynamic_cast<juce::Button*>(child))
            {
                if (button->getName() == "Options" || button->getButtonText() == "Options")
                {
                    button->setVisible(false);
                    originalOptionsButton = button;
                }
            }
        }

        customOptionsButton.setButtonText("Options");
        customOptionsButton.onClick = [this] { openCustomSettings(); };
        addAndMakeVisible(&customOptionsButton);

        // The sample memory is taken as the app starts, in the background (D155).
        coolsynth::synth::v3::SampleLibrary::instance().prepareMemory();

        // A connected MiniLab 3 becomes the panel's hardware twin (docs/interface-v3.md section 5).
        if (auto* host = getPluginHolder())
            if (auto* synth = dynamic_cast<SynthAudioProcessor*>(host->processor.get()))
            {
                // Multi-core as chosen in Options last time; Auto when never chosen (D167). COOLSYNTH_MULTICORE=0 wins.
                if (auto* store = coolsynth::standalone::getStandaloneSettingsStore();
                    store != nullptr && juce::SystemStats::getEnvironmentVariable("COOLSYNTH_MULTICORE", "1") != "0")
                    synth->setMultiCore(store->getMultiCore());
                // The panel's Options (D174) keep the choice where this window's do, and offer the devices.
                synth->rememberMultiCore = [](bool on)
                {
                    if (auto* store = coolsynth::standalone::getStandaloneSettingsStore())
                        store->setMultiCore(on);
                };
                synth->showAudioSettings = [this]
                {
                    if (auto* h = getPluginHolder())
                        h->showAudioSettingsDialog();
                };
                miniLab = std::make_unique<coolsynth::standalone::MiniLab3Driver>(synth->getInterfaceModel(),
                                                                                  host->deviceManager,
                                                                                  *synth);
            }
    }

    void resized() override
    {
        juce::StandaloneFilterWindow::resized();

        if (originalOptionsButton != nullptr)
        {
            customOptionsButton.setBounds(originalOptionsButton->getBounds());
        }
    }

private:
    std::unique_ptr<coolsynth::standalone::MiniLab3Driver> miniLab; // destroyed before the holder
    juce::TextButton customOptionsButton;
    juce::Button* originalOptionsButton = nullptr;
    void openCustomSettings()
    {
        // The same Options as the panel's own button (D174): audio and MIDI, Multi-core (kept in the app's settings,
        // D167), the CPU's work.
        auto* holder = getPluginHolder();
        if (auto* synth = holder != nullptr ? dynamic_cast<SynthAudioProcessor*>(holder->processor.get()) : nullptr)
            PanelEditor::showOptions(*synth, customOptionsButton);
        else if (holder != nullptr)
            holder->showAudioSettingsDialog();
    }
};

class CoolSynthStandaloneApp final : public juce::JUCEApplication
{
public:
    CoolSynthStandaloneApp() = default;

    const juce::String getApplicationName() override   { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override         { return true; }

    void initialise(const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = getApplicationName();
        options.filenameSuffix = ".settings";
        options.folderName = "tomcoolpxl";
        options.osxLibrarySubFolder = "Application Support";
        options.commonToAllUsers = false;
        options.ignoreCaseOfKeyNames = false;
        options.storageFormat = juce::PropertiesFile::storeAsXML;

        appProperties.setStorageParameters (options);

        if (auto* userSettings = appProperties.getUserSettings())
        {
            settingsStore = std::make_unique<coolsynth::standalone::StandaloneSettingsStore>(*userSettings);
            coolsynth::standalone::bindStandaloneSettingsStore(settingsStore.get());
        }

        mainWindow = std::make_unique<CoolSynthStandaloneWindow>(
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
            createPluginHolder());

        mainWindow->setVisible(true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        coolsynth::standalone::bindStandaloneSettingsStore(nullptr);
        settingsStore = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
        auto holder = std::make_unique<juce::StandalonePluginHolder>(
            appProperties.getUserSettings(),
            false,
            juce::String{},
            nullptr,
            juce::Array<juce::StandalonePluginHolder::PluginInOuts>{},
            false);

        coolsynth::standalone::maybeApplyPreferredAudioBackend(holder->deviceManager,
                                                               settingsStore.get());
        return holder;
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<coolsynth::standalone::StandaloneSettingsStore> settingsStore;
    std::unique_ptr<CoolSynthStandaloneWindow> mainWindow;
};
}

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new CoolSynthStandaloneApp();
}

#endif
