#!/usr/bin/env bash
set -euo pipefail

readonly projectDirectory="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly image="devkitpro/devkitarm@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5"
cd "$projectDirectory"

if [[ -n "${DEVKITARM:-}" && -x "${DEVKITARM}/bin/arm-none-eabi-g++" ]]; then
    make -j2 "$@"
else
    docker run --rm --init --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --volume "$projectDirectory:/workspace" --workdir /workspace \
        "$image" make -j2 "$@"
fi

if [[ "${1:-}" == "clean" ]]; then
    exit 0
fi

mkdir -p dist
cp 3DSLink.3dsx 3DSLink.smdh dist/
