#include "SynthAudioProcessorEditor.h"

namespace
{
    juce::String wrapTooltipBody(juce::String text, int maxCharsPerLine = 38)
    {
        juce::StringArray paragraphs;
        paragraphs.addLines(text);

        juce::StringArray wrappedParagraphs;

        for (auto paragraph : paragraphs)
        {
            paragraph = paragraph.trim();

            if (paragraph.isEmpty())
            {
                wrappedParagraphs.add({});
                continue;
            }

            juce::StringArray words;
            words.addTokens(paragraph, " ", {});
            words.removeEmptyStrings();

            juce::String currentLine;
            juce::StringArray lines;

            for (const auto& word : words)
            {
                const auto candidate = currentLine.isEmpty() ? word : currentLine + " " + word;

                if (! currentLine.isEmpty() && candidate.length() > maxCharsPerLine)
                {
                    lines.add(currentLine);
                    currentLine = word;
                }
                else
                {
                    currentLine = candidate;
                }
            }

            if (currentLine.isNotEmpty())
                lines.add(currentLine);

            wrappedParagraphs.add(lines.joinIntoString("\n"));
        }

        return wrappedParagraphs.joinIntoString("\n");
    }

    juce::String makeTooltipText(juce::String title, juce::String body)
    {
        return title.toUpperCase() + "\n" + wrapTooltipBody(body);
    }
}

void SynthAudioProcessorEditor::setParameterTooltip(juce::SettableTooltipClient& surface,
                                                    juce::String name,
                                                    juce::String description)
{
    auto body = std::move(description).trim();
    if (! body.endsWithChar('.'))
        body << ".";
    surface.setTooltip(makeTooltipText(std::move(name), std::move(body)));
}

