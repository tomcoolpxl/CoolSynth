#include "MiniLab3Driver.h"

#include "diagnostics/FlightRecorder.h"
#include "interface/InterfaceModel.h"
#include "midi/ControllerSetup.h"
#include "midi/MiniLab3Protocol.h"
#include "plugin/SynthAudioProcessor.h"

namespace coolsynth::ui
{
    namespace ml = coolsynth::midi::minilab3;

    juce::InterProcessLock& MiniLab3Driver::programLock()
    {
        static juce::InterProcessLock lock("CoolSynth MiniLab 3");
        return lock;
    }

    MiniLab3Driver::MiniLab3Driver(InterfaceModel& m, SynthAudioProcessor& processor, juce::AudioDeviceManager* deviceManager)
        : model(m), synth(processor), devices(deviceManager), inPlugin(deviceManager == nullptr)
    {
        // The first CoolSynth in this program takes the MiniLab, if no other program has it (D211).
        if (owner == nullptr && takeDevice())
            tryOpen();
        startTimerHz(30);
    }

    MiniLab3Driver::~MiniLab3Driver()
    {
        stopTimer();
        if (owner == this)
        {
            close(true);
            owner = nullptr;
            pluginDriving.store(false, std::memory_order_relaxed);
            coolsynth::midi::ControllerSetup::instance().setMiniLabDriven(false);
            if (programHoldsLock)
            {
                programLock().exit();
                programHoldsLock = false;
            }
        }
    }

    bool MiniLab3Driver::takeDevice()
    {
        // Between programs: whoever took it first keeps it until it closes. In this program the lock stays taken
        // while any driver here owns the MiniLab.
        if (! programHoldsLock)
        {
            if (! programLock().enter(0))
                return false;
            programHoldsLock = true;
        }
        owner = this;
        return true;
    }

    void MiniLab3Driver::claim()
    {
        if (owner == this)
            return;
        if (owner != nullptr)
            owner->letGo();
        if (takeDevice())
        {
            ticksSinceProbe = 0;
            tryOpen();
        }
    }

    void MiniLab3Driver::letGo()
    {
        // Another CoolSynth here takes over: the MiniLab stays in its DAW program, the new owner talks to it at once.
        close(false);
        if (owner == this)
            owner = nullptr;
        setConnected(false);
    }

    void MiniLab3Driver::tryOpen()
    {
        if (owner != this || isConnected())
            return;
        close(false);

        juce::MidiDeviceInfo inInfo, outInfo;
        for (const auto& info : juce::MidiInput::getAvailableDevices())
            if (ml::isDawPort(info.name))
                inInfo = info;
        for (const auto& info : juce::MidiOutput::getAvailableDevices())
            if (ml::isDawPort(info.name))
                outInfo = info;
        if (inInfo.identifier.isEmpty() || outInfo.identifier.isEmpty())
            return;

        // In the app the driver owns the port: the player must not also hear it (pads would play notes).
        if (devices != nullptr && devices->isMidiInputDeviceEnabled(inInfo.identifier))
            devices->setMidiInputDeviceEnabled(inInfo.identifier, false);

        input = juce::MidiInput::openDevice(inInfo.identifier, this);
        output = juce::MidiOutput::openDevice(outInfo.identifier);
        if (input == nullptr || output == nullptr)
        {
            // Another program has the port and Windows does not share it (no Windows MIDI Services): try again later.
            close(false);
            return;
        }
        inputIdentifier = inInfo.identifier;
        input->start();
        send(ml::connect());
        coolsynth::diag::record(coolsynth::diag::Event::miniLab, 0, 0, 0.0f, 0.0f, inPlugin ? "connected in a plugin, DAW program"
                                                                                           : "connected, DAW program");
        lastKnobValue.fill(-1);
        refreshDevice(true);
        setConnected(true);
    }

    void MiniLab3Driver::close(bool sayGoodbye)
    {
        if (input != nullptr)
            input->stop();
        if (output != nullptr && sayGoodbye)
            output->sendMessageNow(ml::disconnect());
        input = nullptr;
        output = nullptr;
        inputIdentifier = {};
    }

    void MiniLab3Driver::setConnected(bool connected)
    {
        if (connected == wasConnected)
            return;
        wasConnected = connected;
        model.setHardwareConnected(connected);
        if (inPlugin)
            pluginDriving.store(connected, std::memory_order_relaxed);
        auto& setup = coolsynth::midi::ControllerSetup::instance();
        setup.setMiniLabDriven(connected);
        // A DAW's own MiniLab script would talk to it too, and the two screens would fight (D211).
        if (connected && inPlugin)
            setup.announce("MiniLab 3 connected. In your DAW, set its MiniLab control surface to None.");
    }

