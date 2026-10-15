#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// Vergelijkt versies zoals "0.2.0-alpha.2" of "v1.0.0" (semantic versioning):
//   - eerst major.minor.patch, numeriek;
//   - bij gelijke nummers is een release nieuwer dan een pre-release;
//   - pre-release-delen ("alpha", "2") worden één voor één vergeleken,
//     cijfers numeriek, tekst alfabetisch.
namespace VersionCompare
{
struct Parsed
{
    std::vector<long> core;                  // major, minor, patch
    std::vector<std::string> prerelease;     // bijv. { "alpha", "2" }
};

inline std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::string current;

    for (const char c : text)
    {
        if (c == separator)
        {
            parts.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(c);
        }
    }

    parts.push_back(current);
    return parts;
}

inline bool isNumber(const std::string& text)
{
    return !text.empty()
        && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

inline Parsed parse(std::string version)
{
    Parsed result;

    if (!version.empty() && (version.front() == 'v' || version.front() == 'V'))
        version.erase(version.begin());

    // Build-metadata (+...) telt niet mee.
    version = version.substr(0, version.find('+'));

    const auto dash = version.find('-');
    const std::string core = version.substr(0, dash);

    for (const auto& part : split(core, '.'))
        result.core.push_back(isNumber(part) ? std::stol(part) : 0);

    while (result.core.size() < 3)
        result.core.push_back(0);

    if (dash != std::string::npos)
        result.prerelease = split(version.substr(dash + 1), '.');

    return result;
}

inline bool isPrerelease(const std::string& version)
{
    return !parse(version).prerelease.empty();
}

// -1: a < b, 0: gelijk, 1: a > b
inline int compare(const std::string& a, const std::string& b)
{
    const auto left = parse(a);
    const auto right = parse(b);

    for (std::size_t i = 0; i < std::max(left.core.size(), right.core.size()); ++i)
    {
        const long x = i < left.core.size() ? left.core[i] : 0;
        const long y = i < right.core.size() ? right.core[i] : 0;

        if (x != y)
            return x < y ? -1 : 1;
    }

    // Een release is nieuwer dan een pre-release van hetzelfde nummer.
    if (left.prerelease.empty() != right.prerelease.empty())
        return left.prerelease.empty() ? 1 : -1;

    for (std::size_t i = 0; i < std::min(left.prerelease.size(), right.prerelease.size()); ++i)
    {
        const auto& x = left.prerelease[i];
        const auto& y = right.prerelease[i];

        if (x == y)
            continue;

        if (isNumber(x) && isNumber(y))
            return std::stol(x) < std::stol(y) ? -1 : 1;

        // Cijfers gaan voor op tekst.
        if (isNumber(x) != isNumber(y))
            return isNumber(x) ? -1 : 1;

        return x < y ? -1 : 1;
    }

    if (left.prerelease.size() != right.prerelease.size())
        return left.prerelease.size() < right.prerelease.size() ? -1 : 1;

    return 0;
}
}
