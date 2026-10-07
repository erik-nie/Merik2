#pragma once

#include "PlaybackInterfaces.h"
#include <mutex>

class ConsoleEventSink final : public IPlaybackEventSink
{
public:
    void onSecond(std::chrono::milliseconds position) override;
    void onLyric(const LyricEvent& lyric) override;
    void onMidiEvent(const RawMidiEvent& event) override;
    void onChord(const SongChordEvent& chord) override;
    void setCurrentChord(const std::string& chord);

private:
    static std::string formatTime(std::chrono::milliseconds position,
                                  bool includeMilliseconds);
    std::mutex outputMutex;
    std::string currentChord;
};
