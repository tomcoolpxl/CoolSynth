#include "CoolSynthTools.h"

#include <algorithm>

#include <juce_gui_basics/juce_gui_basics.h>

#include "diagnostics/FlightRecorder.h"
#include "interface/InterfaceModel.h"
#include "parameters/v3/PartParameters.h"
#include "parameters/v3/SlotParameters.h"
#include "plugin/PanelEditor.h"
#include "plugin/SynthAudioProcessor.h"
#include "presets/FactoryPresets.h"
#include "synth/v3/UserLibrary.h"

namespace coolsynth::mcp
{
    namespace
    {
        namespace pv3 = coolsynth::parameters::v3;

        juce::var object(std::initializer_list<std::pair<const char*, juce::var>> properties)
        {
            auto* o = new juce::DynamicObject();
            for (const auto& [key, value] : properties)
                o->setProperty(key, value);
            return juce::var(o);
        }

        /** The panel in the app's window, wherever it sits among the window's components (D227). */
        PanelEditor* findPanel(juce::Component* component)
        {
            if (component == nullptr)
                return nullptr;
            if (auto* panel = dynamic_cast<PanelEditor*>(component))
                return panel;
            for (auto* child : component->getChildren())
                if (auto* panel = findPanel(child))
                    return panel;
            return nullptr;
        }

        juce::RangedAudioParameter* parameter(SynthAudioProcessor& synth, const juce::String& id)
        {
            return synth.getValueTreeState().getParameter(id);
        }

        juce::String valueOf(SynthAudioProcessor& synth, const juce::String& id)
        {
            if (auto* p = parameter(synth, id))
                return p->getCurrentValueAsText();
            return {};
        }

        /** One slot as the screen shows it: its type and, when it is not Off, every value of its pages by name. */
        juce::var describeSlot(SynthAudioProcessor& synth, const pv3::SlotPath& path)
        {
            const auto prefix = pv3::slotPrefix(path);
            const auto type = valueOf(synth, pv3::typeParameterId(path));
            auto slot = object({ { "slot", prefix }, { "type", type } });
            if (type.isEmpty() || type == "Off")
                return slot;
            juce::Array<juce::var> values;
            if (auto* first = dynamic_cast<pv3::GenericSlotParameter*>(parameter(synth, pv3::genericParameterId(path, 1))))
            {
                const auto& d = first->currentDescriptor();
                for (int i = 0; i < d.usedParams; ++i)
                {
                    const auto id = pv3::genericParameterId(path, i + 1);
                    values.add(object({ { "id", id }, { "name", juce::String(d.params[static_cast<std::size_t>(i)].name) },
                                        { "value", valueOf(synth, id) } }));
                }
            }
            if (path.kind == pv3::SlotKind::generator)
            {
                for (const char* fixed : { "level", "pan", "oct", "fine" })
                    if (auto* p = parameter(synth, pv3::fixedParameterId(path, fixed)))
                        values.add(object({ { "id", pv3::fixedParameterId(path, fixed) }, { "name", p->getName(32) },
                                            { "value", p->getCurrentValueAsText() } }));
                const auto file = synth.getUserFile(path.part == pv3::Part::a ? 0 : 1, path.index);
                if (file.isNotEmpty())
                {
                    slot.getDynamicObject()->setProperty("file", file);
                    slot.getDynamicObject()->setProperty("fileStatus", synth.getUserFileStatus(path.part == pv3::Part::a ? 0 : 1, path.index));
                }
            }
            slot.getDynamicObject()->setProperty("values", values);
            return slot;
        }

