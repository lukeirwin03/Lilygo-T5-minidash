#!/usr/bin/env bash
# Convert TrueType fonts in fonts_tff/ into Adafruit GFX `GFXfont` C headers
# under include/fonts/, using the fontconvert tool shipped with the Adafruit
# GFX Library. Idempotent — safe to re-run.
#
# Requirements (auto-detected):
#   - gcc, freetype2 headers + libfreetype (to build fontconvert)
#   - Run `pio run` once first so the Adafruit GFX Library is downloaded
#     into .pio/libdeps/<env>/ (its fontconvert/ sources are compiled here).
#
# Usage: ./convert_fonts.sh
set -euo pipefail

# Always run from the script's directory (project root) regardless of CWD.
cd "$(dirname "$0")"

FONTS_DIR="fonts_tff"
OUTDIR="include/fonts"
BUILD_DIR="/tmp/opencode/fontconvert-build"

# ---------------------------------------------------------------------------
# Locate the Adafruit GFX fontconvert source. The dependency path contains a
# space, so quote carefully. Build it to BUILD_DIR (NOT inside the vendored
# library) so we never modify the Adafruit library tree.
# ---------------------------------------------------------------------------
shopt -s nullglob
sources=(.pio/libdeps/*/Adafruit\ GFX\ Library/fontconvert/fontconvert.c)
shopt -u nullglob

if [ "${#sources[@]}" -eq 0 ]; then
    echo "Error: Adafruit GFX fontconvert source not found under" >&2
    echo "       .pio/libdeps/*/Adafruit GFX Library/fontconvert/." >&2
    echo "       Run 'pio run' first to download the Adafruit GFX Library dependency." >&2
    exit 1
fi
FONTCONVERT_SRC="${sources[0]}"

mkdir -p "$BUILD_DIR" "$OUTDIR"
FONTCONVERT="${BUILD_DIR}/fontconvert"

# (Re)build fontconvert if missing or older than its source.
if [ ! -x "$FONTCONVERT" ] || [ "$FONTCONVERT_SRC" -nt "$FONTCONVERT" ]; then
    echo "Compiling fontconvert ($FONTCONVERT_SRC)..."
    gcc -Wall -I/usr/include/freetype2 "$FONTCONVERT_SRC" -lfreetype -lm \
        -o "$FONTCONVERT"
fi

# convert <NAME> <SIZE> <SRC_TTF>
#
# Runs fontconvert on <SRC_TTF> at <SIZE> and writes
# ${OUTDIR}/${NAME}${SIZE}pt7b.h atomically (tmp + mv), so a failed run can
# never leave a half-written header.
#
# fontconvert derives the C symbol from the TTF *basename*: it strips the
# extension, appends "<size>pt7b", and replaces any space/punctuation char
# with '_'. The DS-DIGI.TTF basename would yield "DS_DIGI<size>pt7b"
# (underscore). To get a clean symbol matching the file name we want
# ("DSDIGI<size>pt7b", no separator), we copy the TTF to a clean basename
# "${NAME}.ttf" and feed that to fontconvert. This avoids any fragile sed
# rewriting of the generated symbol.
convert() {
    local name="$1" size="$2" src_ttf="$3"
    local clean_ttf="${BUILD_DIR}/${name}.ttf"
    local out="${OUTDIR}/${name}${size}pt7b.h"
    local tmp="${out}.tmp"

    if [ ! -f "$src_ttf" ]; then
        echo "  FAIL: $name (${size}pt) — source TTF not found: $src_ttf" >&2
        return 1
    fi

    cp "$src_ttf" "$clean_ttf"

    # Prepend the Adafruit GFX preamble so the header matches the format of
    # the stock Adafruit font headers (e.g. FreeSansBold9pt7b.h) and is
    # self-contained for the firmware build.
    if ! {
            printf '%s\n' '#pragma once'
            printf '%s\n' '#include <Adafruit_GFX.h>'
            printf '\n'
            "$FONTCONVERT" "$clean_ttf" "$size"
        } > "$tmp"; then
        rm -f "$tmp"
        echo "  FAIL: $name (${size}pt) — converter error" >&2
        return 1
    fi

    mv "$tmp" "$out"
    echo "  OK:   $name (${size}pt)"
}

echo "Converting fonts..."

# NAME:SIZE pairs. NAME is both the C symbol base and the output file base;
# the source TTF for each pair is passed explicitly to convert().
FONTS=(
    "DSDIGI:10"
    "DSDIGI:12"
    "DSDIGI:14"
    "DSDIGI:18"
    "DSDIGI:24"
    "DSDIGI:32"
    "DSDIGI:48"
)

rc=0
for entry in "${FONTS[@]}"; do
    name="${entry%%:*}"
    size="${entry##*:}"
    convert "$name" "$size" "${FONTS_DIR}/DS-DIGI.TTF" || rc=1
done

# TODO(bold): enable the bold variant once fonts_tff/DS-DIGIB.TTF is added.
# Uncomment this block (and drop the file into fonts_tff/) to generate the
# DSDIGIB<size>pt7b headers. The converter derives clean symbols
# "DSDIGIB<size>pt7b" via the same clean-basename trick used above.
#
# for entry in "DSDIGIB:14" "DSDIGIB:24" "DSDIGIB:32" "DSDIGIB:48"; do
#     name="${entry%%:*}"
#     size="${entry##*:}"
#     convert "$name" "$size" "${FONTS_DIR}/DS-DIGIB.TTF" || rc=1
# done

if [ "$rc" -ne 0 ]; then
    echo >&2
    echo "One or more fonts failed to convert (see FAIL lines above)." >&2
    exit 1
fi

echo
echo "Done. 'git add ${OUTDIR}/*.h' to track the generated headers."
