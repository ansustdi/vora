#!/usr/bin/env bash
# Build a portable folder (dist/Vora) on Windows.
# Run from an MSYS2 UCRT64 shell in the project root:  ./deploy/windows-deploy.sh
set -euo pipefail

BUILD=build
DIST=dist/Vora

cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"

rm -rf "$DIST"
mkdir -p "$DIST"
cp "$BUILD/vora.exe" "$DIST/"

# Qt plugins (platforms, imageformats, styles, tls...)
WINDEPLOY=$(command -v windeployqt6 || command -v windeployqt-qt6 || command -v windeployqt)
"$WINDEPLOY" --release --no-translations --no-system-d3d-compiler --no-opengl-sw \
    --dir "$DIST" "$DIST/vora.exe"

# Everything else the exe and the Qt plugins link from the MSYS2 prefix (libmpv, ffmpeg, exiv2...)
copy_deps() {
    ldd "$1" | awk '/\/ucrt64\// {print $3}' | while read -r dll; do
        base=$(basename "$dll")
        if [ ! -f "$DIST/$base" ]; then
            cp "$dll" "$DIST/"
            copy_deps "$dll"
        fi
    done
}
copy_deps "$DIST/vora.exe"

# ffmpeg.exe: yt-dlp uses it to merge YouTube video+audio and to convert to MP3.
# Shares the FFmpeg DLLs already copied for libmpv, so it adds very little.
cp /ucrt64/bin/ffmpeg.exe "$DIST/"
copy_deps "$DIST/ffmpeg.exe"
find "$DIST" -name '*.dll' -path '*/*/*' | while read -r plugin; do copy_deps "$plugin"; done

strip "$DIST"/*.exe "$DIST"/*.dll 2>/dev/null || true
du -sh "$DIST"
echo "Portable build ready: $DIST"
