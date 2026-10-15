#pragma once

#include "MerikPackage.h"

#include <functional>

// Laadt een .merik op een eigen thread (de stems decoderen kan seconden duren),
// zodat het venster bedienbaar blijft en de voortgang getoond kan worden.
class PackageLoader final : public juce::Thread
{
public:
    PackageLoader(const juce::File& packageFile, double rate, int loadId, std::function<void(int)> doneCallback)
        : juce::Thread("Merik package loader"),
          file(packageFile), deviceRate(rate), id(loadId), onDone(std::move(doneCallback))
    {
        startThread(juce::Thread::Priority::normal);
    }

    ~PackageLoader() override
    {
        progress.cancel = true;
        stopThread(20000);
    }

    void cancel() { progress.cancel = true; }

    const juce::File file;
    const int id;
    MerikPackage::Progress progress;
    MerikPackage::Contents result;     // pas lezen nadat onDone is aangeroepen

private:
    void run() override
    {
        result = MerikPackage::open(file, deviceRate, &progress);

        // Naar de hoofdthread; die controleert of deze laadactie nog actueel is.
        juce::MessageManager::callAsync([callback = onDone, loadId = id]
        {
            if (callback)
                callback(loadId);
        });
    }

    const double deviceRate;
    std::function<void(int)> onDone;
};
