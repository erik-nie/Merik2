#pragma once

#include "Song.h"

#include <array>
#include <atomic>
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
    static constexpr int familyCount = 8;

    using FamilyVolumeCallback =
        std::function<void(int family, int volume, bool enabled)>;

    WebServer();
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    bool start(int port = 8080);
    void stop();

    [[nodiscard]] bool isRunning() const;

    void setSong(std::shared_ptr<const Song> song);
    void setPositionSamples(std::int64_t positionSamples);

    void setFamilyVolumeCallback(FamilyVolumeCallback callback);

    void setFamilySettings(
        const std::array<int, familyCount>& volumes,
        const std::array<bool, familyCount>& enabled);

private:
    struct ClientState
    {
        int socket = -1;
    };

    void serverThread();
    void handleClient(int socket);

    [[nodiscard]] std::string createHtml() const;
    [[nodiscard]] std::string createJson() const;
    [[nodiscard]] std::string escapeHtml(const std::string& text) const;
    [[nodiscard]] std::string escapeJson(const std::string& text) const;
    [[nodiscard]] std::string currentSongTitle() const;

    std::atomic<bool> running { false };

    int port_ = 8080;
    int serverSocket_ = -1;

    std::thread thread_;
    mutable std::mutex mutex_;

    std::shared_ptr<const Song> song_;
    std::int64_t positionSamples_ = 0;

    FamilyVolumeCallback familyVolumeCallback_;

    std::array<int, familyCount> familyVolumes_ {
        127, 127, 127, 127, 127, 127, 127, 127
    };

    std::array<bool, familyCount> familyEnabled_ {
        true, true, true, true, true, true, true, true
    };
};