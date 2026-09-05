#include "ConsoleUi.hpp"
#include "qrcodegen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ThreeDsLink {
    const auto background = C2D_Color32(17, 17, 22, 255);
    const auto surface = C2D_Color32(28, 28, 37, 255);
    const auto accent = C2D_Color32(186, 186, 255, 255);
    const auto ink = C2D_Color32(36, 36, 56, 255);
    const auto foreground = C2D_Color32(240, 239, 248, 255);
    const auto muted = C2D_Color32(199, 198, 214, 255);
    const auto mint = C2D_Color32(177, 230, 202, 255);
}

bool ThreeDsLink::CConsoleUi::init() {
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2)) {
        return false;
    }
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini();
        return false;
    }

    C2D_Prepare();
    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    textBuffer = C2D_TextBufNew(4096);
    font = C2D_FontLoad("romfs:/fonts/ui.bcfnt");
    largeFont = C2D_FontLoad("romfs:/fonts/ui-large.bcfnt");
    if (font != nullptr) {
        C2D_FontSetFilter(font, GPU_NEAREST, GPU_NEAREST);
    }
    if (largeFont != nullptr) {
        C2D_FontSetFilter(largeFont, GPU_NEAREST, GPU_NEAREST);
    }

    return top != nullptr && bottom != nullptr && textBuffer != nullptr;
}

void ThreeDsLink::CConsoleUi::shutdown() {
    if (largeFont != nullptr) {
        C2D_FontFree(largeFont);
    }

    if (font != nullptr) {
        C2D_FontFree(font);
    }

    if (textBuffer != nullptr) {
        C2D_TextBufDelete(textBuffer);
    }

    C2D_Fini();
    C3D_Fini();
}

void ThreeDsLink::CConsoleUi::setAddress(const std::string &value) {
    address = value;
    uint8_t temporary[512]{};
    qrReady = !address.empty() &&
              qrcodegen_encodeText(address.c_str(), temporary, qrCode, qrcodegen_Ecc_MEDIUM, 1, 6,
                                   qrcodegen_Mask_AUTO, true);
}

void ThreeDsLink::CConsoleUi::text(float x, float baseline, ETextSize size, uint32_t color,
                                   const std::string &value, float maxWidth, bool centered) {
    const auto selectedFont = size == ETextSize::LARGE ? largeFont : font;
    //Citro2D normalizes fonts to 30 pixels. Undo this to draw the hinted glyphs at native size.
    const auto scale = selectedFont == nullptr
                           ? (size == ETextSize::LARGE ? 0.9f : 0.6f)
                           : C2D_FontGetInfo(selectedFont)->tglp->cellHeight / 30.0f;
    C2D_Text parsed{};
    C2D_TextFontParse(&parsed, selectedFont, textBuffer, value.c_str());
    C2D_TextOptimize(&parsed);
    if (centered) {
        float width = 0;
        C2D_TextGetDimensions(&parsed, scale, scale, &width, nullptr);
        x += (maxWidth - width) / 2;
    }

    C2D_DrawText(&parsed, C2D_WithColor | C2D_AtBaseline | C2D_WordWrap, std::round(x),
                 std::round(baseline), 0.5f, scale, scale, color, maxWidth);
}

void ThreeDsLink::CConsoleUi::panel(float x, float y, float width, float height, uint32_t color) {
    const auto radius = std::min(width, height) * 0.1f;
    C2D_DrawRectSolid(x + radius, y, 0, width - radius * 2, height, color);
    C2D_DrawRectSolid(x, y + radius, 0, width, height - radius * 2, color);
    C2D_DrawCircleSolid(x + radius, y + radius, 0, radius, color);
    C2D_DrawCircleSolid(x + width - radius, y + radius, 0, radius, color);
    C2D_DrawCircleSolid(x + radius, y + height - radius, 0, radius, color);
    C2D_DrawCircleSolid(x + width - radius, y + height - radius, 0, radius, color);
}

