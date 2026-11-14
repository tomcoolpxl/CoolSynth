#include "AuditionEditor.h"

#include "SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "synth/v3/UserLibrary.h"

namespace
{
    constexpr int kMenuHeight = 36;
    constexpr int kFilesHeight = 32;
    constexpr const char* kAudioPatterns = "*.wav;*.aif;*.aiff;*.flac;*.ogg";

    juce::String slotName(int part, int slot) { return juce::String(part == 0 ? "A" : "B") + " G" + juce::String(slot); }
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

    // The user's own sounds: a file from the User folder for a Wavetable, Sample or Grain slot.
    filesLabel.setText("Own files:", juce::dontSendNotification);
    addAndMakeVisible(filesLabel);
    for (int i = 0; i < static_cast<int>(fileButtons.size()); ++i)
    {
        const int part = i / 3, slot = i % 3 + 1;
        auto& button = fileButtons[static_cast<std::size_t>(i)];
        button.onClick = [this, part, slot] { showFileMenu(part, slot); };
        addAndMakeVisible(button);
    }
    timerCallback();
    startTimerHz(4);

    addAndMakeVisible(sliders);

    setResizable(true, true);
    setSize(juce::jmax(760, sliders.getWidth()), sliders.getHeight() + kMenuHeight + kFilesHeight);
}

void AuditionEditor::timerCallback()
{
    for (int i = 0; i < static_cast<int>(fileButtons.size()); ++i)
    {
        const int part = i / 3, slot = i % 3 + 1;
        const auto name = synth.getUserFile(part, slot);
        const auto status = synth.getUserFileStatus(part, slot);
        auto text = slotName(part, slot) + ": " + (name.isEmpty() ? juce::String("-") : juce::File::createFileWithoutCheckingPath(name).getFileName());
        if (name.isNotEmpty() && status.isNotEmpty())
            text += status == "still loading" ? juce::String(" (loading)") : juce::String(" (!)");
        auto& button = fileButtons[static_cast<std::size_t>(i)];
        if (button.getButtonText() != text)
            button.setButtonText(text);
        button.setTooltip(name.isEmpty() ? "No file: the slot plays its own choice. Click to give it one of your files."
                                         : name + (status.isEmpty() ? juce::String() : "\n" + status));
    }
}

void AuditionEditor::showFileMenu(int part, int slot)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Choose a file...");
    menu.addItem(2, "No file (the slot's own choice)", synth.getUserFile(part, slot).isNotEmpty());
    menu.addItem(3, "Show the User folder");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(fileButtons[static_cast<std::size_t>(part * 3 + slot - 1)]),
                       [this, part, slot](int choice)
                       {
                           if (choice == 1)
                               chooseFile(part, slot);
                           else if (choice == 2)
                               synth.setUserFile(part, slot, {});
                           else if (choice == 3)
                           {
                               const auto folder = coolsynth::synth::v3::UserLibrary::instance().folder();
                               folder.createDirectory();
                               folder.revealToUser();
                           }
                       });
}

void AuditionEditor::chooseFile(int part, int slot)
{
    const auto folder = coolsynth::synth::v3::UserLibrary::instance().folder();
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser>("A sound for " + slotName(part, slot), folder, kAudioPatterns);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, part, slot, folder](const juce::FileChooser& fc)
                         {
                             auto file = fc.getResult();
                             if (! file.existsAsFile())
                                 return;
                             // A file from elsewhere is copied into the User folder: presets name files there.
                             if (! file.isAChildOf(folder))
                             {
                                 auto copy = folder.getChildFile(file.getFileName());
                                 if (copy.existsAsFile() && copy.getSize() != file.getSize())
                                     copy = copy.getNonexistentSibling(false);
                                 if (! copy.existsAsFile() && ! file.copyFileTo(copy))
                                     return;
                                 file = copy;
                             }
                             synth.setUserFile(part, slot, file.getRelativePathFrom(folder).replaceCharacter('\\', '/'));
                         });
}

void AuditionEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void AuditionEditor::resized()
{
    auto area = getLocalBounds();
    presetMenu.setBounds(area.removeFromTop(kMenuHeight).reduced(6));
    auto row = area.removeFromTop(kFilesHeight).reduced(6, 3);
    filesLabel.setBounds(row.removeFromLeft(72));
    const int width = row.getWidth() / static_cast<int>(fileButtons.size());
    for (auto& button : fileButtons)
        button.setBounds(row.removeFromLeft(width).reduced(2, 0));
    sliders.setBounds(area);
}
