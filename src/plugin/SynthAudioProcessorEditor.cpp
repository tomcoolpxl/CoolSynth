#include "SynthAudioProcessorEditor.h"

#include <array>

#include "SynthAudioProcessor.h"
#include "midi/ControllerProfile.h"
#include "parameters/ParameterIDs.h"
#include "presets/PatchState.h"
#include "standalone/SettingsStore.h"
#include "standalone/StandaloneAudioSupport.h"
#include "ui/StandaloneSettingsDialog.h"
#include "ui/StandaloneStatusBar.h"

SynthAudioProcessorEditor::SynthAudioProcessorEditor(SynthAudioProcessor& inProcessor)
    : juce::AudioProcessorEditor(&inProcessor)
    , processor(inProcessor)
    , pianoBar(processor.getKeyboardState())
{
    namespace ids = coolsynth::parameters::ids;
    auto& apvts = processor.getValueTreeState();

    parameterRefs.waveform = apvts.getParameter(ids::waveform);
    parameterRefs.filterCutoffHz = apvts.getParameter(ids::filterCutoffHz);
    parameterRefs.filterResonance = apvts.getParameter(ids::filterResonance);
    parameterRefs.ampAttackMs = apvts.getParameter(ids::ampAttackMs);
    parameterRefs.ampDecayMs = apvts.getParameter(ids::ampDecayMs);
    parameterRefs.ampSustain = apvts.getParameter(ids::ampSustain);
    parameterRefs.ampReleaseMs = apvts.getParameter(ids::ampReleaseMs);
    parameterRefs.delayTimeMs = apvts.getParameter(ids::delayTimeMs);
    parameterRefs.delayFeedback = apvts.getParameter(ids::delayFeedback);
    parameterRefs.delayMix = apvts.getParameter(ids::delayMix);
    parameterRefs.masterGainDb = apvts.getParameter(ids::masterGainDb);

    titleLabel.setText("CoolSynth", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(32.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    midiLearnStatusLabel.setText("", juce::dontSendNotification);
    midiLearnStatusLabel.setFont(juce::FontOptions(14.0f));
    midiLearnStatusLabel.setColour(juce::Label::textColourId, juce::Colours::yellow);
    midiLearnStatusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(midiLearnStatusLabel);

    // --- Oscillator Section ---
    addAndMakeVisible(oscillatorSection);
    addAndMakeVisible(waveformKnob);
    waveformAttachment = std::make_unique<SliderAttachment>(apvts, ids::waveform, waveformKnob.slider());

    // --- Filter Section ---
    addAndMakeVisible(filterSection);
    addAndMakeVisible(cutoffKnob);
    addAndMakeVisible(resonanceKnob);
    cutoffAttachment = std::make_unique<SliderAttachment>(apvts, ids::filterCutoffHz, cutoffKnob.slider());
    resonanceAttachment = std::make_unique<SliderAttachment>(apvts, ids::filterResonance, resonanceKnob.slider());

    // --- Envelope Section ---
    addAndMakeVisible(envelopeSection);
    addAndMakeVisible(attackKnob);
    addAndMakeVisible(decayKnob);
    addAndMakeVisible(sustainKnob);
    addAndMakeVisible(releaseKnob);
    attackAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampAttackMs, attackKnob.slider());
    decayAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampDecayMs, decayKnob.slider());
    sustainAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampSustain, sustainKnob.slider());
    releaseAttachment = std::make_unique<SliderAttachment>(apvts, ids::ampReleaseMs, releaseKnob.slider());

    // --- Delay Section ---
    addAndMakeVisible(delaySection);
    addAndMakeVisible(delayTimeKnob);
    addAndMakeVisible(delayFeedbackKnob);
    addAndMakeVisible(delayMixKnob);
    delayTimeAttachment = std::make_unique<SliderAttachment>(apvts, ids::delayTimeMs, delayTimeKnob.slider());
    delayFeedbackAttachment = std::make_unique<SliderAttachment>(apvts, ids::delayFeedback, delayFeedbackKnob.slider());
    delayMixAttachment = std::make_unique<SliderAttachment>(apvts, ids::delayMix, delayMixKnob.slider());

    // --- Output Section ---
    addAndMakeVisible(outputSection);
    addAndMakeVisible(masterGainFader);
    masterGainAttachment = std::make_unique<SliderAttachment>(apvts, ids::masterGainDb, masterGainFader.slider());

    auto applyKnobState = [](coolsynth::ui::HardwareKnob& knob, bool armed, juce::String badge)
    {
        knob.setLearnState(armed, badge);
    };
    auto applyFaderState = [](coolsynth::ui::HardwareFader& fader, bool armed, juce::String badge)
    {
        fader.setLearnState(armed, badge);
    };

    registerLearnableControl(cutoffKnob, ids::filterCutoffHz, "Cutoff", [&](bool a, juce::String b) { applyKnobState(cutoffKnob, a, b); });
    registerLearnableControl(resonanceKnob, ids::filterResonance, "Resonance", [&](bool a, juce::String b) { applyKnobState(resonanceKnob, a, b); });
    registerLearnableControl(attackKnob, ids::ampAttackMs, "Attack", [&](bool a, juce::String b) { applyKnobState(attackKnob, a, b); });
    registerLearnableControl(decayKnob, ids::ampDecayMs, "Decay", [&](bool a, juce::String b) { applyKnobState(decayKnob, a, b); });
    registerLearnableControl(sustainKnob, ids::ampSustain, "Sustain", [&](bool a, juce::String b) { applyKnobState(sustainKnob, a, b); });
    registerLearnableControl(releaseKnob, ids::ampReleaseMs, "Release", [&](bool a, juce::String b) { applyKnobState(releaseKnob, a, b); });
    registerLearnableControl(delayTimeKnob, ids::delayTimeMs, "Delay Time", [&](bool a, juce::String b) { applyKnobState(delayTimeKnob, a, b); });
    registerLearnableControl(delayFeedbackKnob, ids::delayFeedback, "Delay Feedback", [&](bool a, juce::String b) { applyKnobState(delayFeedbackKnob, a, b); });
    registerLearnableControl(delayMixKnob, ids::delayMix, "Delay Mix", [&](bool a, juce::String b) { applyKnobState(delayMixKnob, a, b); });
    registerLearnableControl(masterGainFader, ids::masterGainDb, "Master Gain", [&](bool a, juce::String b) { applyFaderState(masterGainFader, a, b); });

    allNotesOffButton.onClick = [this] { processor.requestPanic(); };
    addAndMakeVisible(allNotesOffButton);

    addAndMakeVisible(pianoBar);

    midiLearnManager = std::make_unique<coolsynth::midi::MidiLearnManager>();
    midiLearnManager->replaceBindings(processor.getLearnedMidiBindings());

    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        patchActionsVisible = true;

        initPatchButton.onClick = [this] { triggerInitPatch(); };
        savePatchButton.onClick = [this] { triggerSavePatch(); };
        loadPatchButton.onClick = [this] { triggerLoadPatch(); };

        addAndMakeVisible(initPatchButton);
        addAndMakeVisible(savePatchButton);
        addAndMakeVisible(loadPatchButton);

        auto* deviceManager = coolsynth::standalone::getStandaloneAudioDeviceManager();
        auto* settingsStore = coolsynth::standalone::getStandaloneSettingsStore();

        jassert(deviceManager != nullptr);

        if (settingsStore != nullptr)
        {
            auto mappings = settingsStore->loadLearnedMidiMappings();
            midiLearnManager->replaceBindings(mappings);
            processor.setLearnedMidiBindings(midiLearnManager->getBindings());
        }

        standaloneMidiController = std::make_unique<coolsynth::standalone::StandaloneMidiInputController>(
            *deviceManager,
            settingsStore,
            [this](const coolsynth::midi::ControllerMidiEvent& event)
            {
                handleStandaloneControllerEvent(event);
            },
            [this]
            {
                processor.requestPanic();
            });

        standaloneMidiController->addChangeListener(this);
        refreshStandaloneControllerProfileSelection();

        standaloneStatusBar = std::make_unique<StandaloneStatusBar>(*standaloneMidiController);
        addAndMakeVisible(*standaloneStatusBar);

        setSize(1280, 540);
    }
    else
    {
        setSize(1280, 500);
    }

    startTimerHz(24);
    refreshMidiLearnVisuals();
    refreshValueDisplays();
}

