#include "glbloader.h"

#include <iomanip>
#include <iostream>

namespace {
void quoted(const std::string &value) {
  std::cout << '"';
  for (unsigned char c : value) {
    switch (c) {
    case '"': std::cout << "\\\""; break;
    case '\\': std::cout << "\\\\"; break;
    case '\n': std::cout << "\\n"; break;
    case '\r': std::cout << "\\r"; break;
    case '\t': std::cout << "\\t"; break;
    default:
      if (c < 32)
        std::cout << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                  << int(c) << std::dec << std::setfill(' ');
      else
        std::cout << c;
    }
  }
  std::cout << '"';
}

void bounds(const otglb::Bounds &value) {
  if (value.empty) {
    std::cout << "null";
    return;
  }
  std::cout << "{\"minimum\":[" << value.minimum[0] << ',' << value.minimum[1]
            << ',' << value.minimum[2] << "],\"maximum\":[" << value.maximum[0]
            << ',' << value.maximum[1] << ',' << value.maximum[2] << "]}";
}

int inspect(const std::filesystem::path &path) {
  const auto result = otglb::load(path);
  std::cout << std::setprecision(9) << "{\n  \"status\": ";
  quoted(otglb::statusName(result.status));
  std::cout << ",\n  \"error\": ";
  quoted(result.error);
  std::cout << ",\n  \"warnings\": [";
  for (std::size_t i = 0; i < result.warnings.size(); ++i) {
    if (i) std::cout << ',';
    quoted(result.warnings[i]);
  }
  std::cout << "],\n  \"fileBytes\": " << result.fileBytes
            << ",\n  \"decodedBytes\": " << result.decodedBytes
            << ",\n  \"parserPeakBytes\": " << result.parserPeakBytes
            << ",\n  \"loadMilliseconds\": " << result.loadMilliseconds;
  if (result) {
    const auto &asset = *result.asset;
    std::size_t vertices = 0, triangles = 0, primitives = 0;
    for (const auto &mesh : asset.meshes)
      for (const auto &primitive : mesh.primitives) {
        vertices += primitive.vertexCount;
        triangles += primitive.triangleCount;
        ++primitives;
      }
    std::cout << ",\n  \"meshes\": " << asset.meshes.size()
              << ",\n  \"primitives\": " << primitives
              << ",\n  \"vertices\": " << vertices
              << ",\n  \"triangles\": " << triangles
              << ",\n  \"nodes\": " << asset.nodes.size()
              << ",\n  \"materials\": " << asset.materials.size()
              << ",\n  \"embeddedImages\": " << asset.images.size()
              << ",\n  \"textures\": " << asset.textures.size()
              << ",\n  \"animations\": " << asset.animationCount
              << ",\n  \"skins\": " << asset.skinCount
              << ",\n  \"defaultScene\": " << asset.defaultScene
              << ",\n  \"scenes\": [";
    for (std::size_t i = 0; i < asset.scenes.size(); ++i) {
      if (i) std::cout << ',';
      std::cout << "{\"name\":";
      quoted(asset.scenes[i].name);
      std::cout << ",\"bounds\":";
      bounds(asset.scenes[i].bounds);
      std::cout << '}';
    }
    std::cout << ']';
  }
  std::cout << "\n}\n";
  // Loaded-with-warnings is a usable inspection result, not a parse failure.
  return result ? 0 : 1;
}
}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
  if (argc != 2) {
    std::cerr << "Usage: glb_inspect <model.glb>\n"
                 "Read-only inspection; writes a JSON report to stdout.\n";
    return 2;
  }
  try {
    return inspect(std::filesystem::path(argv[1]));
  } catch (const std::exception &error) {
    std::cerr << "Inspection failed: " << error.what() << '\n';
    return 1;
  }
}
