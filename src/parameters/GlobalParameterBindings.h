#pragma once

#include <array>
#include <atomic>
#include <utility>

#include "parameters/ParameterIDs.h"
#include "synth/SynthParameters.h"

/*
    Binds the engine's pointers to the parameters outside a V3 part (synth/SynthParameters.h) to CoolSynth's parameter
    IDs. Moved here from the engine (D265): the engine knows its settings, CoolSynth its IDs.
*/
namespace coolsynth::synth
{
    using GlobalParamMemberPtr = std::atomic<float>* GlobalParameterPointers::*;

    inline const std::array<std::pair<const char*, GlobalParamMemberPtr>,
                            9> kGlobalParamPtrBindings = {{
        { coolsynth::parameters::ids::arpInternalTempoBpm,  &GlobalParameterPointers::arpInternalTempoBpm  },
        { coolsynth::parameters::ids::masterGainDb,         &GlobalParameterPointers::masterGainDb         },
        { coolsynth::parameters::ids::brightness,           &GlobalParameterPointers::brightness           },
        { coolsynth::parameters::ids::space,                &GlobalParameterPointers::space                },
        { coolsynth::parameters::ids::volumeA,              &GlobalParameterPointers::volumeA              },
        { coolsynth::parameters::ids::volumeB,              &GlobalParameterPointers::volumeB              },
        { coolsynth::parameters::ids::volumeFx,             &GlobalParameterPointers::volumeFx             },
        { coolsynth::parameters::ids::volumeMaster,         &GlobalParameterPointers::volumeMaster         },
        { coolsynth::parameters::ids::masterBoost,          &GlobalParameterPointers::masterBoost          },
    }};
    /** Binds layer `layer`'s arp pointers (0 A, 1 B), by the IDs of ParameterIDs.h. */
    template <typename State>
    inline ArpParameterPointers bindArpPointers(State& state, int layer)
    {
        namespace ids = coolsynth::parameters::ids;
        const auto get = [&](const char* layerAId) { return state.getRawParameterValue(ids::arpOfLayer(layer, layerAId)); };
        ArpParameterPointers p;
        p.enabled = get(ids::arpEnabled);
        p.rateDivision = get(ids::arpRateDivision);
        p.pattern = get(ids::arpPattern);
        p.octaveRange = get(ids::arpOctaveRange);
        p.gate = get(ids::arpGate);
        p.swing = get(ids::arpSwing);
        p.chance = get(ids::arpChance);
        p.ratchetCount = get(ids::arpRatchetCount);
        p.ratchetChance = get(ids::arpRatchetChance);
        p.accentEvery = get(ids::arpAccentEvery);
        p.accentAmount = get(ids::arpAccentAmount);
        p.rhythm = get(ids::arpRhythm);
        p.euclideanPulses = get(ids::arpEuclideanPulses);
        p.euclideanSteps = get(ids::arpEuclideanSteps);
        p.euclideanRotation = get(ids::arpEuclideanRotation);
        p.latch = get(ids::arpLatch);
        p.newPatterns = get(ids::arpNewPatterns);
        return p;
    }
}
