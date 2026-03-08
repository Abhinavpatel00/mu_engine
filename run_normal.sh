#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

# Disable Debug Printf in renderer desc
perl -0pi -e 's/\.enable_debug_printf\s*=\s*(true|false)/.enable_debug_printf             = false/' main.c

# Ensure Debug Printf env vars are not set
unset VK_LAYER_PRINTF_ONLY_PRESET
unset VK_LAYER_PRINTF_TO_STDOUT

# Compile shaders without Debug Printf
rm -f compiledshaders/triangle.vert.spv compiledshaders/triangle.frag.spv
./compileslang.sh

# Build app
make release

# Run normally
./build/app