SynthAudioProcessorEditor::~SynthAudioProcessorEditor()
{
    if (standaloneMidiController != nullptr)
        standaloneMidiController->removeChangeListener(this);

    stopTimer();
}

int SynthAudioProcessorEditor::getControlParameterIndex(juce::Component& component)
{
    if (const auto* registration = findParameterSurfaceForComponent(&component))
        if (auto* parameter = findParameterForId(registration->parameterId))
            return parameter->getParameterIndex();

    return -1;
}

void SynthAudioProcessorEditor::refreshStandaloneControllerProfileSelection()
{
    if (standaloneMidiController == nullptr)
        return;

    juce::String resolvedProfileId;
    auto selection = coolsynth::standalone::PersistedControllerProfileSelection {};

    if (auto* settingsStore = coolsynth::standalone::getStandaloneSettingsStore())
        selection = settingsStore->loadPersistedControllerProfileSelection();

    switch (selection.mode)
    {
        case coolsynth::standalone::ControllerProfileSelectionMode::none:
            break;

        case coolsynth::standalone::ControllerProfileSelectionMode::explicitProfile:
            resolvedProfileId = selection.profileId;
            break;

        case coolsynth::standalone::ControllerProfileSelectionMode::autoDetect:
        default:
        {
            const auto& snapshot = standaloneMidiController->getSnapshot();
            if (snapshot.selectedDeviceIdentifier.isNotEmpty() || snapshot.selectedDeviceName.isNotEmpty())
            {
                const juce::MidiDeviceInfo device(snapshot.selectedDeviceName,
                                                  snapshot.selectedDeviceIdentifier);
                resolvedProfileId = coolsynth::midi::ControllerProfileRegistry::get()
                                        .findBestProfileIdForDevice(device);
            }
            break;
        }
    }

    if (!processor.setActiveControllerProfile(resolvedProfileId))
        processor.setActiveControllerProfile({});

    refreshMidiLearnVisuals();
}

