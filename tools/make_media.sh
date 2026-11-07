#!/bin/sh
# Regenerates every image/video in assets/ from the real renderer.
# Needs: a C compiler, zlib, python3, ffmpeg.
set -e
cd "$(dirname "$0")/.."
python3 tools/gen_icon.py
make clean >/dev/null
make notecraft-demo
mkdir -p assets/screenshots
for s in book cyrillic selection menu sign read files; do
    ./notecraft-demo shot "assets/screenshots/$s.png" "$s" 3
done
./notecraft-demo banner assets/icon-32.png assets/banner.png 4
FRAMES="$(mktemp -d)"
./notecraft-demo frames "$FRAMES" 2
ffmpeg -y -loglevel error -framerate 20 -i "$FRAMES/frame_%04d.png" \
    -vf "scale=640:-1:flags=neighbor,split[a][b];[a]palettegen=max_colors=64:stats_mode=full[p];[b][p]paletteuse=dither=none" \
    -loop 0 assets/demo.gif
ffmpeg -y -loglevel error -framerate 20 -i "$FRAMES/frame_%04d.png" \
    -c:v libx264 -pix_fmt yuv420p -crf 18 -vf "scale=1280:-2:flags=neighbor" assets/demo.mp4
rm -rf "$FRAMES"
echo "assets regenerated"
