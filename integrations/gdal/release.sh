#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GROK_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd $HOME/src/grok-test-data
git checkout master
cd "$GROK_ROOT"
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DGRK_ENABLE_LIBCURL=1
make -j$(nproc) && make install -j$(nproc)

cd $HOME/src/gdal
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
