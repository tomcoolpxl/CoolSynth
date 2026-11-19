#if JucePlugin_Build_Standalone

#include "StandaloneAudioSupport.h"
#include "BuildInfo.h"
#include "mcp/CoolSynthTools.h"
#include "SettingsStore.h"
#include "interface/MiniLab3Driver.h"
#include "parameters/ParameterIDs.h"
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

class CoolSynthStandaloneWindow final : public juce::StandaloneFilterWindow,
                                        private juce::Timer
{
public:
    CoolSynthStandaloneWindow(const juce::String& title, juce::Colour bgColour, std::unique_ptr<juce::StandalonePluginHolder> holder)
        : juce::StandaloneFilterWindow(title, bgColour, std::move(holder))
    {
        // No Options in the window's bar (D187): the panel's Prefs is CoolSynth's one Options, with Audio and MIDI too.
        for (auto* child : getChildren())
            if (auto* button = dynamic_cast<juce::Button*>(child))
                if (button->getName() == "Options" || button->getButtonText() == "Options")
                    button->setVisible(false);

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
                if (auto* store = coolsynth::standalone::getStandaloneSettingsStore())
                    synth->setVelocityCurve(store->getVelocityCurve()); // D175
                synth->rememberVelocityCurve = [](int curve)
                {
                    if (auto* store = coolsynth::standalone::getStandaloneSettingsStore())
                        store->setVelocityCurve(curve);
                };
                synth->showAudioSettings = [this]
                {
                    if (auto* h = getPluginHolder())
                        h->showAudioSettingsDialog();
                };
                miniLab = std::make_unique<coolsynth::ui::MiniLab3Driver>(synth->getInterfaceModel(), *synth,
                                                                          &host->deviceManager);
                // Claude's way in while it guides a test (D225): off until switched on in Prefs, remembered.
                synth->mcpServerOn = [this] { return mcp != nullptr && mcp->listening(); };
                synth->chooseMcpServer = [this, synth](bool on)
                {
                    switchMcp(*synth, on);
                    if (auto* store = coolsynth::standalone::getStandaloneSettingsStore())
                        store->setMcpServer(on);
                };
                synth->mcpServerStatus = [this] { return mcpStatus; };
                if (auto* store = coolsynth::standalone::getStandaloneSettingsStore(); store != nullptr && store->getMcpServer())
                    switchMcp(*synth, true);
            }
        startTimer(1000);
    }

    ~CoolSynthStandaloneWindow() override
    {
        stopTimer();
        alive->store(false); // a tool's job still queued finds the app gone and does nothing
        mcp = nullptr;
    }

    // The MiniLab cannot be asked where its faders stand (its faders only send their moves; D187): so the app keeps
    // where they were. JUCE saves the whole state when the window closes; this also saves it once a fader has moved,
    // so a crash or a forced close keeps them too, and the next start matches the faders where they stand.
    void timerCallback() override
    {
        auto* holder = getPluginHolder();
        auto* synth = holder != nullptr ? dynamic_cast<SynthAudioProcessor*>(holder->processor.get()) : nullptr;
        if (synth == nullptr)
            return;
        std::array<float, 4> now {};
        std::size_t i = 0;
        for (const char* id : { coolsynth::parameters::ids::volumeA, coolsynth::parameters::ids::volumeB,
                                coolsynth::parameters::ids::volumeFx, coolsynth::parameters::ids::volumeMaster })
            if (auto* p = synth->getValueTreeState().getParameter(id))
                now[i++] = p->getValue();
        if (now == savedVolumes)
            return;
        savedVolumes = now;
        holder->savePluginState();
        if (auto* file = dynamic_cast<juce::PropertiesFile*>(holder->settings.get()))
            file->saveIfNeeded();
    }


private:
    void switchMcp(SynthAudioProcessor& synth, bool on)
    {
        mcp = nullptr;
        mcpStatus = "Off: Claude cannot reach this app.";
        if (! on)
            return;
        // A tool runs on the message thread, as the panel's own edits do, and the server waits for it (10 s at most).
        coolsynth::mcp::Server::Runner runner = [alive = alive](std::function<void()> job)
        {
            if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            {
                job();
                return;
            }
            auto done = std::make_shared<juce::WaitableEvent>();
            juce::MessageManager::callAsync([alive, done, job = std::move(job)]
            {
                if (alive->load())
                    job();
                done->signal();
            });
            done->wait(10000);
        };
        // Controller moves enter as a MIDI controller's would: the app's own MIDI input, with their time.
        auto sendMidi = [this](const juce::MidiMessage& message)
        {
            if (auto* holder = getPluginHolder())
                holder->player.getMidiMessageCollector().addMessageToQueue(message);
        };
        auto tools = coolsynth::mcp::coolSynthTools(synth, [this]() -> juce::Component* { return getContentComponent(); }, runner, sendMidi);
        mcp = std::make_unique<coolsynth::mcp::Server>("CoolSynth", coolsynth::build::getVersion(), std::move(tools), runner);
        if (mcp->start(coolsynth::mcp::defaultPort))
            mcpStatus = "On: Claude reaches this app at http://127.0.0.1:" + juce::String(coolsynth::mcp::defaultPort) + "/mcp.";
        else
        {
            mcp = nullptr;
            mcpStatus = "Could not start: port " + juce::String(coolsynth::mcp::defaultPort) + " is taken (another CoolSynth?).";
        }
    }

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    std::unique_ptr<coolsynth::mcp::Server> mcp; // stopped before the holder and the panel go
    juce::String mcpStatus = "Off: Claude cannot reach this app.";
    std::unique_ptr<coolsynth::ui::MiniLab3Driver> miniLab; // destroyed before the holder
    std::array<float, 4> savedVolumes { -1.0f, -1.0f, -1.0f, -1.0f }; // the faders as last saved (D187)
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