juce::String SynthAudioProcessorEditor::getResolvedStandaloneControllerProfileDisplayName() const
{
    auto displayName = processor.getActiveControllerProfileDisplayName();
    return displayName.isNotEmpty() ? displayName : "None";
}

void SynthAudioProcessorEditor::resetStandaloneMidiSettings()
{
    if (auto* settingsStore = coolsynth::standalone::getStandaloneSettingsStore())
        settingsStore->clearStandaloneMidiState();

    if (midiLearnManager != nullptr)
    {
        midiLearnManager->cancelLearning();
        midiLearnManager->replaceBindings({});
    }

    processor.setLearnedMidiBindings({});
    badgeVisibilityCounter = 0;
    lastShowCcLabelsSetting = true;

    refreshStandaloneControllerProfileSelection();
    refreshMidiLearnVisuals();
}

void SynthAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == standaloneMidiController.get())
        refreshStandaloneControllerProfileSelection();
}

juce::RangedAudioParameter* SynthAudioProcessorEditor::findParameterForId(juce::StringRef parameterId) const noexcept
{
    return processor.getValueTreeState().getParameter(parameterId);
}

void SynthAudioProcessorEditor::registerParameterSurface(juce::Component& surface, juce::String parameterId)
{
    for (auto& registration : parameterSurfaces)
    {
        if (registration.surface == &surface)
        {
            registration.parameterId = std::move(parameterId);
            return;
        }
    }

    parameterSurfaces.push_back({ std::move(parameterId), &surface });
    surface.addMouseListener(this, true);
}

void SynthAudioProcessorEditor::registerLearnableControl(juce::Component& surface,
                                                         juce::String parameterId,
                                                         juce::String displayName,
                                                         std::function<void(bool, juce::String)> applyVisualState)
{
    registerParameterSurface(surface, parameterId);
    learnableControls.push_back({ parameterId, displayName, &surface, std::move(applyVisualState) });
}

const SynthAudioProcessorEditor::ParameterSurfaceRegistration*
SynthAudioProcessorEditor::findParameterSurfaceForComponent(const juce::Component* component) const noexcept
{
    for (auto* current = component; current != nullptr; current = current->getParentComponent())
        for (const auto& registration : parameterSurfaces)
            if (registration.surface == current)
                return &registration;

    return nullptr;
}

