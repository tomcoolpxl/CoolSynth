#include "InstrumentCatalogue.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <InstrumentData.h>

namespace coolsynth::synth::v3
{
    namespace
    {
        std::string trimmed(const std::string& text)
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return {};
            const auto last = text.find_last_not_of(" \t\r\n");
            return text.substr(first, last - first + 1);
        }

        std::vector<std::string> fields(const std::string& line)
        {
            std::vector<std::string> parts;
            std::string part;
            std::istringstream stream(line);
            while (std::getline(stream, part, '|'))
                parts.push_back(trimmed(part));
            return parts;
        }
    }

    std::string InstrumentCatalogue::slug(const std::string& name)
    {
        std::string out;
        bool pendingHyphen = false;
        for (const char c : name)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) != 0)
            {
                if (pendingHyphen && ! out.empty())
                    out += '-';
                pendingHyphen = false;
                out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            else
            {
                pendingHyphen = true;
            }
        }
        return out.empty() ? "x" : out;
    }

    std::string InstrumentCatalogue::plain(const std::string& name)
    {
        std::string out;
        for (const char c : name)
            if (std::isalnum(static_cast<unsigned char>(c)) != 0)
                out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

    InstrumentCatalogue::InstrumentCatalogue(const std::string& text)
    {
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            line = trimmed(line.substr(0, line.find('#')));
            if (line.empty())
                continue;
            const auto parts = fields(line);
            if (parts.size() < 4 || parts[0].empty() || parts[1].empty() || parts[2].empty())
                continue;

            auto found = std::find_if(instruments.begin(), instruments.end(), [&](const Instrument& i) { return i.name == parts[1]; });
            if (found == instruments.end())
            {
                instruments.push_back({ parts[0], parts[1], {} });
                found = instruments.end() - 1;
                if (std::find(familyNames.begin(), familyNames.end(), parts[0]) == familyNames.end())
                    familyNames.push_back(parts[0]);
            }
            if (static_cast<int>(found->styles.size()) >= maxInstrumentStyles)
                continue;
            found->styles.push_back({ parts[2], slug(found->family) + "/" + slug(found->name) + "/" + slug(parts[2]), plain(parts[3]) == "yes" });
        }

        for (const auto& instrument : instruments)
            joined += (joined.empty() ? "" : "|") + instrument.name;
    }

    const InstrumentCatalogue& InstrumentCatalogue::instance()
    {
        static const InstrumentCatalogue catalogue(std::string(InstrumentData::catalogue_txt, static_cast<std::size_t>(InstrumentData::catalogue_txtSize)));
        return catalogue;
    }

    const Instrument& InstrumentCatalogue::at(int index) const noexcept
    {
        static const Instrument none {};
        return index >= 0 && index < count() ? instruments[static_cast<std::size_t>(index)] : none;
    }

    int InstrumentCatalogue::find(const std::string& name) const noexcept
    {
        const auto wanted = plain(name);
        for (int i = 0; i < count(); ++i)
            if (plain(instruments[static_cast<std::size_t>(i)].name) == wanted)
                return i;
        return -1;
    }

    int InstrumentCatalogue::findStyle(int instrument, const std::string& name) const noexcept
    {
        const auto wanted = plain(name);
        const auto& styles = at(instrument).styles;
        for (std::size_t i = 0; i < styles.size(); ++i)
            if (plain(styles[i].name) == wanted)
                return static_cast<int>(i);
        return -1;
    }
}
