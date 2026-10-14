#pragma once

#include <string>
#include <vector>

namespace otlut {

struct Image {
  int width  = 0;
  int height = 0;
  std::vector<float> rgb;
};

Image loadImage(const std::string& path);

}  // namespace otlut
