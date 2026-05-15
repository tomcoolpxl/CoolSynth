# CoolSynth V2 Phase 8 Manual Smoke

## Artifacts
- Standalone: `C:\Users\thraa\github\CoolSynth\build\CoolSynth_artefacts\Debug\Standalone\CoolSynth.exe`
- VST3: `C:\Users\thraa\github\CoolSynth\build\CoolSynth_artefacts\Debug\VST3\CoolSynth.vst3`

## Standalone pass
1. Launch the standalone build.
2. Start from an init-like dry patch: `Osc A level = 1.0`, `Osc B level = 0.0`, `Noise = 0.0`, moderate amp sustain, filter fairly open.
3. Confirm the fully dry sound first with all FX disabled.
4. Enable Drive: `Amount ~ 0.6`, `Mix = 1.0`. Expect a denser, saturated tone without instability.
5. Enable Chorus: `Rate ~ 0.6 Hz`, `Depth ~ 0.5`, `Mix ~ 0.35`. Expect width and motion without the dry core disappearing.
6. Enable Delay: `Time ~ 300 ms`, `Feedback ~ 0.55`, `Mix ~ 0.4`. Play a short stab and confirm repeats.
7. While the delay is ringing, switch `Delay Enabled` off. The repeats should stop immediately rather than hiding and reappearing later.
8. Enable Reverb: `Size ~ 0.75`, `Damping ~ 0.3`, `Mix ~ 0.35`. Expect an obvious tail.
9. While the reverb is ringing, switch `Reverb Enabled` off. The tail should clear immediately.
10. Re-enable Delay and Reverb together, play a note or chord, then trigger `Panic`. Output should go silent immediately.

## VST3 host pass
1. Rescan or load the Debug VST3 in REAPER or Ableton Live Lite.
2. Verify a dry baseline with all FX disabled.
3. Repeat the Drive, Chorus, Delay, and Reverb checks above inside the host.
4. While Delay or Reverb is ringing, disable that stage in the plugin UI and confirm the tail is cleared immediately.
5. Stop transport after an effected note or chord. Confirm there is no stuck-note behavior, and check that reloading playback still behaves normally.

## Expected outcome
- Dry path stays unchanged when every FX mix is `0.0` or the stages are disabled.
- Extreme settings stay stable: no NaNs, runaway blasts, or persistent stuck tails.
- Delay and Reverb disable acts like a real bypass flush for stored tails.