#include "WebServer.h"

//#include <juce_core/juce_core.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
constexpr int backlog = 8;

std::string sendResponse(
    int socket,
    const std::string& body,
    const char* contentType)
{
    const std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: " + std::string(contentType) + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n"
        "\r\n";

    const std::string response =
        header + body;

    const char* data = response.data();
    std::size_t remaining = response.size();

    while (remaining > 0)
    {
        const auto sent =
            ::send(
                socket,
                data,
                remaining,
                0);

        if (sent <= 0)
            break;

        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }

    return response;
}
}

// ============================================================================
// Construction
// ============================================================================

WebServer::WebServer()
{
}

WebServer::~WebServer()
{
    stop();
}

// ============================================================================
// Start / stop
// ============================================================================

bool WebServer::start(int port)
{
    if (running)
        return true;

    port_ = port;

    serverSocket_ =
        ::socket(
            AF_INET,
            SOCK_STREAM,
            0);

    if (serverSocket_ < 0)
        return false;

    int reuseAddress = 1;

    ::setsockopt(
        serverSocket_,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuseAddress,
        sizeof(reuseAddress));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(
        static_cast<std::uint16_t>(port_));

    if (::bind(
            serverSocket_,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0)
    {
        ::close(serverSocket_);
        serverSocket_ = -1;
        return false;
    }

    if (::listen(
            serverSocket_,
            backlog) < 0)
    {
        ::close(serverSocket_);
        serverSocket_ = -1;
        return false;
    }

    running = true;

    thread_ =
        std::thread(
            &WebServer::serverThread,
            this);

    return true;
}

void WebServer::stop()
{
    if (!running.exchange(false))
        return;

    if (serverSocket_ >= 0)
    {
        ::shutdown(
            serverSocket_,
            SHUT_RDWR);

        ::close(serverSocket_);

        serverSocket_ = -1;
    }

    if (thread_.joinable())
        thread_.join();
}

bool WebServer::isRunning() const
{
    return running.load();
}

// ============================================================================
// Data
// ============================================================================

void WebServer::setSong(
    std::shared_ptr<const Song> song)
{
    std::scoped_lock lock(mutex_);

    song_ = std::move(song);
    positionSamples_ = 0;
}

void WebServer::setPositionSamples(
    std::int64_t positionSamples)
{
    std::scoped_lock lock(mutex_);

    positionSamples_ =
        std::max<std::int64_t>(
            0,
            positionSamples);
}

// ============================================================================
// Server thread
// ============================================================================

void WebServer::serverThread()
{
    while (running)
    {
        sockaddr_in clientAddress {};
        socklen_t clientLength =
            sizeof(clientAddress);

        const int clientSocket =
            ::accept(
                serverSocket_,
                reinterpret_cast<sockaddr*>(&clientAddress),
                &clientLength);

        if (clientSocket < 0)
        {
            if (running)
                continue;

            break;
        }

        handleClient(clientSocket);

        ::shutdown(
            clientSocket,
            SHUT_RDWR);

        ::close(clientSocket);
    }
}

// ============================================================================
// HTTP client
// ============================================================================

void WebServer::handleClient(int socket)
{
    char buffer[4096] {};

    const auto received =
        ::recv(
            socket,
            buffer,
            sizeof(buffer) - 1,
            0);

    if (received <= 0)
        return;

    buffer[received] = '\0';

    const std::string request(buffer);

    if (request.rfind(
            "GET /api/song",
            0) == 0)
    {
        sendResponse(
            socket,
            createJson(),
            "application/json; charset=utf-8");

        return;
    }

    sendResponse(
        socket,
        createHtml(),
        "text/html; charset=utf-8");
}

// ============================================================================
// JSON
// ============================================================================

std::string WebServer::createJson() const
{
    std::shared_ptr<const Song> song;
    std::int64_t positionSamples = 0;

    {
        std::scoped_lock lock(mutex_);

        song = song_;
        positionSamples = positionSamples_;
    }

    if (!song)
    {
        return R"({"song":"","position":0,"lyrics":[],"chords":[]})";
    }

    const double sampleRate =
        song->sampleRate > 0.0
            ? song->sampleRate
            : 48000.0;

    const double positionSeconds =
        static_cast<double>(positionSamples)
        / sampleRate;

    std::string json;

    json += "{";
    json += "\"song\":\"";
    std::string title;

    const auto& path = song->sourceFile;

    const auto slash = path.find_last_of("/\\");
    const auto start =
        slash == std::string::npos
            ? 0
            : slash + 1;

    const auto dot = path.find_last_of('.');

    const auto end =
        (dot != std::string::npos && dot > start)
            ? dot
            : path.length();

    title = path.substr(start, end - start);

    json += escapeJson(title);
    json += "\",";

    json += "\"position\":";
    json += std::to_string(positionSeconds);
    json += ",";

    json += "\"lyrics\":[";

    for (std::size_t i = 0;
         i < song->lyrics.size();
         ++i)
    {
        if (i != 0)
            json += ",";

        const auto& lyric =
            song->lyrics[i];

        json += "{";
        json += "\"time\":";
        json += std::to_string(
            lyric.seconds);
        json += ",";

        json += "\"text\":\"";
        json += escapeJson(
            lyric.text);
        json += "\"";
        json += "}";
    }

    json += "],";

    // Chords are intentionally empty for now.
    // Song will receive a dedicated chord timeline later.
    json += "\"chords\":[]";

    json += "}";

    return json;
}

// ============================================================================
// HTML
// ============================================================================

std::string WebServer::createHtml() const
{
    return R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport"
      content="width=device-width,initial-scale=1">

<title>Merik</title>

<style>

:root {
    color-scheme: dark;
}

* {
    box-sizing: border-box;
}

body {
    margin: 0;
    background: #191919;
    color: #ffffff;
    font-family:
        -apple-system,
        BlinkMacSystemFont,
        "SF Pro Text",
        "Segoe UI",
        sans-serif;
}

header {
    padding: 24px 30px 16px;
    border-bottom: 1px solid #333;
}

#song {
    font-size: 28px;
    font-weight: 600;
}

#time {
    margin-top: 6px;
    color: #aaa;
    font-size: 15px;
}

