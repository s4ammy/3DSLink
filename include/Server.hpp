#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>

namespace ThreeDsLink {
    struct TStorage {
        uint64_t freeBytes = 0;
        uint64_t totalBytes = 0;
    };

    struct TConfig {
        std::string root;
        std::string assets;
        std::string bindAddress = "0.0.0.0";
        std::string pairingCode;
        std::string sessionToken;
        uint16_t port = 8000;
    };

    struct TRequest {
        std::string method;
        std::string route;
        std::map<std::string, std::string> query;
        std::map<std::string, std::string> headers;
        std::string body;
        uint64_t contentLength = 0;
    };

    std::string JsonString(const std::string &value);
    bool DecodeUrl(const std::string &value, std::string &decoded);
    TStorage GetStorage(const std::string &root);

    class CServer {
        public:
        explicit CServer(TConfig config);
        ~CServer();
        bool start(std::string &error);
        void run();
        void stop();
        void setPaused(bool value);
        bool isPaused() const;
        bool isBusy() const;
        uint32_t transferredBytes() const;
        uint32_t transferBytes() const;
        uint32_t completedTransfers() const;
        uint32_t pairedDevices() const;

        private:
        TConfig config;
        int listener = -1;
        std::atomic<bool> running{false};
        std::atomic<bool> paused{false};
        std::atomic<bool> busy{false};
        std::atomic<uint32_t> transferred{0};
        std::atomic<uint32_t> transferSize{0};
        std::atomic<uint32_t> completed{0};
        std::atomic<uint32_t> paired{0};
        unsigned int failedPairings = 0;
        int64_t pairingBlockedUntil = 0;

        bool waitSocket(int socket, bool writing);
        bool sendAll(int socket, const char *data, size_t size);
        bool readRequest(int socket, TRequest &request);
        void handle(int socket, const TRequest &request);
        void respond(int socket, int status, const std::string &body,
                     const std::string &type = "application/json; charset=utf-8",
                     const std::string &extraHeaders = "");
        bool sendHeaders(int socket, int status, uint64_t size, const std::string &type,
                         const std::string &extraHeaders = "");
        void fail(int socket, int status, const std::string &message);
        bool resolvePath(const std::string &path, std::string &resolved, bool allowMissing = false);
        bool authenticated(const TRequest &request) const;
        void listFiles(int socket, const TRequest &request);
        void sendFile(int socket, const TRequest &request, const std::string &path, bool download);
        void uploadFile(int socket, const TRequest &request);
    };
}
