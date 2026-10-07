#include "WebServer.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
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
    address.sin_port =
        htons(static_cast<std::uint16_t>(port_));

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

        ::close(
            serverSocket_);

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

    // ------------------------------------------------------------------------
    // Song title
    // ------------------------------------------------------------------------

    json += "\"song\":\"";

    const auto& path =
        song->sourceFile;

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

    const std::string title =
        path.substr(
            start,
            end - start);

    json += escapeJson(title + " " + song->info);
    json += "\",";

    // ------------------------------------------------------------------------
    // Position and length
    // ------------------------------------------------------------------------

    json += "\"position\":";
    json += std::to_string(positionSeconds);
    json += ",";

    double durationSeconds = 0.0;

    if (!song->playbackEvents.empty())
    {
        durationSeconds =
            song->playbackEvents.back().seconds;
    }

    json += "\"duration\":";
    json += std::to_string(durationSeconds);
    json += ",";

    // ------------------------------------------------------------------------
    // Lyrics
    //
    // Each MIDI lyric event remains a separate timed item.
    // The browser combines them visually into readable lines.
    // ------------------------------------------------------------------------

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
        json += std::to_string(lyric.seconds);

        json += ",";

        json += "\"line\":";
        json += std::to_string(lyric.lineIndex);

        json += ",";

        json += "\"startsNewWord\":";
        json += lyric.startsNewWord ? "true" : "false";

        json += ",";

        json += "\"endsWord\":";
        json += lyric.endsWord ? "true" : "false";

        json += ",";

        json += "\"startsNewLine\":";
        json += lyric.startsNewLine ? "true" : "false";

        json += ",";

        json += "\"endsLine\":";
        json += lyric.endsLine ? "true" : "false";

        json += ",";

        json += "\"text\":\"";
        json += escapeJson(lyric.text);
        json += "\"";

        json += "}";
    }

    json += "],";

    json += "\"chords\":[";

    for (std::size_t i = 0;
        i < song->chords.size();
        ++i)
    {
        if (i != 0)
        {
            json += ",";
        }

        const auto& chord =
            song->chords[i];

        json += "{";

        json += "\"time\":";
        json += std::to_string(
            chord.seconds);

        json += ",";

        json += "\"end\":";
        json += std::to_string(chord.endSeconds);

        json += ",";

        json += "\"label\":\"";
        json += escapeJson(
            chord.label);

        json += "\"";

        json += "}";
    }

    json += "]";

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

html,
body {
    margin: 0;
    padding: 0;
    width: 100%;
    height: 100%;
}

body {
    background: #111;
    color: #fff;

    font-family:
        -apple-system,
        BlinkMacSystemFont,
        "SF Pro Display",
        "SF Pro Text",
        "Segoe UI",
        sans-serif;

    overflow: hidden;
}

/* --------------------------------------------------------------------------
   Header
   -------------------------------------------------------------------------- */

header {
    position: fixed;

    top: 0;
    left: 0;
    right: 0;

    z-index: 10;

    padding: 18px 30px 14px;

    background:
        linear-gradient(
            to bottom,
            rgba(17,17,17,0.98),
            rgba(17,17,17,0.90),
            rgba(17,17,17,0)
        );

    pointer-events: none;
}

#song {
    font-size: 26px;
    font-weight: 600;
    letter-spacing: 0.01em;
}

#time {
    margin-top: 4px;

    color: #888;

    font-size: 14px;
    font-variant-numeric: tabular-nums;
}

/* --------------------------------------------------------------------------
   Main lyrics area
   -------------------------------------------------------------------------- */

main {
    position: absolute;

    top: 0;
    left: 0;
    right: 0;
    bottom: 0;

    overflow: hidden;

    display: flex;
    justify-content: center;
}

#lyricsViewport {
    width: 100%;
    max-width: 1100px;
    height: 100%;

    overflow-y: auto;
    overflow-x: hidden;

    scrollbar-width: none;

    padding:
        120px 35px
        45vh 35px;
}

#lyricsViewport::-webkit-scrollbar {
    display: none;
}

/* --------------------------------------------------------------------------
   Lyrics
   -------------------------------------------------------------------------- */

#lyrics {
    width: 100%;
    font-size: clamp(38px, 4.3vw, 64px);
    line-height: 1.22;
    font-weight: 650;
    letter-spacing: -0.015em;
    text-align: center;
}

.lyric-line {
    width: 100%;
    margin: 0 auto 0.42em;
    padding: 0.08em 0;
}

.lyric-part {
    display: inline;
}

.lyric-part.past {
    color: #ffd800;
}

.lyric-part.current {
    color: #ffffff;
}

.lyric-part.future {
    color: #ff3030;
}

.current-line {
    transform: scale(1.02);
}
/* --------------------------------------------------------------------------
   Chord
   -------------------------------------------------------------------------- */

