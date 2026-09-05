#!/usr/bin/env bash
set -euo pipefail

readonly projectDirectory="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$projectDirectory"
mkdir -p .tools

if [[ ! -d .tools/Project_CTR ]]; then
    git clone --depth 1 --branch makerom-v0.18.4 https://github.com/3DSGuy/Project_CTR.git .tools/Project_CTR
fi
if [[ "$(git -C .tools/Project_CTR rev-parse HEAD)" != "c0488dcb6c3048e6a519716f2b21e2c65859ca75" ]]; then
    printf 'Unexpected makerom revision. Use makerom-v0.18.4.\n' >&2
    exit 1
fi

make -C .tools/Project_CTR/makerom deps -j2
make -C .tools/Project_CTR/makerom -j2

if [[ ! -d .tools/bannertool ]]; then
    git clone https://github.com/carstene1ns/3ds-bannertool.git .tools/bannertool
    git -C .tools/bannertool checkout 734d33be79fd3f8c29c6296158f06ac7c5ca9dcb
fi
if [[ "$(git -C .tools/bannertool rev-parse HEAD)" != "734d33be79fd3f8c29c6296158f06ac7c5ca9dcb" ]]; then
    printf 'Unexpected bannertool revision. See scripts/setupPackaging.sh.\n' >&2
    exit 1
fi

cmake -S .tools/bannertool -B .tools/bannertool/build -DCMAKE_BUILD_TYPE=Release
cmake --build .tools/bannertool/build -j2
