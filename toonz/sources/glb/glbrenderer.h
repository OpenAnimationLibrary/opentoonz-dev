#pragma once

#include "glbloader.h"

namespace otglb {

// A fixed image plane keeps projection independent of preview zoom, output
// resolution and tile size. The FX applies the normal OpenToonz 2D affine.
constexpr double ProjectionHeight = 1000.0;

enum class MaterialShader { Source, MetallicRoughness, Matcap, Normal };

struct MaterialRig {
  MaterialShader shader = MaterialShader::Source;
  double metallicScale = 1.0;
  double roughnessScale = 1.0;
  double exposure = 1.0;

  bool operator==(const MaterialRig &other) const {
    return shader == other.shader && metallicScale == other.metallicScale &&
           roughnessScale == other.roughnessScale && exposure == other.exposure;
  }
};

struct DirectionalLight {
  // Camera-space direction from the shaded surface toward the light.
  std::array<double, 3> direction{{0.0, 0.0, 1.0}};
  // Display-referred sRGB light color in [0,1].
  std::array<float, 3> color{{1.0f, 1.0f, 1.0f}};
  double intensity = 1.0;

  bool operator==(const DirectionalLight &other) const {
    return direction == other.direction && color == other.color &&
           intensity == other.intensity;
  }
};

struct LightingRig {
  // Ambient and master are linear-light multipliers. Directional colors are
  // converted from sRGB before contributing to diffuse/PBR illumination.
  double ambient = 0.0;
  double master = 1.0;
  std::vector<DirectionalLight> lights;

  bool operator==(const LightingRig &other) const {
    return ambient == other.ambient && master == other.master &&
           lights == other.lights;
  }
};

struct RenderOptions {
  std::array<double, 3> position{}, rotation{};  // Degrees, applied X then Y then Z.
  double scale = 1.0;
  double cameraDistance = 10.0, fieldOfView = 45.0, orthoHeight = 10.0;
  double nearClip = 0.1, farClip = 1000.0;
  bool perspective = false, headlight = false, wireframe = false;
  bool materialColors = false;  // False preserves the original grayscale output.
  // Display-referred RGB, one per asset material plus the default material.
  // Empty uses the asset's linear baseColor factors, converted to sRGB.
  std::vector<std::array<float, 3>> colors;

  // Optional downstream 3D material and lighting state. These are independent
  // so Material and Light FX can be composed in either order in the schematic.
  bool useMaterialRig = false;
  MaterialRig material;
  bool useLightingRig = false;
  LightingRig lighting;

  // NoIndex preserves the historical static/base-geometry path exactly.
  int animation = NoIndex;
  double sourceSeconds = 0.0;

  bool operator==(const RenderOptions &other) const;
};

struct ProjectedVertex {
  double x, y, depth;
};
struct RenderTriangle {
  std::array<ProjectedVertex, 3> vertices;
  std::array<bool, 3> edges;
  std::array<float, 3> color;
};
struct RenderScene {
  std::vector<RenderTriangle> triangles;
  std::array<double, 4> bounds{};
  std::vector<std::string> warnings;
  bool wireframe = false;
};
struct ColorPixel {
  float r = 0, g = 0, b = 0, alpha = 0;
};
struct RenderTile {
  int width = 0, height = 0;
  double x = 0, y = 0;
  std::array<double, 6> affine{{1, 0, 0, 0, 1, 0}};
};

RenderScene prepareRender(const Asset &asset, const RenderOptions &options,
                          const int *canceled = nullptr);
std::vector<ColorPixel> renderTile(const RenderScene &scene, const RenderTile &tile,
                                   const int *canceled = nullptr);

float linearToSrgb(float value);
float srgbToLinear(float value);
std::array<float, 3> materialColor(const Asset &asset, std::size_t index);

}  // namespace otglb
