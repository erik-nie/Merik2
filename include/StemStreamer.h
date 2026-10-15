#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Speelt één stem uit een niet-gecomprimeerde zip zonder alles in het geheugen
// te laden: het begin van het nummer staat altijd klaar, de rest wordt door een
// eigen thread blok voor blok vooruit gedecodeerd (en naar de samplefrequentie
// van het apparaat omgezet). De audiothread leest alleen uit klare blokken en
// wacht nooit: is een blok er (nog) niet, dan klinkt dat stuk even stil.
class StemStreamer final : private juce::Thread
{
public:
    static constexpr int blockSize = 32768;   // ~0,7 s per blok
    static constexpr int headBlocks = 16;     // ~11 s begin, blijft altijd in het geheugen
    static constexpr int ringBlocks = 48;     // ~32 s eromheen, loopt mee met de speelpositie
    static constexpr int aheadBlocks = ringBlocks - 6;

    // keepAlive houdt het zipbestand open zolang de stream leest.
    StemStreamer(std::shared_ptr<juce::ZipFile> keepAlive,
                 std::unique_ptr<juce::AudioFormatReader> sourceReader,
                 double deviceRate);
    ~StemStreamer() override;

    [[nodiscard]] std::int64_t lengthSamples() const noexcept { return outLength; }

    // Decodeert het begin (blokkerend); daarna start de achtergrondthread.
    // Geeft het aantal klare blokken door aan onBlock (voor de voortgang).
    void preloadHead(const std::atomic<bool>* cancel, const std::function<void(float)>& onProgress);
    void startStreaming();

    // Speelpositie (in samples van deze stem); stuurt wat er vooruit geladen wordt.
    void setPosition(std::int64_t sample) noexcept { playPosition.store(sample, std::memory_order_relaxed); }

    // Audiothread: kopieert n samples vanaf `first` (negatief of voorbij het einde = stilte).
    // Geeft false als een nodig blok nog niet klaarstond (dat stuk is dan stil).
    bool read(std::int64_t first, int n, float* left, float* right) noexcept;

    // true als sinds de vorige aanroep een blok ontbrak.
    bool takeMissed() noexcept { return missed.exchange(false, std::memory_order_relaxed); }

private:
    struct Block
    {
        std::atomic<std::int64_t> index { -1 };
        std::vector<float> left, right;
    };

    void run() override;
    void decodeBlock(std::int64_t blockNumber, Block& block);

    Block& slotFor(std::int64_t blockNumber) noexcept
    {
        return blockNumber < headBlocks ? head[static_cast<std::size_t>(blockNumber)]
                                        : ring[static_cast<std::size_t>(blockNumber % ringBlocks)];
    }

    std::shared_ptr<juce::ZipFile> zip;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::CriticalSection readerLock;

    const double sourceRate;
    const double ratio;                   // bronsamples per apparaatsample
    std::int64_t outLength = 0;           // lengte in samples van het apparaat
    std::int64_t numBlocks = 0;

    std::vector<Block> head;
    std::vector<Block> ring;
    std::vector<float> sourceLeft, sourceRight;   // werkruimte voor decodeBlock

    std::atomic<std::int64_t> playPosition { 0 };
    std::atomic<bool> missed { false };
};
