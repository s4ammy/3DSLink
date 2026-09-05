#include "Server.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: 3dslink-host SD_DIRECTORY WEB_DIRECTORY [PORT] [PIN]\n");
        return 1;
    }

    std::signal(SIGPIPE, SIG_IGN);
    ThreeDsLink::TConfig config;
    config.root = std::filesystem::canonical(argv[1]).string();
    config.assets = std::filesystem::canonical(argv[2]).string();
    config.bindAddress = "127.0.0.1";
    if (argc > 3) {
        const auto port = std::strtol(argv[3], nullptr, 10);
        if (port < 1 || port > 65535) {
            std::fprintf(stderr, "Invalid port.\n");
            return 1;
        }

        config.port = static_cast<uint16_t>(port);
    }

    std::random_device random;
    char code[5];
    std::snprintf(code, sizeof(code), "%04u", random() % 10000);
    config.pairingCode = argc > 4 ? argv[4] : code;
    if (config.pairingCode.size() != 4 ||
        config.pairingCode.find_first_not_of("0123456789") != std::string::npos) {
        std::fprintf(stderr, "PIN must contain four digits.\n");
        return 1;
    }

    for (int index = 0; index < 16; ++index) {
        char byte[3];
        std::snprintf(byte, sizeof(byte), "%02x", random() & 255);
        config.sessionToken += byte;
    }

    ThreeDsLink::CServer server(config);
    std::string error;
    if (!server.start(error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    std::printf("3DSLink: http://127.0.0.1:%u  PIN: %s\n", config.port, config.pairingCode.c_str());
    std::fflush(stdout);
    server.run();
    return 0;
}
