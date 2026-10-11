#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// MIDI-bestanden bevatten tekst (lyrics, titels) vaak in Windows-1252 /
// Latin-1 in plaats van UTF-8. Een "é" is dan het enkele byte 0xE9, wat
// als UTF-8 ongeldig is en in de browser als een vraagteken-blokje
// verschijnt. Deze helpers herkennen dat en zetten het om naar UTF-8.
namespace TextEncoding
{
    [[nodiscard]] inline bool isValidUtf8(const std::string& text)
    {
        std::size_t i = 0;

        while (i < text.size())
        {
            const auto c = static_cast<unsigned char>(text[i]);
            std::size_t extra = 0;

            if (c < 0x80)       extra = 0;
            else if (c >= 0xC2 && c <= 0xDF) extra = 1;
            else if (c >= 0xE0 && c <= 0xEF) extra = 2;
            else if (c >= 0xF0 && c <= 0xF4) extra = 3;
            else return false;

            if (i + extra >= text.size() && extra != 0)
                return false;

            for (std::size_t k = 1; k <= extra; ++k)
                if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
                    return false;

            i += extra + 1;
        }

        return true;
    }

    inline void appendCodePoint(std::string& out, std::uint32_t cp)
    {
        if (cp < 0x80)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    // Windows-1252 -> UTF-8 (0xA0-0xFF is identiek aan Latin-1).
    [[nodiscard]] inline std::string windows1252ToUtf8(const std::string& text)
    {
        static constexpr std::uint16_t high[32] = {
            0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
            0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
            0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
            0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
        };

        std::string out;
        out.reserve(text.size() + text.size() / 4);

        for (const char ch : text)
        {
            const auto c = static_cast<unsigned char>(ch);

            if (c >= 0x80 && c < 0xA0)
                appendCodePoint(out, high[c - 0x80]);
            else
                appendCodePoint(out, c);
        }

        return out;
    }
}
