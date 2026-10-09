#include "WebServer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
constexpr int backlog = 8;

void sendResponse(int socket, const std::string& body, const char* contentType)
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
        const auto sent = ::send(socket, data, remaining, 0);
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
    serverSocket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ < 0)
        return false;

    int reuseAddress = 1;
    ::setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR,
                 &reuseAddress, sizeof(reuseAddress));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<std::uint16_t>(port_));

    if (::bind(serverSocket_, reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) < 0 ||
        ::listen(serverSocket_, backlog) < 0)
    {
        ::close(serverSocket_);
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
        ::shutdown(serverSocket_, SHUT_RDWR);
        ::close(serverSocket_);
        serverSocket_ = -1;
    }

    if (thread_.joinable())
        thread_.join();
}

bool WebServer::isRunning() const { return running.load(); }

void WebServer::setSong(std::shared_ptr<const Song> song)
{
    std::scoped_lock lock(mutex_);
    song_ = std::move(song);
    positionSamples_ = 0;
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

void WebServer::serverThread()
{
    while (running)
    {
        sockaddr_in clientAddress {};
        socklen_t clientLength = sizeof(clientAddress);
        const int clientSocket = ::accept(
            serverSocket_, reinterpret_cast<sockaddr*>(&clientAddress),
            &clientLength);

        if (clientSocket < 0)
        {
            if (running)
                continue;
            break;
        }

        handleClient(clientSocket);
        ::shutdown(clientSocket, SHUT_RDWR);
        ::close(clientSocket);
    }
}

void WebServer::handleClient(int socket)
{
    char buffer[4096] {};
    const auto received = ::recv(socket, buffer, sizeof(buffer) - 1, 0);

    if (received <= 0)
        return;

    buffer[received] = '\0';
    const std::string request(buffer);

    if (request.rfind("GET /api/song", 0) == 0)
    {
        sendResponse(socket, createJson(), "application/json; charset=utf-8");
        return;
    }

    if (request.rfind("GET /api/family?", 0) == 0)
    {
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
        return R"({"song":"","position":0,"duration":0,"lyrics":[],"chords":[]})";

    const double sampleRate = song->sampleRate > 0.0 ? song->sampleRate : 48000.0;
    const double positionSeconds = static_cast<double>(positionSamples) / sampleRate;

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
    const std::string title = path.substr(start, end - start);

    std::string json = "{";
    json += "\"song\":\"" + escapeJson(title) + "\",";
    json += "\"position\":" + std::to_string(positionSeconds) + ",";
    json += "\"duration\":" + std::to_string(durationSeconds) + ",";

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
        json += "{";
        json += "\"time\":" + std::to_string(chord.seconds) + ",";
        json += "\"end\":" + std::to_string(chord.endSeconds) + ",";
        json += "\"label\":\"" + escapeJson(chord.label) + "\"}";
    }
    json += "]}";
    return json;
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

    background: rgba(0, 0, 0, 0.35);
    color: #ff4a4a;

    font-size: 15px;
    font-weight: 600;

    pointer-events: auto;
}

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
@media (max-width:600px) {
    header { padding:15px 18px 40px; }
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
#familyPanelContent { position: absolute; inset: 0; padding: 20px 16px; overflow-x: hidden; overflow-y: auto; background: rgba(15,15,28,0.97); border-left: 1px solid #444; pointer-events: auto; }
#familyToggle { position: absolute; z-index: 101; top: 50%; left: -28px; width: 28px; height: 76px; padding: 0; transform: translateY(-50%); border: 1px solid #555; border-right: none; border-radius: 8px 0 0 8px; background: #181824; color: #ff4a4a; font-size: 28px; cursor: pointer; pointer-events: auto; }
body.family-panel-open #familyPanel { transform: translateX(0); }
body.family-panel-open #familyPanelContent { pointer-events: auto; }
#lyricsViewport { width: 100%; margin-right: 0; transition: width 280ms ease, margin-right 280ms ease; }
body.family-panel-open #lyricsViewport { width: 80%; margin-right: 20%; }
#familyPanelHeader { display: flex; align-items: center; justify-content: space-between; gap: 10px; margin-bottom: 22px; font-size: 20px; font-weight: 700; color: #ff4a4a; }
#familyClose { width: 36px; height: 36px; border: 0; border-radius: 6px; background: #30303d; color: #fff; font-size: 24px; cursor: pointer; }
.family-control { margin-bottom: 5px; padding-bottom: 4px; border-bottom: 1px solid #353543; }
.family-control-header { display: flex; align-items: center; justify-content: space-between; gap: 8px; margin-bottom: 4px; }
.family-name { color: #fff; font-size: 15px; font-weight: 600; }
.family-enable { appearance: none; -webkit-appearance: none; width: 34px; height: 18px; flex-shrink: 0; border-radius: 9px; background: #555; position: relative; cursor: pointer; transition: background 180ms ease; }
.family-enable::before { content: ""; position: absolute; width: 14px; height: 14px; top: 2px; left: 2px; border-radius: 50%; background: #fff; transition: transform 180ms ease; }
.family-enable:checked { background: #ff4a4a; }
.family-enable:checked::before { transform: translateX(16px); }
.family-enable:focus-visible { outline: 2px solid #ff4a4a; outline-offset: 3px; }
.family-slider-row { display: flex; align-items: center; gap: 3px; }
.family-slider { width: 100%; min-width: 0; accent-color: #ff4a4a; cursor: pointer; }
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
<header><div id="song">Merik</div><div id="time">00:00 / 00:00</div></header>
<div id="songProgress"><div id="songProgressFill"></div></div>
<div id="lyricCountdown"></div>
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
             class="chord-progress" future-progress">
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
let chordAnimation = null;

let lastPreparedLineIndex = -1;
let initialLyricsPositioned = false;
let lyricScrollAnimation = null;

let lastSongTitle = "";
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
        songData.song !== lastSongTitle;

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
     * Bouw de tekst opnieuw wanneer de actieve lyric
     * verandert.
     */
const lyricIndexChanged =
    currentIndex !== lastCurrentIndex;

const lyricsNeedInitialRender =
    container.children.length === 0;

if (lyricIndexChanged ||
    lyricsNeedInitialRender)
{
    lastCurrentIndex =
        currentIndex;

    container.innerHTML = "";

    const lines =
        buildLines(lyrics);

    for (const line of lines)
    {
        const lineElement =
            document.createElement("div");

        lineElement.className =
            "lyric-line";

        let lineIsCurrent = false;

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

        if (lineIsCurrent)
            lineElement.classList.add("current-line");

        container.appendChild(lineElement);
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
        buildLines(lyrics);

    let currentLineIndex = 0;

    if (currentIndex >= 0)
    {
        for (let i = 0;
             i < lines.length;
             ++i)
        {
            const segments =
                lines[i].segments;

            if (segments.some(
                lyric =>
                    lyric.originalIndex === currentIndex))
            {
                currentLineIndex = i;
                break;
            }
        }
    }

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

    const lineElements =
        container.querySelectorAll(".lyric-line");

    const nextLineElement =
        lineElements[nextLineIndex];

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
     * Nog vóór het eerste akkoord.
     */
    if (currentIndex < 0)
    {
        document.getElementById(
            "currentChord")
            .textContent = "";

        renderFutureChord(
            1,
            chords[0]);

        renderFutureChord(
            2,
            chords[1]);

        renderFutureChord(
            3,
            chords[2]);

        renderFutureChord(
            4,
            chords[3]);

        renderChordProgress(null);

        return;
    }

    /*
     * De labels hoeven alleen bij een akkoordwissel
     * opnieuw te worden ingevuld.
     *
     * De voortgangsbalk wordt wel bij iedere update
     * aangepast.
     */
    const chordChanged =
        currentIndex !== lastChordIndex;

    if (chordChanged)
    {
        document.getElementById(
            "currentChord")
            .textContent =
                chords[currentIndex]
                    ?.label || "";

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


async function update() {
    try {
        const response=await fetch("/api/song",{cache:"no-store"});
        if(!response.ok) return;
        songData=await response.json();
        document.getElementById("song").textContent=songData.song||"Merik";
        document.getElementById("time").textContent=formatTime(songData.position)+" / "+formatTime(songData.duration);
 
        const duration = Number(songData.duration) || 0;
        const position = Number(songData.position) || 0;
        const progress = duration > 0 ? Math.max(0, Math.min(100, position / duration * 100))   : 0;
            document.getElementById("songProgressFill") .style.width = progress + "%";

        renderLyrics();
        renderChords();
        renderCountdown();
    } catch(error) { console.log(error); }
}


// Family mixer gekoppeld aan de FluidSynth-engine.
(function initFamilyMixer() {
    const toggle = document.getElementById("familyToggle");
    const panel = document.getElementById("familyPanel");
    const close = document.getElementById("familyClose");
    const controls = document.getElementById("familyControls");

    const families = [
        "Drums",
        "Bass",
        "Guitars",
        "Keys",
        "Strings",
        "Winds",
        "FX",
        "Other"
    ];

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
                enabled: enabled.checked ? "1" : "0"
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
            value.textContent = slider.value;
            scheduleUpdate(false);
        });

        enabled.addEventListener("change", function () {
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