# Third-party components

- **Project Nayuki QR Code generator, C edition:** `third_party/qrcodegen/qrcodegen.c` and `.h`. The MIT license is retained in both source files. Original project: https://www.nayuki.io/page/qr-code-generator-library . The vendored files were copied without modification from the existing local 3DS project.
- **DejaVu Sans:** `romfs/fonts/ui.bcfnt` is generated from DejaVu Sans using devkitPro's `mkbcfnt`. The font license is included at `meta/fonts/DejaVu-LICENSE.txt`. Original project: https://dejavu-fonts.github.io/ . This is an ASCII subset for the console interface.
- **devkitPro libraries:** the 3DS executable links libctru, Citro2D, and Citro3D. Licenses and source are supplied by their devkitPro packages: https://github.com/devkitPro/libctru , https://github.com/devkitPro/citro2d , https://github.com/devkitPro/citro3d .
- **makerom:** CIA packaging uses Project_CTR makerom v0.18.4, commit `c0488dcb6c3048e6a519716f2b21e2c65859ca75`. Source and MIT license: https://github.com/3DSGuy/Project_CTR . Build-only dependency.
- **bannertool:** CIA banners are generated with the CMake fork at commit `734d33be79fd3f8c29c6296158f06ac7c5ca9dcb`. Source and license: https://github.com/carstene1ns/3ds-bannertool . Build-only dependency; its banner template is used in the generated banner.

The web interface, vector artwork, application code, and HTTP server were written for 3DSLink. No source code from ftpd, 3ds-httpd, or 3DS-FileBrowser was copied into the application.
