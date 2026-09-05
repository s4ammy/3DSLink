# 3DSLink

> **Design notice:** The 3DS UI and web interface were designed by AI. 3DSLink shares the same basic idea as [3ds-httpd](https://github.com/dimaguy/3ds-httpd): hosting a web server on the console. Its additions focus on file sharing, with a four-digit PIN set on the 3DS, QR access, screenshot previews, and an upload queue.

Transfer files between your 3DS SD card and your phone or computer over Wi-Fi. Open the file browser on iPhone, Android, Windows, or Linux without installing a companion app.

## Getting started

1. Install `dist/3DSLink.cia` with FBI on your modded 3DS.
2. Connect both devices to the same Wi-Fi network and open 3DSLink.
3. Scan the QR code or open the address shown on the top screen.
4. Enter the PIN, then browse, upload, or download files.

For the Homebrew Launcher, copy `3DSLink.3dsx` and `3DSLink.smdh` into `/3ds/3dslink/`. The release ZIP includes this folder layout.

The file browser has search, sorting, folder creation, and shortcuts for screenshots, camera photos, homebrew, and exported save backups. Take screenshots with Rosalina first, then transfer them through 3DSLink.

## Controls

- **A:** Pause or resume sharing.
- **X:** Set a new PIN using the touchscreen. Paired browsers will need to reconnect.
- **B:** Cancel.
- **START:** Exit. Press twice if a transfer is running.

A PIN you set is saved for future launches. Otherwise, the app generates one each time.

Keep the app open while transferring files. Use a network you trust: the PIN controls access, but HTTP traffic is not encrypted. Existing files cannot be overwritten or deleted through the browser.

## Building

With Docker installed, run:

```sh
./scripts/build.sh
```

For a CIA and release ZIP, also install a C/C++ compiler, Git, Make, and CMake, then run:

```sh
./scripts/setupPackaging.sh
./scripts/package.sh
```

Builds go into `dist/`. See [THIRD_PARTY.md](THIRD_PARTY.md) for dependencies and licenses.
