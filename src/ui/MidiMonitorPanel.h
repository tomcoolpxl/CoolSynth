#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "midi/MidiMonitor.h"

class MidiMonitorPanel final : public juce::Component,
                               private juce::TableListBoxModel,
                               private juce::Timer
{
public:
    explicit MidiMonitorPanel(coolsynth::midi::MidiMonitorBuffer& monitorBuffer);
    ~MidiMonitorPanel() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g,
                            int rowNumber,
                            int width,
                            int height,
                            bool rowIsSelected) override;
    void paintCell(juce::Graphics& g,
                   int rowNumber,
                   int columnId,
                   int width,
                   int height,
                   bool rowIsSelected) override;
    void timerCallback() override;
    void drainPendingEvents();

    coolsynth::midi::MidiMonitorBuffer& monitorBuffer;
    juce::TableListBox table;
    juce::Array<coolsynth::midi::MidiMonitorEvent> recentEvents;

    enum ColumnId
    {
        orderCol = 1,
        typeCol,
        channelCol,
        data1Col,
        data2Col,
        noteCol,
        ccCol
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMonitorPanel)
};
