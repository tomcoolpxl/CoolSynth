# CoolSynth on macOS

Research for TODO item J2, written on 2026-11-20 for the port on the user's Mac.
CoolSynth today: JUCE 9.0.5, C++20, CMake, MSVC; a standalone app, a VST3 and a CLAP (clap-juce-extensions); an Inno Setup installer that also downloads the instruments; the MiniLab 3 driver opening "Minilab3 MIDI" directly, shared through Windows MIDI Services.

## What in the code is Windows-only today

Found by reading the source, not by building.

- SIMD
  - `LadderProcessor.h` and `VaGenerator.h` use SSE2 behind `_M_X64 || __SSE2__`; on arm64 both fall back to their scalar paths, which compile but run slower and may differ in the last bits.
- Threads and memory
  - `AudioWorkers.cpp` starts its helper threads only on Windows; on macOS the engine renders on one thread.
  - `SampleMemory.cpp` locks sample pages only on Windows.
- Folders that come out wrong
  - `FlightRecorder.cpp` writes to `~/Local/CoolSynth/logs`; the Mac place is `~/Library/Logs/CoolSynth`.
  - JUCE's `userApplicationDataDirectory` is `~/Library` itself and `commonApplicationDataDirectory` is `/Library` ([JUCE File docs](https://docs.juce.com/master/classFile.html)), so the controller setup lands in `~/Library/CoolSynth` and the sample library in `/Library/CoolSynth/Library`, which users cannot write.
- `tests/AudioThreadAlarm.cpp` patches Windows import tables; on macOS it returns false, so the alarm is silent.
- `ml::isDawPort` wants "minilab3" with no space; Arturia's guides also spell the port "MiniLab 3 MIDI" ([Arturia DAW integration](https://support.arturia.com/hc/en-us/articles/6283884020508-MiniLab-3-DAW-Integration)), which would not match.

## Building on macOS with CMake

