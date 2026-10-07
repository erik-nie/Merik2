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
        sendResponse(socket, createJson(), "application/json; charset=utf-8");
    else
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
    json += "\"song\":\"" + escapeJson(title + " " + song->info) + "\",";
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
    background:#111; color:#fff; overflow:hidden;
    font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display","Segoe UI",sans-serif;
}
header {
    position:fixed; inset:0 0 auto 0; z-index:10; padding:18px 30px 50px;
    background:linear-gradient(to bottom,rgba(17,17,17,.99),rgba(17,17,17,.9),transparent);
    pointer-events:none;
}
#song { font-size:26px; font-weight:600; }
#time { margin-top:4px; color:#888; font-size:14px; font-variant-numeric:tabular-nums; }
main { position:absolute; inset:0; display:flex; justify-content:center; overflow:hidden; }
#lyricsViewport {
    width:100%; max-width:1100px; height:100%; overflow-y:auto; overflow-x:hidden;
    scrollbar-width:none; padding:120px 35px 260px;
}
#lyricsViewport::-webkit-scrollbar { display:none; }
#lyrics { width:100%; font-size:clamp(38px,4.3vw,64px); line-height:1.22; font-weight:650; text-align:center; }
.lyric-line { width:100%; margin:0 auto .42em; padding:.08em 0; }
.lyric-part { display:inline; }
.lyric-part.past { color:#ffd800; }
.lyric-part.current { color:#fff; }
.lyric-part.future { color:#ff3030; }
.current-line { transform:scale(1.02); }
footer {
    position:fixed; inset:auto 0 0 0; z-index:20; padding:82px 28px 24px;
    background:linear-gradient(to top,rgba(0,0,0,1) 0%,rgba(0,0,0,.98) 42%,rgba(0,0,0,.86) 70%,transparent 100%);
    pointer-events:none;
}
#chordViewport { width:100%; max-width:1000px; margin:0 auto; overflow:hidden; }

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
    justify-content:flex-end; gap:11px; opacity:.58;
    transition:transform 180ms ease,opacity 180ms ease;
}
.chord-slot.current { opacity:1; }
.chord {
    width:100%; overflow:hidden; white-space:nowrap; text-overflow:ellipsis;
    color:#939393; font-size:clamp(24px,3vw,38px); font-weight:650;
    line-height:1; text-align:center;
}
.chord-slot.current .chord {
    color:#2e9afe; font-size:clamp(34px,4.2vw,54px); font-weight:800;
}
.chord-progress {
    position:relative; width:100%; height:8px; overflow:hidden;
    background:#3d3d3d; border-radius:999px;
}
.chord-progress-fill {
    position:absolute; inset:0 auto 0 0; width:100%;
    background:#2e9afe; border-radius:999px; transition:width 100ms linear;
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
    .chord-slot.current .chord { font-size:31px; }
    .chord-progress { height:6px; }
}
</style>
</head>
<body>
<header><div id="song">Merik</div><div id="time">00:00 / 00:00</div></header>
<main><div id="lyricsViewport"><div id="lyrics"></div></div></main>
<footer>
  <div id="chordViewport">
    <div id="chords">
      <div class="chord-slot current"><span id="currentChord" class="chord"></span><div class="chord-progress"><div id="chordRemaining" class="chord-progress-fill"></div></div></div>
      <div class="chord-slot"><span id="next1" class="chord"></span><div class="chord-progress"></div></div>
      <div class="chord-slot"><span id="next2" class="chord"></span><div class="chord-progress"></div></div>
      <div class="chord-slot"><span id="next3" class="chord"></span><div class="chord-progress"></div></div>
      <div class="chord-slot"><span id="next4" class="chord"></span><div class="chord-progress"></div></div>
    </div>
  </div>
</footer>
<script>
let songData = null;

let lastCurrentIndex = -1;
let lastChordIndex = -2;
let chordAnimation = null;
`

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
function renderLyrics() {
    if(!songData) return;
    const lyrics=songData.lyrics||[];
    const position=Number(songData.position||0);
    const container=document.getElementById("lyrics");
    const viewport=document.getElementById("lyricsViewport");
    if(!lyrics.length){container.innerHTML="";viewport.scrollTo(0,0);lastCurrentIndex=-1;return;}
    let currentIndex=-1;
    for(let i=0;i<lyrics.length;++i){if(Number(lyrics[i].time)<=position)currentIndex=i;else break;}
    if(currentIndex===lastCurrentIndex) return;
    lastCurrentIndex=currentIndex;
    container.innerHTML="";
    let currentElement=null;
    for(const line of buildLines(lyrics)) {
        const lineElement=document.createElement("div");
        lineElement.className="lyric-line";
        let lineIsCurrent=false;
        for(const lyric of line.segments) {
            const span=document.createElement("span");
            span.className="lyric-part";
            if(lyric.originalIndex<currentIndex) span.classList.add("past");
            else if(lyric.originalIndex===currentIndex){span.classList.add("current");lineIsCurrent=true;}
            else span.classList.add("future");
            span.textContent=lyric.text||"";
            lineElement.appendChild(span);
            const nextIndex=line.segments.indexOf(lyric)+1;
            if(nextIndex<line.segments.length) {
                const nextLyric=line.segments[nextIndex];
                if(nextLyric.startsNewWord||lyric.endsWord)
                    lineElement.appendChild(document.createTextNode(" "));
            }
        }
        if(lineIsCurrent){lineElement.classList.add("current-line");currentElement=lineElement;}
        container.appendChild(lineElement);
    }
    if(currentElement) currentElement.scrollIntoView({behavior:"smooth",block:"center",inline:"nearest"});
}
function renderChordProgress(currentChord) {
    const element=document.getElementById("chordRemaining");
    if(!currentChord||!songData){element.style.width="0%";return;}
    const position=Number(songData.position||0);
    const start=Number(currentChord.time||0);
    const end=Number(currentChord.end||0);
    const duration=end-start;
    if(duration<=0){element.style.width="0%";return;}
    const remaining=Math.max(0,Math.min(duration,end-position));
    element.style.width=(remaining/duration*100)+"%";
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
                duration: 220,

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
        for (const id of ids)
        {
            document.getElementById(id)
                .textContent = "";
        }

        renderChordProgress(null);

        lastChordIndex = -2;

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
        /*
         * Alleen opnieuw vullen als de getoonde
         * akkoordset werkelijk veranderd is.
         */
        if (lastChordIndex !== -1)
        {
            document.getElementById(
                "currentChord")
                .textContent = "";

            for (let index = 1;
                 index < ids.length;
                 ++index)
            {
                document.getElementById(
                    ids[index])
                    .textContent =
                        chords[index - 1]
                            ?.label || "";
            }

            lastChordIndex = -1;
        }

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
        for (let index = 0;
             index < ids.length;
             ++index)
        {
            document.getElementById(
                ids[index])
                .textContent =
                    chords[currentIndex + index]
                        ?.label || "";
        }

        lastChordIndex =
            currentIndex;

        animateChordChange();
    }

    renderChordProgress(
        chords[currentIndex]);
}
async function update() {
    try {
        const response=await fetch("/api/song",{cache:"no-store"});
        if(!response.ok) return;
        songData=await response.json();
        document.getElementById("song").textContent=songData.song||"Merik";
        document.getElementById("time").textContent=formatTime(songData.position)+" / "+formatTime(songData.duration);
        renderLyrics();
        renderChords();
    } catch(error) { console.log(error); }
}
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
