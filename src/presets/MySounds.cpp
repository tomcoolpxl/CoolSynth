#include "MySounds.h"

#include "PatchState.h"

namespace coolsynth::presets
{
    namespace
    {
        juce::File& testFolder()
        {
            static juce::File folder;
            return folder;
        }
    }

    juce::File MySounds::folder()
    {
        if (testFolder() != juce::File())
            return testFolder();
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("CoolSynth").getChildFile("My Sounds");
    }

    void MySounds::setFolderForTesting(const juce::File& folder)
    {
        testFolder() = folder;
    }

    juce::Array<juce::File> MySounds::list()
    {
        auto files = folder().findChildFiles(juce::File::findFiles, false, juce::String("*") + defaultPatchExtension);
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension()) < 0;
        });
        return files;
    }

    juce::String MySounds::cleanName(const juce::String& name)
    {
        return juce::File::createLegalFileName(name.trim()).trim().substring(0, 60).trim();
    }

    juce::File MySounds::fileFor(const juce::String& name)
    {
        return folder().getChildFile(cleanName(name) + defaultPatchExtension);
    }
}
