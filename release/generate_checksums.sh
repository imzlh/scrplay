#!/bin/bash
set -ex
cd "$(dirname ${BASH_SOURCE[0]})"
. build_common

cd "$OUTPUT_DIR"
sha256sum "scrplay-server-$VERSION" \
    "scrplay-linux-x86_64-$VERSION.tar.gz" \
    "scrplay-win32-$VERSION.zip" \
    "scrplay-win64-$VERSION.zip" \
    "scrplay-macos-aarch64-$VERSION.tar.gz" \
    "scrplay-macos-x86_64-$VERSION.tar.gz" \
        | tee SHA256SUMS.txt
echo "Release checksums generated in $PWD/SHA256SUMS.txt"
