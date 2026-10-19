#include "WebServer.h"
#include "ChordParser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
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
 #include <netinet/in.h>
 #include <sys/socket.h>
 #include <sys/time.h>
 #include <unistd.h>
#endif

namespace
{
constexpr int backlog = 8;

// ---------------------------------------------------------------------------
// Sockets: BSD-sockets op macOS/Linux, Winsock op Windows. De rest van de
// server gebruikt alleen deze kleine laag. Een socket is hier een std::intptr_t
// (-1 = ongeldig), zodat dezelfde code voor beide werkt.
// ---------------------------------------------------------------------------

#ifdef _WIN32
using NativeSocket = SOCKET;
#else
using NativeSocket = int;
#endif

#ifdef _WIN32
// Winsock moet eenmalig worden gestart.
struct WinsockSession
{
    WinsockSession()
    {
        WSADATA data {};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockSession()
    {
        if (ok)
            WSACleanup();
    }

    bool ok = false;
};

WinsockSession& winsock()
{
    static WinsockSession session;
    return session;
}
#endif

std::intptr_t openListeningSocket()
{
#ifdef _WIN32
    if (!winsock().ok)
        return -1;
#endif
    return static_cast<std::intptr_t>(::socket(AF_INET, SOCK_STREAM, 0));
}

void closeSocket(std::intptr_t handle)
{
#ifdef _WIN32
    ::closesocket(static_cast<SOCKET>(handle));
#else
    ::close(static_cast<int>(handle));
#endif
}

// Sluit beide richtingen; maakt ook een blokkerende accept() vrij.
void shutdownSocket(std::intptr_t handle)
{
#ifdef _WIN32
    ::shutdown(static_cast<SOCKET>(handle), SD_BOTH);
#else
    ::shutdown(static_cast<int>(handle), SHUT_RDWR);
#endif
}

void setSocketTimeouts(std::intptr_t handle, int milliseconds)
{
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(milliseconds);
    ::setsockopt(static_cast<SOCKET>(handle), SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    ::setsockopt(static_cast<SOCKET>(handle), SOL_SOCKET, SO_SNDTIMEO,
                 reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout {};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(static_cast<int>(handle), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(static_cast<int>(handle), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

long receiveSome(std::intptr_t handle, char* buffer, std::size_t size)
{
#ifdef _WIN32
    return ::recv(static_cast<SOCKET>(handle), buffer, static_cast<int>(size), 0);
#else
    return static_cast<long>(::recv(static_cast<int>(handle), buffer, size, 0));
#endif
}

long sendSome(std::intptr_t handle, const char* data, std::size_t size)
{
#ifdef _WIN32
    return ::send(static_cast<SOCKET>(handle), data, static_cast<int>(size), 0);
#else
    return static_cast<long>(::send(static_cast<int>(handle), data, size, 0));
#endif
}

void sendResponse(std::intptr_t socket, const std::string& body, const char* contentType)
{
    const std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: " + std::string(contentType) + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n\r\n";

    const std::string response = header + body;
    const char* data = response.data();
    std::size_t remaining = response.size();

    while (remaining > 0)
    {
        const auto sent = sendSome(socket, data, remaining);
        if (sent <= 0)
            break;
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
}
}

WebServer::WebServer() = default;
WebServer::~WebServer() { stop(); }

bool WebServer::start(int port)
{
    if (running)
        return true;

    if (tryStart(port))
        return true;

    return port == defaultPort && tryStart(fallbackPort);
}

bool WebServer::tryStart(int port)
{
    port_ = port;
    serverSocket_ = openListeningSocket();
    if (serverSocket_ < 0)
        return false;

#ifndef _WIN32
    // Opnieuw starten direct na afsluiten mag. (Op Windows geeft dezelfde
    // optie juist toestemming om een bezette poort over te nemen.)
    int reuseAddress = 1;
    ::setsockopt(static_cast<int>(serverSocket_), SOL_SOCKET, SO_REUSEADDR,
                 &reuseAddress, sizeof(reuseAddress));
#endif

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<std::uint16_t>(port_));

    if (::bind(static_cast<NativeSocket>(serverSocket_),
               reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(static_cast<NativeSocket>(serverSocket_), backlog) < 0)
    {
        closeSocket(serverSocket_);
        serverSocket_ = -1;
        return false;
    }

    activePort() = port_;
    running = true;
    thread_ = std::thread(&WebServer::serverThread, this);
    return true;
}

void WebServer::stop()
{
    if (!running.exchange(false))
        return;

    if (serverSocket_ >= 0)
    {
        shutdownSocket(serverSocket_);
        closeSocket(serverSocket_);
        serverSocket_ = -1;
    }

    if (thread_.joinable())
        thread_.join();
}

bool WebServer::isRunning() const { return running.load(); }

void WebServer::setTranspose(int semitones)
{
    const std::scoped_lock lock(mutex_);

    if (transpose_ != semitones)
    {
        transpose_ = semitones;
        ++contentRevision_;
    }
}

void WebServer::setControlKey(const std::string& key)
{
    const std::scoped_lock lock(mutex_);
    controlKey_ = key;
}

namespace
{
    // Haalt een queryparameter uit de eerste regel van een HTTP-verzoek.
    std::string queryParameter(const std::string& request, const std::string& name)
    {
        const auto targetStart = request.find(' ');
        if (targetStart == std::string::npos)
            return {};

        const auto targetEnd = request.find(' ', targetStart + 1);
        const auto target = request.substr(
            targetStart + 1,
            targetEnd == std::string::npos ? std::string::npos : targetEnd - targetStart - 1);

        auto position = target.find('?');
        if (position == std::string::npos)
            return {};

        const std::string key = name + "=";

        while (position != std::string::npos)
        {
            ++position;

            if (target.compare(position, key.size(), key) == 0)
            {
                const auto end = target.find('&', position);
                const auto raw = target.substr(
                    position + key.size(),
                    end == std::string::npos ? std::string::npos : end - position - key.size());

                std::string decoded;
                for (std::size_t i = 0; i < raw.size(); ++i)
                {
                    if (raw[i] == '%' && i + 2 < raw.size() + 0 && std::isxdigit(static_cast<unsigned char>(raw[i + 1]))
                        && std::isxdigit(static_cast<unsigned char>(raw[i + 2])))
                    {
                        decoded += static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16));
                        i += 2;
                    }
                    else
                        decoded += raw[i] == '+' ? ' ' : raw[i];
                }
                return decoded;
            }

            position = target.find('&', position);
        }

        return {};
    }
}

bool WebServer::isAuthorized(const std::string& request) const
{
    std::string expected;

    {
        const std::scoped_lock lock(mutex_);
        expected = controlKey_;
    }

    return !expected.empty() && queryParameter(request, "key") == expected;
}

void WebServer::setControlCallback(ControlCallback callback)
{
    const std::scoped_lock lock(mutex_);
    controlCallback_ = std::move(callback);
}

void WebServer::setTransportInfo(bool playing, const std::string& nextSongTitle)
{
    const std::scoped_lock lock(mutex_);
    playing_ = playing;
    nextSongTitle_ = nextSongTitle;
}

void WebServer::setSong(std::shared_ptr<const Song> song)
{
    std::scoped_lock lock(mutex_);
    song_ = std::move(song);
    positionSamples_ = 0;
    ++songRevision_;
    ++contentRevision_;
}

void WebServer::setSongTitle(const std::string& title)
{
    std::scoped_lock lock(mutex_);

    if (songTitleOverride_ != title)
    {
        songTitleOverride_ = title;
        ++contentRevision_;
    }
}

void WebServer::setPositionSamples(std::int64_t positionSamples)
{
    std::scoped_lock lock(mutex_);
    positionSamples_ = std::max<std::int64_t>(0, positionSamples);
}

void WebServer::setFamilyVolumeCallback(FamilyVolumeCallback callback)
{
    std::scoped_lock lock(mutex_);
    familyVolumeCallback_ = std::move(callback);
}
void WebServer::setLibrary(std::vector<SetlistSnapshot> lists)
{
    std::scoped_lock lock(mutex_);
    library_ = std::move(lists);
}

void WebServer::setPlaybackContext(const PlaybackContext& context)
{
    std::scoped_lock lock(mutex_);
    context_ = context;
}

void WebServer::setFamilyNames(const std::array<std::string, familyCount>& names)
{
    std::scoped_lock lock(mutex_);
    familyNames_ = names;
}

void WebServer::setMeters(const std::array<float, familyCount>& levels, std::uint32_t stemMask)
{
    std::scoped_lock lock(mutex_);
    familyLevels_ = levels;
    stemMask_ = stemMask;
}

void WebServer::setFamilySettings(
    const std::array<int, familyCount>& volumes,
    const std::array<bool, familyCount>& enabled)
{
    std::scoped_lock lock(mutex_);

    for (int i = 0; i < familyCount; ++i)
    {
        familyVolumes_[i] = std::clamp(volumes[i], 0, 127);
        familyEnabled_[i] = enabled[i];
    }
}

std::string WebServer::createFamilyJson() const
{
    std::array<int, familyCount> volumes;
    std::array<bool, familyCount> enabled;
    std::array<float, familyCount> levels;
    std::uint32_t stemMask;
    std::array<std::string, familyCount> names;

    {
        std::scoped_lock lock(mutex_);
        volumes = familyVolumes_;
        enabled = familyEnabled_;
        levels = familyLevels_;
        stemMask = stemMask_;
        names = familyNames_;
    }

    std::string json = "{\"families\":[";

    for (int i = 0; i < familyCount; ++i)
    {
        if (i > 0)
            json += ",";

        json += "{\"volume\":" + std::to_string(volumes[i]);
        json += ",\"enabled\":";
        json += enabled[i] ? "true" : "false";
        json += ",\"level\":" + std::to_string(static_cast<int>(levels[i] * 100.0f + 0.5f));
        json += ",\"name\":\"" + escapeJson(names[i]) + "\"";
        json += ",\"source\":\"";
        json += (stemMask & (1u << i)) != 0 ? "audio" : (i == 7 ? "click" : "midi");
        json += "\"}";
    }

    json += "]}";
    return json;
}
void WebServer::serverThread()
{
    while (running)
    {
        sockaddr_in clientAddress {};
        socklen_t clientLength = sizeof(clientAddress);
        const auto clientSocket = static_cast<std::intptr_t>(::accept(
            static_cast<NativeSocket>(serverSocket_),
            reinterpret_cast<sockaddr*>(&clientAddress), &clientLength));

        if (clientSocket < 0)
        {
            if (running)
                continue;
            break;
        }

        handleClient(clientSocket);
        shutdownSocket(clientSocket);
        closeSocket(clientSocket);
    }
}

void WebServer::handleClient(std::intptr_t socket)
{
    // Een client die niets stuurt mag de (enkele) serverthread niet
    // eeuwig blokkeren.
    setSocketTimeouts(socket, 2000);

    char buffer[4096] {};
    const auto received = receiveSome(socket, buffer, sizeof(buffer) - 1);

    if (received <= 0)
        return;

    buffer[received] = '\0';
    const std::string request(buffer);

    if (request.rfind("GET /api/state", 0) == 0)
    {
        sendResponse(socket, createStateJson(isAuthorized(request)), "application/json; charset=utf-8");
        return;
    }

    if (request.rfind("POST /api/control?cmd=", 0) == 0)
    {
        const auto start = request.find("cmd=") + 4;
        const auto end = request.find_first_of(" &", start);
        const std::string command =
            request.substr(start, end == std::string::npos ? std::string::npos : end - start);

        ControlCallback callback;

        {
            std::scoped_lock lock(mutex_);
            callback = controlCallback_;
        }

        if (!isAuthorized(request))
        {
            sendResponse(socket, R"({"ok":false,"error":"unauthorized"})",
                         "application/json; charset=utf-8");
            return;
        }

        // "load:<setlist>:<rij>" laadt een nummer (uit de zoekresultaten), "resume" gaat verder
        // met de laatste setlist.
        bool loadCommand = false;

        for (const char* prefix : { "load:", "playnow:", "queue:", "jump:", "jumpnext:" })
        {
            const std::string name(prefix);

            if (command.rfind(name, 0) == 0)
            {
                int setlistIndex = -1, rowIndex = -1;
                loadCommand = std::sscanf(command.c_str() + name.size(), "%d:%d", &setlistIndex, &rowIndex) == 2
                              && setlistIndex >= 0 && rowIndex >= 0;
            }
        }

        // Alleen voor ontwikkeling (screenshots): "seek:<seconden>" als MERIK_DEV gezet is.
        if (std::getenv("MERIK_DEV") != nullptr && (command.rfind("seek:", 0) == 0 || command.rfind("snapshot:", 0) == 0 || command.rfind("settings:", 0) == 0
                                                      || command.rfind("backupto:", 0) == 0 || command.rfind("restorefrom:", 0) == 0))
            loadCommand = true;

        if (callback && (command == "play" || command == "stop" || command == "next" || command == "resume" || loadCommand))
        {
            callback(command);
            sendResponse(socket, R"({"ok":true})", "application/json; charset=utf-8");
        }
        else
        {
            sendResponse(socket, R"({"ok":false})", "application/json; charset=utf-8");
        }

        return;
    }

    if (request.rfind("GET /api/setlist", 0) == 0)
    {
        if (!isAuthorized(request))
        {
            sendResponse(socket, R"({"ok":false,"error":"unauthorized"})", "application/json; charset=utf-8");
            return;
        }

        std::vector<SetlistSnapshot> library;
        PlaybackContext context;

        {
            std::scoped_lock lock(mutex_);
            library = library_;
            context = context_;
        }

        // De setlist waarin gespeeld wordt; anders de getoonde.
        const int index = context.flowSetlist >= 0 ? context.flowSetlist : context.selectedSetlist;

        if (index < 0 || index >= static_cast<int>(library.size()))
        {
            sendResponse(socket, R"({"ok":false,"error":"no setlist"})", "application/json; charset=utf-8");
            return;
        }

        const auto& list = library[static_cast<std::size_t>(index)];

        std::string json = "{\"ok\":true,\"setlist\":" + std::to_string(index) + ",\"name\":\"" + escapeJson(list.name)
                         + "\",\"current\":" + std::to_string(context.flowSetlist == index ? context.flowRow : -1);

        if (context.remainingSeconds >= 0.0)
            json += ",\"remaining\":" + std::to_string(context.remainingSeconds) + ",\"remainingApprox\":"
                  + std::string(context.remainingApproximate ? "true" : "false");

        json += ",\"rows\":[";

        for (std::size_t i = 0; i < list.titles.size(); ++i)
        {
            if (i > 0)
                json += ",";

            const double seconds = i < list.seconds.size() ? list.seconds[i] : -1.0;
            json += "{\"title\":\"" + escapeJson(list.titles[i]) + "\",\"seconds\":" + std::to_string(seconds) + "}";
        }

        json += "]}";
        sendResponse(socket, json, "application/json; charset=utf-8");
        return;
    }

    if (request.rfind("GET /api/search", 0) == 0)
    {
        if (!isAuthorized(request))
        {
            sendResponse(socket, R"({"ok":false,"error":"unauthorized"})", "application/json; charset=utf-8");
            return;
        }

        const auto targetStart = request.find(' ') + 1;
        const auto targetEnd = request.find(' ', targetStart);
        const std::string target = request.substr(targetStart, targetEnd == std::string::npos ? std::string::npos : targetEnd - targetStart);

        const auto parameter = [&target](const std::string& name) -> std::string
        {
            const auto position = target.find("?" + name + "=") != std::string::npos ? target.find("?" + name + "=") + 1
                                                                                     : target.find("&" + name + "=");

            if (position == std::string::npos)
                return {};

            const auto begin = position + name.size() + 1;
            const auto end = target.find('&', begin);
            const auto raw = target.substr(begin, end == std::string::npos ? std::string::npos : end - begin);

            // %xx en + terugzetten.
            std::string decoded;

            for (std::size_t i = 0; i < raw.size(); ++i)
            {
                if (raw[i] == '+')
                    decoded += ' ';
                else if (raw[i] == '%' && i + 2 < raw.size() + 0 && std::isxdigit(static_cast<unsigned char>(raw[i + 1]))
                         && std::isxdigit(static_cast<unsigned char>(raw[i + 2])))
                {
                    decoded += static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                }
                else
                    decoded += raw[i];
            }

            return decoded;
        };

        std::vector<SetlistSnapshot> library;
        int current = 0;

        {
            std::scoped_lock lock(mutex_);
            library = library_;
            current = context_.selectedSetlist;
        }

        const bool all = parameter("scope") != "current";
        const auto hits = SongSearch::search(library, parameter("q"), all ? -1 : current);

        std::string json = "{\"ok\":true,\"results\":[";

        for (std::size_t i = 0; i < hits.size(); ++i)
        {
            if (i > 0)
                json += ",";

            json += "{\"setlist\":" + std::to_string(hits[i].setlist) + ",\"row\":" + std::to_string(hits[i].row)
                  + ",\"setlistName\":\"" + escapeJson(hits[i].setlistName) + "\",\"title\":\"" + escapeJson(hits[i].title) + "\"}";
        }

        json += "]}";
        sendResponse(socket, json, "application/json; charset=utf-8");
        return;
    }

    if (request.rfind("GET /api/families", 0) == 0)
    {
        sendResponse(
            socket,
            createFamilyJson(),
            "application/json; charset=utf-8");
        return;
    }
    if (request.rfind("GET /api/song", 0) == 0)
    {
        sendResponse(socket, createJson(), "application/json; charset=utf-8");
        return;
    }

    if (request.rfind("GET /api/family?", 0) == 0)
    {
        if (!isAuthorized(request))
        {
            sendResponse(socket, R"({"ok":false,"error":"unauthorized"})",
                         "application/json; charset=utf-8");
            return;
        }

        const auto targetStart = request.find(' ') + 1;
        const auto targetEnd = request.find(' ', targetStart);

        if (targetStart == 0 || targetEnd == std::string::npos)
        {
            sendResponse(socket, R"({"ok":false})", "application/json; charset=utf-8");
            return;
        }

        const std::string target =
            request.substr(targetStart, targetEnd - targetStart);

        auto getParameter = [&target](const std::string& name) -> std::string
        {
            const std::string key = name + "=";
            auto position = target.find(key);

            if (position == std::string::npos)
                return {};

            position += key.size();
            const auto end = target.find('&', position);

            return target.substr(
                position,
                end == std::string::npos
                    ? std::string::npos
                    : end - position);
        };

        try
        {
            const int family = std::stoi(getParameter("family"));
            const int volume = std::stoi(getParameter("volume"));
            const int enabled = std::stoi(getParameter("enabled"));

            if (family < 0 || family >= 8 ||
                volume < 0 || volume > 127 ||
                (enabled != 0 && enabled != 1))
            {
                sendResponse(socket, R"({"ok":false})", "application/json; charset=utf-8");
                return;
            }

            const float factor =
                enabled != 0
                    ? static_cast<float>(volume) / 127.0f
                    : 0.0f;

            FamilyVolumeCallback callback;

            {
                std::scoped_lock lock(mutex_);

                familyVolumes_[family] = volume;
                familyEnabled_[family] = enabled != 0;

                callback = familyVolumeCallback_;
            }

            if (!callback)
            {
                sendResponse(socket, R"({"ok":false,"error":"no_callback"})",
                             "application/json; charset=utf-8");
                return;
            }

            // Roep de callback buiten mutex_ aan.
            // De callback kan immers zelf andere objecten benaderen.
            callback(family, volume, enabled);

            sendResponse(socket, R"({"ok":true})",
                         "application/json; charset=utf-8");
        }
        catch (const std::exception&)
        {
            sendResponse(socket, R"({"ok":false,"error":"invalid_parameters"})",
                         "application/json; charset=utf-8");
        }

        return;
    }

    sendResponse(socket, createHtml(), "text/html; charset=utf-8");
}

std::string WebServer::createStateJson(bool control) const
{
    std::shared_ptr<const Song> song;
    std::int64_t positionSamples = 0;
    std::uint64_t contentRevision = 0;
    std::uint64_t songRevision = 0;
    int transpose = 0;
    bool playing = false;
    std::string nextTitle;
    std::string resumeLabel;
    double remainingSeconds = -1.0;
    bool remainingApproximate = false;

    {
        std::scoped_lock lock(mutex_);
        resumeLabel = context_.resumeLabel;
        remainingSeconds = context_.remainingSeconds;
        remainingApproximate = context_.remainingApproximate;
        song = song_;
        positionSamples = positionSamples_;
        contentRevision = contentRevision_;
        songRevision = songRevision_;
        transpose = transpose_;
        playing = playing_;
        nextTitle = nextSongTitle_;
    }

    const double sampleRate =
        song && song->sampleRate > 0.0 ? song->sampleRate : 48000.0;

    std::string json = "{";
    json += "\"rev\":" + std::to_string(contentRevision) + ",";
    json += "\"songRev\":" + std::to_string(songRevision) + ",";
    json += "\"position\":" + std::to_string(static_cast<double>(positionSamples) / sampleRate) + ",";
    json += "\"playing\":" + std::string(playing ? "true" : "false") + ",";
    json += "\"transpose\":" + std::to_string(transpose) + ",";
    json += "\"next\":\"" + escapeJson(nextTitle) + "\",";

    if (remainingSeconds >= 0.0)
        json += "\"remaining\":" + std::to_string(remainingSeconds) + ",\"remainingApprox\":"
              + std::string(remainingApproximate ? "true" : "false") + ",";

    json += "\"control\":" + std::string(control ? "true" : "false");

    // Families alleen voor bedienende clients (scheelt ook een aparte poll).
    if (control)
    {
        const auto families = createFamilyJson();
        json += "," + families.substr(1, families.size() - 2);   // "families":[...]
        json += ",\"resume\":\"" + escapeJson(resumeLabel) + "\"";
    }

    json += "}";
    return json;
}

std::string WebServer::createJson() const
{
    std::scoped_lock lock(mutex_);

    if (songJsonCacheRevision_ == contentRevision_ && !songJsonCache_.empty())
        return songJsonCache_;

    const auto song = song_;
    const int transpose = transpose_;

    std::string json = "{";
    json += "\"rev\":" + std::to_string(contentRevision_) + ",";
    json += "\"songRev\":" + std::to_string(songRevision_) + ",";

    if (!song)
    {
        json += "\"song\":\"\",\"duration\":0,\"lyrics\":[],\"chords\":[]}";
    }
    else
    {
        double durationSeconds = 0.0;
        if (!song->playbackEvents.empty())
            durationSeconds = std::max(durationSeconds, song->playbackEvents.back().seconds);
        if (!song->lyrics.empty())
            durationSeconds = std::max(durationSeconds, song->lyrics.back().seconds);
        if (!song->chords.empty())
            durationSeconds = std::max(durationSeconds, song->chords.back().endSeconds);

        const auto& path = song->sourceFile;
        const auto slash = path.find_last_of("/\\");
        const auto start = slash == std::string::npos ? 0 : slash + 1;
        const auto dot = path.find_last_of('.');
        const auto end = dot != std::string::npos && dot > start ? dot : path.length();
        const std::string title =
            songTitleOverride_.empty() ? path.substr(start, end - start) : songTitleOverride_;

        json += "\"song\":\"" + escapeJson(title) + "\",";
        json += "\"duration\":" + std::to_string(durationSeconds) + ",";

        // Tempo en maatsoort, voor de maatindicator in de browser.
        json += "\"ppq\":" + std::to_string(song->ticksPerQuarterNote) + ",";

        json += "\"tempo\":[";
        for (std::size_t i = 0; i < song->tempoMap.size(); ++i)
        {
            if (i != 0) json += ",";
            const auto& point = song->tempoMap[i];
            json += "[" + std::to_string(point.tick) + "," + std::to_string(point.seconds) + ","
                  + std::to_string(point.bpm) + "]";
        }
        json += "],";

        json += "\"sigs\":[";
        for (std::size_t i = 0; song->tempoKnown && i < song->timeSignatures.size(); ++i)
        {
            if (i != 0) json += ",";
            const auto& point = song->timeSignatures[i];
            json += "[" + std::to_string(point.tick) + "," + std::to_string(point.numerator) + ","
                  + std::to_string(point.denominator) + "]";
        }
        json += "],";

        json += "\"sections\":[";
        for (std::size_t i = 0; i < song->sections.size(); ++i)
        {
            if (i != 0) json += ",";
            json += "{\"time\":" + std::to_string(song->sections[i].seconds)
                  + ",\"name\":\"" + escapeJson(song->sections[i].name) + "\"}";
        }
        json += "],";

        json += "\"lyrics\":[";
        for (std::size_t i = 0; i < song->lyrics.size(); ++i)
        {
            if (i != 0) json += ",";
            const auto& lyric = song->lyrics[i];
            json += "{";
            json += "\"time\":" + std::to_string(lyric.seconds) + ",";
            json += "\"line\":" + std::to_string(lyric.lineIndex) + ",";
            json += "\"startsNewWord\":" + std::string(lyric.startsNewWord ? "true" : "false") + ",";
            json += "\"endsWord\":" + std::string(lyric.endsWord ? "true" : "false") + ",";
            json += "\"startsNewLine\":" + std::string(lyric.startsNewLine ? "true" : "false") + ",";
            json += "\"endsLine\":" + std::string(lyric.endsLine ? "true" : "false") + ",";
            json += "\"text\":\"" + escapeJson(lyric.text) + "\"}";
        }
        json += "],";

        json += "\"chords\":[";
        for (std::size_t i = 0; i < song->chords.size(); ++i)
        {
            if (i != 0) json += ",";
            const auto& chord = song->chords[i];
            const std::string label =
                transpose != 0
                    ? ChordParser::transposeLabel(chord.label, transpose)
                    : chord.label;

            json += "{";
            json += "\"time\":" + std::to_string(chord.seconds) + ",";
            json += "\"end\":" + std::to_string(chord.endSeconds) + ",";
            json += "\"label\":\"" + escapeJson(label) + "\"}";
        }
        json += "]}";
    }

    songJsonCache_ = std::move(json);
    songJsonCacheRevision_ = contentRevision_;
    return songJsonCache_;
}

std::string WebServer::createHtml() const
{
    return R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Merik</title>
<style>
:root { color-scheme: dark; }
* { box-sizing: border-box; }
html, body { margin:0; width:100%; height:100%; }
body {
    background:#002; color:#fff; overflow:hidden;
    font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display","Segoe UI",sans-serif;
}
header {
    position:fixed; inset:0 0 auto 0; z-index:10; padding:18px 30px 50px;
    background:linear-gradient(to bottom,rgba(0,0,0,.99),rgba(0,0,0,.9),transparent);
    pointer-events:none;
}
#songProgress {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    height: 3px;
    z-index: 99999;
    pointer-events: none;
    background: #111;
}

#songProgressFill {
    width: 0%;
    height: 3px;
    background-color: #ff4a4a;
}
#song { font-size:36px; font-weight:600; }
#time { margin-top:4px; color:#bbb; font-size:24px; font-variant-numeric:tabular-nums; }
main { position:absolute; inset:0; display:flex; justify-content:center; overflow:hidden; }
#lyricsViewport {
    width:100%;
    max-width:1100px;
    height:100%;
    overflow-y:auto;
    overflow-x:hidden;
    scrollbar-width:none;
    padding:120px 35px 260px;
}
#lyricsViewport::-webkit-scrollbar { display:none; }
#lyrics { width:100%; font-size:clamp(38px,4.3vw,64px); line-height:1.10; font-weight:650; text-align:center; }
.lyric-line { width:100%; margin:0 auto .15em; padding:.08em 0; }
.lyric-part { display:inline; }
.lyric-part.past { color: #ff4a4a; }
.lyric-part.current { color: #ff4a4a; }
.lyric-part.future { color: #fff; }
footer {
    position:fixed; inset:auto 0 0 0; z-index:20; padding:62px 10px 10px;
    background:linear-gradient(to top,rgba(0,0,0,1) 0%,rgba(0,0,0,.98) 42%,rgba(0,0,0,.86) 70%,transparent 100%);
    pointer-events:none;
}
#chordViewport { width:100%; max-width:1000px; margin:0 auto; overflow:hidden; }

#lyricsFontControl {
    position: fixed;
    right: 10px;
    bottom: 80px;
    z-index: 30;

    display: flex;
    align-items: center;
    gap: 10px;

    padding: 8px 12px;
    border-radius: 8px;

    background: rgba(0, 0, 0, 0.15);
    color: rgba(255, 74, 74, 0.6);
    opacity: .55;
    transition: opacity 150ms ease;

    font-size: 15px;
    font-weight: 600;

    pointer-events: auto;
}

#lyricsFontControl:hover { opacity: 1; }

#lyricsFontControl label {
    font-size: 18px;
    white-space: nowrap;
}

#lyricsFontSize {
    width: 150px;
    accent-color: #ff4a4a;
    cursor: pointer;
}

#lyricsFontSizeValue {
    min-width: 22px;
    text-align: right;
    font-variant-numeric: tabular-nums;
}

@media (max-width: 600px) {
    #lyricsFontControl {
        right: 12px;
        bottom: 125px;
        gap: 6px;
        padding: 6px 8px;
    }

