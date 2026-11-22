# The build settings every CoolSynth program shares (D265): the C++ runtime and the optimised Debug build.
# Included before any target exists, so JUCE's modules are built the same way.

# A Release build carries Microsoft's C++ runtime inside it (/MT): the app and the VST3 run on a computer with
# nothing installed (the debug runtime may not be passed on, so a Debug build never leaves this machine). Debug
# keeps the runtime DLLs, as before. Set before any target exists, so JUCE's modules link the same way.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>")

# Debug is the everyday build, and the plugin it installs must play in real time: compile it
# optimised, with debug info and JUCE's assertions still on. CMake's /RTC1 cannot be combined
# with optimisation, and JUCE's recommended config flags force /Od in Debug, so CoolSynth targets
# use this flags target instead of the juce_recommended_config_flags target.
add_library(coolsynth_config_flags INTERFACE)
if(MSVC)
    foreach(flagVariable CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
        string(REPLACE "/RTC1" "" ${flagVariable} "${${flagVariable}}")
        string(REPLACE "/Ob0" "" ${flagVariable} "${${flagVariable}}")
        string(REPLACE "/Od" "" ${flagVariable} "${${flagVariable}}")
        # Debug info comes from coolsynth_config_flags on the targets that have code; the data-only
        # libraries (the embedded bank, the controller profiles) need none.
        string(REPLACE "/Zi" "" ${flagVariable} "${${flagVariable}}")
    endforeach()
    # No incremental linking: when a new bank file added a member to the embedded bank library,
    # the stale incremental-link databases made the linker reject the library (LNK1236), and an
    # optimised Debug build gains little from linking incrementally.
    foreach(linkerVariable CMAKE_EXE_LINKER_FLAGS_DEBUG CMAKE_SHARED_LINKER_FLAGS_DEBUG CMAKE_MODULE_LINKER_FLAGS_DEBUG)
        string(REPLACE "/INCREMENTAL:NO" "" ${linkerVariable} "${${linkerVariable}}")
        string(REPLACE "/INCREMENTAL" "" ${linkerVariable} "${${linkerVariable}}")
        string(APPEND ${linkerVariable} " /INCREMENTAL:NO")
    endforeach()
    # Release keeps its symbols in a .pdb beside the binary (not inside it), so a crash in a shipped build can be
    # traced; /OPT:REF and /OPT:ICF keep the binary as lean as without them.
    target_compile_options(coolsynth_config_flags INTERFACE
        $<IF:$<CONFIG:Debug>,/O2 /Ob2 /Zi,/Ox /Zi> /MP /EHsc)
    target_link_options(coolsynth_config_flags INTERFACE
        $<$<NOT:$<CONFIG:Debug>>:/DEBUG /OPT:REF /OPT:ICF>)
else()
    target_compile_options(coolsynth_config_flags INTERFACE
        $<IF:$<CONFIG:Debug>,-O2 -g,-O3>)
endif()
