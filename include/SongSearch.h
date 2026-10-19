#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

// Zoeken van nummers in de setlists, gedeeld door de app en de webpagina.
struct SetlistSnapshot
{
    std::string name;
    std::vector<std::string> titles;   // getoonde namen, in volgorde
    std::vector<double> seconds;       // lengte per nummer (< 0 = nog onbekend)
};

struct SearchHit
{
    int setlist = 0;
    int row = 0;
    std::string setlistName;
    std::string title;
};

namespace SongSearch
{
    constexpr int minimumLetters = 3;

    inline std::string lower(const std::string& text)
    {
        std::string result = text;

        for (auto& c : result)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');

        return result;
    }

    inline std::vector<std::string> words(const std::string& text)
    {
        std::vector<std::string> result;
        std::string current;

        for (const char c : text)
        {
            if (c == ' ' || c == '\t')
            {
                if (!current.empty())
                    result.push_back(std::move(current)), current.clear();
            }
            else
            {
                current += c;
            }
        }

        if (!current.empty())
            result.push_back(std::move(current));

        return result;
    }

    // Alle woorden van `query` moeten (in willekeurige volgorde) in de titel voorkomen,
    // zonder op hoofdletters te letten. onlySetlist = -1: alle setlists. Titels die met
    // de zoektekst beginnen komen eerst, daarna titels met een woord dat ermee begint.
    inline std::vector<SearchHit> search(const std::vector<SetlistSnapshot>& lists, const std::string& query,
                                         int onlySetlist, std::size_t limit = 60)
    {
        const auto needle = lower(query);
        const auto tokens = words(needle);

        std::vector<SearchHit> hits;

        if (tokens.empty() || static_cast<int>(needle.size()) < minimumLetters)
            return hits;

        std::vector<int> scores;

        for (std::size_t s = 0; s < lists.size(); ++s)
        {
            if (onlySetlist >= 0 && static_cast<int>(s) != onlySetlist)
                continue;

            for (std::size_t r = 0; r < lists[s].titles.size(); ++r)
            {
                const auto title = lower(lists[s].titles[r]);
                bool all = true;

                for (const auto& token : tokens)
                    if (title.find(token) == std::string::npos)
                    {
                        all = false;
                        break;
                    }

                if (!all)
                    continue;

                int score = 2;

                if (title.rfind(tokens.front(), 0) == 0)
                    score = 0;
                else if (title.find(" " + tokens.front()) != std::string::npos)
                    score = 1;

                SearchHit hit;
                hit.setlist = static_cast<int>(s);
                hit.row = static_cast<int>(r);
                hit.setlistName = lists[s].name;
                hit.title = lists[s].titles[r];
                hits.push_back(std::move(hit));
                scores.push_back(score);
            }
        }

        // Stabiel sorteren op score (eerst beste overeenkomst), anders op volgorde in de lijsten.
        std::vector<std::size_t> order(hits.size());

        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;

        std::stable_sort(order.begin(), order.end(),
                         [&scores](std::size_t a, std::size_t b) { return scores[a] < scores[b]; });

        std::vector<SearchHit> sorted;

        for (std::size_t i = 0; i < order.size() && sorted.size() < limit; ++i)
            sorted.push_back(std::move(hits[order[i]]));

        return sorted;
    }
}