        juce::var describeSound(SynthAudioProcessor& synth)
        {
            juce::Array<juce::var> layers;
            for (const auto part : { pv3::Part::a, pv3::Part::b })
            {
                const juce::String p = part == pv3::Part::a ? "pA" : "pB";
                juce::Array<juce::var> slots;
                for (int i = 1; i <= pv3::partGeneratorSlots; ++i)
                    slots.add(describeSlot(synth, { part, pv3::SlotKind::generator, i }));
                for (int i = 1; i <= pv3::partProcessorSlots; ++i)
                    slots.add(describeSlot(synth, { part, pv3::SlotKind::processor, i }));
                for (int i = 1; i <= pv3::partInsertSlots; ++i)
                    slots.add(describeSlot(synth, { part, pv3::SlotKind::insert, i }));
                layers.add(object({ { "layer", part == pv3::Part::a ? "A" : "B" },
                                    { "playMode", valueOf(synth, p + ".play.mode") },
                                    { "voices", valueOf(synth, p + ".play.voices") },
                                    { "level", valueOf(synth, p + ".mix.level") },
                                    { "slots", slots } }));
            }
            juce::Array<juce::var> master;
            for (int i = 1; i <= coolsynth::synth::v3::masterSlotCount; ++i)
                master.add(describeSlot(synth, { pv3::Part::a, pv3::SlotKind::master, i }));
            return object({ { "sound", synth.getPatchName() }, { "edited", synth.isPatchDirty() },
                            { "layerMode", valueOf(synth, pv3::partModeParameterId) },
                            { "layers", layers }, { "master", master },
                            { "voicesPlaying", synth.voicesPlaying() }, { "voiceLimit", synth.voiceLimit() } });
        }

        void set(juce::RangedAudioParameter& p, float normalised)
        {
            p.beginChangeGesture();
            p.setValueNotifyingHost(normalised);
            p.endChangeGesture();
        }

