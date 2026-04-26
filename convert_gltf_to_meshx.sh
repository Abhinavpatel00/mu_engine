#!/bin/bash

# Convert all GLTF/GLB files in a folder to meshx format
# Usage: ./convert_gltf_to_meshx.sh <input_folder> [output_folder]

set -e

INPUT_FOLDER="${1:-.}"
OUTPUT_FOLDER="${2:-./gameassets}"

# Ensure output folder exists
mkdir -p "$OUTPUT_FOLDER"

# Check if meshx_converter exists
if [ ! -f "./build/meshx_converter" ]; then
    echo "Error: ./build/meshx_converter not found. Please build the project first."
    exit 1
fi

CONVERTER="./build/meshx_converter"
CONVERTED_COUNT=0
FAILED_COUNT=0
TEXTURES_COPIED=0
TEXTURES_MISSING=0

copy_meshx_textures() {
    local gltf_file="$1"
    local meshx_file="$2"
    local source_dir
    local output_dir

    source_dir="$(dirname "$gltf_file")"
    output_dir="$(dirname "$meshx_file")"

    while IFS= read -r texture_uri; do
        [ -z "$texture_uri" ] && continue
        [ "$texture_uri" = "__none__" ] && continue

        # Skip remote URIs; only copy local sidecar texture files.
        if [[ "$texture_uri" == *"://"* ]]; then
            echo "  - Skipping remote texture URI: $texture_uri"
            continue
        fi

        local src_tex
        local dst_tex
        if [[ "$texture_uri" == /* ]]; then
            src_tex="$texture_uri"
            dst_tex="$output_dir/$(basename "$texture_uri")"
        else
            src_tex="$source_dir/$texture_uri"
            dst_tex="$output_dir/$texture_uri"
        fi

        if [ -f "$src_tex" ]; then
            mkdir -p "$(dirname "$dst_tex")"
            cp -f "$src_tex" "$dst_tex"
            TEXTURES_COPIED=$((TEXTURES_COPIED + 1))
            echo "  - Copied texture: $texture_uri"
        else
            TEXTURES_MISSING=$((TEXTURES_MISSING + 1))
            echo "  - Missing texture: $texture_uri (expected at $src_tex)"
        fi
    done < <(grep -E '^[[:space:]]*(base_color_tex|normal_tex|orm_tex)[[:space:]]+"' "$meshx_file" | sed -E 's/^[[:space:]]*(base_color_tex|normal_tex|orm_tex)[[:space:]]+"([^"]+)".*$/\2/')
}

echo "Converting GLTF/GLB files from: $INPUT_FOLDER"
echo "Output folder: $OUTPUT_FOLDER"
echo ""

FOUND_ANY=0

# Find and convert all .glb and .gltf files (null-delimited for space-safe paths)
while IFS= read -r -d '' gltf_file; do
    FOUND_ANY=1
    # Get the basename without extension
    basename_no_ext=$(basename "$gltf_file" | sed 's/\.[^.]*$//')
    output_meshx="$OUTPUT_FOLDER/${basename_no_ext}.meshx"
    
    echo "Converting: $gltf_file -> $output_meshx"
    
    if "$CONVERTER" "$gltf_file" "$output_meshx"; then
        echo "  ✓ Success"
        CONVERTED_COUNT=$((CONVERTED_COUNT + 1))
        copy_meshx_textures "$gltf_file" "$output_meshx"
    else
        echo "  ✗ Failed"
        FAILED_COUNT=$((FAILED_COUNT + 1))
    fi
done < <(find "$INPUT_FOLDER" \( -iname "*.glb" -o -iname "*.gltf" \) -print0)

echo ""
echo "Conversion complete!"
echo "Converted: $CONVERTED_COUNT files"
echo "Copied textures: $TEXTURES_COPIED"
if [ $TEXTURES_MISSING -gt 0 ]; then
    echo "Missing textures: $TEXTURES_MISSING"
fi
if [ $FAILED_COUNT -gt 0 ]; then
    echo "Failed: $FAILED_COUNT files"
fi

if [ $FOUND_ANY -eq 0 ]; then
    echo "No .gltf/.glb files found in: $INPUT_FOLDER"
fi
