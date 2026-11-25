#!/usr/bin/env bash
# Build the hive scene into web/ (hive-engine.js|.wasm + the kit's page scripts).
# Downloads the pinned Strata release into .strata/ (gitignored). ~20 s.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
STRATA_VERSION=0.10.0
STRATA_SHA256=bb86b974c7e0309db91625ba5ee157273704c77d93f4338c8a63ee625e3d56de
tarball=".strata/strata-$STRATA_VERSION-wasm32-emscripten.tar.gz"
# shellcheck disable=SC1091
. "${AGENTVIZ_EMSDK_ENV:-/root/agentviz/tools/emsdk-env.sh}"
mkdir -p .strata
if [ ! -f "$tarball" ]; then
  gh release download "v$STRATA_VERSION" -R Preponderous-Software/strata \
    -p "strata-$STRATA_VERSION-wasm32-emscripten.tar.gz" -D .strata
fi
echo "$STRATA_SHA256  $tarball" | sha256sum -c -
[ -f .strata/kit/build-kit.sh ] || tar -xzf "$tarball" -C .strata
STRATA_TARBALL="$tarball" STRATA_VERSION="$STRATA_VERSION" STRATA_SHA256="$STRATA_SHA256" \
  .strata/kit/build-kit.sh scene/kit.conf
