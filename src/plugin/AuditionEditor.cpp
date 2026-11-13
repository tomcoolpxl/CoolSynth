#include "AuditionEditor.h"

#include "SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"

namespace
{
    constexpr int kMenuHeight = 36;
}

AuditionEditor::AuditionEditor(SynthAudioProcessor& processor)
    : juce::AudioProcessorEditor(processor), synth(processor), sliders(processor)
{
    // Rooms are headings; a room's families (the Conservatory's Keys, Strings, ...) are submenus.
    auto* menu = presetMenu.getRootMenu();
    juce::String room, family;
    juce::PopupMenu familyMenu;
    auto closeFamily = [&]
    {
        if (family.isNotEmpty() && familyMenu.getNumItems() > 0)
            menu->addSubMenu(family, familyMenu);
        familyMenu = {};
    };
    for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
    {
        const auto& preset = coolsynth::presets::getFactoryPreset(i);
        if (preset.category != room)
        {
            closeFamily();
            family = {};
            room = preset.category;
            menu->addSectionHeader(room);
        }
        if (preset.section != family)
        {
            closeFamily();
            family = preset.section;
        }
        if (family.isEmpty())
            menu->addItem(i + 1, preset.name);
        else
            familyMenu.addItem(i + 1, preset.name);
    }
    closeFamily();
    presetMenu.setTextWhenNothingSelected("Factory presets");
    presetMenu.setSelectedId(synth.getCurrentProgram() + 1, juce::dontSendNotification);
    presetMenu.onChange = [this]
    {
        if (const int id = presetMenu.getSelectedId(); id > 0)
            synth.setCurrentProgram(id - 1);
    };
    addAndMakeVisible(presetMenu);
    addAndMakeVisible(sliders);

    setResizable(true, true);
    setSize(juce::jmax(560, sliders.getWidth()), sliders.getHeight() + kMenuHeight);
}

void AuditionEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void AuditionEditor::resized()
{
    auto area = getLocalBounds();
    presetMenu.setBounds(area.removeFromTop(kMenuHeight).reduced(6));
    sliders.setBounds(area);
}
