#include "MdnsResponder.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef _WIN32
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <winsock2.h>
 #include <ws2tcpip.h>
#else
 #include <arpa/inet.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
 #include <sys/time.h>
 #include <unistd.h>
#endif

namespace
{
constexpr std::uint16_t mdnsPort = 5353;
constexpr std::uint32_t mdnsGroup = (224u << 24) | (0u << 16) | (0u << 8) | 251u;
constexpr std::uint32_t recordTtl = 120;

#ifdef _WIN32
using NativeSocket = SOCKET;

void ensureWinsock()
{
    static const bool started = []
    {
        WSADATA data {};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();

    (void) started;
}
#else
using NativeSocket = int;
#endif

NativeSocket native(std::intptr_t handle) { return static_cast<NativeSocket>(handle); }

void closeNative(std::intptr_t handle)
{
#ifdef _WIN32
    ::closesocket(native(handle));
#else
    ::close(native(handle));
#endif
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
}

// "merik" -> 5 merik 5 local 0
void putName(std::vector<std::uint8_t>& out, const std::string& label)
{
    for (const auto& part : { label, std::string("local") })
    {
        out.push_back(static_cast<std::uint8_t>(part.size()));
        out.insert(out.end(), part.begin(), part.end());
    }

    out.push_back(0);
}

// Leest een (eventueel gecomprimeerde) naam; geeft hem terug in kleine letters, "a.b.c".
bool readName(const std::uint8_t* packet, std::size_t length, std::size_t& position, std::string& name)
{
    name.clear();
    std::size_t cursor = position;
    bool jumped = false;
    int guard = 0;

    while (cursor < length && guard++ < 40)
    {
        const auto size = packet[cursor];

        if (size == 0)
        {
            if (!jumped)
                position = cursor + 1;

            return true;
        }

        if ((size & 0xC0) == 0xC0)
        {
            if (cursor + 1 >= length)
                return false;

            const std::size_t target = static_cast<std::size_t>(((size & 0x3F) << 8) | packet[cursor + 1]);

            if (!jumped)
                position = cursor + 2;

            jumped = true;
            cursor = target;
            continue;
        }

        if (cursor + 1 + size > length)
            return false;

        if (!name.empty())
            name += '.';

        for (std::size_t i = 0; i < size; ++i)
            name += static_cast<char>(std::tolower(packet[cursor + 1 + i]));

        cursor += 1 + size;
    }

    return false;
}
}

MdnsResponder::~MdnsResponder()
{
    stop();
}

std::vector<std::uint32_t> MdnsResponder::localAddresses() const
{
    std::vector<std::uint32_t> result;

    for (const auto& address : juce::IPAddress::getAllAddresses(false))
    {
        if (address.isIPv6)
            continue;

        const auto a = static_cast<std::uint32_t>(address.address[0]);
        const auto b = static_cast<std::uint32_t>(address.address[1]);
        const auto c = static_cast<std::uint32_t>(address.address[2]);
        const auto d = static_cast<std::uint32_t>(address.address[3]);

        if (a == 127 || a == 0 || (a == 169 && b == 254))
            continue;   // loopback, niets, link-local

        result.push_back((a << 24) | (b << 16) | (c << 8) | d);
    }

    return result;
}

bool MdnsResponder::start(const std::string& label)
{
    stop();

    label_.clear();

    for (const char c : label)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-')
            label_ += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (label_.empty())
        label_ = "merik";

#ifdef _WIN32
    ensureWinsock();
#endif

    const auto handle = static_cast<std::intptr_t>(::socket(AF_INET, SOCK_DGRAM, 0));

    if (handle < 0)
        return false;

    const int yes = 1;
    ::setsockopt(native(handle), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#ifdef SO_REUSEPORT
    ::setsockopt(native(handle), SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(mdnsPort);

    if (::bind(native(handle), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        closeNative(handle);
        return false;
    }

    // Af en toe terugkomen uit recvfrom, zodat stoppen en het bijhouden van netwerken kan.
#ifdef _WIN32
    const DWORD timeout = 500;
    ::setsockopt(native(handle), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout {};
    timeout.tv_usec = 500 * 1000;
    ::setsockopt(native(handle), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    const unsigned char ttl = 255;
    ::setsockopt(native(handle), IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl));

    socket_ = handle;
    joined_.clear();
    joinInterfaces();

    running = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void MdnsResponder::stop()
{
    if (!running.exchange(false))
        return;

    // Afscheid: de naam met levensduur 0 uit de caches van de anderen.
    sendAnswer(true, true);

    if (thread_.joinable())
        thread_.join();

    if (socket_ >= 0)
    {
        closeNative(socket_);
        socket_ = -1;
    }
}

void MdnsResponder::joinInterfaces()
{
    for (const auto address : localAddresses())
    {
        if (joined_.count(address) != 0)
            continue;

        ip_mreq request {};
        request.imr_multiaddr.s_addr = htonl(mdnsGroup);
        request.imr_interface.s_addr = htonl(address);

        if (::setsockopt(native(socket_), IPPROTO_IP, IP_ADD_MEMBERSHIP,
                         reinterpret_cast<const char*>(&request), sizeof(request)) == 0)
            joined_.insert(address);
    }
}

void MdnsResponder::sendAnswer(bool addressRecords, bool goodbye)
{
    if (socket_ < 0)
        return;

    const auto addresses = localAddresses();

    if (addressRecords && addresses.empty())
        return;

    std::vector<std::uint8_t> packet;
    put16(packet, 0);                                             // id
    put16(packet, 0x8400);                                        // antwoord, gezaghebbend
    put16(packet, 0);                                             // vragen
    put16(packet, static_cast<std::uint16_t>(addressRecords ? addresses.size() : 1));
    put16(packet, 0);
    put16(packet, 0);

    if (addressRecords)
    {
        for (const auto address : addresses)
        {
            putName(packet, label_);
            put16(packet, 1);                                     // A
            put16(packet, 0x8001);                                // IN + cache-flush
            put32(packet, goodbye ? 0 : recordTtl);
            put16(packet, 4);
            put32(packet, address);
        }
    }
    else
    {
        // AAAA: "dit bestaat niet" (alleen een A-record), zodat een client niet hoeft te wachten.
        std::vector<std::uint8_t> rdata;
        putName(rdata, label_);
        rdata.push_back(0);      // blok 0
        rdata.push_back(1);      // 1 byte bitmap
        rdata.push_back(0x40);   // type 1 (A)

        putName(packet, label_);
        put16(packet, 47);                                        // NSEC
        put16(packet, 0x8001);
        put32(packet, recordTtl);
        put16(packet, static_cast<std::uint16_t>(rdata.size()));
        packet.insert(packet.end(), rdata.begin(), rdata.end());
    }

    sockaddr_in destination {};
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = htonl(mdnsGroup);
    destination.sin_port = htons(mdnsPort);

    // Over elk netwerk waarop deze computer zit.
    for (const auto address : addresses)
    {
        in_addr interfaceAddress {};
        interfaceAddress.s_addr = htonl(address);
        ::setsockopt(native(socket_), IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&interfaceAddress),
                     sizeof(interfaceAddress));

        ::sendto(native(socket_), reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
                 reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
    }
}

void MdnsResponder::run()
{
    // Bij het starten twee keer aankondigen.
    sendAnswer(true, false);

    auto lastAnnounce = std::chrono::steady_clock::now();
    bool secondAnnounce = false;
    auto lastInterfaces = lastAnnounce;
    const std::string wanted = label_ + ".local";

    std::uint8_t buffer[1500];

    while (running.load())
    {
        const auto now = std::chrono::steady_clock::now();

        if (!secondAnnounce && now - lastAnnounce > std::chrono::seconds(1))
        {
            secondAnnounce = true;
            sendAnswer(true, false);
        }

        // Een netwerk erbij of weg (wifi aan, kabel erin): opnieuw aansluiten en aankondigen.
        if (now - lastInterfaces > std::chrono::seconds(5))
        {
            lastInterfaces = now;
            const auto before = joined_.size();
            joinInterfaces();

            if (joined_.size() != before)
                sendAnswer(true, false);
        }

        sockaddr_in from {};
#ifdef _WIN32
        int fromLength = sizeof(from);
#else
        socklen_t fromLength = sizeof(from);
#endif
        const auto received = ::recvfrom(native(socket_), reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                         reinterpret_cast<sockaddr*>(&from), &fromLength);

        if (received < 12)
            continue;

        const auto length = static_cast<std::size_t>(received);
        const std::uint16_t flags = static_cast<std::uint16_t>((buffer[2] << 8) | buffer[3]);

        if ((flags & 0x8000) != 0)
            continue;   // een antwoord van een ander

        const auto questions = static_cast<std::size_t>((buffer[4] << 8) | buffer[5]);
        std::size_t position = 12;
        bool wantsAddress = false;
        bool wantsIpv6 = false;

        for (std::size_t q = 0; q < questions && position < length; ++q)
        {
            std::string name;

            if (!readName(buffer, length, position, name) || position + 4 > length)
                break;

            const std::uint16_t type = static_cast<std::uint16_t>((buffer[position] << 8) | buffer[position + 1]);
            position += 4;

            if (name != wanted)
                continue;

            if (type == 1 || type == 255)
                wantsAddress = true;
            else if (type == 28)
                wantsIpv6 = true;
        }

        const auto after = std::chrono::steady_clock::now();

        // Hooguit vijf keer per seconde antwoorden.
        if (wantsAddress && after - lastAnswer_ > std::chrono::milliseconds(200))
        {
            lastAnswer_ = after;
            sendAnswer(true, false);
        }
        else if (wantsIpv6 && after - lastNsec_ > std::chrono::milliseconds(200))
        {
            lastNsec_ = after;
            sendAnswer(false, false);
        }
    }
}
