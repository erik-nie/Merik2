
// mkdir external
// cd external
// git clone https://github.com/craigsapp/midifile.git


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

    std::cout << "Loaded: "
              << filename
              << std::endl;

    std::cout << "Tracks: "
              << midi.getTrackCount()
              << std::endl;

    std::cout << "Ticks per quarter: "
              << midi.getTicksPerQuarterNote()
              << std::endl;

    return true;
}