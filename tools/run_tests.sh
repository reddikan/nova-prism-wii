#!/bin/sh
# Builds and runs the native test harness (no Wii toolchain needed). Screenshots go to tools/out/.
#   sh tools/run_tests.sh          unit tests, bot playthroughs, balance stats, performance, screenshots
#   sh tools/run_tests.sh reel     also renders tools/out/reel/preview.mp4 (needs ffmpeg)
set -e
cd "$(dirname "$0")/.."
mkdir -p tools/out/reel
cc -O2 -std=gnu11 -Wall -Wno-misleading-indentation source/game.c source/draw.c source/levels.c source/gfx.c source/audio.c tools/test.c -lm -o tools/out/ptest
tools/out/ptest tools/out
python3 -c "
import glob, os
from PIL import Image
for p in glob.glob('tools/out/*.ppm'):
    Image.open(p).save(p[:-4] + '.png'); os.remove(p)
" 2>/dev/null || echo "(PNG export skipped: needs python3 + Pillow; the .ppm files are still there)"
if [ "$1" = "reel" ]; then
    tools/out/ptest tools/out reel
    ffmpeg -loglevel error -y -i tools/out/reel/video.mp4 -i tools/out/reel/audio.wav -c:v copy -c:a aac -shortest tools/out/reel/preview.mp4
    echo "preview: tools/out/reel/preview.mp4"
fi
