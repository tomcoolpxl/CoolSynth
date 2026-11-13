"""Print a DX7 voice (the dx7-patches JSON) with its numbers turned into physical values.

After the open DX7 emulation msfa (Dexed's engine), whose envelope a rate of 0 takes 374 s for
the full range, against 342 s measured on the instrument:

    a level step (output level, or envelope level above 19) is 0.7526 dB
    an envelope falls (4 + (q mod 4)) * 2^(q div 4) * 0.0633 dB a second, q = rate * 41 div 64
    a modulator at output level OL and full envelope reaches pi * 2^(33/16 - (99 - OL)/8) radians
      (Chowning and Bristow's table for the DX7)

usage: python dx7_show.py <voice.json>
"""
import json
import math
import sys

voice = json.load(open(sys.argv[1], encoding="utf-8"))
LEVEL_LUT = [0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46]


def scaled(level):
    return 28 + level if level >= 20 else LEVEL_LUT[level]


def level_db(level):
    """An envelope level 0..99 in dB below level 99."""
    return -((scaled(99) >> 1) - (scaled(level) >> 1)) * 64 * 6.0206 / 256.0


def rate_db_per_second(rate, scaling=0):
    q = min(63, ((rate * 41) >> 6) + scaling)
    return (4 + (q & 3)) * 2 ** (q >> 2) * 0.0633


print(voice["name"].strip(), "| algorithm", voice["algorithm"], "| feedback", voice["feedback"], "| transpose", voice["transpose"],
      "| key sync", voice["oscillatorKeySync"])
print("lfo", voice["lfo"])
print("pitch envelope", voice["pitchEG"])
for number, op in enumerate(voice["operators"], start=1):
    eg = op["eg"]
    index = math.pi * 2.0 ** (33.0 / 16.0 - (99 - op["outputLevel"]) / 8.0)
    print(f"\nOP{number}: {op['oscillatorMode']} {op['frequency']}  detune {op['detune']:+d}  output level {op['outputLevel']} "
          f"({-(99 - op['outputLevel']) * 0.7526:.1f} dB; as a modulator {index:.2f} rad)  velocity sens {op['keyVelocitySensitivity']}  "
          f"am sens {op['amSensitivity']}  rate scaling {op['keyboardRateScaling']}")
    print("   level scaling", op["keyboardLevelScaling"])
    for stage in range(1, 5):
        rate, level = eg[f"rate{stage}"], eg[f"level{stage}"]
        print(f"   stage {stage}: to level {level:2d} ({level_db(level):6.1f} dB) at rate {rate:2d} = {rate_db_per_second(rate):8.1f} dB/s")