#chord {
    position: fixed;

    left: 50%;
    bottom: 22px;

    transform:
        translateX(-50%);

    z-index: 20;

    color: #2e9afe;

    font-size: 32px;

    font-weight: 700;

    text-align: center;
}
#chords
{
    display: flex;
    justify-content: center;
    align-items: center;
    gap: 40px;
}
.chord
{
    color: #2e9afe;
    font-size: 54px;
    font-weight: 600;
    transition:
        transform 0.2s ease,
        color 0.2s ease;
}
.chord.current
{
    color: #2e9afe;
    font-size: 54px;
    font-weight: 800;

    transform: scale(1.1);
}
#chordProgress
{
    width: 320px;
    height: 8px;

    margin: 10px auto 0;

    background: #404040;

    border-radius: 999px;

    overflow: hidden;
}
#chordRemaining
{
    width: 100%;
    height: 100%;

    background: #2e9afe;

    transition:
        width 0.1s linear;
}

footer {
    position: fixed;

    left: 0;
    right: 0;
    bottom: 0;

    z-index: 10;

    padding: 50px 30px 25px;

    background:
        linear-gradient(
            to top,
            rgba(0,0,0,0.98),
            rgba(0,0,0,0.95),
            rgba(0,0,0,0.90),
            rgba(0,0,0,0.0)
        );

    text-align: center;
}

/* --------------------------------------------------------------------------
   Small screens
   -------------------------------------------------------------------------- */

@media (max-width: 600px)
{
    header {
        padding:
            15px 18px 10px;
    }

    #song {
        font-size: 21px;
    }

    #time {
        font-size: 13px;
    }

    #lyricsViewport {
        padding:
            100px 18px
            45vh 18px;
    }

    #lyrics {
        font-size:
            clamp(32px, 9vw, 48px);
    }



}

</style>

</head>

<body>

<header>

    <div id="song">
        Merik
    </div>

    <div id="time">
        00:00
    </div>

</header>

<main>

    <div id="lyricsViewport">

        <div id="lyrics"></div>

    </div>

</main>

<footer>

    <div id="chords">

        <span id="currentChord"
              class="chord current">
            Am
        </span>

        <span id="next1"
              class="chord">
            F
        </span>

        <span id="next2"
              class="chord">
            C
        </span>

        <span id="next3"
              class="chord">
            G
        </span>

        <span id="next4"
              class="chord">
            Em
        </span>

    </div>

    <div id="chordProgress">
        <div id="chordRemaining"></div>
    </div>

</footer>

<script>

let songData = null;

let lastCurrentIndex = -1;


/* ==========================================================================
   Time
   ========================================================================== */

function formatTime(seconds)
{
    seconds = Math.max(0, seconds || 0);

    const minutes = Math.floor(seconds / 60);
    const secs = Math.floor(seconds % 60);

    return String(minutes).padStart(2, "0")
        + ":"
        + String(secs).padStart(2, "0");
}


/* ==========================================================================
   Lyric text
   ========================================================================== */

function cleanText(text)
{
    if (!text)
        return "";

    return text.trim();
}


/*
 * MIDI karaoke convention:
 *
 *     ver-
 *     liefd
 *
 * means that "ver" and "liefd" belong to the same word.
 */
function joinsPrevious(text)
{
    if (!text)
        return false;

    return text.trim().startsWith("-");
}


/*
 * More common MIDI convention:
 *
 *     ver-
 *     liefd
 *
 * Here the '-' is at the END of the previous event.
 */
function previousJoinsNext(text)
{
    if (!text)
        return false;

    return text.trim().endsWith("-");
}


/*
 * Remove the continuation marker from what is displayed.
 */
function displayText(text)
{
    text = cleanText(text);

    if (text.endsWith("-"))
        text = text.substring(0, text.length - 1);

    if (text.startsWith("-"))
        text = text.substring(1);

    return text;
}


/* ==========================================================================
   Build lines
   ========================================================================== */
function buildLines(lyrics)
{
    const lines = new Map();

    for (let i = 0; i < lyrics.length; ++i)
    {
        const lyric = {
            ...lyrics[i],
            originalIndex: i
        };

        const lineIndex =
            Number(lyric.line ?? 0);

        if (!lines.has(lineIndex))
            lines.set(lineIndex, []);

        lines.get(lineIndex).push(lyric);
    }

    return [...lines.entries()]
        .sort((a, b) => a[0] - b[0])
        .map(([lineIndex, segments]) => ({
            lineIndex,
            segments
        }));
}


/* ==========================================================================
   Render
   ========================================================================== */

