#pragma once

#include <cstdint>

#if JUCE_MAC
 #include <mach/mach.h>
#elif JUCE_WINDOWS
 #include <windows.h>
 #include <psapi.h>
#endif

#include <juce_core/juce_core.h>

// Geheugen dat dit programma nu gebruikt (zoals Activity Monitor / Taakbeheer het
// tonen), in bytes; 0 als het niet bekend is.
// Bouwdatum en -tijd van dit programma (gedefinieerd in main.cpp).
juce::String merikBuildDateTime();

inline std::uint64_t getProcessMemoryBytes()
{
   #if JUCE_MAC
    task_vm_info_data_t info {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;

    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<std::uint64_t>(info.phys_footprint);

    return 0;
   #elif JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS_EX counters {};

    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        return static_cast<std::uint64_t>(counters.PrivateUsage);

    return 0;
   #else
    const juce::File statm("/proc/self/statm");
    const auto fields = juce::StringArray::fromTokens(statm.loadFileAsString(), " ", "");

    return fields.size() > 1 ? static_cast<std::uint64_t>(fields[1].getLargeIntValue()) * 4096ULL : 0;
   #endif
}

inline juce::String formatMemory(std::uint64_t bytes)
{
    if (bytes == 0)
        return {};

    const double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);

    return megabytes >= 1024.0 ? juce::String(megabytes / 1024.0, 1) + " GB"
                               : juce::String(juce::roundToInt(megabytes)) + " MB";
}
