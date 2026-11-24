# The TR-808's CPU section, for CoolSynth 808

What the panel's copper under the glass and Under the Lid's CPU sheet are drawn from (D312). Our own sequencer drives them; Roland's program is not public, so the signals are equivalent, not cycle exact.

From the service notes of 15 June 1981 (manualslib's scan, pages 3, 4, 14, 15, 16), zoeblade's notebook and MAME's driver notes. The scan is machine-read: port and pin numbers are a best reading.

## Where it sits

The main board (OP3116-130, PCB 291-400A) holds the CPU, its clocks, start and stop, the DIN socket and the common trigger. The voicing board holds the drums, the switch board the keys.

## The chips

- The CPU: an NEC uPD650C-085, a 4-bit CMOS microcontroller (uCOM-43) with Roland's program in its ROM.
  - Its clock is a coil-tuned oscillator (IFT-1), 2 us a cycle, 500 kHz, set at TP-1.
  - Its ports, as the notes give them:
    - PH (pins 26 to 29) scans the switches and the status gate
    - PA (33 to 36) reads switches and the status: tempo, clock, start/stop, fill in
    - PB (37 to 40) reads the step keys
    - PG (22 to 25) drives the step lamps
    - PD, PE and PF carry the memory addresses and the instrument data, by turns on one bus
    - PC (2 to 5) is the 4-bit data bus
    - PI-0 is the memory's write, PI-1 its chip enable, PI-2 the trigger pulse
- The memory: four uPD444C static CMOS RAMs, 1K x 4 bits each (IC7 to IC10), on a backup battery.
  - IC5 decodes PE2 and PE3 to pick one, enabled by PI-1.
  - Data runs 4 bits wide on PC0 to PC3, through R85 to R88.
  - What each holds: IC7 the rhythm track, prescale and steps; IC8 CB, CY, OH, CH; one more MT, HT, RS, CP; IC10 AC, BD, SD, LT.
- The logic is 4000-series CMOS: MC14001 (NOR), TC4011 (NAND), TC4013 (D flip-flops), MC14051 (mux), HD14584 (Schmitt triggers).

## The clocks

- The interrupt clock: IC1, an HD14584, every 1.9 ms (TP-2, TM-1).
- The tempo clock: 8.53 ms a cycle at full tempo to about 65 ms at the slowest (TP-3, TM-2), read through the status gate IC3 on PA.

## Keys and lamps

Each 1.9 ms interrupt runs some 600 us. For the first 150 us all four PH lines go high and the status is read; then one PH line goes low and its group of keys answers on PB and PA.

- PH0: steps 1 to 4, Mode, Clear
- PH1: steps 5 to 8, Prescale, Basic Variation
- PH2: steps 9 to 12, Instrument Select
- PH3: steps 13 to 16, Auto Fill In, Intro/Fill Variation

So the step keys are a 4 by 4 matrix, and so are the lamps: PG lights one group of four until the next interrupt. A whole scan takes 7.6 ms.

## The triggers

- On each step PI-2 sends a 10 us pulse. Q27 turns it over and IC6 (a TC4011) widens it to about 1 ms: the common trigger, sent to every drum's gate.
- Each gate passes it where that drum's instrument-data line is set: twelve lines, the eleven drums and the accent.
- The accent (PF-3) raises the trigger from about 4 V to between 4 and 14 V (VR3); the metal drums take 7 to 14 V.

## Not known

The board's size and where each chip sits, the battery, the packages' pin counts and how DIN sync is made are not on the pages.

## Sources

- The service notes: https://www.manualslib.com/manual/1217113/Roland-Tr-808.html (pages 3, 4, 14, 15, 16)
- https://notebook.zoeblade.com/TR-808.html, https://notebook.zoeblade.com/UPD650.html, https://notebook.zoeblade.com/uPD444.html
- MAME's driver, a skeleton that lists the CPU and the four RAMs: https://git.redump.net/hbmame/tree/src/mame/drivers/roland_tr808.cpp
