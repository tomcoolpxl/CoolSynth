#if JucePlugin_Build_Standalone

#include "StandaloneAudioSupport.h"
#include "SettingsStore.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "ui/StandaloneSettingsDialog.h"
#include "plugin/SynthAudioProcessorEditor.h"

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
    juce::TextButton customOptionsButton;
    juce::Button* originalOptionsButton = nullptr;
    void openCustomSettings()
    {
        if (auto* holder = getPluginHolder())
        {
            if (auto* processor = holder->processor.get())
            {
                if (auto* editor = dynamic_cast<SynthAudioProcessorEditor*>(processor->getActiveEditor()))
                {
                    auto* deviceManager = &holder->deviceManager;
                    auto* midiController = editor->getStandaloneMidiController();
                    if (deviceManager != nullptr && midiController != nullptr)
                    {
                        showStandaloneSettingsDialog(this, *deviceManager, *midiController);
                    }
                }
            }
        }
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
