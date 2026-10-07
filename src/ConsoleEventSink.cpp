#include "ConsoleEventSink.h"

#include <iomanip>
#include <iostream>
#include <sstream>

std::string ConsoleEventSink::formatTime(std::chrono::milliseconds position,
                                         bool includeMilliseconds)
{
    const auto totalMs = position.count();
    const auto minutes = totalMs / 60000;
    const auto seconds = (totalMs / 1000) % 60;
    const auto milliseconds = totalMs % 1000;

    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << minutes
           << std::setw(2) << seconds;

    if (includeMilliseconds)
        output << '.' << std::setw(3) << milliseconds;

    return output.str();
}

void ConsoleEventSink::onSecond(std::chrono::milliseconds position)
{
    const std::scoped_lock lock(outputMutex);
    std::cout << formatTime(position, false) << ":\n";
}

void ConsoleEventSink::onLyric(const LyricEvent& lyric)
{
    const auto time = std::chrono::milliseconds {
        static_cast<std::int64_t>(lyric.seconds * 1000.0 + 0.5)
    };

    const std::scoped_lock lock(outputMutex);
    std::cout
        << formatTime(time, true)
        << ": ";
        if (!currentChord.empty())
        {
        std::cout
        << "["
        << currentChord
        << "] ";
        }
        std::cout
        << lyric.text
        << '\n';
}
void ConsoleEventSink::onChord(
    const SongChordEvent& chord)
{
    const auto time =
        std::chrono::milliseconds{
            static_cast<std::int64_t>(
                chord.seconds * 1000.0 + 0.5)
        };

    const std::scoped_lock lock(outputMutex);

    currentChord = chord.label;

    std::cout
        << formatTime(time, true)
        << ": ["
        << chord.label
        << "]\n";
}

void ConsoleEventSink::setCurrentChord(
    const std::string& chord)
{
    const std::scoped_lock lock(
        outputMutex);

    currentChord = chord;
}

void ConsoleEventSink::onMidiEvent(const RawMidiEvent& event)
{
    if (event.bytes.size() < 3)
        return;

    const auto status = event.bytes[0];
    const auto command = status & 0xF0;

    // Note-on/off are deliberately ignored in this console prototype.
    if (command == 0x80 || command == 0x90)
        return;

    if (command != 0xB0)
        return;

    const auto time = std::chrono::milliseconds {
        static_cast<std::int64_t>(event.seconds * 1000.0 + 0.5)
    };
    const auto controller = static_cast<int>(event.bytes[1]);
    const auto value = static_cast<int>(event.bytes[2]);
    const auto channel = static_cast<int>(status & 0x0F) + 1;

    const std::scoped_lock lock(outputMutex);
    std::cout << formatTime(time, true)
              << ": ch" << channel
              << " cc" << controller
              << ": " << value << '\n';
}