        /** The user's files and kits, as the Sample list shows them (InterfaceModel::refreshUserEntries). */
        juce::StringArray userEntries()
        {
            const auto folder = coolsynth::synth::v3::UserLibrary::instance().folder();
            juce::StringArray entries;
            for (const auto& file : folder.findChildFiles(juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac"))
            {
                entries.add(file.getRelativePathFrom(folder).replaceCharacter('\\', '/'));
                if (const auto parent = file.getParentDirectory(); parent != folder)
                    entries.addIfNotAlreadyThere(parent.getRelativePathFrom(folder).replaceCharacter('\\', '/') + "/ (a kit)");
            }
            entries.sortNatural();
            return entries;
        }

        juce::String noteName(int note)
        {
            return juce::MidiMessage::getMidiNoteName(note, true, true, 4);
        }
    }

    std::vector<Tool> coolSynthTools(SynthAudioProcessor& synth, std::function<juce::Component*()> window, Server::Runner runner,
                                     std::function<void(const juce::MidiMessage&)> sendMidi)
    {
        std::vector<Tool> tools;

        tools.push_back({ "get_sound",
                          "The sound playing now: its name, both layers (play mode, voices, level) with every slot (G1-G3 "
                          "generators, P1-P3 processors, I1-I6 effects) and the master effects F1-F6; each slot's type and its "
                          "values by name, as the screen shows them, with each value's id for set_values. A Sample slot also "
                          "names its file.",
                          objectSchema({}),
                          [&synth](const juce::var&) { return textResult(juce::JSON::toString(describeSound(synth), false)); } });

        tools.push_back({ "list_parameters",
                          "Every parameter whose id starts with a prefix (e.g. 'pA.play', 'pA.g1', 'f2', 'pA.arp'): id, name and "
                          "value as the screen shows it.",
                          objectSchema({ { "prefix", typeSchema("string", "The start of the ids, e.g. 'pA.mix'; empty lists all.") } }),
                          [&synth](const juce::var& args)
                          {
                              const auto prefix = args["prefix"].toString();
                              juce::StringArray lines;
                              for (auto* p : synth.getParameters())
                                  if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
                                      if (withId->paramID.startsWith(prefix))
                                          lines.add(withId->paramID + " | " + p->getName(40) + " | " + p->getCurrentValueAsText());
                              return textResult(lines.isEmpty() ? juce::String("No parameter starts with '" + prefix + "'.") : lines.joinIntoString("\n"));
                          } });

        tools.push_back({ "set_values",
                          "Sets parameters by id to values written as the screen shows them ('Mono', '21 %', '-6.0 dB', 'On'), "
                          "each as one undoable change. Returns each value as it then is.",
                          objectSchema({ { "values", typeSchema("object", "id to value, e.g. {\"pA.play.mode\": \"Mono\", \"pA.g1.p2\": \"0 %\"}") } },
                                       { "values" }),
                          [&synth](const juce::var& args)
                          {
                              juce::StringArray lines;
                              bool failed = false;
                              if (auto* values = args["values"].getDynamicObject())
                                  for (const auto& property : values->getProperties())
                                  {
                                      const auto id = property.name.toString();
                                      if (auto* p = parameter(synth, id))
                                      {
                                          set(*p, p->getValueForText(property.value.toString()));
                                          lines.add(id + " = " + p->getCurrentValueAsText());
                                      }
                                      else
                                      {
                                          failed = true;
                                          lines.add(id + ": no such parameter");
                                      }
                                  }
                              return textResult(lines.isEmpty() ? juce::String("Nothing to set.") : lines.joinIntoString("\n"), failed);
                          } });

        tools.push_back({ "set_slot_type",
                          "Gives a slot a type (e.g. slot 'pA.g1', type 'Sample'), with that type's own defaults, as the panel's "
                          "type list does. Types: generators Off, VA, Noise, FM2, FM4, Wavetable, String, Modal, Sample, Grain, Chip, Kick, TB-303, 808 Drums, 808 Metal; "
                          "processors Off, Ladder, SVF, Crusher, Folder, Drive, Formant, Comb; effects by their names.",
                          objectSchema({ { "slot", typeSchema("string", "e.g. 'pA.g1', 'pB.p2', 'pA.i3', 'f1'") },
                                         { "type", typeSchema("string", "The type's name as the screen shows it") } },
                                       { "slot", "type" }),
                          [&synth](const juce::var& args)
                          {
                              const auto prefix = args["slot"].toString();
                              auto* type = parameter(synth, prefix + ".type");
                              if (type == nullptr)
                                  return textResult("No slot " + prefix, true);
                              synth.performUndoableEdit("Type " + prefix, [&]
                              {
                                  set(*type, type->getValueForText(args["type"].toString()));
                                  for (int i = 1; i <= pv3::genericParameterCount; ++i)
                                      if (auto* generic = dynamic_cast<pv3::GenericSlotParameter*>(parameter(synth, prefix + ".p" + juce::String(i))))
                                          set(*generic, generic->currentTypeDefault());
                              });
                              return textResult(prefix + " is now " + type->getCurrentValueAsText() + ", at its defaults.");
                          } });

        tools.push_back({ "list_presets",
                          "The factory presets: index, room, family and name, optionally only one room's.",
                          objectSchema({ { "room", typeSchema("string", "A room's name, e.g. 'Night Bus'; empty lists all.") } }),
                          [](const juce::var& args)
                          {
                              const auto room = args["room"].toString();
                              juce::StringArray lines;
                              for (int i = 0; i < coolsynth::presets::getFactoryPresetCount(); ++i)
                              {
                                  const auto& p = coolsynth::presets::getFactoryPreset(i);
                                  if (room.isEmpty() || p.category.equalsIgnoreCase(room))
                                      lines.add(juce::String(i) + " | " + p.category + (p.section.isNotEmpty() ? " | " + p.section : juce::String()) + " | " + p.name);
                              }
                              return textResult(lines.joinIntoString("\n"));
                          } });

        tools.push_back({ "load_preset",
                          "Loads a factory preset by its index (from list_presets) or its name (the first of that name).",
                          objectSchema({ { "index", typeSchema("integer", "The preset's index") },
                                         { "name", typeSchema("string", "Or its name") } }),
                          [&synth](const juce::var& args)
                          {
                              int index = args.hasProperty("index") ? static_cast<int>(args["index"]) : -1;
                              if (index < 0)
                                  for (int i = 0; i < coolsynth::presets::getFactoryPresetCount() && index < 0; ++i)
                                      if (coolsynth::presets::getFactoryPreset(i).name.equalsIgnoreCase(args["name"].toString()))
                                          index = i;
                              if (index < 0 || index >= coolsynth::presets::getFactoryPresetCount())
                                  return textResult("No such preset.", true);
                              synth.setCurrentProgram(index);
                              // A sampled instrument is read in after the preset loads, and the audio thread raises the flag
                              // a block later: wait for it, so notes played next are heard (at most 20 s).
                              juce::Thread::sleep(80);
                              const auto started = juce::Time::getMillisecondCounter();
                              while (synth.isLoadingInstrument() && juce::Time::getMillisecondCounter() - started < 20000)
                                  juce::Thread::sleep(20);
                              const auto waited = juce::Time::getMillisecondCounter() - started;
                              return textResult("Loaded " + juce::String(index) + ": " + coolsynth::presets::getFactoryPreset(index).name
                                                + (waited >= 40 ? " (its samples took " + juce::String(waited / 1000.0, 1) + " s to load)" : juce::String())
                                                + (synth.isLoadingInstrument() ? "; still loading" : juce::String()));
                          } });

        tools.push_back({ "list_files",
                          "The player's own sound files and kits (folders) in the sample library's User folder, as the Sample "
                          "list offers them.",
                          objectSchema({}),
                          [](const juce::var&)
                          {
                              const auto entries = userEntries();
                              return textResult("In " + coolsynth::synth::v3::UserLibrary::instance().folder().getFullPathName() + ":\n"
                                                + (entries.isEmpty() ? juce::String("nothing yet") : entries.joinIntoString("\n")));
                          } });

        tools.push_back({ "set_file",
                          "Puts one of the player's files or kits (a name from list_files, without ' (a kit)') into a layer's "
                          "generator slot, which should be of type Sample (or Wavetable or Grain for a file); an empty file lets go.",
                          objectSchema({ { "layer", typeSchema("string", "'A' or 'B'") },
                                         { "slot", typeSchema("integer", "The generator slot, 1 to 3") },
                                         { "file", typeSchema("string", "e.g. 'CST1.wav' or the kit 'CST2'") } },
                                       { "layer", "slot", "file" }),
                          [&synth](const juce::var& args)
                          {
                              const int part = args["layer"].toString().equalsIgnoreCase("B") ? 1 : 0;
                              const int slot = juce::jlimit(1, pv3::partGeneratorSlots, static_cast<int>(args["slot"]));
                              synth.setUserFile(part, slot, args["file"].toString().upToFirstOccurrenceOf("/ (a kit)", false, false));
                              return textResult(juce::String(part == 0 ? "A" : "B") + " G" + juce::String(slot) + " now plays '"
                                                + synth.getUserFile(part, slot) + "'.");
                          } });

        tools.push_back({ "play",
                          "Plays notes (MIDI numbers, 60 is C4 here, the MiniLab's leftmost key at its normal octave is 48 = C3) "
                          "as if keys were held for a time, then let go, and tells the loudest output and the most voices meanwhile. "
                          "Controllers can move while the keys are held (the mod wheel is controller 1), and a trace gives the "
                          "loudest level of every 50 ms, to see when something starts or stops. Instead of notes, a sequence "
                          "plays keys at their own times, overlapping or not (a legato line, an accented 303 line).",
                          objectSchema({ { "notes", typeSchema("array", "MIDI note numbers, e.g. [48, 52, 55]") },
                                         { "velocity", typeSchema("integer", "1 to 127, 100 by default") },
                                         { "seconds", typeSchema("number", "How long the keys are held, 1 by default, at most 10") },
                                         { "release", typeSchema("number", "Seconds listened to after letting go, 0.5 by default, at most 10") },
                                         { "sequence", typeSchema("array", "Instead of notes: [{\"note\": 36, \"at\": 0.0, \"length\": 0.1, \"velocity\": 120}], "
                                                                              "at and length in seconds, velocity 100 by default; the last key up at most 20 s in") },
                                         { "controllers", typeSchema("array", "Moves while held: [{\"at\": 1.0, \"number\": 1, \"value\": 100}], "
                                                                                 "at in seconds from the keys going down, value 0 to 127") },
                                         { "trace", typeSchema("boolean", "Also the loudest level of every 50 ms, in dBFS") } }),
                          [&synth, runner, sendMidi](const juce::var& args)
                          {
                              // Every key as a press and a release in time; plain notes go down together and up at `seconds`.
                              struct Key { double at; int note; float velocity; bool down; };
                              std::vector<Key> events;
                              juce::Array<int> notes;
                              const float velocity = juce::jlimit(1, 127, args.hasProperty("velocity") ? static_cast<int>(args["velocity"]) : 100) / 127.0f;
                              double held = juce::jlimit(0.01, 10.0, args.hasProperty("seconds") ? static_cast<double>(args["seconds"]) : 1.0);
                              if (const auto* steps = args["sequence"].getArray())
                              {
                                  held = 0.0;
                                  for (const auto& step : *steps)
                                  {
                                      const int note = juce::jlimit(0, 127, static_cast<int>(step["note"]));
                                      const double at = juce::jlimit(0.0, 20.0, static_cast<double>(step["at"]));
                                      const double length = juce::jlimit(0.005, 20.0 - at, step.hasProperty("length") ? static_cast<double>(step["length"]) : 0.25);
                                      const float v = juce::jlimit(1, 127, step.hasProperty("velocity") ? static_cast<int>(step["velocity"]) : 100) / 127.0f;
                                      events.push_back({ at, note, v, true });
                                      events.push_back({ at + length, note, 0.5f, false });
                                      held = std::max(held, at + length);
                                      notes.addIfNotAlreadyThere(note);
                                  }
                              }
                              else if (const auto* list = args["notes"].getArray())
                              {
                                  for (const auto& n : *list)
                                      notes.add(juce::jlimit(0, 127, static_cast<int>(n)));
                                  for (const int n : notes)
                                      events.push_back({ 0.0, n, velocity, true });
                                  for (const int n : notes)
                                      events.push_back({ held, n, 0.5f, false });
                              }
                              if (notes.isEmpty())
                                  return textResult("No notes given.", true);
                              // In time order; at the same moment a release before a press, so a repeated key strikes again.
                              std::stable_sort(events.begin(), events.end(), [](const Key& a, const Key& b)
                                               { return a.at < b.at || (a.at == b.at && ! a.down && b.down); });
                              const double after = juce::jlimit(0.0, 10.0, args.hasProperty("release") ? static_cast<double>(args["release"]) : 0.5);
                              auto& keys = synth.getKeyboardState();
                              const auto onMessageThread = [&runner](std::function<void()> job) { if (runner) runner(std::move(job)); else job(); };
                              (void) synth.takeLoudest();
                              (void) synth.takeMostVoices();
                              struct Move { double at; int number, value; };
                              std::vector<Move> moves;
                              if (const auto* list = args["controllers"].getArray())
                                  for (const auto& m : *list)
                                      moves.push_back({ static_cast<double>(m["at"]), juce::jlimit(0, 127, static_cast<int>(m["number"])),
                                                        juce::jlimit(0, 127, static_cast<int>(m["value"])) });
                              std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) { return a.at < b.at; });
                              if (! moves.empty() && ! sendMidi)
                                  return textResult("No MIDI input to move controllers here.", true);
                              const bool trace = args.hasProperty("trace") && static_cast<bool>(args["trace"]);
                              juce::StringArray traceLines;
                              float loudHeld = 0.0f, loudAfter = 0.0f, window = 0.0f;
                              double nextWindow = 0.05;
                              std::size_t nextMove = 0;
                              const auto db = [](float g) { return g > 0.0f ? juce::String(juce::Decibels::gainToDecibels(g), 1) + " dBFS" : juce::String("silence"); };
                              // In 1 ms steps: due keys and controller moves, each 50 ms window's loudest; the last key up at `held`.
                              const double started = juce::Time::getMillisecondCounterHiRes() * 0.001;
                              std::size_t nextKey = 0;
                              bool keysUp = false;
                              for (;;)
                              {
                                  const double now = juce::Time::getMillisecondCounterHiRes() * 0.001 - started;
                                  if (nextKey < events.size() && events[nextKey].at <= now)
                                  {
                                      std::vector<Key> due;
                                      while (nextKey < events.size() && events[nextKey].at <= now)
                                          due.push_back(events[nextKey++]);
                                      onMessageThread([due, &keys]
                                                      {
                                                          for (const auto& k : due)
                                                              if (k.down) keys.noteOn(1, k.note, k.velocity);
                                                              else keys.noteOff(1, k.note, k.velocity);
                                                      });
                                  }
                                  while (nextMove < moves.size() && moves[nextMove].at <= now)
                                  {
                                      sendMidi(juce::MidiMessage::controllerEvent(1, moves[nextMove].number, moves[nextMove].value)
                                                   .withTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001));
                                      ++nextMove;
                                  }
                                  if (now >= nextWindow)
                                  {
                                      const float loud = synth.takeLoudest();
                                      window = loud;
                                      (keysUp ? loudAfter : loudHeld) = std::max(keysUp ? loudAfter : loudHeld, loud);
                                      if (trace)
                                          traceLines.add(juce::String(nextWindow, 2) + " s " + db(window));
                                      nextWindow += 0.05;
                                  }
                                  if (! keysUp && nextKey >= events.size())
                                      keysUp = true;
                                  if (keysUp && now >= held + after)
                                      break;
                                  juce::Thread::sleep(1);
                              }
                              (void) synth.takeLoudest();
                              const int voices = synth.takeMostVoices();
                              juce::StringArray names;
                              for (const int n : notes)
                                  names.add(noteName(n) + " (" + juce::String(n) + ")");
                              return textResult("Played " + names.joinIntoString(", ") + " for " + juce::String(held, 2) + " s: loudest while held "
                                                + db(loudHeld) + ", after letting go " + db(loudAfter) + ", at most " + juce::String(voices) + " voices."
                                                + (trace ? "\nEvery 50 ms:\n" + traceLines.joinIntoString("\n") : juce::String()));
                          },
                          true });