    void MiniLab3Driver::send(const juce::MidiMessage& message)
    {
        if (output != nullptr)
            output->sendMessageNow(message);
    }

    void MiniLab3Driver::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
    {
        // MIDI thread: in the app keys and performance go straight to the sound; in a plugin the host brings them.
        // Controls go to the message thread.
        if (ml::decode(message).kind == ml::Control::Kind::other)
        {
            if (message.isSysEx())
                return;
            if (message.isNoteOn())
                lastKeyNote.store(message.getNoteNumber(), std::memory_order_relaxed);
            if (inPlugin)
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
                // The knobs turn endlessly, like a scroll wheel, but report a count, 0-127: each change is that many
                // clicks. The count stops at its ends, and turning on past one sends the end again (seen on the
                // device, D200): that is one more click, so a knob never runs out.
                auto& last = lastKnobValue[static_cast<std::size_t>(control.index)];
                int steps = last < 0 ? 0 : control.value - last;
                if (steps == 0 && last >= 0 && (control.value == 127 || control.value == 0))
                    steps = control.value == 127 ? 1 : -1;
                last = control.value;
                if (steps == 0)
                    break;
                // A switch or a list moves a step per few counts, a number per count (D200).
                model.turnKnobFromDevice(control.index + 1, steps, shiftHeld);
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

        const auto screen = model.deviceScreen();
        const auto& popup = model.popup();
        const bool newPopup = popup.serial != sentPopupSerial;
        const auto now = juce::Time::getMillisecondCounter();
        bool resend = force;

        // A pad flashes what it put you in on the full screen for a second (D186, D191): firmware 1.2.0 shows a
        // pop-up only while a control moves, so a pad's pop-up came through only while keys were played. Meanwhile
        // the scroll screen waits; a knob or fader, or any other change of the screen, ends the flash at once.
        if (newPopup && popup.pad && ! force)
        {
            sentPopupSerial = popup.serial;
            flashing = true;
            flashUntil = now + padFlashMilliseconds;
            flashLine1 = screen.line1;
            flashLine2 = screen.line2;
            flashPosition = screen.position;
            send(ml::fullScreen(popup.name, popup.value));
        }
        else if (flashing && (force || (newPopup && ! popup.pad) || static_cast<juce::int32>(now - flashUntil) >= 0
                              || screen.line1 != flashLine1 || screen.line2 != flashLine2 || screen.position != flashPosition))
        {
            flashing = false;
            resend = true; // the scroll screen comes back
        }

        // The screen first, then any new pop-up on top of it: the device shows the pop-up for a moment and
        // falls back to the screen by itself, so a screen sent after a pop-up would cut it short.
        if (! flashing && (resend || screen.line1 != sentLine1 || screen.line2 != sentLine2 || screen.position != sentPosition))
        {
            sentLine1 = screen.line1;
            sentLine2 = screen.line2;
            sentPosition = screen.position;
            send(ml::scrollScreen(screen.line1, screen.line2, screen.position));
        }

        if (popup.serial != sentPopupSerial)
        {
            sentPopupSerial = popup.serial;
            if (! force && ! popup.pad && (popup.fader || popup.control != 5)) // a page change shows on line 1 already
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
        if (owner != this)
        {
            // Another CoolSynth drives it, or nobody: the first one to look takes it, every two seconds (D211).
            if (owner == nullptr && ++ticksSinceProbe >= 60)
            {
                ticksSinceProbe = 0;
                if (takeDevice())
                    tryOpen();
            }
            return;
        }
        for (const auto& message : pending)
            apply(message);
        if (const int note = lastKeyNote.exchange(-1, std::memory_order_relaxed); note >= 0)
            model.keyPlayedOnMiniLab(note);

        if (! isConnected())
        {
            if (++ticksSinceProbe >= 60) // look for the device every two seconds
            {
                ticksSinceProbe = 0;
                tryOpen();
            }
        }
        else
        {
            // unplugged: the port is gone from the list
            if (++ticksSinceProbe >= 60)
            {
                ticksSinceProbe = 0;
                bool present = false;
                for (const auto& info : juce::MidiInput::getAvailableDevices())
                    present = present || info.identifier == inputIdentifier;
                if (! present)
                {
                    // the device is gone: drop the ports without talking to it
                    coolsynth::diag::record(coolsynth::diag::Event::miniLab, 0, 0, 0.0f, 0.0f, "unplugged");
                    input = nullptr;
                    output = nullptr;
                    inputIdentifier = {};
                    setConnected(false);
                    return;
                }
            }
            refreshDevice(false);
        }
        // The screen greys its own Hold and octave out while the MiniLab is there (D184).
        setConnected(isConnected());
    }
}