void ThreeDsLink::CConsoleUi::drawTop(const std::string &pin, bool available, bool paused) {
    panel(15, 14, 29, 29, accent);
    panel(21, 19, 17, 10, ink);
    panel(20, 32, 19, 5, ink);
    text(54, 36, ETextSize::LARGE, foreground, "3DSLink");
    const auto statusBaseline = 34.0f;
    C2D_DrawCircleSolid(237, statusBaseline - 6, 0, 3, !available || paused ? muted : mint);
    text(247, statusBaseline, ETextSize::BODY, foreground,
         !available ? "UNAVAILABLE"
         : paused   ? "PAUSED"
                    : "LOCAL SHARING",
         140);

    panel(14, 58, 224, 164, surface);
    text(26, 83, ETextSize::BODY, muted, "Web address", 200);
    if (address.empty()) {
        text(26, 110, ETextSize::BODY, foreground, "Wi-Fi disconnected", 200);
    } else {
        text(26, 106, ETextSize::BODY, foreground, "http://", 200);
        text(26, 128, ETextSize::BODY, foreground, address.substr(7), 200);
    }

    text(26, 163, ETextSize::BODY, muted, "PIN", 200);
    for (size_t index = 0; index < pin.size(); ++index) {
        text(26 + index * 46, 200, ETextSize::LARGE, accent, pin.substr(index, 1));
    }

    panel(248, 58, 138, 164, surface);
    if (qrReady && available && !paused) {
        const auto modules = qrcodegen_getSize(qrCode);
        const auto scale = 3;
        const auto size = (modules + 8) * scale;
        const auto left = 317 - size / 2;
        const auto upper = 126 - size / 2;
        C2D_DrawRectSolid(left, upper, 0, size, size, C2D_Color32(255, 255, 255, 255));
        for (int y = 0; y < modules; ++y) {
            for (int x = 0; x < modules; ++x) {
                if (qrcodegen_getModule(qrCode, x, y)) {
                    C2D_DrawRectSolid(left + (x + 4) * scale, upper + (y + 4) * scale, 0, scale,
                                      scale, C2D_Color32(24, 24, 32, 255));
                }
            }
        }

        text(258, 210, ETextSize::BODY, foreground, "Scan to open", 118, true);
    } else {
        text(260, 103, ETextSize::BODY, accent,
             !available ? "Unavailable"
             : paused   ? "Paused"
                        : "No Wi-Fi",
             114, true);
        text(260, 139, ETextSize::BODY, muted,
             paused ? "Press A\nto resume" : "Connect in\nSettings", 114, true);
    }
}

