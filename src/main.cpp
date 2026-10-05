#include <iostream>
#include "MidiParser.h"

int main()
{
    std::cout << "Merik2 started" << std::endl;

    MidiParser parser;

    parser.load("../test/midi/paradise.mid");

    return 0;
}
