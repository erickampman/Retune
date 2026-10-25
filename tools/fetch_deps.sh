#!/usr/bin/env bash
#
# Initialise every dependency submodule Retune needs, at the pinned commits.
#
# Run this once after a fresh `git clone`, and again after a `git pull` that
# moves, adds or re-points a submodule.
#
# Note on vst3sdk: it declares seven nested submodules. Only base, cmake,
# pluginterfaces and public.sdk are needed; doc, tutorials and vstgui4 are left
# uninitialised so a checkout stays small, which a bare
# `git submodule update --init --recursive` wouldn't do.
#
set -euo pipefail
cd "$(dirname "$0")/.."

echo "==> Submodule URLs (after a pull that changed one)"
git submodule sync --recursive

echo "==> Top-level submodules"
git submodule update --init external/clap external/uw-core external/clap-wrapper external/vst3sdk

echo "==> vst3sdk: nested base, cmake, pluginterfaces, public.sdk"
git -C external/vst3sdk submodule update --init --recursive \
    base cmake pluginterfaces public.sdk

echo "==> Done. Configure with: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release"
