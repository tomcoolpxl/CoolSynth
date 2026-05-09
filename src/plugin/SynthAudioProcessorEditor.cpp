#include "SynthAudioProcessorEditor.h"

#include "SynthAudioProcessor.h"
#include "parameters/ParameterIDs.h"
#include "ui/StandaloneSettingsDialog.h"
#include "ui/StandaloneStatusBar.h"
#include "standalone/StandaloneAudioSupport.h"
#include "standalone/SettingsStore.h"
#include "presets/PatchState.h"

SynthAudioProcessorEditor::SynthAudioProcessorEditor(SynthAudioProcessor& inProcessor)
    : juce::AudioProcessorEditor(&inProcessor)
    , processor(inProcessor)
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
    waveformLabel.setText("Waveform", juce::dontSendNotification);
    addAndMakeVisible(waveformLabel);
    waveformSelector.addItemList({ "sine", "square", "saw" }, 1);
    addAndMakeVisible(waveformSelector);
    waveformAttachment = std::make_unique<ComboBoxAttachment>(apvts, ids::waveform, waveformSelector);

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

    allNotesOffButton.onClick = [this] { processor.requestPanic(); };
    addAndMakeVisible(allNotesOffButton);

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

        midiLearnManager = std::make_unique<coolsynth::midi::MidiLearnManager>();

        auto applyKnobState = [](coolsynth::ui::HardwareKnob& knob, bool armed, juce::String badge) { knob.setLearnState(armed, badge); };
        auto applyFaderState = [](coolsynth::ui::HardwareFader& fader, bool armed, juce::String badge) { fader.setLearnState(armed, badge); };

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

        if (settingsStore != nullptr)
        {
            auto mappings = settingsStore->loadLearnedMidiMappings();
            midiLearnManager->replaceBindings(mappings);
            processor.setLearnedMidiBindings(midiLearnManager->getBindings());
        }

        refreshMidiLearnVisuals();

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

        standaloneStatusBar = std::make_unique<StandaloneStatusBar>(*standaloneMidiController);
        addAndMakeVisible(*standaloneStatusBar);

        setSize(1280, 480);
    }
    else
    {
        setSize(1280, 420);
    }

    startTimerHz(24);
    refreshValueDisplays();
}

SynthAudioProcessorEditor::~SynthAudioProcessorEditor()
{
    stopTimer();
}

void SynthAudioProcessorEditor::registerLearnableControl(juce::Component& surface,
                                                         juce::String parameterId,
                                                         juce::String displayName,
                                                         std::function<void(bool, juce::String)> applyVisualState)
{
    learnableControls.push_back({ parameterId, displayName, &surface, std::move(applyVisualState) });
    surface.addMouseListener(this, true);
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
    if (midiLearnManager == nullptr) return;
    
    auto session = midiLearnManager->getSession();
    midiLearnStatusLabel.setText(session.statusText, juce::dontSendNotification);

    bool shouldShowBadges = lastShowCcLabelsSetting || session.armed || badgeVisibilityCounter > 0;

    for (auto& ctrl : learnableControls)
    {
        bool isArmed = session.armed && session.parameterId == ctrl.parameterId;
        juce::String badge = "";
        
        if (shouldShowBadges)
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

void SynthAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu())
        return;

    if (midiLearnManager == nullptr)
        return;

    for (const auto& ctrl : learnableControls)
    {
        if (event.eventComponent == ctrl.surface || ctrl.surface->isParentOf(event.eventComponent))
        {
            showMidiLearnMenu(ctrl, event.getScreenPosition());
            return;
        }
    }
}

void SynthAudioProcessorEditor::showMidiLearnMenu(const LearnableControlRegistration& registration, juce::Point<int> screenPosition)
{
    juce::PopupMenu menu;
    
    auto session = midiLearnManager->getSession();
    bool isCurrentlyArmed = session.armed && session.parameterId == registration.parameterId;
    
    if (isCurrentlyArmed)
    {
        menu.addItem(1, "Cancel MIDI Learn", true, false);
    }
    else
    {
        menu.addItem(2, "Learn MIDI CC", true, false);
    }
    
    if (midiLearnManager->findBindingForParameter(registration.parameterId) != nullptr)
    {
        menu.addItem(3, "Clear MIDI CC Mapping", true, false);
    }
    
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screenPosition.x, screenPosition.y, 1, 1}),
        [this, paramId = registration.parameterId, paramName = registration.displayName](int result)
        {
            if (result == 1)
            {
                midiLearnManager->cancelLearning();
                refreshMidiLearnVisuals();
            }
            else if (result == 2)
            {
                midiLearnManager->beginLearning(paramId, paramName);
                refreshMidiLearnVisuals();
            }
            else if (result == 3)
            {
                if (midiLearnManager->clearBinding(paramId))
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
    waveformLabel.setBounds(oscContent.removeFromTop(24));
    waveformSelector.setBounds(oscContent.removeFromTop(32));

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

    if (standaloneStatusBar != nullptr)
    {
        auto bounds = getLocalBounds();
        standaloneStatusBar->setBounds(bounds.removeFromBottom(28));
    }
}

void SynthAudioProcessorEditor::timerCallback()
{
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

    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    activePatchChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        if (chooser.getResult() != juce::File())
            handlePatchSaveSelection(chooser.getResult());
    });
}

void SynthAudioProcessorEditor::launchPatchLoadChooser()
{
    activePatchChooser = std::make_unique<juce::FileChooser>(
        "Load Patch", juce::File(), "*" + juce::String(coolsynth::presets::defaultPatchExtension));

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    activePatchChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
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
