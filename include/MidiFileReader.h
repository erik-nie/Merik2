#pragma once

#include "PlaybackInterfaces.h"

class MidiFileReader final : public IMidiFileSource
{
public:
    [[nodiscard]] Song read(const std::string& filename,
                            double sampleRate = 48000.0) const override;
};
