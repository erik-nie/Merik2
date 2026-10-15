#include "StemStreamer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

StemStreamer::StemStreamer(std::shared_ptr<juce::ZipFile> keepAlive,
                           std::unique_ptr<juce::AudioFormatReader> sourceReader,
                           double deviceRate)
    : juce::Thread("Merik stem streamer"),
      zip(std::move(keepAlive)),
      reader(std::move(sourceReader)),
      sourceRate(reader->sampleRate),
      ratio(reader->sampleRate / deviceRate),
      head(headBlocks),
      ring(ringBlocks)
{
    outLength = static_cast<std::int64_t>(std::llround(static_cast<double>(reader->lengthInSamples) / ratio));
    numBlocks = (outLength + blockSize - 1) / blockSize;

    for (auto* blocks : { &head, &ring })
        for (auto& block : *blocks)
        {
            block.left.assign(blockSize, 0.0f);
            block.right.assign(blockSize, 0.0f);
        }

    const auto sourceFrames = static_cast<std::size_t>(std::ceil(blockSize * ratio)) + 16;
    sourceLeft.assign(sourceFrames, 0.0f);
    sourceRight.assign(sourceFrames, 0.0f);
}

StemStreamer::~StemStreamer()
{
    stopThread(5000);
}

void StemStreamer::startStreaming()
{
    startThread(juce::Thread::Priority::high);
}

void StemStreamer::preloadHead(const std::atomic<bool>* cancel, const std::function<void(float)>& onProgress)
{
    const auto count = std::min<std::int64_t>(headBlocks, numBlocks);

    for (std::int64_t b = 0; b < count; ++b)
    {
        if (cancel != nullptr && cancel->load())
            return;

        decodeBlock(b, head[static_cast<std::size_t>(b)]);

        if (onProgress)
            onProgress(0.1f + 0.9f * static_cast<float>(b + 1) / static_cast<float>(count));
    }
}

void StemStreamer::decodeBlock(std::int64_t blockNumber, Block& block)
{
    // Eerst ongeldig maken: de audiothread mag dit blok niet half gevuld lezen.
    block.index.store(-1, std::memory_order_release);

    const std::int64_t outStart = blockNumber * blockSize;
    const bool direct = std::abs(ratio - 1.0) < 1.0e-6;

    const juce::ScopedLock lock(readerLock);

    if (direct)
    {
        float* destination[2] = { block.left.data(), block.right.data() };
        reader->read(destination, 2, outStart, blockSize);

        if (reader->numChannels < 2)
            std::copy(block.left.begin(), block.left.end(), block.right.begin());
    }
    else
    {
        // Bronsamples die voor dit blok nodig zijn (kubische interpolatie: 1 ervoor, 2 erna).
        const auto s0 = static_cast<std::int64_t>(std::floor(static_cast<double>(outStart) * ratio)) - 1;
        const auto s1 = static_cast<std::int64_t>(std::ceil(static_cast<double>(outStart + blockSize - 1) * ratio)) + 2;
        const auto count = static_cast<int>(std::min<std::int64_t>(s1 - s0 + 1, static_cast<std::int64_t>(sourceLeft.size())));

        std::fill(sourceLeft.begin(), sourceLeft.end(), 0.0f);
        std::fill(sourceRight.begin(), sourceRight.end(), 0.0f);

        // Voor het begin van het bestand: nullen.
        const auto skip = static_cast<int>(std::max<std::int64_t>(0, -s0));

        if (skip < count)
        {
            float* destination[2] = { sourceLeft.data() + skip, sourceRight.data() + skip };
            reader->read(destination, 2, s0 + skip, count - skip);

            if (reader->numChannels < 2)
                std::copy(sourceLeft.begin(), sourceLeft.end(), sourceRight.begin());
        }

        const auto* in0 = sourceLeft.data();
        const auto* in1 = sourceRight.data();
        const int last = static_cast<int>(sourceLeft.size()) - 3;

        for (int j = 0; j < blockSize; ++j)
        {
            const double position = static_cast<double>(outStart + j) * ratio - static_cast<double>(s0);
            const int i = std::clamp(static_cast<int>(position), 1, last);
            const float t = static_cast<float>(position - static_cast<double>(i));

            // 4-punts Lagrange (derde orde)
            const float c0 = -t * (t - 1.0f) * (t - 2.0f) * (1.0f / 6.0f);
            const float c1 = (t + 1.0f) * (t - 1.0f) * (t - 2.0f) * 0.5f;
            const float c2 = -(t + 1.0f) * t * (t - 2.0f) * 0.5f;
            const float c3 = (t + 1.0f) * t * (t - 1.0f) * (1.0f / 6.0f);

            block.left[static_cast<std::size_t>(j)] =
                c0 * in0[i - 1] + c1 * in0[i] + c2 * in0[i + 1] + c3 * in0[i + 2];
            block.right[static_cast<std::size_t>(j)] =
                c0 * in1[i - 1] + c1 * in1[i] + c2 * in1[i + 1] + c3 * in1[i + 2];
        }
    }

    // Voorbij het einde van de stem: stilte.
    const auto valid = static_cast<std::int64_t>(std::max<std::int64_t>(0, std::min<std::int64_t>(blockSize, outLength - outStart)));

    if (valid < blockSize)
    {
        std::fill(block.left.begin() + valid, block.left.end(), 0.0f);
        std::fill(block.right.begin() + valid, block.right.end(), 0.0f);
    }

    block.index.store(blockNumber, std::memory_order_release);
}

