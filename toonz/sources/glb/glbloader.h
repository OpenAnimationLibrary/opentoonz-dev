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
  bool hasSkin = false;  // Compatibility mirror of skin != NoIndex.
  std::vector<int> children;
  Matrix local{}, world{};
  int skin = NoIndex;
  bool hasMatrix = false;
  // Authored local components, with glTF defaults for omitted components.
  // Matrix-authored nodes retain their matrix; these defaults are not a
  // decomposition. Static local/world matrices and rendering remain unchanged.
  std::array<float, 3> translation{{0.0f, 0.0f, 0.0f}};
  std::array<float, 4> rotation{{0.0f, 0.0f, 0.0f, 1.0f}};  // XYZW
  std::array<float, 3> scale{{1.0f, 1.0f, 1.0f}};
};

struct Skin {
  std::string name;
  int skeleton = NoIndex;
  std::vector<int> joints;  // Node indices; JOINTS_n indexes this table.
  bool hasInverseBindMatrices = false;
  // All supplied MAT4s in source order (at least joints.size()), or one
  // identity per joint when absent. Never transpose or apply these on load.
  std::vector<Matrix> inverseBindMatrices;
};

enum class AnimationInterpolation { Linear, Step, CubicSpline };
enum class AnimationPath { Unknown, Translation, Rotation, Scale, Weights };

struct AnimationSampler {
  AnimationInterpolation interpolation = AnimationInterpolation::Linear;
  int outputComponents = 0;  // Components per source accessor element.
  std::vector<float> times;  // Seconds, unchanged; includes the final key.
  // Flattened accessor data. CUBICSPLINE keeps in-tangent/value/out-tangent
  // groups. Normalized integers are decoded; no resampling/normalization.
  std::vector<float> values;
};

struct AnimationChannel {
  int sampler = NoIndex;  // Index within this animation, not the asset.
  int node = NoIndex;     // NoIndex/Unknown channels are retained but ignored.
  AnimationPath path = AnimationPath::Unknown;
  std::size_t components = 0;  // 3 TRS, 4 rotation, N morph weights; 0 unknown.
};

struct Animation {
  std::string name;
  std::vector<AnimationSampler> samplers;
  std::vector<AnimationChannel> channels;
  // Range across all stored samplers, including unused samplers. Not a
  // playback trim: glTF clip time starts at zero even if the first key is later.
  float firstKeyTime = 0.0f, lastKeyTime = 0.0f;
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
  // Compatibility counts, derived from the owned vectors below.
  std::size_t animationCount = 0, skinCount = 0;
  std::vector<Skin> skins;
  std::vector<Animation> animations;
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
const char *interpolationName(AnimationInterpolation interpolation);
const char *animationPathName(AnimationPath path);

}  // namespace otglb
