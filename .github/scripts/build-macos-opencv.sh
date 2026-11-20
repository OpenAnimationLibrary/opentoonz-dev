#!/usr/bin/env bash
set -euo pipefail

# Build only the OpenCV modules used by OpenToonz (plus their dependencies).
# Qt/VTK GUI modules are deliberately excluded from the Qt 5 application.
: "${OPENCV_VERSION:?}"
: "${OPENCV_SHA256:?}"
: "${OPENCV_ROOT:?}"
: "${RUNNER_TEMP:?}"

work_dir="${RUNNER_TEMP}/opencv-source"
mkdir -p "$work_dir"
archive="${work_dir}/opencv.tar.gz"
curl --fail --location --retry 3 \
  "https://github.com/opencv/opencv/archive/refs/tags/${OPENCV_VERSION}.tar.gz" \
  --output "$archive"
echo "${OPENCV_SHA256}  ${archive}" | shasum -a 256 --check
tar -xzf "$archive" -C "$work_dir"

cmake -S "${work_dir}/opencv-${OPENCV_VERSION}" -B "${work_dir}/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$OPENCV_ROOT" \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DBUILD_LIST=core,imgproc,imgcodecs,videoio,calib,geometry \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF \
  -DBUILD_opencv_apps=OFF \
  -DWITH_QT=OFF -DWITH_VTK=OFF \
  -DWITH_AVFOUNDATION=ON \
  -DWITH_FFMPEG=OFF -DWITH_GSTREAMER=OFF
cmake --build "${work_dir}/build" --parallel "$(sysctl -n hw.ncpu)"
cmake --install "${work_dir}/build"