void StemStreamer::run()
{
    while (!threadShouldExit())
    {
        const auto position = std::clamp<std::int64_t>(playPosition.load(std::memory_order_relaxed), 0, std::max<std::int64_t>(0, outLength - 1));
        const auto first = position / blockSize;
        bool didWork = false;

        // Vanaf de speelpositie vooruit (het blok ervoor als laatste: kort terugspoelen).
        for (int k = 0; k <= aheadBlocks && !threadShouldExit(); ++k)
        {
            const std::int64_t b = k == aheadBlocks ? first - 1 : first + k;

            if (b < headBlocks || b >= numBlocks)
                continue;

            auto& block = ring[static_cast<std::size_t>(b % ringBlocks)];

            if (block.index.load(std::memory_order_acquire) == b)
                continue;

            decodeBlock(b, block);
            didWork = true;

            // Is de speelpositie intussen ver verschoven (spoelen)? Opnieuw beginnen.
            const auto now = std::clamp<std::int64_t>(playPosition.load(std::memory_order_relaxed), 0, outLength);

            if (std::llabs(now / blockSize - first) > 1)
                break;
        }

        if (!didWork)
            wait(15);
    }
}

bool StemStreamer::read(std::int64_t first, int n, float* left, float* right) noexcept
{
    std::fill(left, left + n, 0.0f);
    std::fill(right, right + n, 0.0f);

    bool complete = true;
    std::int64_t position = std::max<std::int64_t>(first, 0);
    const std::int64_t end = std::min<std::int64_t>(first + n, outLength);

    while (position < end)
    {
        const auto blockNumber = position / blockSize;
        const auto within = static_cast<int>(position % blockSize);
        const auto count = static_cast<int>(std::min<std::int64_t>(blockSize - within, end - position));
        const auto destination = static_cast<int>(position - first);

        auto& block = slotFor(blockNumber);

        if (block.index.load(std::memory_order_acquire) == blockNumber)
        {
            std::copy_n(block.left.begin() + within, count, left + destination);
            std::copy_n(block.right.begin() + within, count, right + destination);

            // Is het blok intussen vervangen terwijl we kopieerden? Dan dit stuk stil.
            std::atomic_thread_fence(std::memory_order_acquire);

            if (block.index.load(std::memory_order_relaxed) != blockNumber)
            {
                std::fill_n(left + destination, count, 0.0f);
                std::fill_n(right + destination, count, 0.0f);
                complete = false;
            }
        }
        else
        {
            complete = false;
        }

        position += count;
    }

    if (!complete)
        missed.store(true, std::memory_order_relaxed);

    return complete;
}
