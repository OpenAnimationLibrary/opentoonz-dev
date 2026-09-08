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
  -an -sws_flags bicubic+accurate_rnd -c:v mpeg4 -q:v 3 -pix_fmt yuv420p -g 24 \
  -movflags +frag_keyframe+empty_moov+default_base_moof ../encoder-smoke.mp4
test -s ../encoder-smoke.mp4

# A separate test-only decoder verifies actual pixels in recorder_test, rather
# than just the MP4 container. Never copy this executable into the application.
# Disable all assembly here to provide a reference conversion independent of the
# encoder's partially enabled x86 scaler. Reuse the same pinned source archive.
make distclean
./configure --disable-autodetect --disable-everything --disable-network \
  --disable-avdevice --disable-doc --disable-debug --disable-asm \
  --disable-shared --enable-static --enable-ffmpeg \
  --enable-decoder=mpeg4 --enable-demuxer=mov \
  --enable-encoder=rawvideo --enable-muxer=rawvideo \
  --enable-protocol=file,pipe --enable-filter=buffer,buffersink,format,scale,null \
  --enable-swscale --extra-ldflags=-static
make -j2
mkdir -p ../recorder-test-decoder
cp ffmpeg.exe ../recorder-test-decoder/
