// #include "FluidSynthEventSink.h"
#include "ConsoleEventSink.h"
#include "MidiFileReader.h"
#include "MidiPlayer.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

int main(int argc, char* argv[])
{
    if (argc < 2 || argc > 3)
    {
        std::cerr << "Usage: merik <file.mid>  [start-seconds]\n";
        return 2;
    }

    try
    {
        auto song = std::make_shared<Song>(MidiFileReader{}.read("../test/midi/badgirls.mid"));
        song = std::make_shared<Song>(MidiFileReader{}.read("../test/midi/corrie.MID"));
        song = std::make_shared<Song>(MidiFileReader{}.read("../test/midi/dolly.mid"));
        song = std::make_shared<Song>(MidiFileReader{}.read("../test/midi/paradise.mid"));
        song = std::make_shared<Song>(MidiFileReader{}.read("../test/midi/relightmyfire.mid"));
        song = std::make_shared<Song>(MidiFileReader{}.read("../test/midi/superstition.mid"));
        song = std::make_shared<Song>(MidiFileReader{}.read("../test/midi/terug.mid"));
        song = std::make_shared<Song>(MidiFileReader{}.read("../test/midi/thelastdance.mid"));
        song = std::make_shared<Song>(MidiFileReader{}.read("../test/midi/valerie.mid"));
        song = std::make_shared<Song>(MidiFileReader{}.read("../test/midi/verliefd.mid"));
        
        song = std::make_shared<Song>(MidiFileReader{}.read(argv[1]));

        std::cout << "Loaded: " << song->sourceFile << '\n'
                  << "Events: " << song->playbackEvents.size() << '\n'
                  << "Lyrics: " << song->lyrics.size() << '\n'
                  << "Sample rate: " << song->sampleRate << " Hz\n\n";

        ConsoleEventSink sink;
        MidiPlayer player(sink);
        player.load(song);



        if (argc == 3)
        {
            const auto seconds =
                std::stod(argv[2]);

            player.seek(
                std::chrono::milliseconds{
                    static_cast<std::int64_t>(
                        seconds * 1000.0)
                });
        }

        player.start();

        while (player.isPlaying())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds{50});
        }

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "Error: "
            << error.what()
            << '\n';

        return 1;
    }
}