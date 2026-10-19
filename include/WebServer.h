#pragma once

#include "Song.h"
#include "SongSearch.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class WebServer final
{
public:
    static constexpr int defaultPort = 80;
    static constexpr int fallbackPort = 8888;   // als poort 80 bezet of niet toegestaan is
    static constexpr int familyCount = 8;

    // Commando's vanuit de webpagina: "play" (play/pauze), "stop", "next".
    // Wordt op de serverthread aangeroepen; de ontvanger moet zelf
    // naar de juiste thread overschakelen.
    using ControlCallback = std::function<void(const std::string& command)>;

    using FamilyVolumeCallback =
        std::function<void(int family, int volume, bool enabled)>;

    WebServer();
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    // Probeert de gevraagde poort; de standaardpoort valt terug op fallbackPort.
    bool start(int port = defaultPort);
    [[nodiscard]] int port() const noexcept { return port_; }

    // ":8888" of leeg bij poort 80, voor in een adres.
    [[nodiscard]] static std::string portSuffix()
    {
        const int p = activePort().load();
        return p == 80 ? std::string() : ":" + std::to_string(p);
    }

    static std::atomic<int>& activePort() { static std::atomic<int> value { defaultPort }; return value; }
    void stop();

    [[nodiscard]] bool isRunning() const;

    void setSong(std::shared_ptr<const Song> song);

    // Eigen weergavenaam (hernoemd in de setlist); leeg = bestandsnaam.
    void setSongTitle(const std::string& title);
    void setPositionSamples(std::int64_t positionSamples);

    // Transpose in halve tonen; wordt op de akkoorden toegepast.
    void setTranspose(int semitones);

    void setControlCallback(ControlCallback callback);

    // Sleutel waarmee de webpagina transport en mixer mag bedienen
    // (http://host:poort/?key=...). Leeg = bediening staat volledig uit.
    void setControlKey(const std::string& key);

    // Toestand die de webpagina toont naast de positie.
    void setTransportInfo(bool playing, const std::string& nextSongTitle);

    void setFamilyVolumeCallback(FamilyVolumeCallback callback);

    // Zoeken in de setlists (alleen voor bedienende clients): de titels van alle
    // setlists, welke setlist nu getoond wordt ("huidige"), en de tekst van de knop
    // om verder te gaan met de laatste setlist (leeg = niet beschikbaar).
    void setLibrary(std::vector<SetlistSnapshot> lists);
    struct PlaybackContext
    {
        int selectedSetlist = 0;           // de setlist die in de app getoond wordt
        int flowSetlist = -1;              // de setlist waarin nu gespeeld wordt (-1 = geen)
        int flowRow = -1;                  // het nummer daarin
        std::string resumeLabel;           // tekst van de Continue-knop (leeg = niet beschikbaar)
        double remainingSeconds = -1.0;    // resterende tijd van de setlist (< 0 = onbekend)
        bool remainingApproximate = false; // niet van alle nummers is de lengte bekend
    };

    void setPlaybackContext(const PlaybackContext& context);

    // Niveaus (0..1) en welke families uit audio-stems komen (bit n = familie n).
    // Eigen namen van families (uit de stems van een .merik); leeg = standaardnaam.
    void setFamilyNames(const std::array<std::string, familyCount>& names);

    void setMeters(const std::array<float, familyCount>& levels, std::uint32_t stemMask);

    void setFamilySettings(
        const std::array<int, familyCount>& volumes,
        const std::array<bool, familyCount>& enabled);

private:
    struct ClientState
    {
        int socket = -1;
    };

    void serverThread();
    void handleClient(std::intptr_t socket);

    [[nodiscard]] std::string createHtml() const;
    // Volledige songdata (lyrics + akkoorden); wordt gecachet en alleen
    // opnieuw opgebouwd bij een nieuw nummer of een andere transpose.
    [[nodiscard]] std::string createJson() const;

    // Kleine, snel gepollde toestand: positie, play-status, families.
    [[nodiscard]] std::string createStateJson(bool control) const;
    [[nodiscard]] bool isAuthorized(const std::string& request) const;
    [[nodiscard]] std::string escapeHtml(const std::string& text) const;
    [[nodiscard]] std::string escapeJson(const std::string& text) const;
    [[nodiscard]] std::string currentSongTitle() const;
    [[nodiscard]] std::string createFamilyJson() const;

    std::atomic<bool> running { false };

    bool tryStart(int port);
    int port_ = defaultPort;
    std::intptr_t serverSocket_ = -1;   // -1 = geen socket (SOCKET op Windows)

    std::thread thread_;
    mutable std::mutex mutex_;

    std::shared_ptr<const Song> song_;
    std::int64_t positionSamples_ = 0;

    FamilyVolumeCallback familyVolumeCallback_;

    ControlCallback controlCallback_;
    std::string songTitleOverride_;
    std::string controlKey_;

    int transpose_ = 0;
    bool playing_ = false;
    std::string nextSongTitle_;

    // Verhoogd bij elke wijziging van de songdata; de pagina haalt de
    // zware /api/song alleen op als dit getal verandert.
    std::uint64_t contentRevision_ = 1;
    std::uint64_t songRevision_ = 1;

    mutable std::string songJsonCache_;
    mutable std::uint64_t songJsonCacheRevision_ = 0;

    std::vector<SetlistSnapshot> library_;
    PlaybackContext context_;

    std::array<float, familyCount> familyLevels_ {};
    std::uint32_t stemMask_ = 0;
    std::array<std::string, familyCount> familyNames_ {};

    std::array<int, familyCount> familyVolumes_ {
        127, 127, 127, 127, 127, 127, 127, 127
    };

    std::array<bool, familyCount> familyEnabled_ {
        true, true, true, true, true, true, true, true
    };
};