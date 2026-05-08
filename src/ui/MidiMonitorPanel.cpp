#include "MidiMonitorPanel.h"

MidiMonitorPanel::MidiMonitorPanel(coolsynth::midi::MidiMonitorBuffer& mb)
    : monitorBuffer(mb)
{
    table.setModel(this);
    table.getHeader().addColumn("#", orderCol, 50);
    table.getHeader().addColumn("Type", typeCol, 80);
    table.getHeader().addColumn("Ch", channelCol, 40);
    table.getHeader().addColumn("Data 1", data1Col, 60);
    table.getHeader().addColumn("Data 2", data2Col, 60);
    table.getHeader().addColumn("Note", noteCol, 60);
    table.getHeader().addColumn("CC", ccCol, 40);
    
    addAndMakeVisible(table);
    
    startTimerHz(20);
}

void MidiMonitorPanel::resized()
{
    table.setBounds(getLocalBounds().reduced(2));
}

void MidiMonitorPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colours::grey.withAlpha(0.2f));
    g.drawRoundedRectangle(getLocalBounds().toFloat(), 5.0f, 1.0f);
}

int MidiMonitorPanel::getNumRows()
{
    return recentEvents.size();
}

void MidiMonitorPanel::paintRowBackground(juce::Graphics& g,
                                         int rowNumber,
                                         int width,
                                         int height,
                                         bool rowIsSelected)
{
    juce::ignoreUnused(rowNumber, width, height);

    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue.withAlpha(0.2f));
    else if (rowNumber % 2 == 0)
        g.fillAll(juce::Colours::black.withAlpha(0.1f));
}

void MidiMonitorPanel::paintCell(juce::Graphics& g,
                                int rowNumber,
                                int columnId,
                                int width,
                                int height,
                                bool /*rowIsSelected*/)
{
    juce::ignoreUnused(width, height);

    if (rowNumber >= recentEvents.size())
        return;

    const auto& event = recentEvents[rowNumber];
    juce::String text;

    switch (columnId)
    {
        case orderCol:   text = juce::String(event.eventOrder); break;
        case typeCol:    text = coolsynth::midi::formatMonitorMessageType(event.type); break;
        case channelCol: text = juce::String(event.channel); break;
        case data1Col:   text = juce::String(event.primaryValue); break;
        case data2Col:   text = juce::String(event.secondaryValue); break;
        case noteCol:
            if (event.type == coolsynth::midi::MidiMonitorMessageType::noteOn ||
                event.type == coolsynth::midi::MidiMonitorMessageType::noteOff)
                text = coolsynth::midi::formatNoteName(event.noteNumber);
            else
                text = "-";
            break;
        case ccCol:
            if (event.type == coolsynth::midi::MidiMonitorMessageType::controlChange)
                text = juce::String(event.controllerNumber);
            else
                text = "-";
            break;
    }

    g.setColour(juce::Colours::white);
    g.drawText(text, 2, 0, width - 4, height, juce::Justification::centredLeft, true);
}

void MidiMonitorPanel::timerCallback()
{
    drainPendingEvents();
}

void MidiMonitorPanel::drainPendingEvents()
{
    std::array<coolsynth::midi::MidiMonitorEvent, coolsynth::midi::MidiMonitorBuffer::queueCapacity> pending;
    int numRead = monitorBuffer.drainPending(pending.data(), static_cast<int>(pending.size()));

    if (numRead > 0)
    {
        for (int i = 0; i < numRead; ++i)
        {
            recentEvents.add(pending[static_cast<size_t>(i)]);
        }

        while (recentEvents.size() > coolsynth::midi::MidiMonitorBuffer::visibleHistoryCapacity)
        {
            recentEvents.remove(0);
        }

        table.updateContent();
        table.scrollToEnsureRowIsOnscreen(recentEvents.size() - 1);
    }
}
