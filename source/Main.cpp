#include "ConsoleUi.hpp"
#include "Server.hpp"

#include <3ds.h>
#include <arpa/inet.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace ThreeDsLink {
    void RunServer(void *argument);
    bool NewCredentials(TConfig &config);
    bool SavePin(const std::string &pin);
    void LoadPin(TConfig &config);
}

void ThreeDsLink::RunServer(void *argument) {
    static_cast<CServer *>(argument)->run();
}

bool ThreeDsLink::NewCredentials(TConfig &config) {
    std::array<uint8_t, 20> random{};
    if (R_FAILED(PS_GenerateRandomBytes(random.data(), random.size()))) {
        return false;
    }

    config.sessionToken.clear();
    for (size_t index = 0; index < 16; ++index) {
        char byte[3];
        std::snprintf(byte, sizeof(byte), "%02x", random[index]);
        config.sessionToken += byte;
    }

    if (config.pairingCode.empty()) {
        const auto number =
            static_cast<uint32_t>(random[16]) | (static_cast<uint32_t>(random[17]) << 8) |
            (static_cast<uint32_t>(random[18]) << 16) | (static_cast<uint32_t>(random[19]) << 24);
        char pin[5];
        std::snprintf(pin, sizeof(pin), "%04lu", static_cast<unsigned long>(number % 10000));
        config.pairingCode = pin;
    }

    return true;
}

bool ThreeDsLink::SavePin(const std::string &pin) {
    mkdir("sdmc:/3ds", 0755);
    mkdir("sdmc:/3ds/3dslink", 0755);
    auto *file = std::fopen("sdmc:/3ds/3dslink/pin.txt", "wb");
    if (file == nullptr) {
        return false;
    }

    const auto written = std::fwrite(pin.data(), 1, pin.size(), file) == pin.size();
    return std::fclose(file) == 0 && written;
}

void ThreeDsLink::LoadPin(TConfig &config) {
    auto *file = std::fopen("sdmc:/3ds/3dslink/pin.txt", "rb");
    if (file == nullptr) {
        return;
    }

    char pin[5]{};
    const auto size = std::fread(pin, 1, sizeof(pin), file);
    std::fclose(file);
    const std::string value(pin, size);
    if (value.size() == 4 && value.find_first_not_of("0123456789") == std::string::npos) {
        config.pairingCode = value;
    }
}

