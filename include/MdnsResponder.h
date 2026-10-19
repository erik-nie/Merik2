#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <set>
#include <string>
#include <thread>
#include <vector>

// Een kleine mDNS-responder (Bonjour): laat "merik.local" naar deze computer wijzen, zodat de
// webpagina op een tablet of telefoon op http://merik.local te vinden is, zonder IP-adres.
// Beantwoordt alleen vragen naar de eigen naam (A-record; voor AAAA komt een "bestaat niet"-antwoord).
class MdnsResponder final
{
public:
    MdnsResponder() = default;
    ~MdnsResponder();

    MdnsResponder(const MdnsResponder&) = delete;
    MdnsResponder& operator=(const MdnsResponder&) = delete;

    // label: de naam vóór ".local" (letters, cijfers en "-"; standaard "merik").
    bool start(const std::string& label);
    void stop();

    [[nodiscard]] bool isRunning() const noexcept { return running.load(); }
    [[nodiscard]] std::string hostName() const { return label_ + ".local"; }

private:
    void run();
    void sendAnswer(bool addressRecords, bool goodbye);
    void joinInterfaces();

    [[nodiscard]] std::vector<std::uint32_t> localAddresses() const;   // IPv4, hostvolgorde

    std::atomic<bool> running { false };
    std::intptr_t socket_ = -1;
    std::thread thread_;
    std::string label_ = "merik";
    std::set<std::uint32_t> joined_;
    std::chrono::steady_clock::time_point lastAnswer_ {};
    std::chrono::steady_clock::time_point lastNsec_ {};
};
