#include "image_loader.h"

#define STBI_WINDOWS_UTF8
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdexcept>

namespace otlut {

Image loadImage(const std::string& path) {
  int width    = 0;
  int height   = 0;
  int channels = 0;

  stbi_uc* pixels =
      stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb);
  if (!pixels) {
    const char* reason = stbi_failure_reason();
    throw std::runtime_error("Could not load image '" + path +
                             "': " + (reason ? reason : "unknown error"));
  }

  Image image;
  image.width  = width;
  image.height = height;
  image.rgb.resize(static_cast<size_t>(width) * height * 3);

  for (size_t i = 0; i < image.rgb.size(); ++i) {
    image.rgb[i] = static_cast<float>(pixels[i]) / 255.0f;
  }

  stbi_image_free(pixels);
  return image;
}

}  // namespace otlut
