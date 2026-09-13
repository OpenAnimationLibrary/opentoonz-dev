#pragma once

#include "glbloader.h"

namespace otglb {

// A fixed image plane keeps projection independent of preview zoom, output
// resolution and tile size. The FX applies the normal OpenToonz 2D affine.
constexpr double ProjectionHeight = 1000.0;

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
  bool operator==(const RenderOptions &other) const;
};

struct ProjectedVertex {
  double x, y, depth;  // Larger depth is nearer (1/d for perspective, -d otherwise).
};
struct RenderTriangle {
  std::array<ProjectedVertex, 3> vertices;
  std::array<bool, 3> edges;  // Excludes triangulation diagonals after clipping.
  std::array<float, 3> color;
};
struct RenderScene {
  std::vector<RenderTriangle> triangles;
  std::array<double, 4> bounds{};  // x0, y0, x1, y1 in the fixed image plane.
  std::vector<std::string> warnings;
  bool wireframe = false;
};
struct ColorPixel {
  float r = 0, g = 0, b = 0, alpha = 0;  // Premultiplied sRGB, in [0,1].
};
struct RenderTile {
  int width = 0, height = 0;
  double x = 0, y = 0;
  // Row-major 2D affine: a11,a12,a13,a21,a22,a23.
  std::array<double, 6> affine{{1, 0, 0, 0, 1, 0}};
};

// Opaque, two-sided base geometry. Textures/deformation are not
// evaluated. Uses the declared default scene, otherwise the first scene.
// Throws std::runtime_error on invalid settings or resource limits.
RenderScene prepareRender(const Asset &asset, const RenderOptions &options,
                          const int *canceled = nullptr);

// Four coverage/depth samples per pixel; coordinates and coverage do not
// depend on tile boundaries. No graphics context or global mutable state.
std::vector<ColorPixel> renderTile(const RenderScene &scene, const RenderTile &tile,
                                 const int *canceled = nullptr);

float linearToSrgb(float value);
float srgbToLinear(float value);
std::array<float, 3> materialColor(const Asset &asset, std::size_t index);

}  // namespace otglb