const SynthAudioProcessorEditor::LearnableControlRegistration*
SynthAudioProcessorEditor::findLearnableControl(juce::StringRef parameterId) const noexcept
{
    for (const auto& registration : learnableControls)
        if (registration.parameterId == parameterId)
            return &registration;

    return nullptr;
}

void SynthAudioProcessorEditor::handleStandaloneControllerEvent(const coolsynth::midi::ControllerMidiEvent& event)
{
    if (midiLearnManager != nullptr)
    {
        const auto outcome = midiLearnManager->handleIncomingEvent(event);

        if (outcome.bindingsChanged)
        {
            auto* settingsStore = coolsynth::standalone::getStandaloneSettingsStore();
            if (settingsStore != nullptr)
            {
                settingsStore->saveLearnedMidiMappings(midiLearnManager->getBindings());
            }
            processor.setLearnedMidiBindings(midiLearnManager->getBindings());
            badgeVisibilityCounter = 72; // Show briefly if settings hide them
            refreshMidiLearnVisuals();
        }

        if (event.type == coolsynth::midi::ControllerMidiEventType::controlChange)
        {
            for (const auto& b : midiLearnManager->getBindings())
            {
                if (b.cc.channel == event.channel && b.cc.controllerNumber == event.data1)
                {
                    if (!lastShowCcLabelsSetting && badgeVisibilityCounter == 0)
                    {
                        badgeVisibilityCounter = 72;
                        refreshMidiLearnVisuals();
                    }
                    else
                    {
                        badgeVisibilityCounter = 72;
                    }
                    break;
                }
            }
        }

        if (outcome.result == coolsynth::midi::MidiLearnCaptureResult::captured)
        {
            processor.handleStandaloneControllerEvent(event);
            return;
        }
        else if (outcome.result == coolsynth::midi::MidiLearnCaptureResult::rejectedNonCc)
        {
            midiLearnStatusLabel.setText(outcome.statusText, juce::dontSendNotification);
        }
    }

    processor.handleStandaloneControllerEvent(event);
}

void SynthAudioProcessorEditor::refreshMidiLearnVisuals()
{
    auto session = coolsynth::midi::MidiLearnSession {};
    if (midiLearnManager != nullptr)
        session = midiLearnManager->getSession();

    auto statusText = session.statusText;
    if (statusText.isEmpty() && standaloneMidiController != nullptr)
        statusText = "Profile: " + getResolvedStandaloneControllerProfileDisplayName();

    midiLearnStatusLabel.setText(statusText, juce::dontSendNotification);

    const bool shouldShowBadges = midiLearnManager != nullptr
                               && (lastShowCcLabelsSetting || session.armed || badgeVisibilityCounter > 0);

    for (auto& ctrl : learnableControls)
    {
        bool isArmed = session.armed && session.parameterId == ctrl.parameterId;
        juce::String badge = "";
        
        if (shouldShowBadges && midiLearnManager != nullptr)
        {
            if (auto* binding = midiLearnManager->findBindingForParameter(ctrl.parameterId))
            {
                badge = "CC" + juce::String(binding->cc.controllerNumber);
            }
        }
        
        if (ctrl.applyVisualState)
        {
            ctrl.applyVisualState(isArmed, badge);
        }
    }
}

void SynthAudioProcessorEditor::pollPluginMidiLearnEvents()
{
    if (standaloneMidiController != nullptr || midiLearnManager == nullptr)
        return;

    std::array<coolsynth::midi::ControllerMidiEvent, 32> localEvents {};
    bool visualsChanged = false;

    while (true)
    {
        const auto drained = processor.drainPendingPluginControllerEvents(localEvents.data(),
                                                                          static_cast<int> (localEvents.size()));
        if (drained == 0)
            break;

        for (int i = 0; i < drained; ++i)
        {
            const auto& event = localEvents[static_cast<size_t> (i)];
            const auto outcome = midiLearnManager->handleIncomingEvent(event);

            if (outcome.bindingsChanged)
            {
                processor.setLearnedMidiBindings(midiLearnManager->getBindings());
                badgeVisibilityCounter = 72;
                visualsChanged = true;
            }

            if (event.type == coolsynth::midi::ControllerMidiEventType::controlChange)
            {
                for (const auto& binding : midiLearnManager->getBindings())
                {
                    if (binding.cc.channel == event.channel && binding.cc.controllerNumber == event.data1)
                    {
                        badgeVisibilityCounter = 72;
                        visualsChanged = true;
                        break;
                    }
                }
            }

            if (outcome.result == coolsynth::midi::MidiLearnCaptureResult::captured)
            {
                processor.handleStandaloneControllerEvent(event);
            }
            else if (outcome.result == coolsynth::midi::MidiLearnCaptureResult::rejectedNonCc)
            {
                visualsChanged = true;
            }
        }
    }

    if (visualsChanged)
        refreshMidiLearnVisuals();
}

void SynthAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu())
        return;

    const auto* registration = findParameterSurfaceForComponent(event.eventComponent);
    if (registration == nullptr)
        return;

    juce::String displayName = registration->parameterId;
    if (const auto* learnable = findLearnableControl(registration->parameterId))
        displayName = learnable->displayName;
    else if (auto* parameter = findParameterForId(registration->parameterId))
        displayName = parameter->getName(64);

    showParameterContextMenu(registration->parameterId, displayName, event.getScreenPosition());
}

void SynthAudioProcessorEditor::showParameterContextMenu(juce::String parameterId,
                                                         juce::String displayName,
                                                         juce::Point<int> screenPosition)
{
    juce::PopupMenu menu;
    constexpr int cancelMidiLearnItemId = 1001;
    constexpr int startMidiLearnItemId = 1002;
    constexpr int clearMidiLearnItemId = 1003;

    if (midiLearnManager != nullptr)
    {
        if (const auto* learnable = findLearnableControl(parameterId))
        {
            const auto session = midiLearnManager->getSession();
            const bool isCurrentlyArmed = session.armed && session.parameterId == parameterId;

            if (isCurrentlyArmed)
                menu.addItem(cancelMidiLearnItemId, "Cancel MIDI Learn", true, false);
            else
                menu.addItem(startMidiLearnItemId, "Learn MIDI CC", true, false);

            if (midiLearnManager->findBindingForParameter(parameterId) != nullptr)
                menu.addItem(clearMidiLearnItemId, "Clear MIDI CC Mapping", true, false);
        }
    }

    if (auto* parameter = findParameterForId(parameterId))
    {
        if (auto* context = getHostContext())
        {
            if (auto hostMenu = context->getContextMenuForParameter(parameter))
            {
                auto hostPopup = hostMenu->getEquivalentPopupMenu();
                if (hostPopup.getNumItems() > 0)
                {
                    if (menu.getNumItems() > 0)
                        menu.addSeparator();

                    menu.addSubMenu("Host", hostPopup);
                }
            }
        }
    }

    if (menu.getNumItems() == 0)
        return;

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screenPosition.x, screenPosition.y, 1, 1}),
        [this, parameterId = std::move(parameterId), displayName = std::move(displayName)](int result)
        {
            if (result == cancelMidiLearnItemId)
            {
                if (midiLearnManager != nullptr)
                {
                    midiLearnManager->cancelLearning();
                    refreshMidiLearnVisuals();
                }
            }
            else if (result == startMidiLearnItemId)
            {
                if (midiLearnManager != nullptr)
                {
                    midiLearnManager->beginLearning(parameterId, displayName);
                    refreshMidiLearnVisuals();
                }
            }
            else if (result == clearMidiLearnItemId)
            {
                if (midiLearnManager != nullptr && midiLearnManager->clearBinding(parameterId))
                {
                    auto* settingsStore = coolsynth::standalone::getStandaloneSettingsStore();
                    if (settingsStore != nullptr)
                    {
                        settingsStore->saveLearnedMidiMappings(midiLearnManager->getBindings());
                    }
                    processor.setLearnedMidiBindings(midiLearnManager->getBindings());
                    refreshMidiLearnVisuals();
                }
            }
        });
}

void SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void SynthAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    auto titleArea = area.removeFromTop(48);
    titleLabel.setBounds(titleArea.removeFromLeft(200));
    midiLearnStatusLabel.setBounds(titleArea.removeFromLeft(400).withTrimmedTop(12));
    
    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        titleArea.removeFromRight(16);
    }
    allNotesOffButton.setBounds(titleArea.removeFromRight(120).withSizeKeepingCentre(100, 24));
    
    if (patchActionsVisible)
    {
        titleArea.removeFromRight(16);
        loadPatchButton.setBounds(titleArea.removeFromRight(90).withSizeKeepingCentre(80, 24));
        titleArea.removeFromRight(8);
        savePatchButton.setBounds(titleArea.removeFromRight(90).withSizeKeepingCentre(80, 24));
        titleArea.removeFromRight(8);
        initPatchButton.setBounds(titleArea.removeFromRight(90).withSizeKeepingCentre(80, 24));
    }
    area.removeFromTop(16);

    auto synthRow = area.removeFromTop(240);
    
    // Oscillator
    auto oscArea = synthRow.removeFromLeft(160);
    oscillatorSection.setBounds(oscArea);
    auto oscContent = oscArea.reduced(12);
    oscContent.removeFromTop(24); // Title space
    waveformKnob.setBounds(oscContent.withSizeKeepingCentre(80, 120));

    synthRow.removeFromLeft(16);

    // Filter
    auto filterArea = synthRow.removeFromLeft(200);
    filterSection.setBounds(filterArea);
    auto filterContent = filterArea.reduced(12);
    filterContent.removeFromTop(24); // Title space
    auto filterGrid = filterContent.withSizeKeepingCentre(filterContent.getWidth(), 120);
    cutoffKnob.setBounds(filterGrid.removeFromLeft(filterGrid.getWidth() / 2));
    resonanceKnob.setBounds(filterGrid);

    synthRow.removeFromLeft(16);

    // Envelope
    auto envArea = synthRow.removeFromLeft(300);
    envelopeSection.setBounds(envArea);
    auto envContent = envArea.reduced(12);
    envContent.removeFromTop(24); // Title space
    auto envGrid = envContent.withSizeKeepingCentre(envContent.getWidth(), 120);
    attackKnob.setBounds(envGrid.removeFromLeft(envGrid.getWidth() / 4));
    decayKnob.setBounds(envGrid.removeFromLeft(envGrid.getWidth() / 3));
    sustainKnob.setBounds(envGrid.removeFromLeft(envGrid.getWidth() / 2));
    releaseKnob.setBounds(envGrid);

    synthRow.removeFromLeft(16);

    // Delay
    auto delayArea = synthRow.removeFromLeft(220);
    delaySection.setBounds(delayArea);
    auto delayContent = delayArea.reduced(12);
    delayContent.removeFromTop(24); // Title space
    auto delayGrid = delayContent.withSizeKeepingCentre(delayContent.getWidth(), 120);
    delayTimeKnob.setBounds(delayGrid.removeFromLeft(delayGrid.getWidth() / 3));
    delayFeedbackKnob.setBounds(delayGrid.removeFromLeft(delayGrid.getWidth() / 2));
    delayMixKnob.setBounds(delayGrid);

    synthRow.removeFromLeft(16);

    // Output
    auto outArea = synthRow; // Remaining area
    outputSection.setBounds(outArea);
    auto outContent = outArea.reduced(12);
    outContent.removeFromTop(24); // Title space
    masterGainFader.setBounds(outContent.withSizeKeepingCentre(80, outContent.getHeight()));

    area.removeFromTop(16);
    pianoBar.setBounds(area.removeFromTop(pianoBar.getDesiredHeight()));

    if (standaloneStatusBar != nullptr)
    {
        auto bounds = getLocalBounds();
        standaloneStatusBar->setBounds(bounds.removeFromBottom(28));
    }
}

void SynthAudioProcessorEditor::timerCallback()
{
    pollPluginMidiLearnEvents();
    refreshValueDisplays();
    
    if (badgeVisibilityCounter > 0)
    {
        badgeVisibilityCounter--;
        if (badgeVisibilityCounter == 0)
            refreshMidiLearnVisuals();
    }
    else
    {
        auto* store = coolsynth::standalone::getStandaloneSettingsStore();
        if (store != nullptr)
        {
            bool currentSetting = store->getShowCcLabels();
            if (currentSetting != lastShowCcLabelsSetting)
            {
                lastShowCcLabelsSetting = currentSetting;
                refreshMidiLearnVisuals();
            }
        }
    }
}