        tools.push_back({ "panel",
                          "Works the panel as the MiniLab or the mouse does, to bring a page into view or to try a control: press "
                          "a pad (1-8, as the screen numbers them from the left: 1 is Home), turn a knob (1-8; 5 turns the "
                          "pages) or the main knob by steps, or press a knob or the main knob. Take a screenshot to see the result.",
                          objectSchema({ { "control", typeSchema("string", "'pad', 'knob', 'main' (the big knob under the screen)") },
                                         { "number", typeSchema("integer", "The pad (1-8) or knob (1-8); not for 'main'") },
                                         { "action", typeSchema("string", "'press' or 'turn'") },
                                         { "steps", typeSchema("integer", "For 'turn': steps, negative turns left; 1 by default") },
                                         { "shift", typeSchema("boolean", "A pad pressed with Shift held") } },
                                       { "control", "action" }),
                          [&synth](const juce::var& args)
                          {
                              auto& model = synth.getInterfaceModel();
                              const auto control = args["control"].toString();
                              const auto action = args["action"].toString();
                              const int number = static_cast<int>(args["number"]);
                              const int steps = args.hasProperty("steps") ? static_cast<int>(args["steps"]) : 1;
                              if (control == "pad" && action == "press" && number >= 1 && number <= 8)
                                  model.pressPad(number - 1, static_cast<bool>(args["shift"]));
                              else if (control == "knob" && action == "turn" && number == 5)
                                  model.turnPageKnob(steps);
                              else if (control == "knob" && action == "turn" && number >= 1 && number <= 8)
                                  model.turnKnob(number, steps, false);
                              else if (control == "knob" && action == "press" && number >= 1 && number <= 8)
                                  model.pressKnob(number);
                              else if (control == "main" && action == "turn")
                                  model.turnMain(steps);
                              else if (control == "main" && action == "press")
                                  model.pressMain();
                              else
                                  return textResult("Not a control: " + control + " " + juce::String(number) + " " + action, true);
                              return textResult("Done: " + control + (control == "main" ? juce::String() : " " + juce::String(number)) + " " + action
                                                + (action == "turn" ? " " + juce::String(steps) : juce::String()) + ".");
                          } });

