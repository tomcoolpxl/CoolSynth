#include "ControllerSetup.h"

#include <algorithm>

#include <juce_audio_devices/juce_audio_devices.h>

namespace coolsynth::midi
{
    std::array<std::atomic<std::uint64_t>, 2 * 16 * 2> ControllerSetup::linked {};
    std::atomic<bool> ControllerSetup::learningNow { false };

    std::optional<ControllerEvent> toControllerEvent(const juce::MidiMessage& message) noexcept
    {
        ControllerEvent event;
        if (message.isNoteOnOrOff())
        {
            event.control = { ControlId::Kind::note, message.getChannel(), message.getNoteNumber() };
            event.value = message.isNoteOn() ? message.getVelocity() : 0;
            event.note = true;
            return event;
        }
        if (message.isController())
        {
            event.control = { ControlId::Kind::cc, message.getChannel(), message.getControllerNumber() };
            event.value = message.getControllerValue();
            return event;
        }
        return std::nullopt;
    }

    ControllerSetup& ControllerSetup::instance()
    {
        static ControllerSetup setup;
        return setup;
    }

    juce::File ControllerSetup::file() const
    {
        if (testFile != juce::File())
            return testFile;
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("CoolSynth").getChildFile("controller.json");
    }

    void ControllerSetup::start()
    {
        if (started || testing)
            return;
        started = true;
        readFile();
        // Known controllers are looked for when Windows says the MIDI devices changed, not every few seconds (D216);
        // the first look comes a moment later, once the app's MiniLab driver is there (D184).
        deviceList = juce::MidiDeviceListConnection::make([this] { lookAgain.store(true, std::memory_order_relaxed); });
        lookAgain.store(true, std::memory_order_relaxed);
        fileTimer = std::make_unique<juce::TimedCallback>([this] { tick(); });
        fileTimer->startTimer(2000);
    }

    void ControllerSetup::shutDown()
    {
        stopAllLearning();
        fileTimer.reset();
        learnTimer.reset();
        deviceList.reset();
        started = false;
    }

    void ControllerSetup::useForTesting(ControllerMap map)
    {
        testing = true;
        started = false;
        testFile = juce::File();
        driverOn = true;
        keys = 0;
        fileTimer.reset();
        current = std::move(map);
        currentOrigin = current.empty() ? Origin::none : Origin::saved;
        publish();
    }

    void ControllerSetup::useFileForTesting(const juce::File& f)
    {
        fileTimer.reset();
        testing = false;
        started = true; // start() makes no timers now
        testFile = f;
        readFile();
    }

    void ControllerSetup::setMap(ControllerMap map)
    {
        current = std::move(map);
        currentOrigin = current.empty() ? Origin::none : Origin::saved;
        foundProfile = {};
        save();
        publish();
        if (current.empty() && ! testing)
            lookForKnownController();
    }

    void ControllerSetup::setMiniLabDriverOn(bool on)
    {
        if (on == driverOn)
            return;
        driverOn = on;
        // A found setup stays found; a saved one stays saved: only the switch is written with it.
        save();
        ++changes;
    }

    void ControllerSetup::setKeyboardKeys(int count)
    {
        if (std::find(keyboardSizes.begin(), keyboardSizes.end(), count) == keyboardSizes.end() || count == keys)
            return;
        keys = count;
        save();
        ++changes;
    }

    void ControllerSetup::save()
    {
        if (testing)
            return;
        const auto f = file();
        const bool saved = currentOrigin == Origin::saved;
        if (! saved && driverOn && keys == 0)
            f.deleteFile(); // nothing of the player's own to keep
        else
        {
            auto value = saved ? current.toVar() : ControllerMap {}.toVar();
            if (auto* object = value.getDynamicObject())
            {
                object->setProperty("miniLabDriver", driverOn);
                if (keys != 0)
                    object->setProperty("keyboardKeys", keys);
            }
            f.getParentDirectory().createDirectory();
            f.replaceWithText(juce::JSON::toString(value));
        }
        fileTime = f.getLastModificationTime();
    }

