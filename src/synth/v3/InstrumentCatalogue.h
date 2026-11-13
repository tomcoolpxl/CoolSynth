#pragma once

#include <string>
#include <vector>

/*
    The sampled instruments the synth knows (decisions D96 to D98), read from
    resources/instruments/catalogue.txt, which is built into the binary.

    Three levels: a family (Keys, Strings, ...), an instrument (Cellos) and up to four styles of
    playing it (Sustain, Tremolo, Pizzicato). An instrument's number is the order in which it
    first appears in the file and its styles are numbered in order; both are saved in patches,
    so the file only ever grows at the end.

    The catalogue says what exists. Whether an instrument's audio is installed is the sample
    library's business (SampleLibrary.h): each style lives in the folder named here, under the
    library's root.
*/
namespace coolsynth::synth::v3
{
    inline constexpr int maxInstrumentStyles = 4;

    struct InstrumentStyle
    {
        std::string name;      // "Tremolo"
        std::string folder;    // "strings/cellos/tremolo", below the library root
        bool sustains = false; // a held note goes on as long as the key is down
    };

    struct Instrument
    {
        std::string family; // "Strings"
        std::string name;   // "Cellos"
        std::vector<InstrumentStyle> styles;
    };

    class InstrumentCatalogue final
    {
    public:
        /** The built-in catalogue, parsed on first use (thread-safe). */
        static const InstrumentCatalogue& instance();

        /** Parses catalogue text: "family | instrument | style | yes or no | source" per line, '#' comments. */
        explicit InstrumentCatalogue(const std::string& text);

        [[nodiscard]] int count() const noexcept { return static_cast<int>(instruments.size()); }
        [[nodiscard]] const Instrument& at(int index) const noexcept;
        [[nodiscard]] const std::vector<std::string>& families() const noexcept { return familyNames; }

        /** The instrument whose name matches, ignoring case, spaces and punctuation; -1 if none. */
        [[nodiscard]] int find(const std::string& name) const noexcept;
        /** The style of an instrument by name, compared the same way; -1 if none. */
        [[nodiscard]] int findStyle(int instrument, const std::string& name) const noexcept;

        /** Every instrument name joined with '|', in catalogue order. */
        [[nodiscard]] const std::string& joinedNames() const noexcept { return joined; }

        /** Lower case, every run of other characters one hyphen: "Mallets and Bells" to "mallets-and-bells". */
        [[nodiscard]] static std::string slug(const std::string& name);
        /** Lower case letters and digits only: how names are compared. */
        [[nodiscard]] static std::string plain(const std::string& name);

    private:
        std::vector<Instrument> instruments;
        std::vector<std::string> familyNames;
        std::string joined;
    };
}
