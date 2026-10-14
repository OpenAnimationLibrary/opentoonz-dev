#pragma once

#include "lut_fit.h"

#include <iosfwd>
#include <string>

namespace otlut {

enum class LutFormat {
  Cube,
  ThreeDl,
};

struct LutWriteOptions {
  int outputBitDepth = 12;
  std::string title  = "otlut LUT";
};

using IdentityLutOptions = LutWriteOptions;

bool isSupportedCubeSize(int size);
bool isSupportedThreeDlSize(int size);
int threeDlInputBitDepthForSize(int size);

void writeCube(std::ostream& output, const Lut3D& lut,
               const LutWriteOptions& options = {});
void writeThreeDl(std::ostream& output, const Lut3D& lut,
                  const LutWriteOptions& options = {});

void writeIdentityCube(std::ostream& output, int size,
                       const LutWriteOptions& options = {});
void writeIdentityThreeDl(std::ostream& output, int size,
                          const LutWriteOptions& options = {});

LutFormat formatFromPath(const std::string& path);

}  // namespace otlut
