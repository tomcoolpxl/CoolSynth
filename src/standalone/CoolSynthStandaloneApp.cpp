#if JucePlugin_Build_Standalone

#include "StandaloneAudioSupport.h"
#include "SettingsStore.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
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

        mainWindow = std::make_unique<juce::StandaloneFilterWindow>(
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
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};
}

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new CoolSynthStandaloneApp();
}

#endif