- Formats
  - `FORMATS Standalone VST3 AU` in `juce_add_plugin`, with the CLAP still added by `clap_juce_extensions_plugin` ([JUCE CMake API](https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/CMake%20API.md), [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions), MIT).
  - CoolSynth's codes already fit the AU rules: the manufacturer code `Tcpx` has an upper-case first letter and the rest lower-case, as GarageBand wants; the plugin code `Csyn` has exactly one upper-case letter ([JUCE CMake API](https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/CMake%20API.md)).
  - `IS_SYNTH TRUE` makes the AU an `aumu` (music device).
  - Logic, GarageBand and MainStage load only AUs; FL Studio on macOS loads VST3, AU and CLAP ([Image-Line](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins_supported.htm)); Ableton loads VST3 and AU.
  - On Apple silicon, Logic runs AUs out of process in AUHostingService, and may silently fall back to the x86_64 slice if the arm64 one fails ([Apple support](https://support.apple.com/en-US/102082), [Steinberg forum](https://forums.steinberg.net/t/auwrapped-auv2-runs-as-intel-in-logic/827682)).
  - Add `HARDENED_RUNTIME_ENABLED TRUE` (needed for notarisation) and, for the app, `MICROPHONE_PERMISSION_ENABLED` if it ever opens an audio input.
  - Set `BUNDLE_ID` once (for example `com.tomcoolpxl.coolsynth`) and never change it.
- Universal binaries
  - `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` at configure time ([JUCE CMake API](https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/CMake%20API.md)); check with `lipo -info` on the binary inside each bundle.
  - A universal build takes about twice as long, so everyday builds can stay arm64 only ([JUCE forum](https://forum.juce.com/t/cmake-plugins-compile-too-many-sources-vs-projucer/47821)).
  - Xcode 15.3 and later can override the architectures from the scheme ("Match Run Destination"), which silently builds one ([JUCE forum](https://forum.juce.com/t/building-a-juce-plugin-for-any-mac/62574)).
- Minimum macOS
  - JUCE 9.0.5 raised its default deployment target to macOS 12.0, because Xcode 27 no longer supports anything older ([JUCE BREAKING_CHANGES](https://raw.githubusercontent.com/juce-framework/JUCE/master/BREAKING_CHANGES.md)).
  - Xcode 27 builds for macOS 12 to 27, runs only on Apple silicon and needs macOS Tahoe 26.6 ([Xcode 27 summary of Apple's notes](https://blakecrosley.com/blog/xcode-27-release), [Apple forum](https://developer.apple.com/forums/thread/835691)).
  - It still builds universal binaries back to macOS 12, but a target of 27.0 drops x86_64 by default.
- Ninja works for plugins and is what Pamplejuce uses; the Xcode generator helps for debugging.
- JUCE 9's CoreAudio builds a private aggregate device, with lower latency for the app ([JUCE 9 announcement](https://forum.juce.com/t/juce-9-is-available-now/69175)); 9.0.0 did not compile against the macOS 13.x SDK ([JUCE forum](https://forum.juce.com/t/juce-9-0-0-macos-coreaudio-build-break-on-macos-13-x-sdk/69188)), so use a current Xcode.

## Validation

- auval
  - `auval -strict -v aumu Csyn Tcpx`. It finds AUs only in the Components folders; `killall -9 AudioComponentRegistrar` forces a rescan ([Moonbase on auval](https://moonbase.sh/articles/debugging-your-audio-unit-plugin-with-auval-aka-auvaltool/)).
  - Logic runs its own validation and disables an AU that fails.
- pluginval checks AU and VST3 and runs auval from strictness 5 ([pluginval](https://github.com/Tracktion/pluginval)); its AU tests have crashed on GitHub runners while passing locally ([JUCE forum](https://forum.juce.com/t/pluginval-fails-with-au-only-when-running-for-github-actions-passes-locally/67339)).
- `clap-validator validate CoolSynth.clap`; its macOS builds are unsigned, so clear the tool's quarantine flag ([clap-validator](https://github.com/free-audio/clap-validator)).

## Apple Silicon

- SSE2 code
  - The scalar fallbacks already exist, so the first build plays.
  - sse2neon is one MIT header that maps SSE to NEON, supported with Apple clang 12 and later; on ARMv8, `_mm_div_ps` is a true division ([sse2neon](https://github.com/DLTcollab/sse2neon)).
  - JUCE's `dsp::SIMDRegister` has a NEON backend, but its double type on arm64 is two plain doubles, not native ([JUCE forum](https://forum.juce.com/t/simdregister-add-native-double-support-for-arm-neon-since-aarch64-supports-it/49613)).
- Denormals
  - `juce::ScopedNoDenormals` on 64-bit ARM sets the FZ bit (bit 24) of FPCR and restores it afterwards ([JUCE source](https://raw.githubusercontent.com/juce-framework/JUCE/master/modules/juce_audio_basics/buffers/juce_FloatVectorOperations.cpp)); FZ flushes inputs and outputs alike ([Arm FPCR](https://df.lth.se/~getz/ARM/SysReg/AArch64-fpcr.html)).
  - `FloatVectorOperations` calls Apple's vDSP, which may not follow the FZ bit ([JUCE forum](https://forum.juce.com/t/heads-up-floatvectoroperations-multiply-might-produce-denormals-on-apple-silicon/68983)).
- Same sound as on Windows
  - Clang has fused multiply and add within an expression by default since clang 14 (`-ffp-contract=on`), and arm64 always has FMA, so numbers can differ from MSVC's ([LLVM issue](https://github.com/llvm/llvm-project/issues/54927), [LLVM discourse](https://discourse.llvm.org/t/fp-contraction-fma-on-by-default/64975)).
  - `-ffp-contract=off` keeps the arithmetic as written.
- Worker threads should join the audio device's workgroup, so the scheduler treats them as audio work ([Apple](https://developer.apple.com/documentation/audiotoolbox/adding-parallel-real-time-threads-to-audio-workgroups), [JUCE AudioWorkgroup](https://docs.juce.com/master/classjuce_1_1AudioWorkgroup.html)); a join needs a real-time constraint on the thread first ([JUCE forum](https://forum.juce.com/t/os-workgroup-join-consistently-returns-einval-cant-join-audio-workgroup/54240)).
- macOS 27 is the last release with Rosetta and runs only on Apple silicon; Apple asks for universal binaries ([Apple](https://developer.apple.com/news/?id=w5ngl9k2), [AppleInsider](https://appleinsider.com/articles/26/06/12/how-and-when-macos-will-finally-stop-support-for-intel-apps)). Intel Macs on macOS 12 to 26 remain.

## Real-time checks on macOS

- RealtimeSanitizer is in upstream clang since LLVM 20.1: `-fsanitize=realtime` stops on malloc, free, locks and file calls inside functions marked `[[clang::nonblocking]]`, and `__rtsan::ScopedDisabler` covers accepted calls ([LLVM 20 notes](https://releases.llvm.org/20.1.0/tools/clang/docs/ReleaseNotes.html), [clang docs](https://clang.llvm.org/docs/RealtimeSanitizer.html)).
- Apple clang does not have it, and Apple has announced nothing ([Apple forum](https://developer.apple.com/forums/thread/807360)); Homebrew's LLVM 20.1.8 is on GitHub's macOS 26 image ([runner image](https://github.com/actions/runner-images/blob/main/images/macos/macos-26-arm64-Readme.md)), so a separate sanitizer build can use it.
- Without RTSan: fishhook (BSD) rebinds imported symbols in Mach-O images, the macOS twin of the import-table patching ([fishhook](https://github.com/facebook/fishhook)).

## Signing, notarisation and installers

- The Apple Developer Program costs 99 USD a year, notarisation included ([Apple](https://developer.apple.com/programs/whats-included/), [Apple forum](https://developer.apple.com/forums/thread/746992)).
  - It gives a Developer ID Application certificate (the app and plugins) and a Developer ID Installer certificate (the .pkg).
  - What was signed and notarised keeps working after the membership lapses ([JUCE forum](https://forum.juce.com/t/mac-open-anyway-not-working-for-unsigned-mac-vst3/64574)).
- Steps
  - `codesign --force --options=runtime --timestamp -s "Developer ID Application: ..."` on every bundle, after all files are in place.
  - `pkgbuild` per format, `productbuild --sign "Developer ID Installer: ..."`, then `xcrun notarytool submit --wait` and `xcrun stapler staple` on the .pkg ([Moonbase on pkgbuild](https://moonbase.sh/articles/how-to-make-macos-installers-for-juce-projects-with-pkgbuild-and-productbuild/)).
  - Set `BundleIsRelocatable` to NO for the app, or the installer may update a copy elsewhere.
  - In CI, Apple's engineers suggest an App Store Connect API key (a .p8 file) for notarytool instead of an Apple ID password ([Apple forum](https://developer.apple.com/forums/thread/786582)).
- Without signing
  - On Apple silicon all code needs at least an ad-hoc signature, which the linker adds ([Apple forum](https://developer.apple.com/forums/thread/698167)).
  - Since macOS 15 the Control-click bypass is gone: a blocked app or pkg needs System Settings, Privacy & Security, Open Anyway ([Michael Tsai](https://mjtsai.com/blog/2024/07/05/sequoia-removes-gatekeeper-contextual-menu-override)). That does not work for plugins, which need `xattr -dr com.apple.quarantine` ([JUCE forum](https://forum.juce.com/t/mac-open-anyway-not-working-for-unsigned-mac-vst3/64574)).
  - Files the Installer lays down are not quarantined ([Apple forum](https://developer.apple.com/forums/thread/743380)), so an unsigned pkg let through Open Anyway installs plugins that load: the free path, with a warning.

## Where things go

- Plugins
  - VST3: `~/Library/Audio/Plug-Ins/VST3` first, then `/Library/Audio/Plug-Ins/VST3` ([Steinberg](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Plugin+Locations.html)).
  - CLAP: `/Library/Audio/Plug-Ins/CLAP` and `~/Library/Audio/Plug-Ins/CLAP`, plus `CLAP_PATH` ([CLAP entry.h](https://raw.githubusercontent.com/free-audio/clap/main/include/clap/entry.h)).
  - AU: `/Library/Audio/Plug-Ins/Components` or `~/Library/Audio/Plug-Ins/Components`, nowhere else ([Apple](https://support.apple.com/en-us/102239)).
  - The app: `/Applications`.
- User data
  - Settings and the controller setup in `~/Library/Application Support/CoolSynth`, logs in `~/Library/Logs/CoolSynth`, My Sounds in `~/Documents` as now.
  - Large sample content usually goes to `/Library/Application Support/<maker>`; `~/Music` is the one place sandboxed hosts such as GarageBand let an AU read freely ([JUCE forum](https://forum.juce.com/t/permission-denied-writing-to-library-on-mac/55211), [JUCE forum](https://forum.juce.com/t/should-i-store-presets-in-users-shared-or-documents/68171)).
- DAW scripts
  - FL Studio: `~/Documents/Image-Line/FL Studio/Settings/Hardware/<name>`, the same path as on Windows ([Image-Line](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/midi_scripting.htm), [FL API stubs](https://il-group.github.io/FL-Studio-API-Stubs/midi_controller_scripting/script_files)).
  - Ableton Live: `~/Music/Ableton/User Library/Remote Scripts/<name>`, read at Live's start ([Ableton](https://help.ableton.com/hc/en-us/articles/209072009)).
  - A pkg runs as root, so copying into the user's folders needs a postinstall script that finds the console user, or the app copies them on first run.

## MIDI and the MiniLab 3

- CoreMIDI shares every port between programs by design ([WWDC 2001 CoreMIDI session](https://nonstrict.eu/wwdcindex/wwdc2001/210/)), so the MiniLab is never "busy" on a Mac; but the DAW hears the DAW port too, so the plugin's drop of driver-owned messages and the duplicate-note filter matter as much as in FL on Windows.
- Ports
  - Four: the main MIDI port, DIN Thru, MCU/HUI and ALV ([Arturia general questions](https://support.arturia.com/hc/en-us/articles/6189475866396-MiniLab-3-General-Questions)). Read the real names in Audio MIDI Setup's MIDI Studio and in JUCE's device list.
  - JUCE 8.0.11 briefly put a colon between device and port names of MIDI 2.0 devices; reverted in December 2025 ([JUCE forum](https://forum.juce.com/t/juce-8-0-11-midi-port-name/67745)).
- SysEx to the screen
  - Same bytes on both systems. On macOS JUCE sends through CoreMIDI's UMP path; a 2026 report had a long SysEx split with a 0x0F packet, and JUCE now sends a whole sequence at once, but the OS may still split long SysEx (same thread). Check the longest screen messages on the device.
- Plugin instances
  - JUCE 8.0.11 and 8.0.12 failed to reopen a MIDI device after a plugin instance was removed on macOS; fixed in February 2026 ([JUCE forum](https://forum.juce.com/t/midi-device-fails-to-reconnect-after-plugin-instance-removal-on-macos-j-8-0-11-8-0-12/68132)). Test add, remove, re-add in a DAW.
  - Logic hosts AUs in AUHostingService, so the "one CoolSynth" rule and the named lock need a test with several instances.
- Logic
  - Arturia ships a Logic control-surface script for the MiniLab 3 (Logic 10.7.4 and later) that also takes the screen ([Arturia DAW integration](https://support.arturia.com/hc/en-us/articles/6283884020508-MiniLab-3-DAW-Integration)). With CoolSynth's driver on, both would likely draw on the screen; to check, and if so, the README tells Logic users to remove that surface.

## CI

- GitHub's macOS 26 runner is arm64, with Xcode 26.0.1 to 26.6 (26.6 default), CMake 4.4 and Homebrew LLVM 20; image of 2026-11-02 ([runner image](https://github.com/actions/runner-images/blob/main/images/macos/macos-26-arm64-Readme.md), [GitHub changelog](https://github.blog/changelog/2026-02-26-macos-26-is-now-generally-available-for-github-hosted-runners/)).
- `macos-26-intel` exists for x86_64 testing.
- Standard runners are free for public repositories ([GitHub billing](https://docs.github.com/en/billing/managing-billing-for-github-actions/about-billing-for-github-actions)).
- Secrets: each certificate as a base64 .p12 plus its password, imported into a temporary keychain ([GitHub docs](https://docs.github.com/en/actions/how-tos/deploy/deploy-to-third-party-platforms/sign-xcode-applications)), and the notarisation key.
- Pamplejuce (MIT, Sudara Williams) is the model: Ninja, universal, pluginval at strictness 10, `sudara/basic-macos-keychain-action`, codesign with the hardened runtime, pkgbuild per format, productbuild, notarytool and stapler ([workflow](https://github.com/sudara/pamplejuce/blob/main/.github/workflows/build_and_test.yml), [licence](https://github.com/sudara/pamplejuce/blob/main/LICENSE)).

## The plan

### Phase 1: a first build that runs

- Xcode, CMake and Ninja; macOS presets in `CMakePresets.json` (arm64, Ninja, target 12.0). The `else()` branch of `coolsynth_config_flags` already gives Clang an optimised Debug.
- Fix what Clang rejects and the folders above; build the standalone and the VST3; run the test program.
- Listen to every room against Windows renders, on the scalar paths.

### Phase 2: the formats

- Add AU to `FORMATS`, the hardened runtime and the bundle ID; build the CLAP.
- auval, pluginval at strictness 10, clap-validator.
- Try Logic, GarageBand, Ableton, FL Studio, Reaper and Bitwig, including CLAP remote pages.
- Turn on the universal build.
- Bring the performance up: the SIMD paths on NEON, the worker threads joined to the audio workgroup, then compare voice counts with Windows.
- A Homebrew-LLVM sanitizer build of the test program with RTSan.

### Phase 3: the MiniLab

- Read the MiniLab's port names on macOS and widen `isDawPort` (ignore spaces) if needed.
- Connect, screen, pad colours, knobs, unplug and replug, in the app and in each DAW.
- Several instances, in one DAW and in two at once; remove and re-add an instance.
- Notes arriving twice, pads playing notes, Logic's Arturia surface.
- Install the FL and Ableton scripts into their macOS folders.

### Phase 4: signing and the installer

- The pkg: AU, VST3, CLAP and the app as separate choices; the scripts; the instruments.
- Sign, notarise, staple; test on a clean user account, downloaded through a browser.
- README: the macOS install, the Gatekeeper step if unsigned, and building from source on a Mac.

### Phase 5: CI

- A macOS job beside the Windows one: build, tests, validators, the pkg, the release upload.
- The signing secrets, if the program is bought.

## Decisions for the user

Each with the recommended option first.

### Signing and notarisation

- Join the Apple Developer Program and notarise.
  - Gives: a pkg and plugins that install and load with no warning, in every host.
  - Cost: 99 USD a year, an Apple ID with two-factor sign-in, enrolment as an individual under the user's legal name ([Apple enrolment](https://developer.apple.com/help/account/membership/program-enrollment/)).
  - Side effects: the certificates become CI secrets to guard; builds made in a lapsed year cannot be notarised.
- Ship unsigned (ad-hoc signed) and explain the steps.
  - Gives: no cost.
  - Cost: every user must find Open Anyway in System Settings for the pkg, or run `xattr` in Terminal for zips.
  - Side effects: many Mac users will stop at the warning.

### Minimum macOS

- macOS 12 Monterey.
  - Gives: the oldest that Xcode 27 and JUCE 9.0.5 support; Macs from about 2015 on.
  - Cost: none now.
  - Side effects: test on 12 needs an old Mac or a VM.
- macOS 13, 14 or 15.
  - Gives: newer APIs and fewer old systems to test.
  - Cost: shuts out users on older Macs.
- macOS 11 Big Sur.
  - Gives: a few more old Macs.
  - Cost: Xcode 27 cannot build it; stuck on Xcode 26.

### Architectures

- Universal (arm64 and x86_64).
  - Gives: Intel Macs on macOS 12 to 26, Logic under Rosetta, and hosts still running Intel.
  - Cost: builds take about twice as long; two SIMD paths to keep equal.
  - Side effects: Intel testing needs `macos-26-intel` runners or an Intel Mac.
- Apple silicon only.
  - Gives: one path, quicker builds.
  - Cost: no Intel Macs; an x86 host cannot load it.

### Audio Unit

- AUv2 (.component).
  - Gives: Logic, GarageBand and MainStage, in the way Mac users expect.
  - Cost: auval in the checks; one more format to test.
  - A 2026 JUCE forum thread leans to AUv2 alone for a desktop plugin ([JUCE forum](https://forum.juce.com/t/to-auv3-or-not-to-auv3/68301)).
- AUv2 and AUv3.
  - Gives: sandbox safety in GarageBand, a path to iPad later.
  - Cost: a host app wrapper, sandbox entitlements for the sample library, and Logic picks the AUv3 when both are installed ([JUCE forum](https://forum.juce.com/t/auv2-and-auv3-compatibility/56300)).
- No AU: no Logic or GarageBand at all.

### Installer

- A signed .pkg (productbuild) with a choice per format.
  - Gives: each file in its right folder; no quarantine on what it installs.
  - Cost: distribution.xml and scripts to keep.
- A .dmg with the app and the plugin bundles to drag.
  - Gives: simple to make; suits an app.
  - Cost: users must find the plugin folders themselves; downloaded bundles stay quarantined if unsigned.

### Where plugins install

- The system folders in `/Library/Audio/Plug-Ins`.
  - Gives: every host and every user finds them; what most makers do.
  - Cost: an administrator password at install.
- The user folders in `~/Library/Audio/Plug-Ins`.
  - Gives: no administrator needed.
  - Cost: only this user; a few hosts scan only the system folders.

### Where the sample library lives

- `/Library/Application Support/CoolSynth/Library`.
  - Gives: the Mac's usual place for shared content, like Logic's own.
  - Cost: the installer must make it writable for `User`, or user samples go elsewhere.
  - Side effects: GarageBand's sandbox may not let an AU read it.
- `/Users/Shared/CoolSynth/Library` (JUCE's `commonDocumentsDirectory`).
  - Gives: shared and writable by all users, no permission work.
  - Cost: less usual; visible in Finder as a plain folder.
- `~/Music/CoolSynth/Library`.
  - Gives: readable even in sandboxed hosts.
  - Cost: per user; 2.6 GB per account.

### Downloading the instruments

- The app and the plugin offer the download on first run.
  - Gives: one small pkg; the same downloader for later instruments.
  - Cost: a downloader with resume and checksums in CoolSynth.
- A postinstall script in the pkg.
  - Gives: everything in one go, as on Windows.
  - Cost: a 2.6 GB download as root inside the installer, with poor progress.

### SIMD on Apple silicon

- The SSE paths through sse2neon (MIT).
  - Gives: the same operations as on Windows, four lanes, little new code.
  - Cost: one more header; check bits against the scalar path.
- Hand-written NEON: the fastest, but a third path to keep equal.
- The scalar paths only: nothing to write, fewer voices for unison and the ladder.

### Floating-point contraction

- `-ffp-contract=off` for Clang.
  - Gives: the same arithmetic as written and as on MSVC.
  - Cost: a little speed.
- Clang's default.
  - Gives: a little speed.
  - Cost: renders differ from Windows in the last bits; golden-number tests may fail.

### Real-time checks

- RTSan through Homebrew LLVM in a separate build, and keep the Windows alarm.
  - Gives: malloc, locks and file calls caught on macOS with stacks.
  - Cost: a second compiler and a CI job.
- A fishhook branch in the existing alarm.
  - Gives: the same test program on both systems.
  - Cost: new low-level code to maintain.

### CI

- GitHub's hosted macOS 26 arm64 runner, on release tags like the Windows job, plus a manual run.
  - Gives: free for a public repository; a clean machine every time.
  - Cost: slower queues than Windows; Xcode 27 not on the image yet.
- The user's own Mac as a self-hosted runner.
  - Gives: fast, with the real hardware.
  - Cost: the Mac must be on; secrets on a personal machine.
