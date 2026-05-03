#!/usr/bin/env bash
# Capture a single snapshot from the IMX708 via the Argus pipeline.
# Argus needs ~10 frames for AE/AWB to converge, so we capture a burst
# and keep the last image.
#
# Usage:
#   ./snapshot.sh                        # default: 2304x1296, saves snap.jpg
#   ./snapshot.sh myshot.jpg
#   ./snapshot.sh fullres.jpg 4608 2592  # full 12 MP

set -euo pipefail

OUT="${1:-snap.jpg}"
WIDTH="${2:-2304}"
HEIGHT="${3:-1296}"
FPS="${4:-30}"
SENSOR_ID="${5:-0}"

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

echo "Capturing 20 frames at ${WIDTH}x${HEIGHT}@${FPS}fps..."

gst-launch-1.0 -e nvarguscamerasrc num-buffers=20 sensor-id="$SENSOR_ID" \
    wbmode=5 awblock=true \
    exposuretimerange="8000000 16000000" \
    gainrange="1 4" ispdigitalgainrange="1 1" ! \
    "video/x-raw(memory:NVMM),width=${WIDTH},height=${HEIGHT},framerate=${FPS}/1" ! \
    nvvidconv ! "video/x-raw,format=I420" ! \
    jpegenc quality=95 ! \
    multifilesink location="${TMPDIR}/frame_%03d.jpg"

LAST="$(ls "$TMPDIR"/frame_*.jpg | tail -1)"
cp "$LAST" "$OUT"
echo "Saved: $OUT"

#    Make it executable after creating:
#    in bash
#    chmod +x examples/snapshot.sh
