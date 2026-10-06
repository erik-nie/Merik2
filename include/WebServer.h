#pragma once

#include "Song.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class WebServer final
{
public:
    WebServer();
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    bool start(int port = 8080);
    void stop();

    [[nodiscard]] bool isRunning() const;

    void setSong(std::shared_ptr<const Song> song);

    void setPositionSamples(std::int64_t positionSamples);

private:
    struct ClientState
    {
        int socket = -1;
    };

    void serverThread();

    void handleClient(int socket);

    [[nodiscard]] std::string createHtml() const;
    [[nodiscard]] std::string createJson() const;

    [[nodiscard]] std::string escapeHtml(
        const std::string& text) const;

    [[nodiscard]] std::string escapeJson(
        const std::string& text) const;

    [[nodiscard]] std::string currentSongTitle() const;

    std::atomic<bool> running { false };

    int port_ = 8080;
    int serverSocket_ = -1;

    std::thread thread_;

    mutable std::mutex mutex_;

    std::shared_ptr<const Song> song_;

    std::int64_t positionSamples_ = 0;
};