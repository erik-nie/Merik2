#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

// Herkent songonderdelen ("Intro", "Verse 1", "Refrain", "Solo", ...) in marker-teksten van
// een MIDI-bestand. Vormen die voorkomen: "Intro", "Verse 2", "Chorus", en de variant
// "15@Refrain 1:" of "1@Refrain:..." (nummer, @, naam, dubbele punt). Andere markers
// (auteursrecht, "SPJ-01", lege teksten, akkoorden) worden genegeerd.
namespace SectionParser
{
    inline std::string trim(const std::string& text)
    {
        std::size_t begin = 0;
        std::size_t end = text.size();

        while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
            ++begin;

        while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
            --end;

        return text.substr(begin, end - begin);
    }

    inline std::string lowerAscii(std::string text)
    {
        for (auto& c : text)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');

        return text;
    }

    inline bool startsWithWord(const std::string& lowered, const char* word)
    {
        const std::string w(word);

        if (lowered.rfind(w, 0) != 0)
            return false;

        // Hele woord (of gevolgd door een cijfer, spatie of streepje): "verse 2", "vers-1", "chorus".
        return lowered.size() == w.size() || !std::isalpha(static_cast<unsigned char>(lowered[w.size()]));
    }

    // "INTRO" -> "Intro", "VERS-2" -> "Vers 2". Gemengde schrijfwijze blijft zoals ze is.
    inline std::string tidy(std::string name)
    {
        for (std::size_t i = 1; i + 1 < name.size(); ++i)
            if (name[i] == '-' && std::isalpha(static_cast<unsigned char>(name[i - 1]))
                && std::isdigit(static_cast<unsigned char>(name[i + 1])))
                name[i] = ' ';

        bool anyLower = false;
        bool anyUpper = false;

        for (const char c : name)
        {
            anyLower = anyLower || (c >= 'a' && c <= 'z');
            anyUpper = anyUpper || (c >= 'A' && c <= 'Z');
        }

        if (anyUpper && !anyLower)
        {
            bool startOfWord = true;

            for (auto& c : name)
            {
                if (std::isalpha(static_cast<unsigned char>(c)))
                {
                    c = startOfWord ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                                    : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    startOfWord = false;
                }
                else
                {
                    startOfWord = !std::isdigit(static_cast<unsigned char>(c));
                }
            }
        }

        return name;
    }

    inline std::optional<std::string> parse(const std::string& rawText)
    {
        auto text = trim(rawText);

        if (text.empty())
            return std::nullopt;

        // "15@Refrain 1:" -> nummer@naam:
        bool atForm = false;
        std::size_t digits = 0;

        while (digits < text.size() && std::isdigit(static_cast<unsigned char>(text[digits])))
            ++digits;

        if (digits > 0 && digits < text.size() && text[digits] == '@')
        {
            atForm = true;
            text = trim(text.substr(digits + 1));
        }

        // Afsluitende ":" en "..." weghalen ("Refrain:...", "Intro:").
        while (!text.empty() && (text.back() == ':' || text.back() == '.' || std::isspace(static_cast<unsigned char>(text.back()))))
            text.pop_back();

        const auto colon = text.find(':');

        if (colon != std::string::npos)
            text = trim(text.substr(0, colon));

        if (text.empty())
            return std::nullopt;

        bool hasLetter = false;

        for (const char c : text)
            hasLetter = hasLetter || std::isalpha(static_cast<unsigned char>(c));

        if (!hasLetter)
            return std::nullopt;

        if (!atForm)
        {
            static const char* const words[] = {
                "intro", "verse", "vers", "couplet", "strofe", "strophe", "chorus", "refrain", "koor", "bridge",
                "brug", "solo", "interlude", "tussenspel", "instrumental", "instr", "outro", "ending", "coda",
                "fine", "pre-chorus", "prechorus", "pre chorus", "thema", "theme", "break", "part", "middle",
                "hook", "tag", "turnaround", "vamp", "end", "slot", "einde", "refrein", "opening", "reprise"
            };

            const auto lowered = lowerAscii(text);
            bool known = false;

            for (const char* word : words)
                if (startsWithWord(lowered, word))
                {
                    known = true;
                    break;
                }

            if (!known)
                return std::nullopt;
        }

        // Een naam is kort; langere teksten zijn geen songonderdeel.
        if (text.size() > 30)
            return std::nullopt;

        return tidy(text);
    }
}
