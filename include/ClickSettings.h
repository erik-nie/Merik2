#pragma once

// Waar de klik klinkt. Kanaalnummers zijn posities onder de actieve
// uitgangskanalen van het audioapparaat (zoals in de instellingen gekozen).
namespace ClickRoute
{
    constexpr int automatic = 0;     // meerkanaals: 2e paar (3-4); anders in de mix
    constexpr int mixed = 1;         // in de hoofdmix (links en rechts)
    constexpr int split = 2;         // klik links, muziek (mono) rechts
    constexpr int monoBase = 100;    // 100 + n: klik op uitgang n
    constexpr int pairBase = 200;    // 200 + n: klik op uitgang n en n+1
}