void ThreeDsLink::CConsoleUi::drawBottom(const CServer *server, const std::string &error,
                                         bool editing, const std::string &newPin,
                                         bool confirmExit) {
    if (editing) {
        text(16, 29, ETextSize::LARGE, foreground, "Change PIN");
        text(16, 48, ETextSize::BODY, muted, "Browsers must reconnect.", 288);
        for (size_t index = 0; index < 4; ++index) {
            text(97 + index * 34, 71, ETextSize::LARGE, accent,
                 index < newPin.size() ? newPin.substr(index, 1) : "_");
        }

        for (int index = 0; index < 12; ++index) {
            const auto x = 17 + (index % 3) * 97;
            const auto y = 80 + (index / 3) * 35;
            panel(x, y, 91, 30, index == 11 ? accent : surface);
            const auto label = index < 9     ? std::to_string(index + 1)
                               : index == 9  ? "Delete"
                               : index == 10 ? "0"
                                             : "Save";
            text(x, y + 21, ETextSize::BODY, index == 11 ? ink : foreground, label, 91, true);
        }

        text(17, 237, ETextSize::BODY, muted, "B  Cancel");
        return;
    }

    text(16, 30, ETextSize::LARGE, foreground, "Status");
    panel(14, 44, 140, 91, surface);
    panel(165, 44, 141, 91, surface);
    text(26, 68, ETextSize::BODY, muted, "SD card free");
    char freeSpace[32];
    const auto freeGiB = storage.freeBytes / 1073741824.0;
    std::snprintf(freeSpace, sizeof(freeSpace), freeGiB >= 100 ? "%.0f GiB" : "%.1f GiB", freeGiB);
    text(26, 101, ETextSize::LARGE, accent, storage.totalBytes == 0 ? "Unknown" : freeSpace, 117);
    char totalSpace[32];
    const auto totalGiB = storage.totalBytes / 1073741824.0;
    std::snprintf(totalSpace, sizeof(totalSpace),
                  totalGiB >= 100 ? "%.0f GiB total" : "%.1f GiB total", totalGiB);
    text(26, 126, ETextSize::BODY, muted, storage.totalBytes == 0 ? "" : totalSpace, 117);
    text(178, 68, ETextSize::BODY, muted, "Transferred", 117);
    text(178, 101, ETextSize::LARGE, accent,
         std::to_string(server == nullptr ? 0 : server->completedTransfers()));
    text(178, 126, ETextSize::BODY, muted, "This session");

    if (confirmExit) {
        text(16, 158, ETextSize::BODY, accent, "Cancel transfer and exit?", 288);
    } else if (!error.empty()) {
        text(16, 154, ETextSize::BODY, C2D_Color32(255, 195, 192, 255), error, 288);
    } else if (server != nullptr && server->isBusy()) {
        const auto total = server->transferBytes();
        const auto done = server->transferredBytes();
        const auto ratio = total == 0 ? 0.0f : std::min(1.0f, static_cast<float>(done) / total);
        text(16, 158, ETextSize::BODY, accent, "Transfer in progress");
        panel(16, 172, 288, 5, surface);
        if (ratio > 0) {
            C2D_DrawRectSolid(16, 172, 0, 288 * ratio, 5, accent);
        }
    } else {
        text(16, 162, ETextSize::BODY, muted,
             server == nullptr    ? "Sharing unavailable"
             : server->isPaused() ? "Sharing paused"
                                  : "Waiting for transfers",
             287);
    }

    panel(14, 190, 140, 28, server != nullptr && server->isPaused() ? accent : surface);
    panel(165, 190, 141, 28, surface);
    text(14, 209, ETextSize::BODY, server != nullptr && server->isPaused() ? ink : foreground,
         server != nullptr && server->isPaused() ? "A  Resume" : "A  Pause", 140, true);
    text(165, 209, ETextSize::BODY, foreground, "X  Change PIN", 141, true);
    text(16, 237, ETextSize::BODY, confirmExit ? accent : muted,
         confirmExit ? "START  Confirm     B  Stay" : "START  Exit", 288);
}

void ThreeDsLink::CConsoleUi::draw(const CServer *server, const std::string &pin,
                                   const std::string &error, bool editing,
                                   const std::string &newPin, bool confirmExit) {
    if (osGetTime() >= nextStorageCheck) {
        storage = GetStorage("sdmc:");
        nextStorageCheck = osGetTime() + 2000;
    }

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(textBuffer);
    C2D_TargetClear(top, background);
    C2D_TargetClear(bottom, background);
    C2D_SceneBegin(top);
    drawTop(pin, server != nullptr, server != nullptr && server->isPaused());
    C2D_SceneBegin(bottom);
    drawBottom(server, error, editing, newPin, confirmExit);
    C3D_FrameEnd(0);
}

int ThreeDsLink::CConsoleUi::keypadDigit(int x, int y) const {
    if (x < 17 || x >= 302 || y < 80 || y >= 215) {
        return -1;
    }

    const auto column = (x - 17) / 97;
    const auto row = (y - 80) / 35;
    if ((x - 17) % 97 >= 91 || (y - 80) % 35 >= 30) {
        return -1;
    }

    const auto index = row * 3 + column;
    return index < 9 ? index + 1 : index == 9 ? 10 : index == 10 ? 0 : 11;
}
