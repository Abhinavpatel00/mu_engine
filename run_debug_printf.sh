#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

# Enable Debug Printf in renderer desc
perl -0pi -e 's/\.enable_debug_printf\s*=\s*(true|false)/.enable_debug_printf             = true/' main.c

# Compile shaders
rm -f compiledshaders/triangle.vert.spv compiledshaders/triangle.frag.spv
SLANG_DEFINES="-DDEBUG_PRINTF=1" ./compileslang.sh

# Build app
make release
export VK_LAYER_PRINTF_BUFFER_SIZE=525312 
# Run with Debug Printf enabled in validation layers
export VK_LAYER_PRINTF_ONLY_PRESET=1
export VK_LAYER_PRINTF_TO_STDOUT=1

./build/app
