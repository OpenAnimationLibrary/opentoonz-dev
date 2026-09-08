#!/usr/bin/env bash
set -euo pipefail

# FFmpeg n8.1.2. Keep matching sources and this recipe with the CI binary.
revision=38b88335f99e76ed89ff3c93f877fdefce736c13
mkdir -p recorder-encoder
curl --fail --location --retry 3 \
  "https://github.com/FFmpeg/FFmpeg/archive/${revision}.tar.gz" \
  -o recorder-encoder/ffmpeg-source.tar.gz
tar -xzf recorder-encoder/ffmpeg-source.tar.gz
cd "FFmpeg-${revision}"
./configure --disable-autodetect --disable-everything --disable-network \
  --disable-avdevice --disable-doc --disable-debug --disable-x86asm \
  --disable-shared --enable-static --enable-ffmpeg \
  --enable-encoder=mpeg4 --enable-decoder=rawvideo \
  --enable-demuxer=rawvideo --enable-muxer=mp4 \
  --enable-protocol=file,pipe --enable-filter=buffer,buffersink,format,scale,null \
  --enable-swscale --extra-ldflags=-static
make -j2
cp ffmpeg.exe COPYING.LGPLv2.1 LICENSE.md ../recorder-encoder/
cp ../.github/ot-dev/recorder/build-ffmpeg.sh ../recorder-encoder/
./ffmpeg.exe -buildconf > ../recorder-encoder/build-configuration.txt 2>&1
# Exercise exactly the recorder's raw-video -> MPEG-4/fragmented-MP4 pipeline.
dd if=/dev/zero bs=16384 count=24 2>/dev/null | \
  ./ffmpeg.exe -hide_banner -loglevel error -nostdin -n \
  -f rawvideo -pixel_format bgra -video_size 64x64 -framerate 12 -i pipe:0 \
  -an -c:v mpeg4 -q:v 3 -pix_fmt yuv420p -g 24 \
  -movflags +frag_keyframe+empty_moov+default_base_moof ../encoder-smoke.mp4
test -s ../encoder-smoke.mp4
