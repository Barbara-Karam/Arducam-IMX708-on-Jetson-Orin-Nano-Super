#!/usr/bin/env bash
# Record an MP4 clip from the IMX708.
# Encodes with software x264 because Orin Nano has no NVENC.
#
# Usage:
#   ./record_1080p.sh                  # default: 10 sec at 30 fps
#   ./record_1080p.sh clip.mp4 600     # 20 sec (600 frames at 30 fps)

set -euo pipefail

OUT="${1:-clip.mp4}"
NUM_BUFFERS="${2:-300}"
SENSOR_ID="${3:-0}"

echo "Recording $NUM_BUFFERS frames to $OUT..."

gst-launch-1.0 -e nvarguscamerasrc num-buffers="$NUM_BUFFERS" sensor-id="$SENSOR_ID" \
    wbmode=5 awblock=true \
    exposuretimerange="8000000 16000000" \
    gainrange="1 4" ispdigitalgainrange="1 1" ! \
    "video/x-raw(memory:NVMM),width=2304,height=1296,framerate=30/1" ! \
    nvvidconv ! "video/x-raw,format=I420,width=1920,height=1080" ! \
    x264enc bitrate=6000 speed-preset=ultrafast tune=zerolatency ! \
    h264parse ! mp4mux ! filesink location="$OUT"

echo "Saved: $OUT ($(du -h "$OUT" | cut -f1))"

#  Make it executable:
#  in bash terminal:
#  chmod +x examples/record_1080p.sh
