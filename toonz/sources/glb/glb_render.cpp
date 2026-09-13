#include "glbrenderer.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
int render(const std::filesystem::path &model, const std::filesystem::path &output,
           double height, double distance) {
  // Never overwrite a model or any existing output, including symlink targets.
  if (std::filesystem::exists(output))
    throw std::runtime_error("Output already exists; choose a new .tga filename.");
  const auto loaded = otglb::load(model);
  if (!loaded) throw std::runtime_error(loaded.error);
  for (const auto &warning : loaded.warnings) std::cerr << warning << '\n';
  otglb::RenderOptions options;
  options.headlight = true; options.orthoHeight = height; options.cameraDistance = distance;
  const auto scene = otglb::prepareRender(*loaded.asset, options);
  for (const auto &warning : scene.warnings) std::cerr << warning << '\n';
  if (scene.triangles.empty()) throw std::runtime_error("No visible triangle surfaces; check the camera and model.");
  otglb::RenderTile tile;
  tile.width = tile.height = 512;
  tile.affine = {{.512, 0, 256, 0, .512, 256}};
  const auto pixels = otglb::renderTile(scene, tile);
  // Uncompressed 32-bit TGA, top-left origin, straight alpha. The renderer and
  // OpenToonz use premultiplied alpha and bottom-up rows internally.
  unsigned char header[18]{};
  header[2] = 2; header[13] = header[15] = 2; header[16] = 32; header[17] = 0x28;
  std::ofstream file(output, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot create output image.");
  file.write(reinterpret_cast<const char *>(header), sizeof(header));
  for (int y = 511; y >= 0; --y) for (int x = 0; x < 512; ++x) {
    const auto &p = pixels[std::size_t(y) * 512 + x];
    const auto gray = static_cast<unsigned char>(p.alpha > 0 ? std::lround(p.gray / p.alpha * 255) : 0);
    const unsigned char rgba[] = {gray, gray, gray, static_cast<unsigned char>(std::lround(p.alpha * 255))};
    file.write(reinterpret_cast<const char *>(rgba), sizeof(rgba));
  }
  if (!file) throw std::runtime_error("Cannot finish writing output image.");
  std::cout << "Rendered " << scene.triangles.size() << " clipped triangles to 512x512 TGA.\n";
  return 0;
}
}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
  if (argc < 3 || argc > 5) {
    std::cerr << "Usage: glb_render <model.glb> <new-image.tga> [orthographic-height=10] [camera-distance=10]\n";
    return 2;
  }
  try {
    const double height = argc >= 4 ? std::stod(argv[3]) : 10;
    const double distance = argc >= 5 ? std::stod(argv[4]) : 10;
    return render(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]), height, distance);
  } catch (const std::exception &e) {
    std::cerr << "GLB render failed: " << e.what() << '\n'; return 1;
  }
}