int main() {
    gfxInitDefault();
    const auto romfsReady = R_SUCCEEDED(romfsInit());
    ThreeDsLink::CConsoleUi ui;
    if (!ui.init()) {
        if (romfsReady) {
            romfsExit();
        }

        gfxExit();
        return 1;
    }

    ThreeDsLink::TConfig config;
    config.root = "sdmc:";
    config.assets = "romfs:/web";
    std::string error;
    const auto randomReady = R_SUCCEEDED(psInit());
    auto *socketBuffer = static_cast<uint32_t *>(memalign(0x1000, 1024 * 1024));
    const auto socketReady =
        socketBuffer != nullptr && R_SUCCEEDED(socInit(socketBuffer, 1024 * 1024));
    std::unique_ptr<ThreeDsLink::CServer> server;
    Thread worker = nullptr;
    ThreeDsLink::LoadPin(config);
    if (!romfsReady || !randomReady || !socketReady) {
        error = "Could not start services. Reopen 3DSLink to retry.";
    } else if (!ThreeDsLink::NewCredentials(config)) {
        error = "Could not generate a secure session. Please reopen.";
    } else {
        server = std::make_unique<ThreeDsLink::CServer>(config);
        if (server->start(error)) {
            worker = threadCreate(ThreeDsLink::RunServer, server.get(), 96 * 1024, 0x31, -2, false);
            if (worker == nullptr) {
                error = "Could not start the server thread. Please reopen.";
                server.reset();
            }
        } else {
            server.reset();
        }
    }

    aptSetSleepAllowed(false);
    bool editing = false;
    bool confirmExit = false;
    std::string newPin;
    uint32_t previousAddress = 0;
    uint64_t nextNetworkCheck = 0;
    while (aptMainLoop()) {
        hidScanInput();
        const auto keys = hidKeysDown();
        touchPosition touch{};
        hidTouchRead(&touch);
        if (socketReady && osGetTime() >= nextNetworkCheck) {
            nextNetworkCheck = osGetTime() + 1000;
            const auto hostAddress = static_cast<uint32_t>(gethostid());
            const auto address = hostAddress == INADDR_NONE ? 0 : hostAddress;
            if (address != previousAddress) {
                previousAddress = address;
                in_addr internetAddress{};
                internetAddress.s_addr = address;
                ui.setAddress(address == 0
                                  ? ""
                                  : std::string("http://") + inet_ntoa(internetAddress) + ":8000");
            }
        }

        if (keys & KEY_START) {
            if (server != nullptr && server->isBusy() && !confirmExit) {
                confirmExit = true;
            } else {
                break;
            }
        }
        if (keys & KEY_B) {
            editing = false;
            confirmExit = false;
        }

        const auto changePinTouched = (keys & KEY_TOUCH) && touch.px >= 165 && touch.px <= 306 &&
                                      touch.py >= 190 && touch.py <= 218;
        const auto pauseTouched = (keys & KEY_TOUCH) && touch.px >= 14 && touch.px <= 154 &&
                                  touch.py >= 190 && touch.py <= 218;
        if (editing) {
            if (keys & KEY_TOUCH) {
                const auto digit = ui.keypadDigit(touch.px, touch.py);
                if (digit >= 0 && digit <= 9 && newPin.size() < 4) {
                    newPin += static_cast<char>('0' + digit);
                } else if (digit == 10 && !newPin.empty()) {
                    newPin.pop_back();
                } else if (digit == 11 && newPin.size() == 4 && server != nullptr) {
                    if (server->isBusy()) {
                        error = "Finish the transfer before changing your PIN.";
                        editing = false;
                        continue;
                    }

                    error.clear();
                    auto nextConfig = config;
                    nextConfig.pairingCode = newPin;
                    if (!ThreeDsLink::NewCredentials(nextConfig) || !ThreeDsLink::SavePin(newPin)) {
                        error = "Could not save the PIN. Check your SD card.";
                        editing = false;
                    } else {
                        server->stop();
                        threadJoin(worker, U64_MAX);
                        threadFree(worker);
                        worker = nullptr;
                        server.reset();
                        config = nextConfig;
                        server = std::make_unique<ThreeDsLink::CServer>(config);
                        if (server->start(error)) {
                            worker = threadCreate(ThreeDsLink::RunServer, server.get(), 96 * 1024,
                                                  0x31, -2, false);
                        }
                        if (worker == nullptr) {
                            error = "Could not restart sharing. Reopen 3DSLink.";
                            server.reset();
                        }

                        editing = false;
                    }
                }
            }
        } else if (server != nullptr && ((keys & KEY_X) || changePinTouched)) {
            if (server->isBusy()) {
                error = "Finish the transfer before changing your PIN.";
            } else {
                error.clear();
                newPin.clear();
                editing = true;
            }
        } else if (server != nullptr && ((keys & KEY_A) || pauseTouched)) {
            server->setPaused(!server->isPaused());
            error.clear();
        }

        ui.draw(server.get(), config.pairingCode, error, editing, newPin, confirmExit);
    }

    if (server != nullptr) {
        server->stop();
    }
    if (worker != nullptr) {
        threadJoin(worker, U64_MAX);
        threadFree(worker);
    }

    server.reset();
    aptSetSleepAllowed(true);
    if (socketReady) {
        socExit();
    }

    std::free(socketBuffer);
    if (randomReady) {
        psExit();
    }
    if (romfsReady) {
        romfsExit();
    }

    ui.shutdown();
    gfxExit();
    return 0;
}
