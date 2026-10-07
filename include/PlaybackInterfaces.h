#pragma once

#include "Song.h"
#include <chrono>
#include <memory>
#include <string>

class IMidiFileSource
{
public:
    virtual ~IMidiFileSource() = default;
    [[nodiscard]] virtual Song read(const std::string& filename,
                                    double sampleRate = 48000.0) const = 0;
};

class IPlaybackEventSink
{
public:
    virtual ~IPlaybackEventSink() = default;
    virtual void onSecond(std::chrono::milliseconds position) = 0;
    virtual void onLyric(const LyricEvent& lyric) = 0;
    virtual void onMidiEvent(const RawMidiEvent& event) = 0;
    // Standaard no-op, omdat niet iedere sink
    // iets met akkoorden hoeft te doen.
    virtual void onChord( const SongChordEvent& chord) { (void) chord; }
};

class IMidiPlayer
{
public:
    virtual ~IMidiPlayer() = default;
    virtual void load(std::shared_ptr<const Song> song) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void seek(std::chrono::milliseconds position) = 0;
    [[nodiscard]] virtual bool isPlaying() const = 0;
    [[nodiscard]] virtual std::chrono::milliseconds position() const = 0;
};
