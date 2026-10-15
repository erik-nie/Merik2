#pragma once

#include "VersionCompare.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <utility>

// Kijkt op GitHub of er een nieuwere versie van Merik is. Draait op een
// achtergronddraad (de app blijft gewoon starten); het resultaat komt op de
// GUI-thread terug. Er wordt niets over de gebruiker verstuurd behalve het
// gewone HTTPS-verzoek zelf.
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
    static Result fetch(const juce::String& currentVersion)
    {
        Result result;

        const juce::URL url("https://api.github.com/repos/erik-nie/Merik2/releases?per_page=15");

        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                           .withExtraHeaders("Accept: application/vnd.github+json\r\n"
                                             "User-Agent: Merik/" + currentVersion)
                           .withConnectionTimeoutMs(6000);

        const auto stream = url.createInputStream(options);

        if (stream == nullptr)
            return result;

        const auto json = juce::JSON::parse(stream->readEntireStreamAsString());
        const auto* releases = json.getArray();

        if (releases == nullptr)
            return result;

        // Wie een pre-release gebruikt (alpha/beta) krijgt ook pre-releases
        // te zien; wie een gewone release gebruikt alleen gewone releases.
        const bool acceptPrereleases = VersionCompare::isPrerelease(currentVersion.toStdString());

        juce::String bestTag;
        juce::String bestUrl;

        for (const auto& release : *releases)
        {
            if (static_cast<bool>(release["draft"]))
                continue;

            if (static_cast<bool>(release["prerelease"]) && !acceptPrereleases)
                continue;

            const auto tag = release["tag_name"].toString();

            if (tag.isEmpty())
                continue;

            if (bestTag.isEmpty() || VersionCompare::compare(tag.toStdString(), bestTag.toStdString()) > 0)
            {
                bestTag = tag;
                bestUrl = release["html_url"].toString();
            }
        }

        result.ok = true;

        if (bestTag.isNotEmpty())
        {
            result.latestVersion = bestTag.trimCharactersAtStart("vV");
            result.url = bestUrl;
            result.updateAvailable =
                VersionCompare::compare(bestTag.toStdString(), currentVersion.toStdString()) > 0;
        }

        return result;
    }
};
