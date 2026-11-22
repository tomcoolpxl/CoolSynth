// The engine's own test program (D265): the tests that need nothing but the engine, run as CoolSynth's runner runs
// its own (tests/MidiLearnTests.cpp), without the app around them.

#include <cstdio>
#include <iostream>

#include <juce_core/juce_core.h>

#include "TestSampleLibrary.h"

namespace
{
    class StdoutLogger : public juce::Logger
    {
    protected:
        void logMessage(const juce::String& message) override { std::cout << message.toRawUTF8() << std::endl; }
    };
}

int main()
{
    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);
    // A crash prints where it happened (the thread's stack, with names when the .pdb is beside the runner).
    juce::SystemStats::setApplicationCrashHandler([](void*)
    {
        std::fputs(("\nCRASH\n" + juce::SystemStats::getStackBacktrace() + "\n").toRawUTF8(), stderr);
        std::fflush(stderr);
    });
    // No test reads the real sampled-instrument library: a small stand-in is written and used instead.
    coolsynth::testing::installTestSampleLibrary();
    juce::UnitTestRunner runner;
    // COOLSYNTH_TEST_ONLY=part of a test's name runs only the tests whose names contain it.
    if (const auto only = juce::SystemStats::getEnvironmentVariable("COOLSYNTH_TEST_ONLY", {}); only.isNotEmpty())
    {
        juce::Array<juce::UnitTest*> chosen;
        for (auto* test : juce::UnitTest::getAllTests())
            if (test->getName().containsIgnoreCase(only))
                chosen.add(test);
        runner.runTests(chosen);
    }
    else
        runner.runAllTests();
    coolsynth::testing::removeTestSampleLibrary();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (auto* result = runner.getResult(i))
            failures += result->failures;

    juce::Logger::setCurrentLogger(nullptr);
    return failures == 0 ? 0 : 1;
}