function renderLyrics()
{
    if (!songData)
        return;

    const lyrics =
        songData.lyrics || [];

    const position =
        Number(songData.position || 0);

    const container =
        document.getElementById("lyrics");

    const viewport =
        document.getElementById(
            "lyricsViewport");

    if (!lyrics.length)
    {
        container.innerHTML = "";
        viewport.scrollTo(0, 0);
        lastCurrentIndex = -1;
        return;
    }

    /*
     * Find current lyric event.
     */
    let currentIndex = -1;

    for (let i = 0; i < lyrics.length; ++i)
    {
        const time =
            Number(lyrics[i].time);

        if (time <= position)
            currentIndex = i;
        else
            break;
    }

    /*
     * Don't rebuild the complete HTML 10 times per second
     * if the current lyric hasn't changed.
     *
     * This is also important for smooth scrolling.
     */
    const currentChanged =
        currentIndex !== lastCurrentIndex;

    if (!currentChanged)
        return;

    lastCurrentIndex =
        currentIndex;

    const lines =
        buildLines(lyrics);

    container.innerHTML = "";

    let currentElement = null;

    for (const line of lines)
    {
        const lineElement =
            document.createElement("div");

        lineElement.className =
            "lyric-line";

        let lineIsCurrent = false;

        for (const lyric of line.segments)
        {
            const span =
                document.createElement("span");

            span.className =
                "lyric-part";

            /*
            * Kleur gebaseerd op het oorspronkelijke
            * MIDI-event.
            */
            if (lyric.originalIndex < currentIndex)
            {
                span.classList.add("past");
            }
            else if (lyric.originalIndex === currentIndex)
            {
                span.classList.add("current");
                lineIsCurrent = true;
            }
            else
            {
                span.classList.add("future");
            }

            span.textContent =
                lyric.text || "";

            lineElement.appendChild(span);

            /*
            * Een spatie wordt alleen toegevoegd wanneer
            * de LyricsParser zegt dat hier een woord eindigt.
            *
            * Dus:
            *
            * "dro" + "men"   -> dromen
            * "zoek" + "naar" -> zoek naar
            */
            // if (lyric.endsWord)
            // {
            //     lineElement.appendChild(
            //         document.createTextNode(" ")
            //     );
            // }
            const nextIndex =
                line.segments.indexOf(lyric) + 1;

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

        if (lineIsCurrent)
        {
            lineElement.classList.add(
                "current-line");

            currentElement =
                lineElement;
        }

        container.appendChild(
            lineElement);
    }




    /* ----------------------------------------------------------------------
       Automatic scrolling
       ---------------------------------------------------------------------- */

    if (currentElement)
    {
        /*
         * The current line is now in the DOM.
         *
         * scrollIntoView() is much more reliable than manually
         * calculating transforms.
         */
        currentElement.scrollIntoView({
            behavior: "smooth",
            block: "center",
            inline: "nearest"
        });
    }



}


// ######################## CHORDS ###############################

function renderChords()
{
    if (!songData)
        return;

    const chords =
        songData.chords || [];

    const position =
        Number(songData.position || 0);

    const currentChordElement =
        document.getElementById(
            "currentChord");

    const next1Element =
        document.getElementById(
            "next1");

    const next2Element =
        document.getElementById(
            "next2");

    const next3Element =
        document.getElementById(
            "next3");

    const next4Element =
        document.getElementById(
            "next4");

    const progressElement =
        document.getElementById(
            "chordRemaining");

    if (!chords.length)
    {
        currentChordElement.textContent = "";
        next1Element.textContent = "";
        next2Element.textContent = "";
        next3Element.textContent = "";
        next4Element.textContent = "";

        progressElement.style.width = "0%";

        return;
    }

    let currentIndex = -1;

    for (let index = 0;
         index < chords.length;
         ++index)
    {
        const chordStart =
            Number(chords[index].time || 0);

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
     * Nog vóór het eerste akkoord.
     */
    if (currentIndex < 0)
    {
        currentChordElement.textContent = "";

        next1Element.textContent =
            chords[0]?.label || "";

        next2Element.textContent =
            chords[1]?.label || "";

        next3Element.textContent =
            chords[2]?.label || "";

        next4Element.textContent =
            chords[3]?.label || "";

        progressElement.style.width = "0%";

        return;
    }

    const currentChord =
        chords[currentIndex];

    currentChordElement.textContent =
        currentChord.label || "";

    next1Element.textContent =
        chords[currentIndex + 1]?.label || "";

    next2Element.textContent =
        chords[currentIndex + 2]?.label || "";

    next3Element.textContent =
        chords[currentIndex + 3]?.label || "";

    next4Element.textContent =
        chords[currentIndex + 4]?.label || "";

    renderChordProgress(
        currentChord);
}

function renderChordProgress(
    currentChord)
{
    const progressElement =
        document.getElementById(
            "chordRemaining");

    if (!currentChord ||
        !songData)
    {
        progressElement.style.width =
            "0%";

        return;
    }

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

    const elapsed =
        Math.max(
            0,
            Math.min(
                duration,
                position - start));

    const percentage =
        elapsed /
        duration *
        100;

    progressElement.style.width =
        percentage + "%";
}

/* ==========================================================================
   Update 
   ========================================================================== */

async function update()
{
    try
    {
        const response =
            await fetch(
                "/api/song",
                {
                    cache: "no-store"
                });

        if (!response.ok)
            return;

        songData =
            await response.json();

        document.getElementById("song")
            .textContent =
                songData.song ||
                "Merik";

        document.getElementById("time")
            .textContent =
                formatTime( songData.position)+ " / " + formatTime(songData.duration);

        renderLyrics();
        renderChords();
    }
    catch (error)
    {
        console.log(error);
    }
}


update();

setInterval(
    update,
    100);

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