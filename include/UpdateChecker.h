#pragma once

#include "VersionCompare.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <utility>

// Kijkt op de website of er een nieuwere versie van Merik Studio is (een klein JSON-bestand,
// zie docs/update-check.md). Draait op een achtergronddraad (de app blijft gewoon starten);
// het resultaat komt op de GUI-thread terug. Er wordt niets over de gebruiker verstuurd
// behalve het gewone HTTPS-verzoek zelf.
class UpdateChecker
{
public:
    struct Result
    {
        bool ok = false;                // false: geen verbinding of onbegrijpelijk antwoord
        bool updateAvailable = false;
        juce::String latestVersion;     // bijv. "0.3.0-alpha.1"
        juce::String url;               // pagina van de release
    };

    static void check(const juce::String& currentVersion, std::function<void(Result)> done)
    {
        juce::Thread::launch(
            [currentVersion, done = std::move(done)]
            {
                const Result result = fetch(currentVersion);

                juce::MessageManager::callAsync([done, result] { done(result); });
            });
    }

private:
    static constexpr const char* feeds[] = {
        "https://merik.app/studio/latest.json",
    };

    // latest.json: { "version": "1.0.1", "url": "https://.../studio/#download" }
    static Result fetch(const juce::String& currentVersion)
    {
        Result result;

        for (const auto* feed : feeds)
        {
            auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                               .withExtraHeaders("Accept: application/json\r\n"
                                                 "User-Agent: MerikStudio/" + currentVersion)
                               .withConnectionTimeoutMs(6000);

            const auto stream = juce::URL(feed).createInputStream(options);

            if (stream == nullptr)
                continue;

            const auto json = juce::JSON::parse(stream->readEntireStreamAsString());
            const auto version = json["version"].toString().trim();

            if (version.isEmpty())
                continue;

            result.ok = true;
            result.latestVersion = version.trimCharactersAtStart("vV");
            result.url = json["url"].toString();

            // Alleen een adres op onze eigen website openen; anders de downloadpagina.
            if (!result.url.startsWith("https://merik.app/"))
                result.url = "https://merik.app/studio/#download";

            // Wie een pre-release gebruikt (alpha/beta) vergelijkt gewoon mee; een gewone versie
            // wordt nooit "teruggezet" naar een lagere.
            result.updateAvailable = VersionCompare::compare(result.latestVersion.toStdString(),
                                                              currentVersion.toStdString()) > 0;
            return result;
        }

        return result;
    }
};
