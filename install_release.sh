#!/usr/bin/env bash
set -e

BUILDDIR=build-auto
# 上游预编译 server jar 的发布地址仍是旧产品名（scrplay 没有上游仓库），URL 不能改；
# 只把下载后的本地文件名改成 scrplay-server
PREBUILT_SERVER_URL=https://github.com/Genymobile/scrcpy/releases/download/v3.3.4/scrcpy-server-v3.3.4
PREBUILT_SERVER_SHA256=8588238c9a5a00aa542906b6ec7e6d5541d9ffb9b5d0f6e1bc0e365e2303079e

echo "[scrplay] Downloading prebuilt server..."
wget "$PREBUILT_SERVER_URL" -O scrplay-server
echo "[scrplay] Verifying prebuilt server..."
echo "$PREBUILT_SERVER_SHA256  scrplay-server" | sha256sum --check

echo "[scrplay] Building client..."
rm -rf "$BUILDDIR"
meson setup "$BUILDDIR" --buildtype=release --strip -Db_lto=true \
    -Dprebuilt_server=scrplay-server
cd "$BUILDDIR"
ninja

echo "[scrplay] Installing (sudo)..."
sudo ninja install