    void ControllerSetup::readFile()
    {
        const auto f = file();
        fileTime = f.getLastModificationTime();
        if (! f.existsAsFile())
        {
            if (! driverOn || keys != 0)
            {
                driverOn = true;
                keys = 0;
                ++changes;
            }
            if (currentOrigin == Origin::saved)
            {
                current = {};
                currentOrigin = Origin::none;
                publish();
            }
            return;
        }
        const auto value = juce::JSON::parse(f);
        if (auto* object = value.getDynamicObject())
        {
            driverOn = ! object->hasProperty("miniLabDriver") || static_cast<bool>(object->getProperty("miniLabDriver"));
            // A size CoolSynth does not offer (a hand-edited file) is no size.
            const int read = static_cast<int>(object->getProperty("keyboardKeys"));
            keys = std::find(keyboardSizes.begin(), keyboardSizes.end(), read) != keyboardSizes.end() ? read : 0;
        }
        auto map = ControllerMap::fromVar(value);
        if (map.empty() && currentOrigin == Origin::found)
        {
            ++changes; // only the switch or the keyboard's size changed: a found setup stays in use
            return;
        }
        current = std::move(map);
        currentOrigin = current.empty() ? Origin::none : Origin::saved;
        foundProfile = {};
        publish();
    }

    void ControllerSetup::lookForKnownController()
    {
        if (currentOrigin == Origin::saved)
            return;
        // The MiniLab 3 with CoolSynth's own driver needs no profile: the driver does more (D184).
        const bool driven = miniLabDriven.load(std::memory_order_relaxed);
        juce::StringArray names;
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            if (! (driven && (input.name.containsIgnoreCase("minilab3") || input.name.containsIgnoreCase("minilab 3"))))
                names.add(input.name);
        const auto* profile = readyProfileForPorts(names);
        const juce::String id = profile != nullptr ? profile->id : juce::String();
        if (id == foundProfile)
            return;
        foundProfile = id;
        if (profile == nullptr)
        {
            current = {};
            currentOrigin = Origin::none;
        }
        else
        {
            current = profile->map;
            currentOrigin = Origin::found;
            notice = profile->name + " is set up for CoolSynth. To change it: Prefs > Controller setup.";
            ++notices;
        }
        publish();
    }

    void ControllerSetup::tick()
    {
        // Another program (the app, or a host) may have saved the setup: read it again.
        if (file().getLastModificationTime() != fileTime)
            readFile();
        // A known controller plugged in or out, or the MiniLab driver taking or leaving the MiniLab, while nothing is set up.
        if (lookAgain.exchange(false, std::memory_order_relaxed) && currentOrigin != Origin::saved)
            lookForKnownController();
    }

    void ControllerSetup::publish()
    {
        std::array<std::uint64_t, 2 * 16 * 2> bits {};
        auto mark = [&bits](const ControlId& c)
        {
            for (int channel = 1; channel <= 16; ++channel)
                if (c.channel == 0 || c.channel == channel)
                {
                    const int word = ((c.kind == ControlId::Kind::note ? 1 : 0) * 16 + (channel - 1)) * 2 + c.number / 64;
                    bits[static_cast<std::size_t>(word)] |= std::uint64_t { 1 } << (c.number % 64);
                }
        };
        for (const auto& link : current.places)
            mark(link.control);
        for (const auto& link : current.settings)
            mark(link.control);
        for (std::size_t i = 0; i < bits.size(); ++i)
            linked[i].store(bits[i], std::memory_order_relaxed);
        ++changes;
    }

    bool ControllerSetup::isLinked(const ControlId& c) noexcept
    {
        if (c.channel < 1 || c.channel > 16 || c.number < 0 || c.number > 127)
            return false;
        const int word = ((c.kind == ControlId::Kind::note ? 1 : 0) * 16 + (c.channel - 1)) * 2 + c.number / 64;
        return (linked[static_cast<std::size_t>(word)].load(std::memory_order_relaxed) >> (c.number % 64)) & 1u;
    }