    #lyricsFontSize {
        width: 100px;
    }
}

#chords {
    display: grid;
    grid-template-columns:
        repeat(5, minmax(0, 1fr));
    align-items: end;
    gap: 22px;
    width: 100%;
    will-change: transform, opacity;
}
.chord-slot {
    min-width:0; display:flex; flex-direction:column; align-items:center;
    justify-content:flex-end; gap:1px; opacity:1;
    transition:transform 180ms ease,opacity 180ms ease;
}
.chord-slot.current { opacity:1; }
.chord {
    width:100%;
    white-space:nowrap;
    color:#fff;
    font-size:clamp(20px,5vw,50px);
    font-weight:700;
    line-height:1.05;
    text-align:center;
    overflow:visible;
}

.chord-slot.current .chord {
    color: #ff4a4a;  font-weight:800;
}
.chord-progress {
    position:relative; width:100%; height:8px; overflow:hidden;
    background: #555; border-radius:999px;
}
.chord-progress-fill{
    position: absolute; top: 0; right: 0; bottom: 0;
    width: 100%;background: #ff4a4a;border-radius: 999px;
    transition:width 100ms linear;
}
#lyricCountdown
{
    position: fixed;

    /* Onderin in het midden, boven de akkoorden */
    bottom: 100px;
    left: 50%;
    transform: translateX(-50%);
    width: 300px;
    text-align: center;
    z-index: 100;
    padding: 18px 18px;
    border-radius: 999px;
    background: #ffd23f;   /* dezelfde gele kleur als de songonderdelen */
    color: #111;
    font-weight: 700;
    font-size: 50px;
    opacity: 0;
    transition:
        opacity 1s ease;
}
.future-progress {
    align-self: center;

    transition:
        width 180ms ease,
        opacity 180ms ease;
}    
/* Rechtsboven: de maatteller met ledjes, daaronder de songonderdelen uit het MIDI-bestand */
#topRight {
    position:fixed; top:18px; right:28px; z-index:90; display:flex; flex-direction:column; align-items:flex-end; gap:12px;
    pointer-events:none;
}
#beatBox {
    --led:18px; --ledgap:10px; --sig:24px;
    display:flex; align-items:center; gap:14px; padding:6px 14px; border-radius:999px;
    background:rgba(255,255,255,.06); border:1px solid rgba(255,255,255,.10);
}
#beatBox.medium { --led:15px; --ledgap:8px; --sig:20px; }
#beatBox.small { --led:12px; --ledgap:6px; --sig:17px; gap:10px; }
#beatBox[hidden] { display:none; }
/* Zoeken (alleen met de bedieningssleutel) */
#searchOverlay { position:fixed; inset:0; z-index:900; display:flex; align-items:flex-start; justify-content:center;
    background:rgba(8,9,12,.92); padding:0 14px 18px; overflow:auto; }
