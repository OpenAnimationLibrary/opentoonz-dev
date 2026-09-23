#pragma once

#include "glbloader.h"

namespace otglb {

// A fixed image plane keeps projection independent of preview zoom, output
// resolution and tile size. The FX applies the normal OpenToonz 2D affine.
constexpr double ProjectionHeight = 1000.0;

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
  // converted from sRGB before contributing to diffuse illumination.
  double ambient = 0.0;
  double master = 1.0;
  std::vector<DirectionalLight> lights;

  bool operator==(const LightingRig &other) const {
    return ambient == other.ambient && master == other.master &&
           lights == other.lights;
  }
};

// A downstream 3D FX can add one or more transforms without flattening the
// source to a raster. Transforms are evaluated in vector order after the GLB
// Model node's own instance transform and before the camera transform.
struct ModelTransform {
  std::array<double, 3> position{};
  std::array<double, 3> rotation{};  // Degrees, applied X then Y then Z.
  std::array<double, 3> scale{{1.0, 1.0, 1.0}};

  bool operator==(const ModelTransform &other) const {
    return position == other.position && rotation == other.rotation &&
           scale == other.scale;
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

  // Optional camera-relative directional lighting supplied by a downstream 3D
  // FX. When enabled it supersedes the legacy headlight but preserves the
  // source node's geometry, camera, material and animation settings.
  bool useLightingRig = false;
  LightingRig lighting;

  // Additional model-space transforms supplied by downstream 3D nodes.
  std::vector<ModelTransform> transforms;

  // NoIndex preserves the historical static/base-geometry path exactly.
  // A valid index evaluates that embedded glTF clip at sourceSeconds and applies
  // node animation plus skinning before projection. Time is always glTF seconds.
  int animation = NoIndex;
  double sourceSeconds = 0.0;

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

// Opaque, two-sided geometry. With options.animation == NoIndex this preserves
// the original static base-geometry path. Otherwise embedded node animation and
// skeletal skinning are evaluated. Morph deformation remains deferred.
// Uses the declared default scene, otherwise the first scene.
// Throws std::runtime_error on invalid settings, animation data or resource limits.
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
