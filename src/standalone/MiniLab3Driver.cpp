#include "MiniLab3Driver.h"

#include "diagnostics/FlightRecorder.h"
#include "interface/InterfaceModel.h"
#include "midi/MiniLab3Protocol.h"
#include "plugin/SynthAudioProcessor.h"

namespace coolsynth::standalone
{
    namespace ml = coolsynth::midi::minilab3;

    MiniLab3Driver::MiniLab3Driver(coolsynth::ui::InterfaceModel& m,
                                   juce::AudioDeviceManager& deviceManager,
                                   SynthAudioProcessor& processor)
        : model(m), devices(deviceManager), synth(processor)
    {
        tryOpen();
        startTimerHz(30);
    }

    MiniLab3Driver::~MiniLab3Driver()
    {
        stopTimer();
        close();
    }

    void MiniLab3Driver::tryOpen()
    {
        if (isConnected())
            return;
        close();

        juce::MidiDeviceInfo inInfo, outInfo;
        for (const auto& info : juce::MidiInput::getAvailableDevices())
            if (ml::isDawPort(info.name))
                inInfo = info;
        for (const auto& info : juce::MidiOutput::getAvailableDevices())
            if (ml::isDawPort(info.name))
                outInfo = info;
        if (inInfo.identifier.isEmpty() || outInfo.identifier.isEmpty())
            return;

        // The driver owns the port: the player must not also hear it (pads would play notes).
        if (devices.isMidiInputDeviceEnabled(inInfo.identifier))
            devices.setMidiInputDeviceEnabled(inInfo.identifier, false);

        input = juce::MidiInput::openDevice(inInfo.identifier, this);
        output = juce::MidiOutput::openDevice(outInfo.identifier);
        if (input == nullptr || output == nullptr)
        {
            close();
            return;
        }
        inputIdentifier = inInfo.identifier;
        input->start();
        send(ml::connect());
        coolsynth::diag::record(coolsynth::diag::Event::miniLab, 0, 0, 0.0f, 0.0f, "connected, DAW program");
        lastKnobValue.fill(-1);
        refreshDevice(true);
    }

    void MiniLab3Driver::close()
    {
        if (input != nullptr)
            input->stop();
        if (output != nullptr)
            output->sendMessageNow(ml::disconnect());
        input = nullptr;
        output = nullptr;
        inputIdentifier = {};
    }

    void MiniLab3Driver::send(const juce::MidiMessage& message)
    {
        if (output != nullptr)
            output->sendMessageNow(message);
    }

    void MiniLab3Driver::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
    {
        // MIDI thread: keys and performance go straight to the sound, controls to the message thread.
        if (ml::decode(message).kind == ml::Control::Kind::other)
        {
            if (message.isSysEx())
                return;
            if (message.isNoteOn())
            {
                const int velocity = message.getVelocity();
                if (velocity <= 1)
                {
                    // the processor's flight recorder logs every note with its velocity; this one is the fault
                    coolsynth::diag::record(coolsynth::diag::Event::velocityFixed, message.getNoteNumber(), lastGoodVelocity);
                    auto fixed = juce::MidiMessage::noteOn(message.getChannel(), message.getNoteNumber(),
                                                           static_cast<juce::uint8>(lastGoodVelocity));
                    fixed.setTimeStamp(message.getTimeStamp());
                    synth.pushLiveMidi(fixed);
                    return;
                }
                lastGoodVelocity = velocity;
            }
            synth.pushLiveMidi(message);
            return;
        }
        const juce::ScopedLock lock(queueLock);
        queue.push_back(message);
    }

    void MiniLab3Driver::apply(const juce::MidiMessage& message)
    {
        const auto control = ml::decode(message);
        using Kind = ml::Control::Kind;
        switch (control.kind)
        {
            case Kind::knob:
            {
                // The knobs turn endlessly but report 0-127: each change is a step count.
                auto& last = lastKnobValue[static_cast<std::size_t>(control.index)];
                const int steps = last < 0 ? 0 : control.value - last;
                last = control.value;
                if (steps == 0)
                    break;
                const int knob = control.index + 1;
                if (knob == 5)
                    model.turnPageKnob(steps);
                else
                    model.turnKnob(knob, steps, shiftHeld);
                break;
            }
            case Kind::fader:
                model.moveFader(control.index, static_cast<float>(control.value) / 127.0f, true);
                break;
            case Kind::mainTurn:
                model.turnMain(control.value);
                break;
            case Kind::mainPress:
                if (control.value != 0)
                    model.pressMain();
                break;
            case Kind::shift:
                shiftHeld = control.value != 0;
                model.setHardwareShift(shiftHeld);
                break;
            case Kind::pad:
                if (control.value != 0)
                    model.pressPad(control.index, shiftHeld);
                break;
            case Kind::other:
                break;
        }
    }

    void MiniLab3Driver::refreshDevice(bool force)
    {
        if (output == nullptr)
            return;

        // The screen first, then any new pop-up on top of it: the device shows the pop-up for a moment and
        // falls back to the screen by itself, so a screen sent after a pop-up would cut it short.
        const auto screen = model.deviceScreen();
        if (force || screen.line1 != sentLine1 || screen.line2 != sentLine2 || screen.position != sentPosition)
        {
            sentLine1 = screen.line1;
            sentLine2 = screen.line2;
            sentPosition = screen.position;
            send(ml::scrollScreen(screen.line1, screen.line2, screen.position));
        }

        const auto& popup = model.popup();
        if (popup.serial != sentPopupSerial)
        {
            sentPopupSerial = popup.serial;
            if (! force && popup.pad)
                send(ml::padPopup(popup.name, popup.value)); // what the pad put you in (D186)
            else if (! force && (popup.fader || popup.control != 5)) // a page change shows on line 1 already
                send(ml::popup(popup.fader, popup.name, popup.value, popup.normalised));
        }

        for (int pad = 0; pad < 8; ++pad)
        {
            // The LEDs' own colours, so each pad lights as the screen shows it (D184).
            const auto light = model.padLight(pad, shiftHeld);
            const auto colour = coolsynth::ui::miniLabColour(light.tint).withMultipliedBrightness(light.brightness);
            auto& sent = sentPadColours[static_cast<std::size_t>(pad)];
            if (force || colour.getARGB() != sent)
            {
                sent = colour.getARGB();
                send(ml::padColour(pad, colour));
            }
        }
    }

    void MiniLab3Driver::timerCallback()
    {
        std::vector<juce::MidiMessage> pending;
        {
            const juce::ScopedLock lock(queueLock);
            pending.swap(queue);
        }
        for (const auto& message : pending)
            apply(message);

        if (! isConnected() && ++ticksSinceProbe >= 60) // look for the device every two seconds
        {
            ticksSinceProbe = 0;
            tryOpen();
        }
        else if (isConnected())
        {
            // unplugged: the port is gone from the list
            bool present = false;
            if (++ticksSinceProbe >= 60)
            {
                ticksSinceProbe = 0;
                for (const auto& info : juce::MidiInput::getAvailableDevices())
                    present = present || info.identifier == inputIdentifier;
                if (! present)
                {
                    // the device is gone: drop the ports without talking to it
                    coolsynth::diag::record(coolsynth::diag::Event::miniLab, 0, 0, 0.0f, 0.0f, "unplugged");
                    input = nullptr;
                    output = nullptr;
                    inputIdentifier = {};
                    model.setHardwareConnected(false);
                    return;
                }
            }
            refreshDevice(false);
        }
        // The screen greys its own Hold and octave out while the MiniLab is there (D184).
        model.setHardwareConnected(isConnected());
    }
}