        tools.push_back({ "screenshot",
                          "A picture of CoolSynth's window as the player sees it now, or of one part of the panel, enlarged: "
                          "for checking the drawing itself (text cut off, overlaps, colours). read_screen gives what it shows as text.",
                          objectSchema({ { "area", typeSchema("string", "Optional: 'main screen', 'layer screen', 'knobs', 'faders', 'pads', 'keys', "
                                                                        "'left', or 'x,y,w,h' in the 1100 x 680 panel; empty: the whole window") },
                                         { "zoom", typeSchema("number", "With an area: 1 to 3 times its size, 2 by default") } }),
                          [window](const juce::var& args)
                          {
                              auto* component = window ? window() : nullptr;
                              if (component == nullptr || component->getWidth() <= 0)
                                  return textResult("No window to picture.", true);
                              const auto area = args["area"].toString();
                              juce::Image image;
                              juce::String what;
                              if (area.isNotEmpty())
                              {
                                  auto* panel = findPanel(component);
                                  if (panel == nullptr)
                                      return textResult("No panel in the window.", true);
                                  juce::String error;
                                  const float zoom = args.hasProperty("zoom") ? static_cast<float>(static_cast<double>(args["zoom"])) : 2.0f;
                                  image = panel->pictureForMcp(area, zoom, error);
                                  if (! image.isValid())
                                      return textResult(error, true);
                                  what = "The panel's " + area + ", ";
                              }
                              else
                              {
                                  image = component->createComponentSnapshot(component->getLocalBounds(), true, 1.0f);
                                  what = "CoolSynth's window, ";
                              }
                              juce::MemoryOutputStream png;
                              juce::PNGImageFormat().writeImageToStream(image, png);
                              return imageResult(png.getMemoryBlock(), what + juce::String(image.getWidth()) + " x "
                                                                           + juce::String(image.getHeight()) + " pixels.");
                          } });

