#!/usr/bin/env bash
set -euo pipefail

# FFmpeg n8.1.2. Keep matching sources and this recipe with the CI binary.
revision=38b88335f99e76ed89ff3c93f877fdefce736c13
# Pinned x264 stable. Only the standalone encoder links this GPL library;
# OpenToonz sends it raw frames over a pipe. Ship both corresponding sources.
x264_revision=31e19f92f00c7003fa115047ce50978bc98c3a0d
mkdir -p recorder-encoder
curl --fail --location --retry 3 \
  "https://github.com/mirror/x264/archive/${x264_revision}.tar.gz" \
  -o recorder-encoder/x264-source.tar.gz
tar -xzf recorder-encoder/x264-source.tar.gz
encoder_prefix="$(pwd)/recorder-codecs"
cd "x264-${x264_revision}"
./configure --prefix="$encoder_prefix" --enable-static --disable-cli \
  --disable-opencl --bit-depth=8 --chroma-format=420
make -j2
make install
cp COPYING ../recorder-encoder/COPYING.x264
cd ..
curl --fail --location --retry 3 \
  "https://github.com/FFmpeg/FFmpeg/archive/${revision}.tar.gz" \
  -o recorder-encoder/ffmpeg-source.tar.gz
tar -xzf recorder-encoder/ffmpeg-source.tar.gz
cd "FFmpeg-${revision}"
export PKG_CONFIG_PATH="$encoder_prefix/lib/pkgconfig"
./configure --disable-autodetect --disable-everything --disable-network \
  --disable-avdevice --disable-doc --disable-debug --disable-x86asm \
  --disable-shared --enable-static --enable-ffmpeg \
  --enable-gpl --enable-libx264 --enable-encoder=libx264 --enable-decoder=rawvideo \
  --enable-demuxer=rawvideo --enable-muxer=mp4 \
  --enable-protocol=file,pipe --enable-filter=buffer,buffersink,format,scale,null \
  --enable-swscale --pkg-config-flags=--static --extra-ldflags=-static
make -j2
cp ffmpeg.exe COPYING.GPLv2 COPYING.LGPLv2.1 LICENSE.md ../recorder-encoder/
cp ../.github/ot-dev/recorder/build-ffmpeg.sh ../recorder-encoder/
./ffmpeg.exe -buildconf > ../recorder-encoder/build-configuration.txt 2>&1
# Exercise the raw-video -> H.264/hybrid-MP4 pipeline including EOF finalization.
dd if=/dev/zero bs=16384 count=24 2>/dev/null | \
  ./ffmpeg.exe -hide_banner -loglevel error -nostdin -n \
  -f rawvideo -pixel_format bgra -video_size 64x64 -framerate 12 -i pipe:0 \
  -an -sws_flags bicubic+accurate_rnd -c:v libx264 -preset ultrafast \
  -tune zerolatency -crf 18 -profile:v baseline -level:v 4.0 -pix_fmt yuv420p \
  -maxrate 20M -bufsize 20M -threads 2 -g 24 -flush_packets 1 \
  -movflags +hybrid_fragmented+frag_keyframe+empty_moov+default_base_moof \
  ../encoder-smoke.mp4
test -s ../encoder-smoke.mp4

# A separate test-only decoder verifies actual pixels in recorder_test, rather
# than just the MP4 container. Never copy this executable into the application.
# Disable all assembly here to provide a reference conversion independent of the
# encoder's partially enabled x86 scaler. Reuse the same pinned source archive.
make distclean
./configure --disable-autodetect --disable-everything --disable-network \
  --disable-avdevice --disable-doc --disable-debug --disable-asm \
  --disable-shared --enable-static --enable-ffmpeg \
  --enable-decoder=h264 --enable-parser=h264 --enable-demuxer=mov \
  --enable-encoder=rawvideo --enable-muxer=rawvideo \
  --enable-protocol=file,pipe --enable-filter=buffer,buffersink,format,scale,null \
  --enable-swscale --extra-ldflags=-static
make -j2
mkdir -p ../recorder-test-decoder
cp ffmpeg.exe ../recorder-test-decoder/
