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

    {
        std::scoped_lock lock(mutex_);
        volumes = familyVolumes_;
        enabled = familyEnabled_;
    }

    std::string json = "{\"families\":[";

    for (int i = 0; i < familyCount; ++i)
    {
        if (i > 0)
            json += ",";

        json += "{\"volume\":" + std::to_string(volumes[i]);
        json += ",\"enabled\":";
        json += enabled[i] ? "true" : "false";
        json += "}";
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

        if (callback && (command == "play" || command == "stop" || command == "next"))
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

    {
        std::scoped_lock lock(mutex_);
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

    json += "\"control\":" + std::string(control ? "true" : "false");

    // Families alleen voor bedienende clients (scheelt ook een aparte poll).
    if (control)
    {
        const auto families = createFamilyJson();
        json += "," + families.substr(1, families.size() - 2);   // "families":[...]
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
        for (std::size_t i = 0; i < song->timeSignatures.size(); ++i)
        {
            if (i != 0) json += ",";
            const auto& point = song->timeSignatures[i];
            json += "[" + std::to_string(point.tick) + "," + std::to_string(point.numerator) + ","
                  + std::to_string(point.denominator) + "]";
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

    top: 20px;
    right: 25px;
    width: 300px;
    text-align: center;
    z-index: 100;
    padding: 18px 18px;
    border-radius: 999px;
    background: rgb(255, 217, 0);
    xxborder: 2px solid #ffd800;
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
/* Maatsoort met ledjes in de maat; schuift onder de countdown-pil als die zichtbaar is */
#beatBox {
    position:fixed; top:22px; right:28px; z-index:90; display:flex; align-items:center; gap:12px;
    padding:7px 14px; border-radius:999px; background:rgba(255,255,255,.06);
    border:1px solid rgba(255,255,255,.10); transition:top 450ms ease; pointer-events:none;
}
body.countdown #beatBox { top:134px; }
#beatSig { font-size:20px; font-weight:600; color:#9ea4b0; font-variant-numeric:tabular-nums; }
#beatLeds { display:flex; gap:9px; }
.led {
    --c:24,118,255; --i:0; width:16px; height:16px; border-radius:50%;
    background:rgba(var(--c), calc(.06 + var(--i) * .94));
    border:1px solid rgba(var(--c), .35);
    box-shadow:0 0 calc(var(--i) * 16px) rgba(var(--c), calc(var(--i) * .85));
}
.led.first { --c:255,176,32; }
@media (max-width:600px) {
    #beatBox { top:12px; right:12px; padding:5px 10px; gap:8px; }
    body.countdown #beatBox { top:108px; }
    #beatSig { font-size:15px; }
    #beatLeds { gap:6px; }
    .led { width:12px; height:12px; }
}

#time .key { margin-left:14px; color:#6aa5ff; }
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
@media (max-width: 700px) { #familyPanel { width: 45%; min-width: 200px; } body.family-panel-open #lyricsViewport { width: 55%; margin-right: 45%; } }
@media (prefers-reduced-motion: reduce) { #familyPanel, #lyricsViewport { transition: none; } }
</style>
</head>

<!-- ################################################################### -->
<!-- ########################## HTML   ################################# -->
<!-- ################################################################### -->
<body>
<header><div id="song">Merik</div><div id="time">00:00 / 00:00</div><div id="nextSong"></div></header>
<div id="songProgress"><div id="songProgressFill"></div></div>
<div id="lyricCountdown"></div>
<div id="beatBox" hidden><span id="beatSig">4/4</span><span id="beatLeds"></span></div>
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
        progressContainer.style.width =
            "10%";

        progressElement.style.width =
            "0%";

        return;
    }

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
        if (Number(lyric.time) > position)
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

    // Schuif onder de countdown-pil zolang die zichtbaar is.
    const pill = document.getElementById("lyricCountdown");
    document.body.classList.toggle("countdown", Number(pill.style.opacity) > 0.02);

    const elapsed = beatSync.playing ? (performance.now() - beatSync.time) / 1000 : 0;
    const beat = computeBeat(Math.max(0, beatSync.position + elapsed));

    const count = Math.min(12, beat.numerator);
    const signature = beat.numerator + "/" + beat.denominator;

    if (signature !== beatSignature)
    {
        beatSignature = signature;
        document.getElementById("beatSig").textContent = signature;

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
async function update() {
    if (stateInFlight)
        return;

    stateInFlight = true;

    try {
        const stateResponse = await fetch("/api/state" + (keyQuery ? "?" + keyQuery : ""), { cache: "no-store" });
        if (!stateResponse.ok) return;
        const state = await stateResponse.json();

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

        renderLyrics();
        renderChords();
        renderCountdown();
    } catch (error) { console.log(error); }
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

        header.append(name, enabled);
        sliderRow.append(slider, value);
        row.append(header, sliderRow);
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