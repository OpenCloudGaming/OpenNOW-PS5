#!/usr/bin/env bash
# Run in the VPS GPU builder (FFmpeg); original test patterns only.
set -euo pipefail
cd "$(dirname "$0")/../.."
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=3840x2160:rate=120 -frames:v 1 -pix_fmt yuv420p10le -c:v libx265 -profile:v main10 -preset ultrafast -x265-params 'pools=2:frame-threads=2:aud=1:repeat-headers=1:keyint=120:bframes=0:level-idc=5.2:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:range=limited:info=0:log-level=error' -color_primaries bt2020 -color_trc smpte2084 -colorspace bt2020nc -color_range tv -f hevc -y assets/hdr-check.hevc
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=3840x2160:rate=120 -frames:v 1 -pix_fmt yuv420p10le -c:v libx265 -profile:v main10 -preset ultrafast -x265-params 'pools=2:frame-threads=2:aud=1:repeat-headers=1:keyint=120:bframes=0:level-idc=5.2:colorprim=bt709:transfer=bt709:colormatrix=bt709:range=limited:info=0:log-level=error' -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv -f hevc -y assets/sdr-main10-check.hevc
for spec in '3840x2160 120 6.0 4k' '1920x1080 60 5.1 1080'; do
 read -r size fps level name <<< "$spec"
 ffmpeg -hide_banner -loglevel error -f lavfi -i "testsrc2=size=$size:rate=$fps" -frames:v 1 -c:v libx264 -preset ultrafast -profile:v high -level:v "$level" -x264-params 'threads=2:aud=1:repeat-headers=1:keyint=120:bframes=0:colorprim=bt709:transfer=bt709:colormatrix=bt709' -f h264 -y "assets/$name-check.h264"
done