#searchOverlay[hidden] { display:none; }
.searchPanel { width:min(760px,100%); display:flex; flex-direction:column; gap:12px; }
.searchHead { display:flex; align-items:center; justify-content:space-between; color:#fff; font-size:22px; font-weight:700;
    position:sticky; top:0; z-index:5; background:#0a0b0e; padding:16px 0 10px; }
.searchHead button { width:44px; height:44px; border:0; border-radius:50%; background:#2b2e35; color:#fff; font-size:20px; cursor:pointer; }
#searchInput { width:100%; box-sizing:border-box; font-size:26px; padding:14px 18px; border-radius:14px;
    border:2px solid #30333a; background:#14161a; color:#fff; outline:none; }
#searchInput:focus { border-color:#1876ff; }
.searchScope { display:flex; gap:8px; }
.searchScope button { flex:1; padding:12px 8px; font-size:17px; border-radius:12px; border:1px solid #30333a; background:#1b1d22; color:#9ea4b0; cursor:pointer; }
.searchScope button.on { background:#1876ff; border-color:#1876ff; color:#fff; font-weight:600; }
#searchResume { padding:13px 16px; font-size:18px; border-radius:12px; border:1px solid #7a141c; background:#3a1217; color:#fff; cursor:pointer; text-align:left; }
#searchResume[hidden] { display:none; }
#searchStatus { color:#8a8f9c; font-size:16px; min-height:22px; }
#searchResults { display:flex; flex-direction:column; gap:8px; }
.resultRow { display:flex; align-items:center; gap:12px; padding:10px 12px 10px 18px; min-height:64px;
    border-radius:12px; border:1px solid #30333a; background:#1b1d22; }
.resultText { flex:1; min-width:0; color:#fff; font-size:22px; overflow-wrap:anywhere; }
.resultText small { display:block; margin-top:3px; font-size:15px; color:#8a8f9c; }
.resultActions { display:flex; gap:8px; flex-shrink:0; }
.resultActions button { padding:13px 16px; font-size:17px; border-radius:10px; border:1px solid #3a3e47;
    background:#2b2e35; color:#fff; cursor:pointer; white-space:nowrap; }
.resultActions button.next { background:#1876ff; border-color:#1876ff; font-weight:600; }
.resultActions button:active { filter:brightness(1.25); }
@media (max-width:560px) { .resultRow { flex-wrap:wrap; } .resultActions { width:100%; } .resultActions button { flex:1; } }
body:not(.control) #btnSearch, body:not(.control) #btnSetlist { display:none; }
#setlistOverlay { position:fixed; inset:0; z-index:900; display:flex; align-items:flex-start; justify-content:center;
    background:rgba(8,9,12,.94); padding:0 14px 18px; overflow:auto; }
#setlistOverlay[hidden] { display:none; }
#setlistInfo { color:#9ec5ff; font-size:18px; min-height:22px; }
#setlistRows { display:flex; flex-direction:column; gap:8px; }
.resultRow.current { border-color:#1876ff; background:#14264a; }
.resultRow .badge { color:#ffd23f; font-weight:700; font-size:16px; padding:0 8px; letter-spacing:.06em; }
/* Merik draait niet (meer): nette melding in plaats van een stille, bevroren pagina */
#offline { position:fixed; inset:0; z-index:1000; display:flex; align-items:center; justify-content:center;
    background:rgba(12,13,16,.94); backdrop-filter:blur(4px); -webkit-backdrop-filter:blur(4px); }
#offline[hidden] { display:none; }
#offline .card { text-align:center; padding:34px 44px; border-radius:18px; background:#1b1d22;
    border:1px solid #30333a; max-width:80vw; }
#offline .title { font-size:30px; font-weight:700; color:#fff; margin-bottom:10px; }
#offline .text { font-size:18px; color:#9ea4b0; line-height:1.4; }
#offline .dots { margin-top:22px; display:flex; gap:10px; justify-content:center; }
#offline .dots span { width:10px; height:10px; border-radius:50%; background:#1876ff; opacity:.25;
    animation:offlinePulse 1.2s infinite ease-in-out; }
#offline .dots span:nth-child(2) { animation-delay:.2s; }
#offline .dots span:nth-child(3) { animation-delay:.4s; }
@keyframes offlinePulse { 0%,80%,100% { opacity:.25; } 40% { opacity:1; } }
/* Songonderdelen onder de maatteller: de huidige met een aflopend streepje, daaronder de volgende
   (kleiner en doorzichtig). Bij een wissel schuift de volgende omhoog en vervaagt de huidige. */
#sections { position:relative; display:flex; flex-direction:column; align-items:flex-end; gap:8px; }
#sections[hidden] { display:none; }
.section-slot { display:flex; flex-direction:column; align-items:stretch; gap:4px; min-width:130px; transform-origin:right top; }
.section-name { color:#ffd23f; font-size:32px; font-weight:700; letter-spacing:.06em; text-transform:uppercase;
    white-space:nowrap; text-align:right; line-height:1.05; }
.section-slot.next .section-name { font-size:22px; opacity:.45; }
.section-progress { position:relative; width:100%; height:6px; overflow:hidden; background:rgba(255,210,63,.28); border-radius:999px; }
.section-progress-fill { position:absolute; top:0; right:0; bottom:0; width:100%; background:#ffd23f; border-radius:999px;
    transition:width 100ms linear; }
@media (max-width:700px) {
    #lyricCountdown { bottom:72px; width:220px; font-size:36px; padding:12px 14px; }
    #topRight { top:12px; right:12px; gap:8px; }
    .section-name { font-size:21px; }
    .section-slot.next .section-name { font-size:15px; }
    .section-slot { min-width:90px; }
}
#beatSig { font-size:var(--sig); font-weight:700; color:#fff; font-variant-numeric:tabular-nums; }
#beatLeds { display:flex; gap:var(--ledgap); }
.led {
    --c:24,118,255; --i:0; width:var(--led); height:var(--led); border-radius:50%;
    background:rgba(var(--c), calc(.06 + var(--i) * .94));
    border:1px solid rgba(var(--c), .35);
    box-shadow:0 0 calc(var(--i) * 14px) rgba(var(--c), calc(var(--i) * .85));
}
.led.first { --c:255,176,32; }
@media (max-width:600px) {
    #beatBox { --led:13px; --ledgap:6px; --sig:17px; padding:5px 10px; gap:8px; }
    #beatBox.medium { --led:11px; --ledgap:5px; --sig:15px; }
    #beatBox.small { --led:9px; --ledgap:4px; --sig:13px; }
}

#time .key { margin-left:14px; color:#6aa5ff; }
#setlistRemaining { margin-top:2px; color:#9ec5ff; font-size:20px; font-variant-numeric:tabular-nums; text-align:left; }
#setlistRemaining:empty { display:none; }
#nextSong { margin-top:2px; color:#9ea4b0; font-size:20px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
#nextSong:empty { display:none; }

/* Standaard is de pagina alleen-lezen; bediening verschijnt pas met ?key=... */
body:not(.control) #transport,
body:not(.control) #familyPanel { display:none; }

/* Transport: verticaal tegen de linkerrand, alleen iconen, heel subtiel */
#transport {
    position:fixed; left:6px; top:50%; transform:translateY(-50%); z-index:30;
    display:flex; flex-direction:column; gap:10px; pointer-events:auto;
}
#transport button {
    width:48px; height:48px; padding:0; border:1px solid rgba(255,255,255,.14);
    border-radius:12px; background:rgba(255,255,255,.06); color:rgba(255,255,255,.55);
    font-size:20px; line-height:1; cursor:pointer; opacity:.7;
    -webkit-tap-highlight-color:transparent;
    transition:background 150ms ease, color 150ms ease, opacity 150ms ease;
}
#transport svg { width:22px; height:22px; fill:currentColor; display:block; margin:0 auto; pointer-events:none; }
#btnPlay .i-pause { display:none; }
#btnPlay[data-playing="1"] .i-play { display:none; }
#btnPlay[data-playing="1"] .i-pause { display:block; }
#transport button:hover { background:rgba(255,255,255,.14); color:#fff; opacity:1; }
#transport button:active { background:rgba(255,255,255,.22); }
#transport button.primary { color:rgba(106,165,255,.95); }
#transport button.primary:hover { color:#fff; background:rgba(24,118,255,.45); }
#transport button:focus-visible { outline:2px solid #6aa5ff; outline-offset:2px; }

@media (max-width:600px) {
    header { padding:15px 18px 40px; }
    #nextSong { font-size:14px; }
    #setlistRemaining { font-size:14px; }
    #transport { left:4px; }
    #transport button { width:42px; height:42px; }
    #song { font-size:21px; }
    #time { font-size:13px; }
    #lyricsViewport { padding:100px 18px 220px; }
    #lyrics { font-size:clamp(32px,9vw,48px); }
    footer { padding:66px 10px 14px; }
    #chords { gap:7px; }
    .chord-slot { gap:7px; }
    .chord { font-size:20px; }
    .chord-slot.current .chord { font-size:20px; }
    .chord-progress { height:6px; }
}

/* ############################################# */
/* FAMILY SLIDERS ############################## */
/* ############################################# */

#familyPanel { position: fixed; z-index: 100; top: 0; right: 0; bottom: 0; width: 20%; min-width: 220px; max-width: 340px; transform: translateX(100%); transition: transform 280ms ease; pointer-events: none; }
#familyPanelContent { position: absolute; inset: 0; padding: 20px 16px; overflow-x: hidden; overflow-y: auto; background: rgba(27,29,34,0.97); border-left: 1px solid #30333a; pointer-events: auto; }
#familyToggle { position: absolute; z-index: 101; top: 50%; left: -28px; width: 28px; height: 76px; padding: 0; transform: translateY(-50%); border: 1px solid #30333a; border-right: none; border-radius: 8px 0 0 8px; background: #1b1d22; color: #9ea4b0; font-size: 28px; cursor: pointer; pointer-events: auto; }
body.family-panel-open #familyPanel { transform: translateX(0); }
body.family-panel-open #familyPanelContent { pointer-events: auto; }
#lyricsViewport { width: 100%; margin-right: 0; transition: width 280ms ease, margin-right 280ms ease; }
body.family-panel-open #lyricsViewport { width: 80%; margin-right: 20%; }
#familyPanelHeader { display: flex; align-items: center; justify-content: space-between; gap: 10px; margin-bottom: 22px; font-size: 20px; font-weight: 700; color: #fff; }
#familyClose { width: 36px; height: 36px; border: 0; border-radius: 6px; background: #2e3138; color: #fff; font-size: 24px; cursor: pointer; }
.family-control { margin-bottom: 5px; padding-bottom: 4px; border-bottom: 1px solid #30333a; }
.family-control-header { display: flex; align-items: center; justify-content: space-between; gap: 8px; margin-bottom: 4px; }
.family-name { color: #fff; font-size: 15px; font-weight: 600; }
.family-enable { appearance: none; -webkit-appearance: none; width: 34px; height: 18px; flex-shrink: 0; border-radius: 9px; background: #555; position: relative; cursor: pointer; transition: background 180ms ease; }
.family-enable::before { content: ""; position: absolute; width: 14px; height: 14px; top: 2px; left: 2px; border-radius: 50%; background: #fff; transition: transform 180ms ease; }
.family-enable:checked { background: #1876ff; }
.family-enable:checked::before { transform: translateX(16px); }
.family-enable:focus-visible { outline: 2px solid #6aa5ff; outline-offset: 3px; }
.family-slider-row { display: flex; align-items: center; gap: 3px; }
.family-slider { width: 100%; min-width: 0; accent-color: #1876ff; cursor: pointer; }
.family-value { width: 28px; text-align: right; color: #bbb; font-size: 12px; font-variant-numeric: tabular-nums; }
.family-control.disabled { opacity: 0.4; }
.family-source { font-size: 11px; font-weight: 600; color: #eceef2; padding: 1px 8px; border-radius: 8px; background: rgba(160,164,178,0.28); margin-left: 8px; margin-right: auto; }
.family-source.midi { background: rgba(160,120,255,0.28); }
.family-source.audio { background: rgba(0,164,235,0.28); }
.family-meter { height: 4px; border-radius: 2px; background: #1e2025; margin: 3px 34px 0 0; overflow: hidden; }
.family-meter-fill { height: 100%; width: 100%; background: linear-gradient(90deg, #2ecc71 0%, #2ecc71 70%, #f5a623 85%, #ff5555 100%); clip-path: inset(0 100% 0 0); transition: clip-path 90ms linear; }
@media (max-width: 700px) { #familyPanel { width: 45%; min-width: 200px; } body.family-panel-open #lyricsViewport { width: 55%; margin-right: 45%; } }
@media (prefers-reduced-motion: reduce) { #familyPanel, #lyricsViewport { transition: none; } }
</style>
</head>

<!-- ################################################################### -->
<!-- ########################## HTML   ################################# -->
<!-- ################################################################### -->
<body>
<header><div id="song">Merik</div><div id="time">00:00 / 00:00</div><div id="nextSong"></div><div id="setlistRemaining"></div></header>
<div id="songProgress"><div id="songProgressFill"></div></div>
<div id="lyricCountdown"></div>
<div id="offline" hidden><div class="card"><div class="title">Merik is not running</div><div class="text">Start Merik on the computer.<br>This page reconnects by itself.</div><div class="dots"><span></span><span></span><span></span></div></div></div>
<div id="topRight">
<div id="beatBox" hidden><span id="beatSig">4/4</span><span id="beatLeds"></span></div>
<div id="sections" hidden><div class="section-slot current"><span class="section-name" id="sectionCurrent"></span><div class="section-progress"><div id="sectionRemaining" class="section-progress-fill"></div></div></div><div class="section-slot next"><span class="section-name" id="sectionNext"></span></div></div>
</div>
<main>
    <div id="lyricsViewport">
        <div id="lyrics"></div>
    </div>

    <div id="lyricsFontControl">
        <label for="lyricsFontSize">Aa</label>
        <input
            id="lyricsFontSize"
            type="range"
            min="24"
            max="90"
            value="64"
            step="1"
            aria-label="Lettergrootte songteksten">
    </div>
</main>

<aside id="familyPanel" aria-hidden="true">
    <button id="familyToggle" type="button" aria-label="Family mixer openen" aria-expanded="false">‹</button>
    <div id="familyPanelContent">
        <div id="familyPanelHeader">
            <span>Families</span>
            <button id="familyClose" type="button" aria-label="Mixer sluiten">X</button>
        </div>
        <div id="familyControls"></div>
    </div>
</aside>

<div id="transport" role="group" aria-label="Transport">
    <button id="btnPlay" type="button" class="primary" aria-label="Play" data-playing="0">
        <svg class="i-play" viewBox="0 0 24 24" aria-hidden="true"><path d="M8 5v14l11-7z"/></svg>
        <svg class="i-pause" viewBox="0 0 24 24" aria-hidden="true"><path d="M7 5h3.5v14H7zM13.5 5H17v14h-3.5z"/></svg>
    </button>
    <button id="btnStop" type="button" aria-label="Stop">
        <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 7h10v10H7z"/></svg>
    </button>
    <button id="btnNext" type="button" aria-label="Volgende nummer">
        <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 6l8 6-8 6zM12 6l8 6-8 6zM19.5 6H22v12h-2.5z"/></svg>
    </button>
    <button id="btnSearch" type="button" aria-label="Zoek een nummer">
        <svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6" fill="none" stroke="currentColor" stroke-width="2.4"/><path d="M15.2 15.2L20.5 20.5" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round"/></svg>
    </button>
    <button id="btnSetlist" type="button" aria-label="Setlist">
        <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M9 6h12v2H9zM9 11h12v2H9zM9 16h12v2H9z"/><circle cx="5" cy="7" r="1.7"/><circle cx="5" cy="12" r="1.7"/><circle cx="5" cy="17" r="1.7"/></svg>
    </button>
</div>

<div id="setlistOverlay" hidden>
    <div class="searchPanel" role="dialog" aria-label="Setlist">
        <div class="searchHead"><span id="setlistTitle">Setlist</span><button id="setlistClose" type="button" aria-label="Sluiten">&#10005;</button></div>
        <div id="setlistInfo"></div>
        <div id="setlistRows"></div>
    </div>
</div>

<div id="searchOverlay" hidden>
    <div class="searchPanel" role="dialog" aria-label="Zoek een nummer">
        <div class="searchHead"><span>Search songs</span><button id="searchClose" type="button" aria-label="Sluiten">&#10005;</button></div>
        <input id="searchInput" type="search" placeholder="Type at least 3 letters..." autocomplete="off" autocorrect="off" autocapitalize="none" spellcheck="false" enterkeyhint="search" inputmode="search">
        <div class="searchScope">
            <button id="scopeAll" type="button" class="on">All setlists</button>
            <button id="scopeCurrent" type="button">Current setlist</button>
        </div>
        <button id="searchResume" type="button" hidden></button>
        <div id="searchStatus">Type at least 3 letters</div>
        <div id="searchResults"></div>
    </div>
</div>

<footer>

  <div id="chordViewport">

    <div id="chords">

      <!-- Current chord -->

      <div class="chord-slot current">

        <span id="currentChord"
              class="chord">
        </span>

        <div id="currentProgress" class="chord-progress">

          <div id="chordRemaining"
               class="chord-progress-fill">
          </div>

        </div>

      </div>

      <!-- Next chord 1 -->

      <div class="chord-slot">

        <span id="next1"
              class="chord">
        </span>

        <div id="nextProgress1"
             class="chord-progress future-progress">
        </div>

      </div>

      <!-- Next chord 2 -->

      <div class="chord-slot">

        <span id="next2"
              class="chord">
        </span>

        <div id="nextProgress2"
             class="chord-progress future-progress">
        </div>

      </div>

      <!-- Next chord 3 -->

      <div class="chord-slot">

        <span id="next3"
              class="chord">
        </span>

        <div id="nextProgress3"
             class="chord-progress future-progress">
        </div>

      </div>

      <!-- Next chord 4 -->

      <div class="chord-slot">

        <span id="next4"
              class="chord">
        </span>

        <div id="nextProgress4"
             class="chord-progress future-progress">
        </div>

      </div>

    </div>

  </div>

</footer>
<script>
let songData = null;

let lastCurrentIndex = -1;
let lastChordIndex = -2;
let lastChordSignature = "";
let chordAnimation = null;

let lastPreparedLineIndex = -1;
let initialLyricsPositioned = false;
let lyricScrollAnimation = null;

let lastSongTitle = "";
let lastSongRev = -1;
let lyricsDomBuilt = false;
let lyricLinesCache = [];
let lyricSpanEls = [];
let lyricSpanState = [];
let lyricLineEls = [];
let lyricLineOf = [];
let markedLyricLine = -1;
let lastPosition = 0;

// Resterende tijd van de setlist: m:ss, of u:mm:ss vanaf een uur.
function formatLeft(seconds) {
    seconds = Math.max(0, Math.floor(Number(seconds || 0)));
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    return hours > 0 ? hours + ":" + String(minutes).padStart(2, "0") + ":" + String(secs).padStart(2, "0")
                     : minutes + ":" + String(secs).padStart(2, "0");
}

function formatTime(seconds) {
    seconds = Math.max(0, Number(seconds || 0));
    const minutes = Math.floor(seconds / 60);
    const secs = Math.floor(seconds % 60);
    return String(minutes).padStart(2,"0") + ":" + String(secs).padStart(2,"0");
}
function buildLines(lyrics) {
    const lines = new Map();
    for (let i=0;i<lyrics.length;++i) {
        const lyric={...lyrics[i],originalIndex:i};
        const lineIndex=Number(lyric.line ?? 0);
        if(!lines.has(lineIndex)) lines.set(lineIndex,[]);
        lines.get(lineIndex).push(lyric);
    }
    return [...lines.entries()].sort((a,b)=>a[0]-b[0]).map(([lineIndex,segments])=>({lineIndex,segments}));
}

function smoothScrollLyrics(targetTop, duration = 1200)
{
    const viewport =
        document.getElementById("lyricsViewport");

    if (!viewport)
        return;

    if (lyricScrollAnimation)
    {
        cancelAnimationFrame(
            lyricScrollAnimation
        );

        lyricScrollAnimation = null;
    }

    const startTop =
        viewport.scrollTop;

    const distance =
        targetTop - startTop;

    if (Math.abs(distance) < 2)
    {
        viewport.scrollTop = targetTop;
        return;
    }

    const startTime =
        performance.now();

    /*
     * Zeer rustige ease-in/ease-out.
     *
     * Begin langzaam,
     * beweeg in het midden vloeiend,
     * en kom langzaam tot stilstand.
     */
    function easeInOut(t)
    {
        return t < 0.5
            ? 4 * t * t * t
            : 1 - Math.pow(-2 * t + 2, 3) / 2;
    }

    function animate(now)
    {
        const elapsed =
            now - startTime;

        const progress =
            Math.min(
                1,
                elapsed / duration
            );

        const eased =
            easeInOut(progress);

        viewport.scrollTop =
            startTop +
            distance * eased;

        if (progress < 1)
        {
            lyricScrollAnimation =
                requestAnimationFrame(
                    animate
                );
        }
        else
        {
            viewport.scrollTop =
                targetTop;

            lyricScrollAnimation = null;
        }
    }

    lyricScrollAnimation =
        requestAnimationFrame(
            animate
        );
}

function renderLyrics()
{
    if (!songData)
        return;

    const lyrics =
        songData.lyrics || [];

    const position =
        Number(songData.position || 0);

    /*
     * Detecteer een nieuw nummer.
     */
    const songChanged =
        songData.songRev !== lastSongRev;

    /*
     * Detecteer een restart / terugspoelen naar het begin.
     *
     * Een kleine terugloop kan door timingverschillen ontstaan,
     * daarom gebruiken we 2 seconden als grens.
     */
    const restarted =
        position < lastPosition - 2;

    if (songChanged || restarted)
    {
        lastCurrentIndex = -1;
        lastPreparedLineIndex = -1;
        initialLyricsPositioned = false;

        if (lyricScrollAnimation)
        {
            cancelAnimationFrame(
                lyricScrollAnimation
            );

            lyricScrollAnimation = null;
        }

        lastSongTitle =
            songData.song || "";
    }

    if (songChanged)
    {
        lastSongRev = songData.songRev;
        lyricsDomBuilt = false;
    }

    lastPosition = position;

    const container =
        document.getElementById("lyrics");

    const viewport =
        document.getElementById("lyricsViewport");

    if (songChanged || restarted)
    {
        container.innerHTML = "";

        viewport.scrollTo({
            top: 0,
            behavior: "auto"
        });
    }

    if (!lyrics.length)
    {
        container.innerHTML = "";

        viewport.scrollTo({
            top: 0,
            behavior: "auto"
        });

        lastCurrentIndex = -1;
        lastPreparedLineIndex = -1;
        initialLyricsPositioned = false;
        lyricsDomBuilt = false;

        return;
    }

    /*
     * Zoek de werkelijk actieve lyric.
     *
     * Als het nummer nog vóór de eerste lyric staat,
     * blijft currentIndex -1. Daardoor wordt de eerste
     * regel wel klaargezet, maar nog niet rood.
     */
    let currentIndex = -1;

    for (let i = 0;
         i < lyrics.length;
         ++i)
    {
        if (Number(lyrics[i].time) <= position)
            currentIndex = i;
        else
            break;
    }

    /*
     * De regels en woorden worden maar één keer per nummer opgebouwd.
     * Daarna worden bij een nieuwe actieve lyric alleen nog de
     * klassen (past / current / future) bijgewerkt.
     */
    if (!lyricsDomBuilt ||
        container.children.length === 0)
    {
        container.innerHTML = "";

        lyricLinesCache = buildLines(lyrics);
        lyricSpanEls = new Array(lyrics.length);
        lyricSpanState = new Array(lyrics.length).fill("");
        lyricLineOf = new Array(lyrics.length).fill(0);
        lyricLineEls = [];
        markedLyricLine = -1;

        const fragment = document.createDocumentFragment();

        lyricLinesCache.forEach((line, lineNumber) =>
        {
            const lineElement =
                document.createElement("div");

            lineElement.className =
                "lyric-line";

            for (let segmentIndex = 0;
                 segmentIndex < line.segments.length;
                 ++segmentIndex)
            {
                const lyric =
                    line.segments[segmentIndex];

                const span =
                    document.createElement("span");

                span.className =
                    "lyric-part";

                span.textContent =
                    lyric.text || "";

                lineElement.appendChild(span);

                lyricSpanEls[lyric.originalIndex] = span;
                lyricLineOf[lyric.originalIndex] = lineNumber;

                const nextIndex =
                    segmentIndex + 1;

                if (nextIndex < line.segments.length)
                {
                    const nextLyric =
                        line.segments[nextIndex];

                    if (nextLyric.startsNewWord ||
                        lyric.endsWord)
                    {
                        lineElement.appendChild(
                            document.createTextNode(" ")
                        );
                    }
                }
            }

            lyricLineEls.push(lineElement);
            fragment.appendChild(lineElement);
        });

        container.appendChild(fragment);

        lyricsDomBuilt = true;
        lastCurrentIndex = -2;   // forceer de eerste klassen-update
    }

    if (currentIndex !== lastCurrentIndex)
    {
        lastCurrentIndex = currentIndex;

        for (let i = 0; i < lyricSpanEls.length; ++i)
        {
            const state =
                i < currentIndex ? "past"
                : i === currentIndex ? "current"
                : "future";

            if (lyricSpanState[i] !== state)
            {
                lyricSpanEls[i].className = "lyric-part " + state;
                lyricSpanState[i] = state;
            }
        }

        const currentLine =
            currentIndex >= 0 ? lyricLineOf[currentIndex] : -1;

        if (currentLine !== markedLyricLine)
        {
            if (markedLyricLine >= 0)
                lyricLineEls[markedLyricLine]?.classList.remove("current-line");

            if (currentLine >= 0)
                lyricLineEls[currentLine]?.classList.add("current-line");

            markedLyricLine = currentLine;
        }
    }

    /*
     * Bij het starten van een nummer:
     *
     * zet de eerste regel meteen klaar.
     * Geen animatie en geen afhankelijkheid van
     * het eerste rode woord.
     */
    if (!initialLyricsPositioned)
    {
        const firstLine =
            container.querySelector(".lyric-line");

        if (firstLine)
        {
            const targetTop =
                firstLine.offsetTop -
                viewport.clientHeight * 0.30;

            viewport.scrollTo({
                top: Math.max(0, targetTop),
                behavior: "auto"
            });

            initialLyricsPositioned = true;
        }
    }

    /*
     * Bepaal welke regel momenteel actief is.
     *
     * Als we nog vóór de eerste lyric zitten,
     * gebruiken we regel 0 als voorbereide regel.
     */
    const lines =
        lyricLinesCache;

    const currentLineIndex =
        currentIndex >= 0
            ? lyricLineOf[currentIndex]
            : 0;

    /*
    * De volgende regel.
    */
    const nextLineIndex =
        currentLineIndex + 1;

    if (nextLineIndex >= lines.length)
        return;

    const nextLine =
        lines[nextLineIndex];

    if (!nextLine ||
        !nextLine.segments.length)
        return;

    /*
    * Bepaal wanneer de huidige regel volledig rood is.
    *
    * De laatste lyric van de huidige regel is het laatste
    * stukje tekst dat rood moet worden. Zodra de timestamp
    * daarvan bereikt is, mag de volgende regel in beeld
    * worden geschoven.
    */
    const currentLine =
        lines[currentLineIndex];

    if (!currentLine ||
        !currentLine.segments.length)
        return;

    const lastLyric =
        currentLine.segments[
            currentLine.segments.length - 1
        ];


    /*
    * De laatste lyric moet eerst actief zijn geworden.
    * Daarna is de hele huidige regel rood.
    */
    if (currentIndex < lastLyric.originalIndex)
        return;

    /*
    * Deze regel mag maar één keer worden voorbereid.
    */
    if (lastPreparedLineIndex === nextLineIndex)
        return;

    lastPreparedLineIndex =
        nextLineIndex;

    const nextLineElement =
        lyricLineEls[nextLineIndex];

    if (!nextLineElement)
        return;

    const targetTop =
        nextLineElement.offsetTop -
        viewport.clientHeight * 0.30;

    /*
    * Rustige scroll naar de volgende regel.
    */
    smoothScrollLyrics(
        Math.max(0, targetTop),
        1200
    ); 
}



function renderChordProgress(currentChord)
{
    const progressContainer =
        document.getElementById(
            "currentProgress");

    const progressElement =
        document.getElementById(
            "chordRemaining");

    if (!progressContainer ||
        !progressElement)
    {
        return;
    }

    if (!currentChord ||
        !songData)
    {
        // Geen akkoord: ook het streepje verbergen.
        progressContainer.style.visibility =
            "hidden";

        progressElement.style.width =
            "0%";

        return;
    }

    progressContainer.style.visibility =
        "visible";

    const totalWidth =
        chordDurationWidth(
            currentChord);

    progressContainer.style.width =
        totalWidth + "%";

    const position =
        Number(
            songData.position || 0);

    const start =
        Number(
            currentChord.time || 0);

    const end =
        Number(
            currentChord.end || 0);

    const duration =
        end - start;

    if (duration <= 0)
    {
        progressElement.style.width =
            "0%";

        return;
    }

    const remaining =
        Math.max(
            0,
            Math.min(
                duration,
                end - position));

    const remainingPercentage =
        remaining /
        duration *
        100;

    progressElement.style.width =
        remainingPercentage + "%";
}


function animateChordChange()
{
    const chordRow =
        document.getElementById("chords");

    if (!chordRow)
        return;

    /*
     * Stop een eventueel nog lopende animatie.
     * Dit is belangrijk bij snelle akkoordwisselingen.
     */
    if (chordAnimation)
    {
        chordAnimation.cancel();
    }

    chordAnimation =
        chordRow.animate(
            [
                {
                    transform:
                        "translateX(18%)",

                    opacity: 0.35
                },
                {
                    transform:
                        "translateX(0)",

                    opacity: 1
                }
            ],
            {
                duration: 1500,

                easing:
                    "cubic-bezier(0.22, 1, 0.36, 1)",

                fill: "both"
            }
        );

    chordAnimation.onfinish =
        () =>
        {
            chordAnimation = null;
        };

    chordAnimation.oncancel =
        () =>
        {
            chordAnimation = null;
        };
}


function chordDurationWidth(chord)
{
    if (!chord)
        return 0;

    const start =
        Number(chord.time || 0);

    const end =
        Number(chord.end || start);

    const duration =
        Math.max(
            0,
            end - start);

    /*
     * 0 seconden   => 10%
     * 1 seconde  => 55%
     * 2+ seconde   => 100%
     */
    const normalized =
        Math.min(
            4,
            duration);

    return 10 +
           90 * normalized/4;
}

/*
 * Een lang akkoord (bijv. F#maj7/Bb) past niet in het plekje dat het
 * krijgt. De tekst wordt dan horizontaal ingedrukt totdat hij past; bij
 * heel lange namen wordt hij daarnaast iets kleiner.
 */
function fitChordLabel(element)
{
    if (!element)
        return;

    element.style.width = "max-content";
    element.style.transform = "";

    const slot = element.parentElement;
    const available = slot ? slot.clientWidth : 0;
    const needed = element.offsetWidth;

    if (available <= 0 || needed <= available)
        return;

    const ratio = available / needed;
    const squeeze = 0.55;

    element.style.transformOrigin = "center";
    element.style.transform =
        ratio >= squeeze
            ? "scaleX(" + ratio + ")"
            : "scale(" + (ratio / squeeze) + ") scaleX(" + squeeze + ")";
}

function refitChordLabels()
{
    for (const id of ["currentChord", "next1", "next2", "next3", "next4"])
        fitChordLabel(document.getElementById(id));
}

window.addEventListener("resize", refitChordLabels);

function renderFutureChord(
    chordIndex,
    chord)
{
    const labelElement =
        document.getElementById(
            "next" + chordIndex);

    const progressElement =
        document.getElementById(
            "nextProgress" + chordIndex);

    if (!chord)
    {
        labelElement.textContent = "";

        progressElement.style.width =
            "0%";

        progressElement.style.opacity =
            "0";

        return;
    }

    labelElement.textContent =
        chord.label || "";

    fitChordLabel(labelElement);

    progressElement.style.width =
        chordDurationWidth(chord) + "%";

    progressElement.style.opacity =
        "1";
}

function renderChords()
{
    if (!songData)
        return;

    const chords =
        songData.chords || [];

    const position =
        Number(songData.position || 0);

    const ids =
    [
        "currentChord",
        "next1",
        "next2",
        "next3",
        "next4"
    ];

    /*
     * Geen akkoorden aanwezig.
     */
    if (!chords.length)
    {
        document.getElementById(
            "currentChord")
            .textContent = "";

        renderFutureChord(1, null);
        renderFutureChord(2, null);
        renderFutureChord(3, null);
        renderFutureChord(4, null);

        renderChordProgress(null);

        lastChordIndex = -2;
        lastChordSignature = "";

        return;
    }

    /*
     * Zoek het actieve akkoord.
     */
    let currentIndex = -1;

    for (let index = 0;
         index < chords.length;
         ++index)
    {
        const chordStart =
            Number(
                chords[index].time || 0);

        if (chordStart <= position)
        {
            currentIndex = index;
        }
        else
        {
            break;
        }
    }

    /*
     * Nog vóór het eerste akkoord (intro): toon het eerste akkoord
     * alvast als huidig akkoord. De voortgangsbalk blijft dan vol
     * tot het akkoord echt begint. Voorheen bleef de plek leeg met
     * alleen een klein streepje.
     */
    if (currentIndex < 0)
        currentIndex = 0;

    /*
     * De labels worden opnieuw ingevuld als het akkoord wisselt, maar
     * ook als de getoonde akkoorden zelf veranderen terwijl de index
     * gelijk blijft (nieuw nummer, transpose). Dat laatste ontbrak:
     * een nieuw nummer dat op index 0 begon toonde dan de akkoorden van
     * het vorige nummer.
     *
     * De voortgangsbalk wordt bij iedere update aangepast.
     */
    const signature =
        [0, 1, 2, 3, 4]
            .map(offset =>
                chords[currentIndex + offset]?.label || "")
            .join("|");

    const indexChanged =
        currentIndex !== lastChordIndex;

    if (indexChanged ||
        signature !== lastChordSignature)
    {
        document.getElementById(
            "currentChord")
            .textContent =
                chords[currentIndex]
                    ?.label || "";

        fitChordLabel(
            document.getElementById("currentChord"));

        renderFutureChord(
            1,
            chords[currentIndex + 1]);

        renderFutureChord(
            2,
            chords[currentIndex + 2]);

        renderFutureChord(
            3,
            chords[currentIndex + 3]);

        renderFutureChord(
            4,
            chords[currentIndex + 4]);

        lastChordIndex =
            currentIndex;

        lastChordSignature =
            signature;

        if (indexChanged)
            animateChordChange();
    }

    renderChordProgress(
        chords[currentIndex]);
}


// Songonderdelen uit het MIDI-bestand onder de maatteller: de huidige met een aflopend streepje
// (zoals bij de akkoorden) en daaronder de volgende. Bij een wissel schuift de volgende omhoog en
// vervaagt de huidige.
let lastSectionIndex = -2;
let lastSectionSong = null;

function renderSections()
{
    const box = document.getElementById("sections");
    const sections = (songData && songData.sections) || [];

    if (!songData || sections.length === 0)
    {
        box.hidden = true;
        lastSectionIndex = -2;
        return;
    }

    box.hidden = false;

    const position = Number(songData.position || 0);
    let current = -1;

    for (let i = 0; i < sections.length; ++i)
    {
        if (Number(sections[i].time) <= position)
            current = i;
        else
            break;
    }

    const currentSlot = box.querySelector(".section-slot.current");
    const nextSlot = box.querySelector(".section-slot.next");
    const currentName = document.getElementById("sectionCurrent");
    const nextName = document.getElementById("sectionNext");
    const next = current + 1 < sections.length ? sections[current + 1] : null;
    const fill = document.getElementById("sectionRemaining");

    if (current !== lastSectionIndex || songData.songRev !== lastSectionSong)
    {
        const forward = songData.songRev === lastSectionSong && current === lastSectionIndex + 1
                        && lastSectionIndex >= 0 && currentName.textContent !== "";

        lastSectionIndex = current;
        lastSectionSong = songData.songRev;

        if (forward)
        {
            // De oude huidige vervaagt (een kopie die er nog even blijft staan, iets omhoog).
            const ghost = currentSlot.cloneNode(true);
            ghost.querySelectorAll("[id]").forEach(function (element) { element.removeAttribute("id"); });
            ghost.style.position = "absolute";
            ghost.style.top = "0";
            ghost.style.right = "0";
            box.appendChild(ghost);

            ghost.animate([{ opacity: 1, transform: "translateY(0)" }, { opacity: 0, transform: "translateY(-16px)" }],
                          { duration: 320, easing: "ease-in", fill: "forwards" }).onfinish = function () { ghost.remove(); };

            currentName.textContent = sections[current].name;
            nextName.textContent = next ? next.name : "";

            // De volgende schuift omhoog naar de plek van de huidige (en wordt groter en helder).
            currentSlot.animate([{ opacity: 0.45, transform: "translateY(40px) scale(0.7)" }, { opacity: 1, transform: "none" }],
                                { duration: 420, easing: "cubic-bezier(.2,.8,.2,1)" });

            // De nieuwe volgende verschijnt eronder.
            nextSlot.animate([{ opacity: 0 }, { opacity: 1 }], { duration: 400, delay: 150, easing: "ease-out", fill: "backwards" });
        }
        else
        {
            currentName.textContent = current >= 0 ? sections[current].name : "";
            nextName.textContent = next ? next.name : "";
            box.animate([{ opacity: 0.2 }, { opacity: 1 }], { duration: 220 });
        }

        // Het streepje begint weer vol.
        fill.style.transition = "none";
        fill.style.width = "100%";
        void fill.offsetWidth;
        fill.style.transition = "";
    }

    currentSlot.style.visibility = current >= 0 ? "visible" : "hidden";
    nextSlot.style.display = next ? "" : "none";

    // Streepje onder het huidige onderdeel: loopt af tot het volgende begint (of het nummer eindigt).
    if (current >= 0)
    {
        const start = Number(sections[current].time);
        const end = next ? Number(next.time) : Math.max(start + 1, Number(songData.duration) || start + 1);
        const remaining = Math.max(0, Math.min(1, (end - position) / Math.max(0.001, end - start)));

        fill.style.width = (remaining * 100) + "%";
    }
}

const lyricsFontSizeSlider =
    document.getElementById("lyricsFontSize");

function setLyricsFontSize(size) {
    const fontSize = Math.max(
        24,
        Math.min(90, Number(size) || 64)
    );

    const lyrics = document.getElementById("lyrics");

    lyrics.style.fontSize = fontSize + "px";
}

lyricsFontSizeSlider.addEventListener("input", function () {
    setLyricsFontSize(this.value);
});

setLyricsFontSize(lyricsFontSizeSlider.value);




let countdownActive = false;
let countdownTargetTime = null;

let lastCountdownSongTitle = "";
let lastCountdownPosition = 0;
let lastCountdownDuration = 0;

// Echte tekst: bevat minstens een letter of cijfer. Streepjes, puntjes (vaak vier per maat om een
// pauze aan te geven), "/" en "\\" (regelwissels) en andere tekens tellen niet mee voor het aftellen.
let sungTextPattern = null;

try { sungTextPattern = new RegExp("[\\p{L}\\p{N}]", "u"); }
catch (error) { sungTextPattern = /[A-Za-z0-9\u00C0-\u024F]/; }

function isSungLyric(lyric)
{
    return sungTextPattern.test(String(lyric.text || ""));
}

function renderCountdown()
{
    if (!songData)
        return;

    const pill =
        document.getElementById(
            "lyricCountdown");

    const lyrics =
        songData.lyrics || [];

    const position =
        Number(songData.position || 0);

    const duration =
        Number(songData.duration || 0);

    /*
     * Detecteer een nieuw nummer.
     *
     * Normaal verandert de songtitel.
     * Daarnaast controleren we op een duidelijke
     * terugval van de positie, zodat ook twee nummers
     * met dezelfde titel goed worden herkend.
     */
    const songChanged =
        songData.song !== lastCountdownSongTitle;

    const restarted =
        position < lastCountdownPosition - 2;

    /*
     * Een verandering van de totale duur is een extra
     * beveiliging voor twee nummers met dezelfde titel
     * waarbij de positie toevallig nog vrijwel gelijk is.
     */
    const durationChanged =
        lastCountdownDuration > 0 &&
        duration > 0 &&
        Math.abs(duration - lastCountdownDuration) > 1;

    if (songChanged ||
        restarted ||
        durationChanged)
    {
        countdownActive = false;
        countdownTargetTime = null;

        lastCountdownSongTitle =
            songData.song || "";
    }

    lastCountdownPosition = position;
    lastCountdownDuration = duration;

    /*
     * Zoek eerstvolgende lyric.
     */
    let nextLyric = null;

    for (const lyric of lyrics)
    {
        if (Number(lyric.time) > position && isSungLyric(lyric))
        {
            nextLyric = lyric;
            break;
        }
    }

    /*
     * Geen volgende lyric.
     */
    if (!nextLyric)
    {
        countdownActive = false;
        countdownTargetTime = null;
        pill.style.opacity = "0";
        return;
    }

    const remaining =
        Number(nextLyric.time) - position;

    /*
     * Timer starten.
     *
     * Belangrijk: de targettijd wordt altijd gekoppeld
     * aan de eerstvolgende lyric van het huidige nummer.
     */
    if (!countdownActive &&
        remaining > 6)
    {
        countdownActive = true;

        countdownTargetTime =
            Number(nextLyric.time);
    }

    /*
     * Geen actieve timer.
     */
    if (!countdownActive)
    {
        pill.style.opacity = "0";
        return;
    }

    /*
     * Timer loopt.
     */
    const countdownRemaining =
        countdownTargetTime -
        position;

    /*
     * Veiligheidscontrole:
     * als de target niet meer overeenkomt met een
     * toekomstige lyric, opnieuw bepalen.
     */
    if (countdownRemaining <= 0)
    {
        countdownActive = false;
        countdownTargetTime = null;
        pill.style.opacity = "0";
        return;
    }

    /*
     * Fade laatste seconde.
     */
    if (countdownRemaining < 1)
    {
        pill.style.opacity =
            countdownRemaining;
    }
    else
    {
        pill.style.opacity = "1";
    }

    pill.textContent =
        Math.ceil(
            countdownRemaining)
        + " sec";
}


// Bediening (transport + mixer) staat standaard uit. Open de pagina als
// http://host:poort/?key=SLEUTEL om die aan te zetten; de server controleert
// de sleutel bij elk commando.
const controlKey = new URLSearchParams(location.search).get("key") || "";
const keyQuery = controlKey ? "key=" + encodeURIComponent(controlKey) : "";

// ---- Maatindicator: tel en fase uit tempo en maatsoort, geanimeerd met
// requestAnimationFrame (de positie komt maar 10x per seconde binnen en
// wordt daartussen doorgetrokken zolang er gespeeld wordt).
let beatSong = null;
let beatSync = { position: 0, time: 0, playing: false };
let beatSignature = "";
let beatValues = [];

function computeBeat(seconds)
{
    const ppq = beatSong.ppq || 480;
    const tempo = beatSong.tempo || [];
    const sigs = beatSong.sigs || [];

    let tick = seconds * 2 * ppq;      // 120 bpm zonder tempokaart

    if (tempo.length)
    {
        let i = 0;
        while (i + 1 < tempo.length && tempo[i + 1][1] <= seconds)
            ++i;

        tick = tempo[i][0] + (seconds - tempo[i][1]) * tempo[i][2] * ppq / 60;
    }

    let active = sigs[0];
    for (const sig of sigs)
        if (sig[0] <= tick)
            active = sig;

    const numerator = active[1];
    const denominator = active[2];
    const beatTicks = ppq * 4 / denominator;
    const beats = (tick - active[0]) / beatTicks;

    if (beats < 0)
        return { numerator, denominator, index: -1, intensity: 0 };

    const whole = Math.floor(beats);
    const phase = beats - whole;

    return {
        numerator,
        denominator,
        index: whole % numerator,
        intensity: 1 - 0.65 * Math.pow(phase, 0.7)
    };
}

function beatFrame()
{
    requestAnimationFrame(beatFrame);

    const box = document.getElementById("beatBox");

    if (!beatSong || !beatSong.sigs || !beatSong.sigs.length)
    {
        box.hidden = true;
        return;
    }

    box.hidden = false;

    const elapsed = beatSync.playing ? (performance.now() - beatSync.time) / 1000 : 0;
    const beat = computeBeat(Math.max(0, beatSync.position + elapsed));

    const count = Math.min(12, beat.numerator);
    const signature = beat.numerator + "/" + beat.denominator;

    if (signature !== beatSignature)
    {
        beatSignature = signature;
        document.getElementById("beatSig").textContent = signature;

        // Lange maatsoorten (veel tellen) krijgen een kleiner blokje.
        box.classList.toggle("medium", count > 6 && count <= 9);
        box.classList.toggle("small", count > 9);

        const leds = document.getElementById("beatLeds");
        leds.textContent = "";
        beatValues = [];

        for (let i = 0; i < count; ++i)
        {
            const led = document.createElement("span");
            led.className = i === 0 ? "led first" : "led";
            leds.appendChild(led);
            beatValues.push(-1);
        }
    }

    const leds = document.getElementById("beatLeds").children;

    for (let i = 0; i < leds.length; ++i)
    {
        const intensity = beatSync.playing && beat.index === i ? beat.intensity : 0;
        const rounded = Math.round(intensity * 20) / 20;

        if (rounded !== beatValues[i])
        {
            beatValues[i] = rounded;
            leds[i].style.setProperty("--i", String(rounded));
        }
    }
}

requestAnimationFrame(beatFrame);

let stateInFlight = false;
let songBase = null;
let lastRevision = -1;

// Haalt iedere 100 ms alleen de kleine toestand op. De zware songdata
// (lyrics + akkoorden) komt pas binnen als die echt verandert.
// Geen verbinding met Merik: een melding over de pagina, en rustiger opnieuw proberen.
let failedPolls = 0;
let appOffline = false;
let offlineSince = 0;
let lastAttempt = 0;

function setOffline(offline) {
    if (offline === appOffline)
        return;

    appOffline = offline;
    document.getElementById("offline").hidden = !offline;
}

async function update() {
    if (stateInFlight)
        return;

    // Offline: hooguit een keer per seconde proberen.
    if (appOffline && performance.now() - lastAttempt < 1000)
        return;

    lastAttempt = performance.now();
    stateInFlight = true;

    try {
        const stateResponse = await fetch("/api/state" + (keyQuery ? "?" + keyQuery : ""), { cache: "no-store" });
        if (!stateResponse.ok) { failedPolls = failedPolls + 1; if (failedPolls >= 2) setOffline(true); return; }
        const state = await stateResponse.json();
        failedPolls = 0;
        setOffline(false);

        if (!songBase || state.rev !== lastRevision) {
            const songResponse = await fetch("/api/song", { cache: "no-store" });
            if (!songResponse.ok) return;
            songBase = await songResponse.json();
            lastRevision = songBase.rev;
        }

        document.body.classList.toggle("control", state.control === true);

        beatSong = songBase;
        beatSync = { position: state.position, time: performance.now(), playing: state.playing === true };

        songData = Object.assign({}, songBase, { position: state.position });

        document.getElementById("song").textContent = songData.song || "Merik";

        const keyText = state.transpose
            ? '<span class="key">Key ' + (state.transpose > 0 ? "+" : "") + state.transpose + "</span>"
            : "";
        document.getElementById("time").innerHTML =
            formatTime(songData.position) + " / " + formatTime(songData.duration) + keyText;

        // Resterende tijd van de setlist, links uitgelijnd onder "Next".
        document.getElementById("setlistRemaining").textContent = state.remaining !== undefined
            ? (state.remainingApprox ? "~" : "") + formatLeft(state.remaining) + " left"
            : "";

        document.getElementById("nextSong").textContent =
            state.next ? "Next: " + state.next : "";

        const playButton = document.getElementById("btnPlay");
        const playingFlag = state.playing ? "1" : "0";
        if (playButton.dataset.playing !== playingFlag) {
            playButton.dataset.playing = playingFlag;
            playButton.setAttribute("aria-label", state.playing ? "Pauze" : "Play");
        }

        const duration = Number(songData.duration) || 0;
        const position = Number(songData.position) || 0;
        const progress = duration > 0 ? Math.max(0, Math.min(100, position / duration * 100)) : 0;
        document.getElementById("songProgressFill").style.width = progress + "%";

        if (window.applyFamilies)
            window.applyFamilies(state.families);

        window.resumeLabel = state.resume || "";
        window.appPlaying = state.playing === true;

        if (window.updateSearchResume)
            window.updateSearchResume();

        renderLyrics();
        renderChords();
        renderCountdown();
        renderSections();
    } catch (error) {
        console.log(error);
        failedPolls = failedPolls + 1;

        if (failedPolls >= 2)
            setOffline(true);
    }
    finally { stateInFlight = false; }
}

// Transportknoppen
async function sendControl(command) {
    try {
        await fetch("/api/control?cmd=" + command + (keyQuery ? "&" + keyQuery : ""),
                    { method: "POST", cache: "no-store" });
        update();
    } catch (error) { console.log(error); }
}

document.getElementById("btnPlay").addEventListener("click", () => sendControl("play"));
document.getElementById("btnStop").addEventListener("click", () => sendControl("stop"));
document.getElementById("btnNext").addEventListener("click", () => sendControl("next"));


// Zoeken in de setlists (alleen met de bedieningssleutel).
(function initSearch() {
    const overlay = document.getElementById("searchOverlay");
    const input = document.getElementById("searchInput");
    const results = document.getElementById("searchResults");
    const status = document.getElementById("searchStatus");
    const resume = document.getElementById("searchResume");
    const scopeAll = document.getElementById("scopeAll");
    const scopeCurrent = document.getElementById("scopeCurrent");

    let scope = "all";
    let timer = 0;
    let sequence = 0;
    let lastHits = [];

    function setScope(value) {
        scope = value;
        scopeAll.classList.toggle("on", value === "all");
        scopeCurrent.classList.toggle("on", value === "current");
        run();
    }

    function updateResume() {
        const label = window.resumeLabel || "";
        resume.hidden = label === "";
        resume.textContent = label ? "Continue: " + label : "";
    }

    window.updateSearchResume = function () { if (!overlay.hidden) updateResume(); };

    function open() {
        overlay.hidden = false;
        input.value = "";
        results.textContent = "";
        lastHits = [];
        status.textContent = "Type at least 3 letters";
        updateResume();
        setTimeout(function () { input.focus(); }, 50);
    }

    function close() { overlay.hidden = true; input.blur(); }

    async function act(command, hit) {
        try {
            await fetch("/api/control?cmd=" + command + ":" + hit.setlist + ":" + hit.row + (keyQuery ? "&" + keyQuery : ""),
                        { method: "POST", cache: "no-store" });
        } catch (error) { console.log(error); }
        close();
        update();
    }

    async function run() {
        const query = input.value.trim();

        if (query.length < 3) {
            results.textContent = "";
            lastHits = [];
            status.textContent = "Type at least 3 letters";
            return;
        }

        const mine = ++sequence;

        try {
            const response = await fetch("/api/search?q=" + encodeURIComponent(query) + "&scope=" + scope
                                         + (keyQuery ? "&" + keyQuery : ""), { cache: "no-store" });
            const data = await response.json();

            if (mine !== sequence || !data.ok)
                return;

            results.textContent = "";
            status.textContent = data.results.length === 0 ? "No songs found"
                : data.results.length + (data.results.length === 1 ? " song" : " songs");

            lastHits = data.results;

            // Elke regel: de titel met daarin twee knoppen, [ nummer [Play now] [Play next] ].
            data.results.forEach(function (hit) {
                const row = document.createElement("div");
                row.className = "resultRow";

                const text = document.createElement("div");
                text.className = "resultText";
                text.textContent = hit.title;
                const small = document.createElement("small");
                small.textContent = hit.setlistName + "  \u00b7  #" + (hit.row + 1);
                text.appendChild(small);
                row.appendChild(text);

                const actions = document.createElement("div");
                actions.className = "resultActions";

                const now = document.createElement("button");
                now.type = "button";
                now.className = "now";
                now.textContent = "Play now";
                now.addEventListener("click", function () { act("playnow", hit); });
                actions.appendChild(now);

                const next = document.createElement("button");
                next.type = "button";
                next.className = "next";
                next.textContent = "Play next";
                next.addEventListener("click", function () { act("queue", hit); });
                actions.appendChild(next);

                row.appendChild(actions);
                results.appendChild(row);
            });
        } catch (error) { console.log(error); }
    }

    input.addEventListener("input", function () {
        clearTimeout(timer);
        timer = setTimeout(run, 150);
    });

    input.addEventListener("keydown", function (event) {
        if (event.key === "Escape")
            close();
        else if (event.key === "Enter") {
            // Enter: erachter als er een nummer klinkt, anders meteen.
            if (lastHits.length > 0)
                act(window.appPlaying ? "queue" : "playnow", lastHits[0]);
        }
    });

    scopeAll.addEventListener("click", function () { setScope("all"); });
    scopeCurrent.addEventListener("click", function () { setScope("current"); });
    document.getElementById("searchClose").addEventListener("click", close);
    document.getElementById("btnSearch").addEventListener("click", open);

    // Een klik op de lege ruimte (naast de regels, of ertussen) sluit ook.
    overlay.addEventListener("click", function (event) {
        if (event.target === overlay || event.target.classList.contains("searchPanel") || event.target === results)
            close();
    });

    resume.addEventListener("click", async function () {
        try {
            await fetch("/api/control?cmd=resume" + (keyQuery ? "&" + keyQuery : ""), { method: "POST", cache: "no-store" });
        } catch (error) { console.log(error); }
        close();
        update();
    });
})();

// Setlist-weergave (alleen met de bedieningssleutel): de lijst waarin nu gespeeld wordt; per nummer
// "Jump now" (direct) of "Jump next" (na het huidige nummer), waarna de setlist daar verder gaat.
(function initSetlistView() {
    const overlay = document.getElementById("setlistOverlay");
    const rows = document.getElementById("setlistRows");
    const info = document.getElementById("setlistInfo");
    const title = document.getElementById("setlistTitle");

    function close() { overlay.hidden = true; }

    async function act(command, setlist, row) {
        try {
            await fetch("/api/control?cmd=" + command + ":" + setlist + ":" + row + (keyQuery ? "&" + keyQuery : ""),
                        { method: "POST", cache: "no-store" });
        } catch (error) { console.log(error); }
        close();
        update();
    }

    async function open() {
        overlay.hidden = false;
        rows.textContent = "";
        info.textContent = "Loading...";

        try {
            const response = await fetch("/api/setlist" + (keyQuery ? "?" + keyQuery : ""), { cache: "no-store" });
            const data = await response.json();

            if (!data.ok) {
                info.textContent = "No setlist";
                return;
            }

            title.textContent = data.name;
            info.textContent = data.remaining !== undefined
                ? (data.remainingApprox ? "~" : "") + formatLeft(data.remaining) + " left"
                : "";

            let currentRow = null;

            // Tijd tot het einde van de setlist per nummer, dit nummer meegerekend.
            const left = new Array(data.rows.length).fill(0);
            const unknown = new Array(data.rows.length).fill(false);

            for (let i = data.rows.length - 1; i >= 0; --i) {
                const seconds = data.rows[i].seconds;
                left[i] = (i + 1 < data.rows.length ? left[i + 1] : 0) + (seconds >= 0 ? seconds : 0);
                unknown[i] = (i + 1 < data.rows.length ? unknown[i + 1] : false) || seconds < 0;
            }

            data.rows.forEach(function (item, index) {
                const row = document.createElement("div");
                row.className = "resultRow" + (index === data.current ? " current" : "");

                const text = document.createElement("div");
                text.className = "resultText";
                text.textContent = item.title;
                const small = document.createElement("small");
                small.textContent = "#" + (index + 1)
                    + (item.seconds >= 0 ? "  \u00b7  " + formatLeft(item.seconds) : "")
                    + "  \u00b7  " + (unknown[index] ? "~" : "") + formatLeft(left[index]);
                text.appendChild(small);
                row.appendChild(text);

                if (index === data.current) {
                    const badge = document.createElement("div");
                    badge.className = "badge";
                    badge.textContent = "PLAYING";
                    row.appendChild(badge);
                    currentRow = row;
                } else {
                    const actions = document.createElement("div");
                    actions.className = "resultActions";

                    const now = document.createElement("button");
                    now.type = "button";
                    now.className = "now";
                    now.textContent = "Jump now";
                    now.addEventListener("click", function () { act("jump", data.setlist, index); });
                    actions.appendChild(now);

                    const next = document.createElement("button");
                    next.type = "button";
                    next.className = "next";
                    next.textContent = "Jump next";
                    next.addEventListener("click", function () { act("jumpnext", data.setlist, index); });
                    actions.appendChild(next);

                    row.appendChild(actions);
                }

                rows.appendChild(row);
            });

            if (currentRow)
                currentRow.scrollIntoView({ block: "center" });
        } catch (error) { console.log(error); info.textContent = "Could not load the setlist"; }
    }

    document.getElementById("btnSetlist").addEventListener("click", open);
    document.getElementById("setlistClose").addEventListener("click", close);

    // Een klik op de lege ruimte (naast de regels, of ertussen) sluit ook.
    overlay.addEventListener("click", function (event) {
        if (event.target === overlay || event.target.classList.contains("searchPanel") || event.target === rows)
            close();
    });
})();

// Family mixer gekoppeld aan de FluidSynth-engine.
(function initFamilyMixer() {
    const toggle = document.getElementById("familyToggle");
    const panel = document.getElementById("familyPanel");
    const close = document.getElementById("familyClose");
    const controls = document.getElementById("familyControls");

    const families = [
        "1 Drums",
        "2 Bass",
        "3 Guitars",
        "4 Keys",
        "5 Strings",
        "6 Winds",
        "7 Others",
        "8 Click"
    ];

    // Ontvangt de families uit /api/state (geen aparte poll meer).
    // Een slider die de gebruiker net zelf bewoog wordt niet
    // overschreven door een iets oudere serverwaarde.
    function applyFamilies(list) {
        if (!Array.isArray(list))
            return;

        const now = Date.now();

        list.forEach(function (family, index) {
            const slider = document.getElementById("familySlider" + index);
            const control = slider?.closest(".family-control");
            const enabled = control?.querySelector(".family-enable");
            const value = control?.querySelector(".family-value");

            if (!slider || !enabled || !value)
                return;

            if (now - (control._localChange || 0) < 1000)
                return;

            const volume = String(family.volume);

            if (slider.value !== volume)
                slider.value = volume;

            if (value.textContent !== volume)
                value.textContent = volume;

            if (enabled.checked !== family.enabled)
                enabled.checked = family.enabled;

            slider.disabled = !family.enabled;
            control.classList.toggle("disabled", !family.enabled);
        });

        list.forEach(function (family, index) {
            const control = document.getElementById("familySlider" + index)?.closest(".family-control");

            if (!control)
                return;

            const nameEl = control.querySelector(".family-name");
            const shownName = family.name ? (index + 1) + " " + family.name : families[index];

            if (nameEl && nameEl.textContent !== shownName)
                nameEl.textContent = shownName;

            const source = control.querySelector(".family-source");
            const fill = control.querySelector(".family-meter-fill");
            const label = family.source === "audio" ? "AUDIO" : (family.source === "click" ? "CLICK" : "MIDI");

            if (source && source.textContent !== label) {
                source.textContent = label;
                source.classList.toggle("audio", family.source === "audio");
                source.classList.toggle("midi", family.source === "midi");
            }

            if (fill)
                fill.style.clipPath = "inset(0 " + (100 - (family.level || 0)) + "% 0 0)";
        });
    }

    window.applyFamilies = applyFamilies;

    function setOpen(open) {
        document.body.classList.toggle("family-panel-open", open);
        toggle.setAttribute("aria-expanded", String(open));
        toggle.setAttribute(
            "aria-label",
            open ? "Family mixer sluiten" : "Family mixer openen"
        );
        toggle.textContent = open ? "›" : "‹";
        panel.setAttribute("aria-hidden", String(!open));
    }

    toggle.addEventListener("click", function () {
        setOpen(!document.body.classList.contains("family-panel-open"));
    });

    close.addEventListener("click", function () {
        setOpen(false);
    });

    families.forEach(function (family, index) {
        const row = document.createElement("div");
        row.className = "family-control";

        const header = document.createElement("div");
        header.className = "family-control-header";

        const name = document.createElement("label");
        name.className = "family-name";
        name.textContent = family;
        name.htmlFor = "familySlider" + index;

        const enabled = document.createElement("input");
        enabled.type = "checkbox";
        enabled.className = "family-enable";
        enabled.checked = true;
        enabled.setAttribute("aria-label", family + " inschakelen");

        const sliderRow = document.createElement("div");
        sliderRow.className = "family-slider-row";

        const slider = document.createElement("input");
        slider.type = "range";
        slider.id = "familySlider" + index;
        slider.className = "family-slider";
        slider.min = "0";
        slider.max = "127";
        slider.step = "1";
        slider.value = "127";
        slider.setAttribute("aria-label", family + " volume");

        const value = document.createElement("span");
        value.className = "family-value";
        value.textContent = slider.value;

        let sendTimer = null;

        async function sendFamilyVolume() {
            const parameters = new URLSearchParams({
                family: String(index),
                volume: slider.value,
                enabled: enabled.checked ? "1" : "0",
                key: controlKey
            });

            try {
                const response = await fetch(
                    "/api/family?" + parameters.toString(),
                    { cache: "no-store" }
                );

                if (!response.ok) {
                    console.error("Family-volume aanpassen mislukt:", family);
                } else {
                    const result = await response.json();

                    if (!result.ok) {
                        console.error(
                            "Family-volume niet toegepast:",
                            family,
                            result.error || ""
                        );
                    }
                }
            } catch (error) {
                console.error("Family-mixer niet bereikbaar:", error);
            }
        }

        function scheduleUpdate(immediate) {
            if (sendTimer !== null) {
                clearTimeout(sendTimer);
                sendTimer = null;
            }

            if (immediate) {
                sendFamilyVolume();
            } else {
                sendTimer = setTimeout(function () {
                    sendTimer = null;
                    sendFamilyVolume();
                }, 50);
            }
        }

        slider.addEventListener("input", function () {
            row._localChange = Date.now();
            value.textContent = slider.value;
            scheduleUpdate(false);
        });

        enabled.addEventListener("change", function () {
            row._localChange = Date.now();
            row.classList.toggle("disabled", !enabled.checked);
            scheduleUpdate(true);
        });

        const source = document.createElement("span");
        source.className = index === 7 ? "family-source" : "family-source midi";
        source.textContent = index === 7 ? "CLICK" : "MIDI";

        const meter = document.createElement("div");
        meter.className = "family-meter";
        const meterFill = document.createElement("div");
        meterFill.className = "family-meter-fill";
        meter.appendChild(meterFill);

        header.append(name, source, enabled);
        sliderRow.append(slider, value);
        row.append(header, sliderRow, meter);
        controls.appendChild(row);
    });

    setOpen(false);
})();

update();
setInterval(update,100);
</script>
</body>
</html>)HTML";
}

std::string WebServer::escapeHtml(const std::string& text) const
{
    std::string result;
    for (const char character : text)
    {
        switch (character)
        {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "'"; break;
            default: result += character; break;
        }
    }
    return result;
}

std::string WebServer::escapeJson(const std::string& text) const
{
    std::string result;
    for (const unsigned char character : text)
    {
        switch (character)
        {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20)
                {
                    char buffer[7] {};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
                    result += buffer;
                }
                else
                    result += static_cast<char>(character);
                break;
        }
    }
    return result;
}

std::string WebServer::currentSongTitle() const
{
    std::scoped_lock lock(mutex_);
    if (!song_) return {};
    const std::string& path = song_->sourceFile;
    const auto slash = path.find_last_of("/\\");
    const auto start = slash == std::string::npos ? 0 : slash + 1;
    const auto dot = path.find_last_of('.');
    const auto end = dot != std::string::npos && dot > start ? dot : path.length();
    return path.substr(start, end - start);
}