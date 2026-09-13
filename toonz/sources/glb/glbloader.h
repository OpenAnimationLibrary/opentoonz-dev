#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace otglb {

using Matrix = std::array<float, 16>;  // glTF column-major, right-handed, Y up.
constexpr int NoIndex = -1;

struct Bounds {
  std::array<float, 3> minimum{}, maximum{};
  bool empty = true;
};

struct Attribute {
  std::string semantic;
  int components = 0;
  std::vector<float> values;  // Normalized integers are expanded to floats.
};

struct Primitive {
  int mode = 4;  // glTF primitive mode (0..6); original topology is retained.
  int material = NoIndex;
  std::vector<Attribute> attributes;
  std::vector<std::uint32_t> indices;  // Empty for non-indexed geometry.
  std::size_t vertexCount = 0;
  std::size_t triangleCount = 0;
  bool hasMorphTargets = false;  // Base geometry only in this stage.
  Bounds bounds;
};

struct Mesh {
  std::string name;
  std::vector<Primitive> primitives;
  Bounds bounds;
};

struct Node {
  std::string name;
  int mesh = NoIndex;
  int parent = NoIndex;
  bool hasSkin = false;
  std::vector<int> children;
  Matrix local{}, world{};
};

struct Scene {
  std::string name;
  std::vector<int> roots;
  Bounds bounds;  // Bounds of base geometry, without skinning or animation.
};

struct TextureRef {
  int texture = NoIndex;
  int texcoord = 0;
  float strength = 1.0f;
  std::array<float, 2> offset{{0.0f, 0.0f}}, scale{{1.0f, 1.0f}};
  float rotation = 0.0f;
};

struct Material {
  std::string name;
  std::array<float, 4> baseColor{{1.0f, 1.0f, 1.0f, 1.0f}};
  std::array<float, 3> emissive{{0.0f, 0.0f, 0.0f}};
  float metallic = 1.0f, roughness = 1.0f, alphaCutoff = 0.5f;
  int alphaMode = 0;  // 0 opaque, 1 mask, 2 blend.
  bool doubleSided = false, unlit = false;
  TextureRef baseColorTexture, metallicRoughnessTexture, normalTexture;
  TextureRef occlusionTexture, emissiveTexture;
};

struct Image {
  std::string name, mimeType;
  std::vector<unsigned char> encoded;  // Retained bytes; no image decoding yet.
};

struct Texture {
  int image = NoIndex;
  int magFilter = 0, minFilter = 0;  // 0 means unspecified.
  int wrapS = 10497, wrapT = 10497;
};

struct Asset {
  std::vector<Mesh> meshes;
  std::vector<Node> nodes;
  std::vector<Scene> scenes;
  std::vector<Material> materials;
  std::vector<Image> images;
  std::vector<Texture> textures;
  int defaultScene = NoIndex;
  std::size_t animationCount = 0, skinCount = 0;
};

enum class Status { Loaded, LoadedWithWarnings, Invalid, Unsupported, IoError,
                    ResourceLimit };

struct Limits {
  std::size_t fileBytes = 1024ull * 1024 * 1024;
  std::size_t parserBytes = 256ull * 1024 * 1024;
  std::size_t decodedBytes = 1024ull * 1024 * 1024;
  std::size_t hierarchyDepth = 1024;
};

struct Result {
  Status status = Status::Invalid;
  std::shared_ptr<const Asset> asset;
  std::string error;
  std::vector<std::string> warnings;
  std::size_t fileBytes = 0, decodedBytes = 0, parserPeakBytes = 0;
  double loadMilliseconds = 0.0;
  explicit operator bool() const { return bool(asset); }
};

// Owns and validates all returned data. The file is opened read-only using the
// native filesystem path (including UTF-16 paths on Windows). Never follows
// external resource URIs. A failed result contains no partial asset.
Result load(const std::filesystem::path &path, const Limits &limits = Limits());
const char *statusName(Status status);

}  // namespace otglb
