#pragma once

#include "PlaybackInterfaces.h"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

class MidiPlayer final : public IMidiPlayer
{
public:
    explicit MidiPlayer(IPlaybackEventSink& sink);
    ~MidiPlayer() override;

    void load(std::shared_ptr<const Song> song) override;
    void start() override;
    void stop() override;
    void seek(std::chrono::milliseconds position) override;
    [[nodiscard]] bool isPlaying() const override;
    [[nodiscard]] std::chrono::milliseconds position() const override;

private:
    using Clock = std::chrono::steady_clock;

    void run();
    void resynchroniseLocked();
    [[nodiscard]] std::chrono::milliseconds currentPositionLocked() const;
    [[nodiscard]] std::chrono::milliseconds songLengthLocked() const;

    IPlaybackEventSink& sink;
    std::shared_ptr<const Song> currentSong;
    mutable std::mutex mutex;
    std::condition_variable wakeup;
    std::thread worker;
    bool shutdownRequested = false;
    bool playing = false;
    std::chrono::milliseconds storedPosition { 0 };
    Clock::time_point playStartedAt {};
    std::size_t nextMidiEvent = 0;
    std::size_t nextLyric = 0;
    std::size_t nextChord = 0;
    std::int64_t nextSecond = 0;
    std::uint64_t revision = 0;
};
