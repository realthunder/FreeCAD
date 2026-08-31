#!/bin/bash
# Stage the expression sandbox image's browser bundle next to the built web
# UI, so the viewer (and the acceptance page) can fetch it.
#
# The bundle is produced by the image build's POST_BUILD step
# (src/App/ExpressionImage/tools/webpack_image.py) into
# build/wasi-image/web: the stripped image, the CPython files it actually
# opens, and fcx.json.  This copies that tree to build/wasm/web/fcx, which is
# what scripts/wasm-viewer.sh serves.
#
# Usage:  scripts/fcx-web-stage.sh [image-build-dir] [web-dir]
#   defaults: build/wasi-image  build/wasm/web
#
# Rebuild the image first after any change to the sandbox or to
# ExpressionCore:
#   .conda/run.sh env CFLAGS= CXXFLAGS= LDFLAGS= ninja -C build/wasi-image
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-$REPO/build/wasi-image/web}
DST=${2:-$REPO/build/wasm/web}/fcx

[ -f "$SRC/fcx.json" ] || {
  echo "no bundle at $SRC -- build the image first (see the header)" >&2
  exit 1
}

mkdir -p "$DST"
rm -rf "$DST/lib"
cp -r "$SRC/." "$DST/"

python3 - "$DST/fcx.json" <<'EOF'
import json, sys
m = json.load(open(sys.argv[1]))
print("staged %s (%.1f MB) + %d stdlib files (%.0f KB) -> %s"
      % (m["image"], m.get("imageBytes", 0) / 1e6, len(m["lib"]),
         m.get("libBytes", 0) / 1024, sys.argv[1].rsplit("/", 1)[0]))
EOF
