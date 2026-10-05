#pragma once

#include <string>
#include <vector>

struct Lyric
{
    double time;
    std::string text;
};

struct Chord
{
    double time;
    std::string name;
};

struct Song
{
    std::vector<Lyric> lyrics;
    std::vector<Chord> chords;
};