main {
    max-width: 1000px;
    margin: 0 auto;
    padding: 30px;
}

#chord {
    min-height: 70px;
    color: #2e9afe;
    font-size: 34px;
    font-weight: 700;
}

#lyrics {
    margin-top: 20px;
    font-size: 30px;
    line-height: 1.55;
}

.lyric {
    color: #666;
    transition:
        color 150ms ease,
        transform 150ms ease;
}

.lyric.current {
    color: #ffffff;
    transform: scale(1.02);
}

@media (max-width: 600px) {

    header {
        padding: 18px;
    }

    main {
        padding: 20px;
    }

    #song {
        font-size: 22px;
    }

    #lyrics {
        font-size: 25px;
    }

    #chord {
        font-size: 30px;
    }
}

</style>
</head>

<body>

<header>
    <div id="song">Merik</div>
    <div id="time">00:00</div>
</header>

<main>

    <div id="chord"></div>

    <div id="lyrics"></div>

</main>

<script>

let songData = null;

function formatTime(seconds)
{
    seconds = Math.max(0, seconds || 0);

    const minutes =
        Math.floor(seconds / 60);

    const secs =
        Math.floor(seconds % 60);

    return String(minutes).padStart(2, "0")
        + ":"
        + String(secs).padStart(2, "0");
}

function renderLyrics()
{
    if (!songData)
        return;

    const lyrics =
        songData.lyrics || [];

    const position =
        songData.position || 0;

    let current = -1;

    for (let i = 0; i < lyrics.length; ++i)
    {
        if (lyrics[i].time <= position)
            current = i;
        else
            break;
    }

    const container =
        document.getElementById("lyrics");

    container.innerHTML = "";

    for (let i = 0; i < lyrics.length; ++i)
    {
        const div =
            document.createElement("div");

        div.className = "lyric";

        if (i === current)
            div.classList.add("current");

        div.textContent =
            lyrics[i].text;

        container.appendChild(div);
    }

    const currentChord =
        document.getElementById("chord");

    currentChord.textContent = "";
}

async function update()
{
    try
    {
        const response =
            await fetch(
                "/api/song",
                { cache: "no-store" });

        songData =
            await response.json();

        document.getElementById("song")
            .textContent =
                songData.song || "Merik";

        document.getElementById("time")
            .textContent =
                formatTime(songData.position);

        renderLyrics();
    }
    catch (error)
    {
        console.log(error);
    }
}

update();

setInterval(
    update,
    250);

</script>

</body>
</html>
)HTML";
}

// ============================================================================
// Helpers
// ============================================================================

std::string WebServer::escapeHtml(
    const std::string& text) const
{
    std::string result;

    for (const char character : text)
    {
        switch (character)
        {
            case '&':
                result += "&amp;";
                break;

            case '<':
                result += "&lt;";
                break;

            case '>':
                result += "&gt;";
                break;

            case '"':
                result += "&quot;";
                break;

            case '\'':
                result += "&#39;";
                break;

            default:
                result += character;
                break;
        }
    }

    return result;
}

std::string WebServer::escapeJson(
    const std::string& text) const
{
    std::string result;

    for (const unsigned char character : text)
    {
        switch (character)
        {
            case '\\':
                result += "\\\\";
                break;

            case '"':
                result += "\\\"";
                break;

            case '\b':
                result += "\\b";
                break;

            case '\f':
                result += "\\f";
                break;

            case '\n':
                result += "\\n";
                break;

            case '\r':
                result += "\\r";
                break;

            case '\t':
                result += "\\t";
                break;

            default:
                if (character < 0x20)
                {
                    char buffer[7] {};
                    std::snprintf(
                        buffer,
                        sizeof(buffer),
                        "\\u%04x",
                        character);

                    result += buffer;
                }
                else
                {
                    result +=
                        static_cast<char>(
                            character);
                }

                break;
        }
    }

    return result;
}

std::string WebServer::currentSongTitle() const
{
    std::scoped_lock lock(mutex_);

    if (!song_)
        return {};

    const std::string& path =
        song_->sourceFile;

    const auto slash =
        path.find_last_of("/\\");

    const auto start =
        slash == std::string::npos
            ? 0
            : slash + 1;

    const auto dot =
        path.find_last_of('.');

    const auto end =
        (dot != std::string::npos &&
         dot > start)
            ? dot
            : path.length();

    return path.substr(
        start,
        end - start);
}