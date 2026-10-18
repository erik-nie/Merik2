#include "RenderPlan.h"

#include "MidiTransformer.h"

#include <algorithm>

RenderPlan buildRenderPlan(const Song& song)
{
    RenderPlan plan;

    const auto familyOf = [](int channel, int program)
    {
        if (channel == 9)
            return 0;

        const int family = MidiTransformer::getFamilyFromProgram(program);
        return family >= 0 && family < RenderPlan::passCount ? family : 3;
    };

    // 1. Welke kanalen klinken in welke families (voor de niet-noot-berichten).
    std::array<std::array<bool, RenderPlan::passCount>, 16> used {};
    {
        std::array<int, 16> program {};

        for (const auto& event : song.playbackEvents)
        {
            if (event.bytes.size() < 2 || event.bytes[0] >= 0xF0)
                continue;

            const int command = event.bytes[0] & 0xF0;
            const int channel = event.bytes[0] & 0x0F;

            if (command == 0xC0)
                program[static_cast<std::size_t>(channel)] = event.bytes[1] & 0x7F;
            else if (command == 0x90 && event.bytes.size() >= 3 && event.bytes[2] > 0)
                used[static_cast<std::size_t>(channel)][static_cast<std::size_t>(
                    familyOf(channel, program[static_cast<std::size_t>(channel)]))] = true;
        }
    }

    // 2. De berichten per pass.
    std::array<int, 16> program {};
    std::array<std::array<int, 128>, 16> noteFamily {};

    for (auto& channel : noteFamily)
        channel.fill(-1);

    for (const auto& event : song.playbackEvents)
    {
        if (event.bytes.size() < 2 || event.bytes[0] >= 0xF0)
            continue;

        const int status = event.bytes[0];
        const int command = status & 0xF0;
        const auto channel = static_cast<std::size_t>(status & 0x0F);

        ScheduledMidi message;
        message.seconds = event.seconds;
        message.size = static_cast<int>(std::min<std::size_t>(event.bytes.size(), 3));

        for (int i = 0; i < message.size; ++i)
            message.data[static_cast<std::size_t>(i)] = event.bytes[static_cast<std::size_t>(i)];

        plan.lengthSeconds = std::max(plan.lengthSeconds, event.seconds);

        if (command == 0xC0)
            program[channel] = event.bytes[1] & 0x7F;

        const bool noteOn = command == 0x90 && event.bytes.size() >= 3 && event.bytes[2] > 0;
        const bool noteOff = command == 0x80 || (command == 0x90 && event.bytes.size() >= 3 && event.bytes[2] == 0);

        if (noteOn)
        {
            const int family = familyOf(static_cast<int>(channel), program[channel]);
            noteFamily[channel][event.bytes[1] & 0x7F] = family;
            plan.passes[static_cast<std::size_t>(family)].push_back(message);
            ++plan.noteCount[static_cast<std::size_t>(family)];
        }
        else if (noteOff)
        {
            // De noot-uit hoort bij de pass van zijn noot-aan.
            auto& family = noteFamily[channel][event.bytes[1] & 0x7F];

            if (family >= 0)
            {
                plan.passes[static_cast<std::size_t>(family)].push_back(message);
                family = -1;
            }
        }
        else
        {
            // Programma, regelaars, pitch bend, ...: naar elke pass waarin dit kanaal klinkt.
            for (int family = 0; family < RenderPlan::passCount; ++family)
                if (used[channel][static_cast<std::size_t>(family)])
                    plan.passes[static_cast<std::size_t>(family)].push_back(message);
        }
    }

    return plan;
}

void RenderPlan::limitTo(double seconds)
{
    lengthSeconds = std::min(lengthSeconds, seconds);

    for (std::size_t family = 0; family < passes.size(); ++family)
    {
        auto& pass = passes[family];

        pass.erase(std::remove_if(pass.begin(), pass.end(),
                                  [seconds](const ScheduledMidi& message) { return message.seconds > seconds; }),
                   pass.end());

        int notes = 0;

        for (const auto& message : pass)
            if ((message.data[0] & 0xF0) == 0x90 && message.size >= 3 && message.data[2] > 0)
                ++notes;

        noteCount[family] = notes;
    }
}
