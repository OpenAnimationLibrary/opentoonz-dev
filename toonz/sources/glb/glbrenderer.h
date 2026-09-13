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
  bool operator==(const RenderOptions &other) const;
};

struct ProjectedVertex {
  double x, y, depth;  // Larger depth is nearer (1/d for perspective, -d otherwise).
};
struct RenderTriangle {
  std::array<ProjectedVertex, 3> vertices;
  std::array<bool, 3> edges;  // Excludes triangulation diagonals after clipping.
  float gray;
};
struct RenderScene {
  std::vector<RenderTriangle> triangles;
  std::array<double, 4> bounds{};  // x0, y0, x1, y1 in the fixed image plane.
  std::vector<std::string> warnings;
  bool wireframe = false;
};
struct GrayPixel {
  float gray = 0, alpha = 0;  // Premultiplied, display-referred, in [0,1].
};
struct RenderTile {
  int width = 0, height = 0;
  double x = 0, y = 0;
  // Row-major 2D affine: a11,a12,a13,a21,a22,a23.
  std::array<double, 6> affine{{1, 0, 0, 0, 1, 0}};
};

// Opaque, two-sided base geometry. Materials/textures/deformation are not
// evaluated. Uses the declared default scene, otherwise the first scene.
// Throws std::runtime_error on invalid settings or resource limits.
RenderScene prepareRender(const Asset &asset, const RenderOptions &options,
                          const int *canceled = nullptr);

// Four coverage/depth samples per pixel; coordinates and coverage do not
// depend on tile boundaries. No graphics context or global mutable state.
std::vector<GrayPixel> renderTile(const RenderScene &scene, const RenderTile &tile,
                                 const int *canceled = nullptr);

}  // namespace otglb
