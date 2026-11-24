#!/bin/sh
# Fetch pinned third-party sources (not committed).
set -e
cd "$(dirname "$0")"
[ -d lvgl ] && exit 0
curl -sSL -o lvgl-9.2.2.tar.gz https://github.com/lvgl/lvgl/archive/refs/tags/v9.2.2.tar.gz
echo "129b4e00e06639fa79d7e8a6cab3c1ecce2445b1a246652ccd34f22e7b17ad6f  lvgl-9.2.2.tar.gz" | sha256sum -c -
tar xzf lvgl-9.2.2.tar.gz && mv lvgl-9.2.2 lvgl