        tools.push_back({ "read_screen",
                          "The panel as text, as it is drawn now: the main screen (header, trail, list or slots, page tabs, the four "
                          "cards, the hint line), the main knob's roles, the layer screen (each layer's sound and chain, the info "
                          "row), knobs 1-8, faders, pads, buttons, the MiniLab's display, and the names the click tool takes. "
                          "Use it instead of a screenshot to see what the screen says.",
                          objectSchema({}),
                          [window](const juce::var&)
                          {
                              auto* panel = findPanel(window ? window() : nullptr);
                              if (panel == nullptr)
                                  return textResult("No panel to read: the app's window is closed or shows something else.", true);
                              return textResult(panel->describeForMcp());
                          } });

        tools.push_back({ "click",
                          "Clicks a part of the panel by the name read_screen gives it, as the mouse does: a list row, a page tab, "
                          "a slot, a card, a layer's name, a block of a layer's chain, a pad, a knob, Hold or Oct, or moves a fader "
                          "to a value. Answers with the panel as text afterwards.",
                          objectSchema({ { "name", typeSchema("string", "e.g. 'screen.4', 'card.1', 'layer.B', 'chain.A.P1', 'pad.2', 'knob.5', "
                                                                        "'hold', 'octave.up', 'fader.4'") },
                                         { "value", typeSchema("number", "For a fader: 0 to 1 of its travel; the Master to 1.25, above 1 the boost") } },
                                       { "name" }),
                          [window](const juce::var& args)
                          {
                              auto* panel = findPanel(window ? window() : nullptr);
                              if (panel == nullptr)
                                  return textResult("No panel to click: the app's window is closed or shows something else.", true);
                              const auto result = panel->clickForMcp(args["name"].toString(),
                                                                     args.hasProperty("value") ? static_cast<double>(args["value"]) : 0.0);
                              return textResult(result + "\n\n" + panel->describeForMcp(), result.startsWith("Nothing") || result.startsWith("Not "));
                          } });

