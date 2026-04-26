#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# convert_assets.sh
#
# Convert all .gltf/.glb files in INPUT_FOLDER into .meshx files in OUTPUT_FOLDER
#
# Usage:
#   ./convert_assets.sh [input_folder] [output_folder]
#
# Examples:
#   ./convert_assets.sh
#   ./convert_assets.sh ./assets
#   ./convert_assets.sh ./assets ./gameassets
#
# -----------------------------------------------------------------------------
#
# VISUAL FLOW
#
#   input folder
#       |
#       v
#   find *.gltf / *.glb
#       |
#       v
#   meshx_converter
#       |
#       v
#   output .meshx
#       |
#       v
#   scan meshx for texture refs
#       |
#       v
#   copy textures beside output
#
# This script exists because manually converting assets one-by-one is how people
# waste entire afternoons and then call it "pipeline work".
# -----------------------------------------------------------------------------

INPUT_FOLDER="${1:-.}"
OUTPUT_FOLDER="${2:-./gameassets}"

# Normalize paths so relative nonsense does not become future suffering
INPUT_FOLDER="$(realpath "$INPUT_FOLDER")"
OUTPUT_FOLDER="$(realpath -m "$OUTPUT_FOLDER")"

mkdir -p "$OUTPUT_FOLDER"

CONVERTER="./build/meshx_converter"

if [ ! -x "$CONVERTER" ]; then
    echo "Error: $CONVERTER not found or not executable. Build the project first."
    exit 1
fi

CONVERTED_COUNT=0
FAILED_COUNT=0
TEXTURES_COPIED=0
TEXTURES_MISSING=0
FOUND_ANY=0

copy_meshx_textures() {
    local gltf_file="$1"
    local meshx_file="$2"
    local source_dir output_dir

    source_dir="$(dirname "$gltf_file")"
    output_dir="$(dirname "$meshx_file")"

    while IFS= read -r texture_uri; do
        [ -z "$texture_uri" ] && continue
        [ "$texture_uri" = "__none__" ] && continue

        if [[ "$texture_uri" == *"://"* ]]; then
            echo "  - Skipping remote texture URI: $texture_uri"
            continue
        fi

        local src_tex dst_tex

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
    done < <(
        grep -E '^[[:space:]]*(base_color_tex|normal_tex|orm_tex)[[:space:]]+"' "$meshx_file" \
        | sed -E 's/^[[:space:]]*(base_color_tex|normal_tex|orm_tex)[[:space:]]+"([^"]+)".*$/\2/'
    )
}

echo "Converting GLTF/GLB files from: $INPUT_FOLDER"
echo "Output folder: $OUTPUT_FOLDER"
echo ""

while IFS= read -r -d '' gltf_file; do
    FOUND_ANY=1

    # Preserve relative folder structure inside output
    rel_path="${gltf_file#$INPUT_FOLDER/}"
    rel_dir="$(dirname "$rel_path")"
    base_name="$(basename "$gltf_file" | sed 's/\.[^.]*$//')"

    mkdir -p "$OUTPUT_FOLDER/$rel_dir"
    output_meshx="$OUTPUT_FOLDER/$rel_dir/${base_name}.meshx"

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

if [ "$TEXTURES_MISSING" -gt 0 ]; then
    echo "Missing textures: $TEXTURES_MISSING"
fi

if [ "$FAILED_COUNT" -gt 0 ]; then
    echo "Failed: $FAILED_COUNT files"
fi

if [ "$FOUND_ANY" -eq 0 ]; then
    echo "No .gltf/.glb files found in: $INPUT_FOLDER"
fi