void SynthAudioProcessorEditor::refreshValueDisplays()
{
    waveformKnob.setValueText(getCurrentParameterText(parameterRefs.waveform));
    cutoffKnob.setValueText(getCurrentParameterText(parameterRefs.filterCutoffHz));
    resonanceKnob.setValueText(getCurrentParameterText(parameterRefs.filterResonance));
    attackKnob.setValueText(getCurrentParameterText(parameterRefs.ampAttackMs));
    decayKnob.setValueText(getCurrentParameterText(parameterRefs.ampDecayMs));
    sustainKnob.setValueText(getCurrentParameterText(parameterRefs.ampSustain));
    releaseKnob.setValueText(getCurrentParameterText(parameterRefs.ampReleaseMs));
    delayTimeKnob.setValueText(getCurrentParameterText(parameterRefs.delayTimeMs));
    delayFeedbackKnob.setValueText(getCurrentParameterText(parameterRefs.delayFeedback));
    delayMixKnob.setValueText(getCurrentParameterText(parameterRefs.delayMix));
    masterGainFader.setValueText(getCurrentParameterText(parameterRefs.masterGainDb));
}

juce::String SynthAudioProcessorEditor::getCurrentParameterText(juce::RangedAudioParameter* parameter) const
{
    if (parameter == nullptr)
        return "-";
    
    return parameter->getCurrentValueAsText();
}

void SynthAudioProcessorEditor::triggerInitPatch()
{
    processor.resetAutomatableParametersToDefaults();
    refreshValueDisplays();
}

void SynthAudioProcessorEditor::triggerSavePatch()
{
    launchPatchSaveChooser();
}

void SynthAudioProcessorEditor::triggerLoadPatch()
{
    launchPatchLoadChooser();
}

void SynthAudioProcessorEditor::launchPatchSaveChooser()
{
    activePatchChooser = std::make_unique<juce::FileChooser>(
        "Save Patch", juce::File(), "*" + juce::String(coolsynth::presets::defaultPatchExtension));

    auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    activePatchChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        if (chooser.getResult() != juce::File())
            handlePatchSaveSelection(chooser.getResult());
    });
}

void SynthAudioProcessorEditor::launchPatchLoadChooser()
{
    activePatchChooser = std::make_unique<juce::FileChooser>(
        "Load Patch", juce::File(), "*" + juce::String(coolsynth::presets::defaultPatchExtension));

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    activePatchChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        if (chooser.getResult() != juce::File())
            handlePatchLoadSelection(chooser.getResult());
    });
}

void SynthAudioProcessorEditor::handlePatchSaveSelection(const juce::File& selectedFile)
{
    auto destination = selectedFile;
    if (!destination.hasFileExtension(coolsynth::presets::defaultPatchExtension))
        destination = destination.withFileExtension(coolsynth::presets::defaultPatchExtension);

    auto stateXml = processor.createParameterStateXml();
    if (stateXml == nullptr)
    {
        showPatchError("Failed to capture synth parameter state.");
        return;
    }

    auto patchXml = coolsynth::presets::createWrappedPatchXml(*stateXml,
                                                              processor.getParameterStateTypeName());
    auto result = coolsynth::presets::writePatchFile(destination, *patchXml);
    if (!result.succeeded())
        showPatchError(result.message);
}

void SynthAudioProcessorEditor::handlePatchLoadSelection(const juce::File& selectedFile)
{
    auto result = coolsynth::presets::readPatchFile(selectedFile,
                                                    processor.getParameterStateTypeName());
    if (!result.succeeded() || result.parameterStateXml == nullptr)
    {
        showPatchError(result.message);
        return;
    }

    if (!processor.applyParameterStateXml(*result.parameterStateXml))
    {
        showPatchError("Patch file contained incompatible parameter state.");
        return;
    }

    refreshValueDisplays();
}

void SynthAudioProcessorEditor::showPatchError(juce::String message)
{
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                "Patch Error",
                                                message,
                                                this);
}