void SynthAudioProcessorEditor::applyTooltips()
{
    oscSection.setTooltip(makeTooltipText("Oscillators",
                                          "Choose the source tone for oscillator A and B.\n"
                                          "This section sets waveform, pitch, pulse width,\n"
                                          "sync, and low-frequency mode."));
    mixSection.setTooltip(makeTooltipText("Mixer",
                                          "Blend oscillator A, oscillator B, and noise\n"
                                          "before the signal reaches the filter."));
    fltSection.setTooltip(makeTooltipText("Filter",
                                          "Shape brightness and emphasis in the main\n"
                                          "low-pass filter.\n"
                                          "Cutoff, resonance, envelope amount, and key\n"
                                          "tracking all meet here."));
    envSection.setTooltip(makeTooltipText("Envelopes",
                                          "Set the time shape for both the filter and amp.\n"
                                          "Attack, decay, sustain, and release define how\n"
                                          "each note starts, holds, and fades."));
    lfoSection.setTooltip(makeTooltipText("LFO",
                                          "Set the global low-frequency modulator.\n"
                                          "Choose its shape and route it to pitch,\n"
                                          "pulse width, and filter cutoff."));
    pmodSection.setTooltip(makeTooltipText("Poly Mod",
                                           "Route oscillator B or the filter envelope into\n"
                                           "extra modulation destinations.\n"
                                           "Use this for more aggressive or animated tones."));
    perfSection.setTooltip(makeTooltipText("Performance",
                                           "Configure how the synth responds while played.\n"
                                           "This includes glide, voice mode, note priority,\n"
                                           "bend range, drift, spread, and velocity."));
    arpSection.setTooltip(makeTooltipText("Arpeggiator",
                                          "Turn held notes into repeating patterns.\n"
                                          "Set tempo, timing division, pattern order,\n"
                                          "octave span, gate length, and latch behavior."));
    drvSection.setTooltip(makeTooltipText("Distortion",
                                          "Add saturation before the time-based effects.\n"
                                          "Use amount and mix to control strength and blend."));
    choSection.setTooltip(makeTooltipText("Chorus",
                                          "Widen and thicken the sound with modulation.\n"
                                          "Rate controls movement, depth controls range,\n"
                                          "and mix controls blend."));
    dlySection.setTooltip(makeTooltipText("Delay",
                                          "Create repeating echoes after the dry signal.\n"
                                          "Time sets spacing, feedback sets repeats,\n"
                                          "and mix sets blend."));
    revSection.setTooltip(makeTooltipText("Reverb",
                                          "Add space and tail after the delay stage.\n"
                                          "Size sets the room, damping darkens the tail,\n"
                                          "and mix sets blend."));
    outSection.setTooltip(makeTooltipText("Output",
                                          "Set the final master level after the full\n"
                                          "effects chain.\n"
                                          "Use this to match loudness without changing\n"
                                          "the patch balance upstream."));

    setParameterTooltip(oscAWaveChoice, "Wave",
                        "Choose the basic tone shape for oscillator A.\n"
                        "Different waveforms change how bright, hollow,\n"
                        "or smooth the sound starts out.");
    oscAWaveChoice.setOptionTooltip(0, makeTooltipText("Pulse",
                                                       "Pulse is a hollow, buzzy waveform.\n"
                                                       "Its width can be changed for thinner or fatter tone.\n"
                                                       "Great for basses, leads, and animated movement."));
    oscAWaveChoice.setOptionTooltip(1, makeTooltipText("Tri - Triangle",
                                                       "Triangle sounds softer and rounder than saw or pulse.\n"
                                                       "Useful for gentle leads and smoother tones."));
    oscAWaveChoice.setOptionTooltip(2, makeTooltipText("Saw",
                                                       "Saw is bright and rich in harmonics.\n"
                                                       "It is the classic starting point for synth brass,\n"
                                                       "strings, basses, and many leads."));
    oscAWaveChoice.setOptionTooltip(3, makeTooltipText("Sin - Sine",
                                                       "Sine is the smoothest and purest oscillator shape.\n"
                                                       "Use it for softer fundamentals, rounded support tone,\n"
                                                       "or cleaner low-end reinforcement."));
    setParameterTooltip(oscAOctaveKnob, "Octave",
                        "Move oscillator A up or down in whole octaves.\n"
                        "Use it to set the register before fine tuning.");
    setParameterTooltip(oscAFineKnob, "Fine",
                        "Fine tune oscillator A in small pitch steps.\n"
                        "Tiny offsets between oscillators add beating and width.");
    setParameterTooltip(oscAPwKnob, "Pulse Width",
                        "Change the width of oscillator A's pulse wave.\n"
                        "This matters most when the waveform is set to Pulse.");
    setParameterTooltip(oscASyncToggle, "Sync",
                        "Hard sync forces oscillator A to restart from oscillator B.\n"
                        "This gives sharper, more tearing sweeps when pitch moves.");
    setParameterTooltip(oscBWaveChoice, "Wave",
                        "Choose the basic tone shape for oscillator B.\n"
                        "This oscillator can act as a second voice or a modulator.");
    oscBWaveChoice.setOptionTooltip(0, makeTooltipText("Pulse",
                                                       "Pulse is a hollow, buzzy waveform.\n"
                                                       "Layer it with oscillator A for thicker classic synth tone.\n"
                                                       "Its width can also be modulated for animation."));
    oscBWaveChoice.setOptionTooltip(1, makeTooltipText("Tri - Triangle",
                                                       "Triangle has less bite than saw or pulse.\n"
                                                       "Good for support tone or gentler modulation."));
    oscBWaveChoice.setOptionTooltip(2, makeTooltipText("Saw",
                                                       "Saw is bright, full, and rich in harmonics.\n"
                                                       "Use it when you want edge, body,\n"
                                                       "or strong filter sweeps."));
    oscBWaveChoice.setOptionTooltip(3, makeTooltipText("Sin - Sine",
                                                       "Sine is smooth and low in harmonics.\n"
                                                       "Useful when oscillator B should add weight,\n"
                                                       "behave gently, or modulate with a rounder shape."));
    setParameterTooltip(oscBOctaveKnob, "Octave",
                        "Move oscillator B up or down in whole octaves.\n"
                        "Use it to separate the two oscillators by register.");
    setParameterTooltip(oscBFineKnob, "Fine",
                        "Fine tune oscillator B in small pitch steps.\n"
                        "A slight detune makes the pair sound wider and thicker.");
    setParameterTooltip(oscBPwKnob, "Pulse Width",
                        "Change the width of oscillator B's pulse wave.\n"
                        "This only matters when oscillator B uses Pulse.");
    setParameterTooltip(oscBLoFreqToggle, "Lo Freq - Low Frequency",
                        "Oscillator B drops into a slow range for modulation\n"
                        "instead of acting like a normal pitched oscillator.");
    setParameterTooltip(mixOscAKnob, "Osc A",
                        "Set how much oscillator A reaches the mixer.\n"
                        "Turn it down if oscillator B or noise should dominate.");
    setParameterTooltip(mixOscBKnob, "Osc B",
                        "Set how much oscillator B reaches the mixer.\n"
                        "Blend it against oscillator A to build the core tone.");
    setParameterTooltip(mixNoiseKnob, "Noise",
                        "Add hiss and texture to the sound.\n"
                        "Small amounts can add bite without sounding obviously noisy.");
    setParameterTooltip(fltCutoffKnob, "Cutoff",
                        "Cutoff sets how much high end the low-pass filter removes.\n"
                        "Lower values sound darker. Higher values sound brighter.");
    setParameterTooltip(fltResKnob, "Resonance",
                        "Resonance emphasizes frequencies near the cutoff point.\n"
                        "It makes sweeps speak more clearly and can sound sharper.");
    setParameterTooltip(fltEnvAmtKnob, "Env Amt - Envelope Amount",
                        "It sets how far the filter envelope pushes the cutoff\n"
                        "each time a note starts.");
    setParameterTooltip(fltKeyTrkChoice, "Key Trk - Key Tracking",
                        "It decides how much higher notes open the filter more.");
    fltKeyTrkChoice.setOptionTooltip(0, makeTooltipText("Off",
                                                        "The filter stays at the same base brightness across the keyboard.\n"
                                                        "Higher notes will not automatically open the filter."));
    fltKeyTrkChoice.setOptionTooltip(1, makeTooltipText("Half",
                                                        "Higher notes open the filter a little as you play upward.\n"
                                                        "This keeps the keyboard more even without going too bright."));
    fltKeyTrkChoice.setOptionTooltip(2, makeTooltipText("Full",
                                                        "Higher notes open the filter strongly as pitch rises.\n"
                                                        "This helps preserve brightness across the keyboard range."));
    setParameterTooltip(fEnvAKnob, "F Atk - Filter Attack",
                        "This sets how long the filter takes to open after a note starts.");
    setParameterTooltip(fEnvDKnob, "F Dec - Filter Decay",
                        "This sets how fast the filter falls after the attack peak.");
    setParameterTooltip(fEnvSKnob, "F Sus - Filter Sustain",
                        "This is the filter level held while a key stays down.");
    setParameterTooltip(fEnvRKnob, "F Rel - Filter Release",
                        "This sets how long the filter takes to fade after key release.");
    setParameterTooltip(aEnvAKnob, "A Atk - Amp Attack",
                        "This controls how fast the note reaches full volume.");
    setParameterTooltip(aEnvDKnob, "A Dec - Amp Decay",
                        "This controls how fast the note falls from the peak level.");
    setParameterTooltip(aEnvSKnob, "A Sus - Amp Sustain",
                        "This is the level held while the key stays down.");
    setParameterTooltip(aEnvRKnob, "A Rel - Amp Release",
                        "This controls how long the note rings out after release.");
    setParameterTooltip(lfoWaveChoice, "Wave",
                        "Choose the shape of the global low-frequency oscillator.\n"
                        "That shape changes whether modulation feels smooth,\n"
                        "stepped, or slanted.");
    lfoWaveChoice.setOptionTooltip(0, makeTooltipText("Saw",
                                                      "A repeating ramp shape.\n"
                                                      "It creates a steady sweep or rise before snapping back."));
    lfoWaveChoice.setOptionTooltip(1, makeTooltipText("Tri",
                                                      "A smooth up-and-down shape.\n"
                                                      "This is the most even and gentle modulation option."));
    lfoWaveChoice.setOptionTooltip(2, makeTooltipText("Sqr - Square",
                                                      "This is a sharp on-off shape.\n"
                                                      "Use it for stepped vibrato, trill-like motion,\n"
                                                      "or abrupt filter changes."));
    lfoWaveChoice.setOptionTooltip(3, makeTooltipText("Sin - Sine",
                                                      "A pure, smooth sine wave.\n"
                                                      "Perfect for gentle, organic vibrato or tremolo."));
    setParameterTooltip(lfoRateKnob, "Rate",
                        "Rate sets how fast the low-frequency oscillator cycles.\n"
                        "Faster values create quicker vibrato, wah,\n"
                        "or pulse-width movement.");
    setParameterTooltip(lfoMwDepKnob, "MW->Dep - Mod Wheel To Depth",
                        "This lets the mod wheel add more LFO amount while you play.");
    setParameterTooltip(lfoPitchKnob, "->Pitch",
                        "Send the LFO to oscillator pitch.\n"
                        "Small amounts create vibrato. Large amounts sound wilder.");
    setParameterTooltip(lfoPwKnob, "->PW - To Pulse Width",
                        "This sends the LFO to pulse width for animated hollow tone.");
    setParameterTooltip(lfoCutoffKnob, "->Cutoff",
                        "Send the LFO to filter cutoff.\n"
                        "Use it for repeating wah, sweep, or tremble-like brightness changes.");
    setParameterTooltip(pmodBPitchKnob, "B->Pitch - Oscillator B To Pitch",
                        "This lets oscillator B modulate pitch for harsher, richer motion.");
    setParameterTooltip(pmodBPwKnob, "B->PW - Oscillator B To Pulse Width",
                        "This lets oscillator B bend pulse width for complex movement.");
    setParameterTooltip(pmodBCutoffKnob, "B->Cutoff - Oscillator B To Cutoff",
                        "This lets oscillator B modulate filter cutoff for aggressive animation.");
    setParameterTooltip(pmodEPitchKnob, "E->Pitch - Envelope To Pitch",
                        "This sends the envelope to pitch for punch or attack snap.");
    setParameterTooltip(pmodEPwKnob, "E->PW - Envelope To Pulse Width",
                        "This makes pulse width change over the life of each note.");
    setParameterTooltip(pmodECutoffKnob, "E->Cutoff - Envelope To Cutoff",
                        "This adds extra envelope movement to the filter cutoff itself.");
    setParameterTooltip(perfGlideKnob, "Glide",
                        "Glide sets how long pitch takes to slide\n"
                        "from one note to the next.");
    setParameterTooltip(perfModeChoice, "Mode",
                        "Choose how voices are allocated while you play.\n"
                        "Poly plays chords, mono plays one note,\n"
                        "and unison stacks voices together.");
    perfModeChoice.setOptionTooltip(0, makeTooltipText("Poly",
                                                       "Each new note can sound with its own voice.\n"
                                                       "Best for chords and layered playing."));
    perfModeChoice.setOptionTooltip(1, makeTooltipText("Mono",
                                                       "Only one note sounds at a time.\n"
                                                       "Best for lead lines, basses, and glide playing."));
    perfModeChoice.setOptionTooltip(2, makeTooltipText("Unison",
                                                       "Multiple voices stack on the same note.\n"
                                                       "This makes the sound thicker, wider, and heavier."));
    setParameterTooltip(perfPrioChoice, "Priority",
                        "Priority matters in mono mode.\n"
                        "It chooses which held note wins when several keys are down.");
    perfPrioChoice.setOptionTooltip(0, makeTooltipText("Last",
                                                       "The newest held note wins.\n"
                                                       "This feels natural for most lead and solo playing."));
    perfPrioChoice.setOptionTooltip(1, makeTooltipText("Low",
                                                       "The lowest held note wins.\n"
                                                       "Useful for bass-oriented mono playing."));
    perfPrioChoice.setOptionTooltip(2, makeTooltipText("High",
                                                       "The highest held note wins.\n"
                                                       "Useful when you want upper notes to take over."));
    setParameterTooltip(perfPbRangeKnob, "PB Range - Pitch Bend Range",
                        "This sets how far the bend wheel can move the note.");
    setParameterTooltip(perfVintageKnob, "Vintage",
                        "Add controlled drift and slight mismatch between voices.\n"
                        "Higher values feel less perfect and more analog-like.");
    setParameterTooltip(perfPanKnob, "Pan Spread - Stereo Spread",
                        "This spreads different voices left and right for width.");
    setParameterTooltip(perfVelAmpKnob, "Vel->Amp - Velocity To Loudness",
                        "Higher values make harder key strikes play louder.");
    setParameterTooltip(perfVelFltKnob, "Vel->Flt - Velocity To Filter",
                        "Higher values make harder key strikes sound brighter.");
    setParameterTooltip(arpOnToggle, "Arp On",
                        "Turn the arpeggiator on or off.\n"
                        "When on, held notes are replayed as a repeating pattern.");
    setParameterTooltip(arpTempoKnob, "Tempo",
                        "Set the arpeggiator speed in beats per minute.\n"
                        "This is used when host timing is not driving the arp.");
    setParameterTooltip(arpRateChoice, "Rate",
                        "Choose the note division for each arpeggiated step.\n"
                        "Smaller fractions play faster.");
    arpRateChoice.setOptionTooltip(0, makeTooltipText("1/4",
                                                      "One arpeggiated step per quarter note.\n"
                                                      "Slow and spacious."));
    arpRateChoice.setOptionTooltip(1, makeTooltipText("1/8",
                                                      "One step per eighth note.\n"
                                                      "A common medium pulse."));
    arpRateChoice.setOptionTooltip(2, makeTooltipText("1/8T - Eighth-Note Triplet",
                                                      "This plays one step per eighth-note triplet\n"
                                                      "for a swinging three-part feel."));
    arpRateChoice.setOptionTooltip(3, makeTooltipText("1/16",
                                                      "One step per sixteenth note.\n"
                                                      "Fast and common for synth sequences."));
    arpRateChoice.setOptionTooltip(4, makeTooltipText("1/16T - Sixteenth-Note Triplet",
                                                      "This plays one step per sixteenth-note triplet\n"
                                                      "for a fast rolling feel."));
    arpRateChoice.setOptionTooltip(5, makeTooltipText("1/32",
                                                      "One step per thirty-second note.\n"
                                                      "Very fast and dense."));
    setParameterTooltip(arpPatternChoice, "Pattern",
                        "Choose the order in which held notes are replayed.\n"
                        "This changes the shape of the sequence without changing the notes.");
    arpPatternChoice.setOptionTooltip(0, makeTooltipText("Up",
                                                         "Plays held notes from low to high,\n"
                                                         "then repeats from the bottom."));
    arpPatternChoice.setOptionTooltip(1, makeTooltipText("Down",
                                                         "Plays held notes from high to low,\n"
                                                         "then repeats from the top."));
    arpPatternChoice.setOptionTooltip(2, makeTooltipText("Up/Dn - Up/Down",
                                                         "This climbs upward, then comes back down.\n"
                                                         "It gives a classic back-and-forth arp motion."));
    arpPatternChoice.setOptionTooltip(3, makeTooltipText("Play - As Played",
                                                         "It reuses the order you actually played the notes.\n"
                                                         "Good when finger order matters."));
    setParameterTooltip(arpOctaveChoice, "Octave",
                        "Choose how many octaves the arpeggiator will cover.\n"
                        "Larger ranges make the pattern feel more sweeping.");
    arpOctaveChoice.setOptionTooltip(1, makeTooltipText("1",
                                                        "Keep the arpeggio within the notes you played.\n"
                                                        "No extra octave repeats are added."));
    arpOctaveChoice.setOptionTooltip(2, makeTooltipText("2",
                                                        "Repeat the arpeggio across two octaves.\n"
                                                        "This makes the pattern feel larger and more animated."));
    arpOctaveChoice.setOptionTooltip(3, makeTooltipText("3",
                                                        "Repeat the arpeggio across three octaves.\n"
                                                        "This creates a broad, cascading motion."));
    setParameterTooltip(arpGateKnob, "Gate",
                        "Gate sets how long each arp note stays open.\n"
                        "Lower values sound choppier. Higher values sound more connected.");
    setParameterTooltip(arpLatchToggle, "Latch",
                        "Latch keeps the current held-note set playing\n"
                        "after you release the keys.");
    setParameterTooltip(drvOnToggle, "Distortion",
                        "Turn the distortion stage on or off.\n"
                        "Distortion adds saturation before the time-based effects.");
    setParameterTooltip(drvAmtKnob, "Amount",
                        "Set how hard the distortion stage pushes the signal.\n"
                        "More amount means more grit and saturation.");
    setParameterTooltip(drvMixKnob, "Mix",
                        "Blend the distorted sound with the clean sound.\n"
                        "Lower values stay subtler. Higher values sound more processed.");
    setParameterTooltip(choOnToggle, "Chorus",
                        "Turn the chorus stage on or off.\n"
                        "Chorus thickens the sound by adding moving detuned copies.");
    setParameterTooltip(choRateKnob, "Rate",
                        "Set how fast the chorus motion cycles.\n"
                        "Slow values feel lush. Faster values feel more obvious.");
    setParameterTooltip(choDepKnob, "Depth",
                        "Depth sets how far the chorus modulation moves.\n"
                        "More depth gives a wider, more dramatic swirl.");
    setParameterTooltip(choMixKnob, "Mix",
                        "Blend the chorus signal with the dry signal.\n"
                        "Use this to keep width under control.");
    setParameterTooltip(dlyOnToggle, "Delay",
                        "Turn the delay stage on or off.\n"
                        "Delay creates repeating echoes after the dry sound.");
    setParameterTooltip(dlyTimeKnob, "Time",
                        "Time sets the space between echoes.\n"
                        "Short values feel tight. Long values feel spacious.");
    setParameterTooltip(dlyFdbkKnob, "Fdbk - Feedback",
                        "It feeds some of each echo back into the delay line\n"
                        "to create more repeats.");
    setParameterTooltip(dlyMixKnob, "Mix",
                        "Blend the echoes with the dry signal.\n"
                        "Lower values tuck delay behind the main sound.");
    setParameterTooltip(revOnToggle, "Reverb",
                        "Turn the reverb stage on or off.\n"
                        "Reverb adds room, air, and a fading tail.");
    setParameterTooltip(revSizeKnob, "Size",
                        "Size changes the apparent room or space.\n"
                        "Higher values sound larger and longer.");
    setParameterTooltip(revDampKnob, "Damp",
                        "Damp controls how quickly high frequencies fade in the tail.\n"
                        "Higher damping makes the reverb darker and softer.");
    setParameterTooltip(revMixKnob, "Mix",
                        "Blend the reverb with the dry sound.\n"
                        "Use small amounts to add space without washing out the patch.");
    setParameterTooltip(outGainKnob, "Master",
                        "Set the final output level of the whole synth.\n"
                        "Use this to match loudness after shaping the patch.");

    initPatchButton.setTooltip(makeTooltipText("Init Patch",
                                               "Reset the panel to the default patch.\n"
                                               "Useful when you want a clean baseline."));
    savePatchButton.setTooltip(makeTooltipText("Save Patch",
                                               "Write the current synth state to a\n"
                                               ".cspatch file for later recall."));
    loadPatchButton.setTooltip(makeTooltipText("Load Patch",
                                               "Load a saved .cspatch file into the synth.\n"
                                               "The current patch state will be replaced."));
    allNotesOffButton.setTooltip(makeTooltipText("All Notes Off",
                                                 "Immediate panic.\n"
                                                 "Stops active notes and clears any\n"
                                                 "stuck playing state."));
    tooltipToggleButton.setTooltip(makeTooltipText("i",
                                                   "Toggle hover help on or off.\n"
                                                   "Turn it off if you want a cleaner panel."));
    pianoBar.setTooltip(makeTooltipText("Keyboard / LED Strip",
                                        "Click to expand or collapse the keyboard area.\n"
                                        "The compact strip shows note activity.\n"
                                        "The expanded view gives octave controls."));
}
