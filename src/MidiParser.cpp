


#include "MidiParser.h"
#include <iostream>
#include "MidiFile.h"

using namespace smf;



bool MidiParser::load(const std::string& filename)
{
    MidiFile midi;

    if (!midi.read(filename))
    {
        std::cout << "Cannot open file: "
                  << filename
                  << std::endl;

        return false;
    }

    midi.doTimeAnalysis();

    std::cout << "\n=== MIDI INFO ===\n";
    std::cout << "Tracks: "
              << midi.getTrackCount()
              << std::endl;

    int lyricCount = 0;
    int chordCount = 0;

    for (int track = 0; track < midi.getTrackCount(); track++)
    {
        for (int event = 0; event < midi[track].size(); event++)
        {
            MidiEvent& me = midi[track][event];

            if (me.isMeta())
            {
                std::cout
                    << "track: "
                    << track
                    << " -> "
                    << me.seconds
                    << " : "
                    << (me.isLyricText()?"L":"")
                    << (me.isText())?"T":"")
                    << " "
                    << me.getMetaType()
                    << " : "
                    << me.getMetaContent()
                    << std::endl;
            }

            continue;

            if (!me.isMeta())
                continue;

            // Lyrics
            if (me.isLyricText())
            {
                if (lyricCount < 10)
                {
                    std::cout
                        << "[LYRIC] "
                        << me.seconds
                        << " : "
                        << me.getMetaContent()
                        << std::endl;
                }

                lyricCount++;
            }

            // Generic text events
            if (me.isText())
            {
                std::string text = me.getMetaContent();

                // simpele akkoorddetectie
                if (text.find("C") != std::string::npos ||
                    text.find("Dm") != std::string::npos ||
                    text.find("G") != std::string::npos)
                {
                    if (chordCount < 10)
                    {
                        std::cout
                            << "[TEXT] "
                            << me.seconds
                            << " : "
                            << text
                            << std::endl;
                    }

                    chordCount++;
                }
            }
        }
    }

    std::cout << "\nLyrics found : "
              << lyricCount
              << std::endl;

    std::cout << "Chord-like text found : "
              << chordCount
              << std::endl;

    return true;
}