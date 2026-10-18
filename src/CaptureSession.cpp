#include "CaptureSession.h"

#include <algorithm>
#include <cmath>

void CaptureSession::prepare(double rate, int channels, std::int64_t maxSamples)
{
    sampleRate = rate;
    channelCount = std::clamp(channels, 1, maxChannels);
    capacity = maxSamples;

    buffers.assign(static_cast<std::size_t>(channelCount), std::vector<float>());

    for (auto& buffer : buffers)
        buffer.assign(static_cast<std::size_t>(maxSamples), 0.0f);

    state.store(idle, std::memory_order_release);
}

void CaptureSession::reset()
{
    state.store(idle, std::memory_order_release);
    recorded.store(0, std::memory_order_relaxed);
}

void CaptureSession::arm(std::vector<Message> schedule, std::int64_t samples)
{
    state.store(idle, std::memory_order_release);

    for (auto& buffer : buffers)
        std::fill(buffer.begin(), buffer.end(), 0.0f);

    messages = std::move(schedule);
    nextMessage = 0;
    recordSamples = std::min(samples, capacity);
    position_ = 0;
    recorded.store(0, std::memory_order_relaxed);

    state.store(armed, std::memory_order_release);
}

std::array<float, CaptureSession::maxChannels> CaptureSession::takePeaks()
{
    std::array<float, maxChannels> result {};

    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = peaks[i].exchange(0.0f, std::memory_order_relaxed);

    return result;
}

void CaptureSession::process(const float* const* input, int numInputs, int numSamples, ExternalMidiOutput* midi)
{
    // Meters lopen altijd mee, ook als er niet opgenomen wordt.
    for (int c = 0; c < channelCount && c < maxChannels; ++c)
    {
        float peak = 0.0f;

        if (c < numInputs && input[c] != nullptr)
            for (int i = 0; i < numSamples; ++i)
                peak = std::max(peak, std::fabs(input[c][i]));

        auto& stored = peaks[static_cast<std::size_t>(c)];

        if (peak > stored.load(std::memory_order_relaxed))
            stored.store(peak, std::memory_order_relaxed);
    }

    int current = state.load(std::memory_order_acquire);

    if (current == armed)
    {
        position_ = 0;
        state.store(running, std::memory_order_release);
        current = running;
    }

    if (current != running)
        return;

    if (midi != nullptr)
        midi->beginBlock(position_, numSamples, sampleRate);

    // Berichten die in dit blok vallen: ingepland op hun eigen samplepositie.
    while (nextMessage < messages.size() && messages[nextMessage].sample < position_ + numSamples)
    {
        const auto& message = messages[nextMessage++];

        if (midi != nullptr)
            midi->push(message.data.data(), message.size, std::max(message.sample, position_));
    }

    // Opnemen.
    const auto room = std::max<std::int64_t>(0, recordSamples - position_);
    const int count = static_cast<int>(std::min<std::int64_t>(numSamples, room));

    for (int c = 0; c < channelCount; ++c)
    {
        auto* destination = buffers[static_cast<std::size_t>(c)].data() + position_;

        if (c < numInputs && input[c] != nullptr)
            std::copy_n(input[c], count, destination);
    }

    position_ += numSamples;
    recorded.store(std::min(position_, recordSamples), std::memory_order_relaxed);

    if (position_ >= recordSamples)
        state.store(finished, std::memory_order_release);
}

DelayResult analyseDelay(const std::vector<std::vector<float>>& channels,
                         const std::vector<std::int64_t>& hits,
                         double rate)
{
    DelayResult result;

    if (channels.empty() || hits.empty())
    {
        result.problem = "No recording.";
        return result;
    }

    const auto length = static_cast<std::int64_t>(channels.front().size());

    const auto envelope = [&](std::int64_t index)
    {
        float value = 0.0f;

        for (const auto& channel : channels)
            if (index >= 0 && index < length)
                value = std::max(value, std::fabs(channel[static_cast<std::size_t>(index)]));

        return value;
    };

    for (const auto& channel : channels)
        for (const float sample : channel)
            result.peak = std::max(result.peak, std::fabs(sample));

    std::vector<double> delays;

    for (const auto sent : hits)
    {
        const auto before = static_cast<std::int64_t>(0.15 * rate);
        const auto after = static_cast<std::int64_t>(0.45 * rate);

        // Achtergrondruis vlak voor de slag.
        float noise = 0.0f;

        for (std::int64_t i = sent - before; i < sent - static_cast<std::int64_t>(0.005 * rate); ++i)
            noise = std::max(noise, envelope(i));

        float peak = 0.0f;

        for (std::int64_t i = sent; i < sent + after; ++i)
            peak = std::max(peak, envelope(i));

        // Te zacht of niet boven de ruis uit: deze slag telt niet mee.
        if (peak < 0.003f || peak < 4.0f * noise)
            continue;

        const float threshold = std::max(0.15f * peak, 3.0f * noise);

        for (std::int64_t i = sent - static_cast<std::int64_t>(0.002 * rate); i < sent + after; ++i)
            if (envelope(i) >= threshold)
            {
                delays.push_back(1000.0 * static_cast<double>(i - sent) / rate);
                break;
            }
    }

    result.hits = static_cast<int>(delays.size());

    if (delays.size() < 3)
    {
        result.problem = result.peak < 0.003f
            ? "No signal on the chosen input. Check the cables, the input channels and the level."
            : "Too few clear hits to measure the delay. Raise the level a little.";
        return result;
    }

    std::sort(delays.begin(), delays.end());
    result.delayMs = delays[delays.size() / 2];
    result.spreadMs = delays.back() - delays.front();
    result.ok = true;
    return result;
}
