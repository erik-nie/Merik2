#include "FluidSynthEventSink.h"
// #include "ConsoleEventSink.h"
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
    if (argc < 3 || argc > 4)
    {
        std::cerr << "Usage: merik <file.mid> <soundfont.sf2> [start-seconds]\n";
        return 2;
    }

    try
    {
        const auto song = std::make_shared<Song>(MidiFileReader{}.read(argv[1]));

        std::cout << "Loaded: " << song->sourceFile << '\n'
                  << "Events: " << song->playbackEvents.size() << '\n'
                  << "Lyrics: " << song->lyrics.size() << '\n'
                  << "Sample rate: " << song->sampleRate << " Hz\n"
                  << "SoundFont: " << argv[2] << "\n\n";

        // ConsoleEventSink sink;
        // MidiPlayer player(sink);
        // player.load(song);

        FluidSynthEventSink sink(argv[2], song->sampleRate);
        MidiPlayer player(sink);
        player.load(song);

        if (argc == 4)
        {
            const auto seconds = std::stod(argv[3]);
            player.seek(std::chrono::milliseconds {
                static_cast<std::int64_t>(seconds * 1000.0)
            });
        }

        player.start();

        while (player.isPlaying())
            std::this_thread::sleep_for(std::chrono::milliseconds { 50 });

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
