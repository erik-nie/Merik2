#pragma once

#include "FluidSynthEngine.h"   // ExternalMidiOutput

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// De kern van het opnemen van een externe module: draait in de audio-callback,
// stuurt MIDI op vaste samplepositie (op de tijdlijn van de opname) en neemt
// tegelijk de gekozen ingangskanalen op. De callback alloceert niet en blokkeert niet.
class CaptureSession
{
public:
    struct Message
    {
        std::int64_t sample = 0;               // positie op de opnametijdlijn
        std::array<std::uint8_t, 8> data {};
        int size = 0;
    };

    static constexpr int maxChannels = 2;

    // Vooraf, buiten de audio-thread.
    void prepare(double rate, int channels, std::int64_t maxSamples);

    // Plan een opname: berichten (gesorteerd op sample) en het aantal samples
    // dat opgenomen wordt. Daarna begint de opname bij de eerstvolgende callback.
    void arm(std::vector<Message> schedule, std::int64_t recordSamples);

    // Audio-thread. input: de actieve ingangskanalen van het apparaat.
    void process(const float* const* input, int numInputs, int numSamples, ExternalMidiOutput* midi);

    [[nodiscard]] bool isFinished() const noexcept { return state.load(std::memory_order_acquire) == finished; }
    [[nodiscard]] bool isRunning() const noexcept { return state.load(std::memory_order_acquire) == running; }
    [[nodiscard]] std::int64_t position() const noexcept { return recorded.load(std::memory_order_relaxed); }

    // Piek (0..1) per kanaal sinds de vorige aanroep (voor de niveaumeter).
    [[nodiscard]] std::array<float, maxChannels> takePeaks();

    // Na isFinished(): de opname per kanaal.
    [[nodiscard]] const std::vector<std::vector<float>>& data() const noexcept { return buffers; }

    void reset();

private:
    enum State { idle, armed, running, finished };

    std::atomic<int> state { idle };
    double sampleRate = 48000.0;
    int channelCount = 1;

    std::vector<std::vector<float>> buffers;
    std::vector<Message> messages;
    std::size_t nextMessage = 0;
    std::int64_t recordSamples = 0;
    std::int64_t capacity = 0;
    std::int64_t position_ = 0;
    std::atomic<std::int64_t> recorded { 0 };

    std::array<std::atomic<float>, maxChannels> peaks {};
};

// Meet de vertraging tussen "MIDI verstuurd" en "geluid binnen" uit een opname
// van een aantal losse, scherpe slagen.
struct DelayResult
{
    bool ok = false;
    double delayMs = 0.0;     // mediaan
    double spreadMs = 0.0;    // grootste minus kleinste
    int hits = 0;             // slagen die gebruikt zijn
    float peak = 0.0f;        // hoogste piek (0..1) in de opname
    const char* problem = "";
};

[[nodiscard]] DelayResult analyseDelay(const std::vector<std::vector<float>>& channels,
                                       const std::vector<std::int64_t>& hitSamples,
                                       double sampleRate);
