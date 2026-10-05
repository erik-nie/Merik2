#include "MidiPlayer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

MidiPlayer::MidiPlayer(IPlaybackEventSink& eventSink) : sink(eventSink)
{
    worker = std::thread([this] { run(); });
}

MidiPlayer::~MidiPlayer()
{
    {
        const std::scoped_lock lock(mutex);
        shutdownRequested = true;
        playing = false;
        ++revision;
    }
    wakeup.notify_all();
    if (worker.joinable())
        worker.join();
}

void MidiPlayer::load(std::shared_ptr<const Song> song)
{
    if (!song)
        throw std::invalid_argument("MidiPlayer::load received an empty Song");

    {
        const std::scoped_lock lock(mutex);
        currentSong = std::move(song);
        playing = false;
        storedPosition = std::chrono::milliseconds { 0 };
        resynchroniseLocked();
        ++revision;
    }
    wakeup.notify_all();
}

void MidiPlayer::start()
{
    {
        const std::scoped_lock lock(mutex);
        if (!currentSong || playing)
            return;

        const auto length = songLengthLocked();
        if (storedPosition >= length)
            storedPosition = std::chrono::milliseconds { 0 };

        resynchroniseLocked();
        playStartedAt = Clock::now();
        playing = true;
        ++revision;
    }
    wakeup.notify_all();
}

void MidiPlayer::stop()
{
    {
        const std::scoped_lock lock(mutex);
        if (!playing)
            return;

        storedPosition = currentPositionLocked();
        playing = false;
        ++revision;
    }
    wakeup.notify_all();
}

void MidiPlayer::seek(std::chrono::milliseconds requested)
{
    {
        const std::scoped_lock lock(mutex);
        if (!currentSong)
            return;

        storedPosition = std::clamp(requested,
                                    std::chrono::milliseconds { 0 },
                                    songLengthLocked());
        resynchroniseLocked();
        if (playing)
            playStartedAt = Clock::now();
        ++revision;
    }
    wakeup.notify_all();
}

bool MidiPlayer::isPlaying() const
{
    const std::scoped_lock lock(mutex);
    return playing;
}

std::chrono::milliseconds MidiPlayer::position() const
{
    const std::scoped_lock lock(mutex);
    return currentPositionLocked();
}

std::chrono::milliseconds MidiPlayer::currentPositionLocked() const
{
    if (!playing)
        return storedPosition;

    return storedPosition
        + std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - playStartedAt);
}

std::chrono::milliseconds MidiPlayer::songLengthLocked() const
{
    if (!currentSong)
        return std::chrono::milliseconds { 0 };

    double endSeconds = 0.0;
    if (!currentSong->playbackEvents.empty())
        endSeconds = std::max(endSeconds, currentSong->playbackEvents.back().seconds);
    if (!currentSong->lyrics.empty())
        endSeconds = std::max(endSeconds, currentSong->lyrics.back().seconds);

    return std::chrono::milliseconds {
        static_cast<std::int64_t>(std::ceil(endSeconds * 1000.0))
    };
}

void MidiPlayer::resynchroniseLocked()
{
    if (!currentSong)
    {
        nextMidiEvent = 0;
        nextLyric = 0;
        nextSecond = 0;
        return;
    }

    const auto positionSeconds = static_cast<double>(storedPosition.count()) / 1000.0;

    nextMidiEvent = static_cast<std::size_t>(std::lower_bound(
        currentSong->playbackEvents.begin(), currentSong->playbackEvents.end(), positionSeconds,
        [](const RawMidiEvent& event, double seconds) { return event.seconds < seconds; })
        - currentSong->playbackEvents.begin());

    nextLyric = static_cast<std::size_t>(std::lower_bound(
        currentSong->lyrics.begin(), currentSong->lyrics.end(), positionSeconds,
        [](const LyricEvent& event, double seconds) { return event.seconds < seconds; })
        - currentSong->lyrics.begin());

    nextSecond = storedPosition.count() / 1000;
    if ((storedPosition.count() % 1000) != 0)
        ++nextSecond;
}

void MidiPlayer::run()
{
    std::unique_lock lock(mutex);

    while (!shutdownRequested)
    {
        wakeup.wait(lock, [this] { return shutdownRequested || (playing && currentSong); });
        if (shutdownRequested)
            break;

        const auto localRevision = revision;
        const auto nowPosition = currentPositionLocked();
        const auto nowSeconds = static_cast<double>(nowPosition.count()) / 1000.0;
        const auto currentSecond = nowPosition.count() / 1000;
        const auto length = songLengthLocked();

        std::vector<RawMidiEvent> midiDue;
        std::vector<LyricEvent> lyricsDue;
        std::vector<std::chrono::milliseconds> secondsDue;

        while (nextSecond <= currentSecond)
            secondsDue.emplace_back(nextSecond++ * 1000);

        while (nextMidiEvent < currentSong->playbackEvents.size()
               && currentSong->playbackEvents[nextMidiEvent].seconds <= nowSeconds)
            midiDue.push_back(currentSong->playbackEvents[nextMidiEvent++]);

        while (nextLyric < currentSong->lyrics.size()
               && currentSong->lyrics[nextLyric].seconds <= nowSeconds)
            lyricsDue.push_back(currentSong->lyrics[nextLyric++]);

        if (nowPosition >= length)
        {
            storedPosition = length;
            playing = false;
        }

        lock.unlock();
        for (const auto second : secondsDue)
            sink.onSecond(second);
        for (const auto& lyric : lyricsDue)
            sink.onLyric(lyric);
        for (const auto& event : midiDue)
            sink.onMidiEvent(event);
        lock.lock();

        if (shutdownRequested)
            break;
        if (revision != localRevision || !playing)
            continue;

        wakeup.wait_for(lock, std::chrono::milliseconds { 2 },
                        [this, localRevision]
                        {
                            return shutdownRequested || !playing || revision != localRevision;
                        });
    }
}
