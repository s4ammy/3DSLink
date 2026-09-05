#pragma once

#include "Server.hpp"
#include <3ds.h>
#include <citro2d.h>
#include <string>

namespace ThreeDsLink {
    enum class ETextSize { BODY, LARGE };

    class CConsoleUi {
        public:
        bool init();
        void shutdown();
        void setAddress(const std::string &address);
        void draw(const CServer *server, const std::string &pin, const std::string &error,
                  bool editing, const std::string &newPin, bool confirmExit);
        int keypadDigit(int x, int y) const;

        private:
        C3D_RenderTarget *top = nullptr;
        C3D_RenderTarget *bottom = nullptr;
        C2D_TextBuf textBuffer = nullptr;
        C2D_Font font = nullptr;
        C2D_Font largeFont = nullptr;
        std::string address;
        uint8_t qrCode[512]{};
        bool qrReady = false;
        TStorage storage;
        uint64_t nextStorageCheck = 0;

        void text(float x, float baseline, ETextSize size, uint32_t color, const std::string &value,
                  float maxWidth = 400.0f, bool centered = false);
        void panel(float x, float y, float width, float height, uint32_t color);
        void drawTop(const std::string &pin, bool available, bool paused);
        void drawBottom(const CServer *server, const std::string &error, bool editing,
                        const std::string &newPin, bool confirmExit);
    };
}