        tools.push_back({ "read_log",
                          "The latest lines of this session's flight log: notes, knob changes ('set id value'), file choices, "
                          "presets, MiniLab events, and each second's voices and loudest level.",
                          objectSchema({ { "lines", typeSchema("integer", "How many of the last lines, 60 by default, at most 2000") },
                                         { "quiet", typeSchema("boolean", "Leave out the once-a-second lines without voices (true by default)") } }),
                          [](const juce::var& args)
                          {
                              const auto file = coolsynth::diag::FlightRecorder::instance().logFile();
                              if (! file.existsAsFile())
                                  return textResult("No flight log in this session.", true);
                              const int wanted = juce::jlimit(1, 2000, args.hasProperty("lines") ? static_cast<int>(args["lines"]) : 60);
                              const bool quiet = ! args.hasProperty("quiet") || static_cast<bool>(args["quiet"]);
                              juce::StringArray all;
                              file.readLines(all);
                              juce::StringArray kept;
                              for (const auto& line : all)
                              {
                                  if (quiet && (line.contains("% of the second") || line.contains("memory: working set")
                                                || (line.contains("second: worst block") && line.contains("voices 0,"))))
                                      continue;
                                  kept.add(line);
                              }
                              kept.removeRange(0, std::max(0, kept.size() - wanted));
                              return textResult(file.getFileName() + ":\n" + kept.joinIntoString("\n"));
                          } });

        return tools;
    }
}