    bool ControllerSetup::takesFromSound(const juce::MidiMessage& message) noexcept
    {
        if (message.isNoteOff() || (! message.isNoteOn() && ! message.isController()))
            return false;
        const auto event = toControllerEvent(message);
        if (! event || isReserved(event->control))
            return false;
        return learningNow.load(std::memory_order_relaxed) || isLinked(event->control);
    }

    bool ControllerSetup::wanted(const juce::MidiMessage& message) noexcept
    {
        const auto event = toControllerEvent(message);
        if (! event || isReserved(event->control))
            return false;
        if (learningNow.load(std::memory_order_relaxed))
            return ! (event->note && event->value == 0);
        return isLinked(event->control);
    }

    // ---- Learning ---------------------------------------------------------------------------------------------

    int ControllerSetup::learn(const void* owner, Expect expect, std::function<void(const Heard&)> done,
                               std::function<void(const juce::String&)> progress)
    {
        expecting = expect;
        learningOwner = owner;
        onHeard = std::move(done);
        onProgress = std::move(progress);
        candidate.reset();
        values.clear();
        learning.store(true, std::memory_order_relaxed);
        learningNow.store(true, std::memory_order_relaxed);
        if (learnTimer == nullptr)
            learnTimer = std::make_unique<juce::TimedCallback>([this] { finishLearning(); });
        learnTimer->startTimer(50);
        return ++currentToken;
    }

    void ControllerSetup::stopLearning(int token)
    {
        // Only the learning still listening: an older one's window closing must not end a newer one (review of D211).
        if (token == currentToken)
            stopAllLearning();
    }

    void ControllerSetup::stopAllLearning()
    {
        learning.store(false, std::memory_order_relaxed);
        learningNow.store(false, std::memory_order_relaxed);
        if (learnTimer != nullptr)
            learnTimer->stopTimer();
        learningOwner = nullptr;
        onHeard = nullptr;
        onProgress = nullptr;
        candidate.reset();
        values.clear();
    }

    void ControllerSetup::hear(const ControllerEvent& event, const void* source)
    {
        // Only the controller of the CoolSynth that asked: a host feeding two CoolSynths would bring each move twice.
        if (! isLearning() || source != learningOwner || isReserved(event.control))
            return;
        if (event.note)
        {
            if (event.value == 0)
                return; // a release says nothing new
            // A key or a pad: a button, at once.
            auto done = onHeard;
            stopAllLearning();
            if (done)
                done({ event.control, Encoding::absolute, true });
            return;
        }
        if (! candidate.has_value() || *candidate != event.control)
        {
            candidate = event.control;
            values.clear();
        }
        values.push_back(event.value);
        lastHeardMs = juce::Time::getMillisecondCounterHiRes();
        if (onProgress && expecting == Expect::turn)
            onProgress("Heard " + event.control.describe() + ". Now the other way, then let go.");
    }

    void ControllerSetup::finishLearning(bool rested)
    {
        if (! isLearning() || ! candidate.has_value() || values.empty())
            return;
        const double quiet = rested ? 1.0e9 : juce::Time::getMillisecondCounterHiRes() - lastHeardMs;
        // A button sends 127 down and 0 up (or one of them); a turn sends values that go somewhere.
        const bool buttonLike = values.size() <= 2
                             && std::all_of(values.begin(), values.end(), [](int v) { return v == 0 || v == 127; });
        Heard heard;
        heard.control = *candidate;
        if (expecting == Expect::press)
        {
            if (quiet < 250.0)
                return;
            if (! buttonLike)
            {
                // A knob or fader touched while a button was wanted: not taken (review of D211).
                candidate.reset();
                values.clear();
                if (onProgress)
                    onProgress("That was a knob or fader: press a button or pad.");
                return;
            }
            heard.button = true;
        }
        else
        {
            if (quiet < 900.0)
                return;
            if (values.size() < 4 && ! buttonLike)
            {
                if (onProgress)
                    onProgress("Heard " + candidate->describe() + ". Turn it a little more, right then left.");
                return;
            }
            heard.button = buttonLike && values.size() <= 2;
            heard.encoding = heard.button ? Encoding::absolute : guessEncoding(values);
        }
        auto done = onHeard;
        stopAllLearning();
        if (done)
            done(heard);
    }
}
