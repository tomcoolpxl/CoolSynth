#!/usr/bin/env python3
"""Insert v3-era effect entries (timbre/excite/phaser/compressor) into every
constexpr preset array in src/presets/FactoryPresets.cpp.

For each preset, locate its `{ ids::masterGainDb, ... },` line and append the
new entries immediately after, preserving the existing indentation. Effect
values are chosen per preset name based on the preset's character.

Run from the project root:
    python scripts/inject_v3_into_factory_presets.py
"""

import re
import sys
from pathlib import Path

# (rateHz, depth) per preset; presets not listed have phaser disabled.
PHASER_ON = {
    'kFunkyClav':         (1.20, 0.55),  # classic Stevie-Wonder-style funky clav
    'kEtherealDream':     (0.18, 0.45),  # slow, dreamy pad motion
    'kSciFiTexture':      (0.60, 0.70),  # texture character
    'kWarmFilmPad':       (0.20, 0.40),  # subtle filmic swirl
    'kGlassProphetPad':   (0.25, 0.50),  # iconic Prophet pad phasing
    'kRisingSyncSwarm':   (0.70, 0.60),  # swarm + phaser chaos
    'kReplicantBrass':    (0.18, 0.35),  # Blade Runner brass, subtle
    'kUpsideDownDrone':   (0.18, 0.65),  # slow ominous drift
    'kHawkinsPulse':      (0.35, 0.55),  # Stranger Things arp tone
    'kTyrellRainPad':     (0.22, 0.55),  # Blade Runner pad
    'kStarcourtSequence': (0.45, 0.50),  # '80s arp motion
    'kLosAngeles2019':    (0.18, 0.55),  # CS-80 / Blade Runner '82
    'kSovietSatellite':   (0.40, 0.55),  # sci-fi character
}

# (amount, mix) per preset; presets not listed have compressor disabled.
COMP_ON = {
    'kMonoSawLead':       (0.30, 1.0),
    'kHardSyncLead':      (0.35, 1.0),
    'kSuperStack':        (0.40, 1.0),
    'kFatMonoBass':       (0.40, 1.0),
    'kAcidBass':          (0.35, 1.0),
    'kGlassyPluck':       (0.25, 1.0),
    'kFunkyClav':         (0.40, 1.0),
    'kBrassStack':        (0.35, 1.0),
    'kWobbleBass':        (0.40, 1.0),
    'kArpBliss':          (0.30, 1.0),
    'kFoundationMono':    (0.45, 1.0),
    'kRubberPWMBass':     (0.40, 1.0),
    'kAcidProphet':       (0.40, 1.0),
    'kSubMetalStack':     (0.35, 1.0),
    'kSyncSaber':         (0.35, 1.0),
    'kUnisonAnthem':      (0.40, 1.0),
    'kVelvetPluck':       (0.30, 1.0),
    'kKotoPulse':         (0.30, 1.0),
    'kAnalogCompKeys':    (0.55, 1.0),  # name asks for it
    'kDustyPolyKeys':     (0.35, 1.0),
    'kNeonRunner':        (0.35, 1.0),
    'kClockworkPulse':    (0.30, 1.0),
    'kOctaveSprinter':    (0.30, 1.0),
    'kReplicantBrass':    (0.30, 1.0),
    'kHawkinsPulse':      (0.30, 1.0),
    'kNeonPoliceSweep':   (0.30, 1.0),
    'kStarcourtSequence': (0.30, 1.0),
}


def main() -> int:
    project_root = Path(__file__).resolve().parent.parent
    target = project_root / 'src' / 'presets' / 'FactoryPresets.cpp'
    text = target.read_text(encoding='utf-8')

    # Collect each preset (start position, name).
    preset_headers = list(re.finditer(
        r'constexpr PresetParameterValue (k\w+)\[\] = \{', text))
    if not preset_headers:
        print('no presets found', file=sys.stderr)
        return 1

    # Process from last to first so insertion offsets stay valid.
    for header_match in reversed(preset_headers):
        name = header_match.group(1)
        search_from = header_match.end()
        mg_match = re.search(
            r'^([ \t]+)\{ ids::masterGainDb,[^}]+\},\n', text[search_from:],
            re.MULTILINE)
        if not mg_match:
            print(f'WARN: no masterGainDb in {name}', file=sys.stderr)
            continue

        indent = mg_match.group(1)
        insert_at = search_from + mg_match.end()

        phs_rate, phs_depth = PHASER_ON.get(name, (0.50, 0.60))
        phs_enabled = 1.0 if name in PHASER_ON else 0.0
        comp_amount, comp_mix = COMP_ON.get(name, (0.30, 1.0))
        comp_enabled = 1.0 if name in COMP_ON else 0.0

        injection = (
            f"{indent}{{ ids::timbre, 0.0f }}, {{ ids::excite, 0.0f }},\n"
            f"{indent}{{ ids::phaserEnabled, {phs_enabled:.1f}f }}, "
            f"{{ ids::phaserRateHz, {phs_rate:.2f}f }}, "
            f"{{ ids::phaserDepth, {phs_depth:.2f}f }},\n"
            f"{indent}{{ ids::compressorEnabled, {comp_enabled:.1f}f }}, "
            f"{{ ids::compressorAmount, {comp_amount:.2f}f }}, "
            f"{{ ids::compressorMix, {comp_mix:.2f}f }},\n"
        )

        text = text[:insert_at] + injection + text[insert_at:]

    target.write_text(text, encoding='utf-8')
    print(f'injected v3 entries into {len(preset_headers)} presets')
    return 0


if __name__ == '__main__':
    sys.exit(main())
