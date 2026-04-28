#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:?usage: ./tools/release.sh v0.1.0}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$ROOT_DIR"

TARGET="mks-${VERSION}-linux-x86_64"

rm -rf build dist

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./tests.sh

mkdir -p "dist/$TARGET/bin"
cp build/mks "dist/$TARGET/bin/"
cp -r std "dist/$TARGET/std"
cp README.md LICENSE "dist/$TARGET/"

tar -czf "dist/$TARGET.tar.gz" -C dist "$TARGET"

echo "Created dist/$TARGET.tar.gz"
