#include "MidiMonitor.h"

namespace coolsynth::midi
{
    void MidiMonitorBuffer::pushMessage(const juce::MidiMessage& message, double timestampSeconds) noexcept
    {
        MidiMonitorEvent event;
        event.timestampSeconds = timestampSeconds;
        event.channel = message.getChannel();

        if (message.isNoteOn())
        {
            event.type = MidiMonitorMessageType::noteOn;
            event.noteNumber = message.getNoteNumber();
            event.primaryValue = event.noteNumber;
            event.secondaryValue = message.getVelocity();
        }
        else if (message.isNoteOff())
        {
            event.type = MidiMonitorMessageType::noteOff;
            event.noteNumber = message.getNoteNumber();
            event.primaryValue = event.noteNumber;
            event.secondaryValue = message.getVelocity();
        }
        else if (message.isController())
        {
            event.type = MidiMonitorMessageType::controlChange;
            event.controllerNumber = message.getControllerNumber();
            event.primaryValue = event.controllerNumber;
            event.secondaryValue = message.getControllerValue();
        }
        else
        {
            return; // Unsupported message type
        }

        event.eventOrder = nextEventOrder.fetch_add(1, std::memory_order_relaxed);

        int start1, size1, start2, size2;
        pendingQueue.prepareToWrite(1, start1, size1, start2, size2);

        if (size1 > 0)
        {
            pendingEvents[static_cast<size_t>(start1)] = event;
            pendingQueue.finishedWrite(1);
        }
    }

    int MidiMonitorBuffer::drainPending(MidiMonitorEvent* destination, int maxEvents) noexcept
    {
        int start1, size1, start2, size2;
        int numAvailable = pendingQueue.getNumReady();
        int numToRead = std::min(numAvailable, maxEvents);

        if (numToRead <= 0)
            return 0;

        pendingQueue.prepareToRead(numToRead, start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i)
            destination[i] = pendingEvents[static_cast<size_t>(start1 + i)];

        for (int i = 0; i < size2; ++i)
            destination[size1 + i] = pendingEvents[static_cast<size_t>(start2 + i)];

        pendingQueue.finishedRead(numToRead);
        return numToRead;
    }

    void MidiMonitorBuffer::clear() noexcept
    {
        int start1, size1, start2, size2;
        int numAvailable = pendingQueue.getNumReady();
        pendingQueue.prepareToRead(numAvailable, start1, size1, start2, size2);
        pendingQueue.finishedRead(numAvailable);
        
        // We don't reset nextEventOrder here to keep the total session order,
        // or we could reset it if we want 'clear' to mean 'reset monitor'.
        // Let's keep it for now.
    }

    juce::String formatMonitorMessageType(MidiMonitorMessageType type)
    {
        switch (type)
        {
            case MidiMonitorMessageType::noteOn:        return "Note On";
            case MidiMonitorMessageType::noteOff:       return "Note Off";
            case MidiMonitorMessageType::controlChange: return "CC";
        }
        return "Unknown";
    }

    juce::String formatNoteName(int noteNumber)
    {
        return juce::MidiMessage::getMidiNoteName(noteNumber, true, true, 3);
    }
}
