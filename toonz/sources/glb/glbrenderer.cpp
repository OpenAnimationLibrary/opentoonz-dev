#include "glbrenderer.h"
#include "glbpose.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace otglb {
namespace {
constexpr double Pi = 3.14159265358979323846;
constexpr std::size_t MemoryLimit = 256ull * 1024 * 1024;
struct Vec { double x, y, z; };
Vec sub(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec cross(Vec a, Vec b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
Vec transform(const Matrix &m, Vec p) {
  return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
          m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
          m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}
struct Instance {
  const RenderOptions &o;
  double sx, cx, sy, cy, sz, cz;
  explicit Instance(const RenderOptions &options) : o(options) {
    sx = std::sin(o.rotation[0] * Pi / 180); cx = std::cos(o.rotation[0] * Pi / 180);
    sy = std::sin(o.rotation[1] * Pi / 180); cy = std::cos(o.rotation[1] * Pi / 180);
    sz = std::sin(o.rotation[2] * Pi / 180); cz = std::cos(o.rotation[2] * Pi / 180);
  }
  Vec operator()(Vec p) const {
    p = {p.x * o.scale, p.y * o.scale, p.z * o.scale};
    p = {p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z};
    p = {cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z};
    p = {cz * p.x - sz * p.y, sz * p.x + cz * p.y, p.z};
    return {p.x + o.position[0], p.y + o.position[1],
            p.z + o.position[2] - o.cameraDistance};
  }
};

struct InfluenceSet {
  const Attribute *joints = nullptr;
  const Attribute *weights = nullptr;
};

std::vector<InfluenceSet> influenceSets(const Primitive &primitive) {
  std::vector<InfluenceSet> sets;
  for (const auto &attribute : primitive.attributes) {
    if (attribute.semantic.rfind("JOINTS_", 0) != 0) continue;
    const std::string weightsName = "WEIGHTS_" + attribute.semantic.substr(7);
    const auto weights = std::find_if(primitive.attributes.begin(), primitive.attributes.end(),
        [&](const Attribute &candidate) { return candidate.semantic == weightsName; });
    require(weights != primitive.attributes.end(),
            "GLB skinned primitive has JOINTS without matching WEIGHTS.");
    require(attribute.components == 4 && weights->components == 4 &&
                attribute.values.size() >= primitive.vertexCount * 4 &&
                weights->values.size() >= primitive.vertexCount * 4,
            "GLB skin influence attribute shape is invalid.");
    sets.push_back({&attribute, &*weights});
  }
  return sets;
}

// Sutherland-Hodgman clipping in camera space, before perspective division.
std::vector<Vec> clip(const std::vector<Vec> &polygon, double distance, bool near) {
  std::vector<Vec> out;
  if (polygon.empty()) return out;
  Vec a = polygon.back();
  auto side = [=](Vec p) { return near ? -p.z - distance : distance + p.z; };
  double da = side(a);
  for (Vec b : polygon) {
    const double db = side(b);
    if ((da >= 0) != (db >= 0)) {
      const double t = da / (da - db);
      out.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y),
                     a.z + t * (b.z - a.z)});
    }
    if (db >= 0) out.push_back(b);
    a = b; da = db;
  }
  return out;
}
double edge(const ProjectedVertex &a, const ProjectedVertex &b, double x, double y) {
  return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}
}  // namespace

bool RenderOptions::operator==(const RenderOptions &b) const {
  return position == b.position && rotation == b.rotation && scale == b.scale &&
         cameraDistance == b.cameraDistance && fieldOfView == b.fieldOfView &&
         orthoHeight == b.orthoHeight && nearClip == b.nearClip &&
         farClip == b.farClip && perspective == b.perspective &&
         headlight == b.headlight && wireframe == b.wireframe &&
         materialColors == b.materialColors && colors == b.colors &&
         animation == b.animation && sourceSeconds == b.sourceSeconds;
}

float linearToSrgb(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}
float srgbToLinear(float v) {
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
std::array<float, 3> materialColor(const Asset &asset, std::size_t index) {
  if (index >= asset.materials.size()) return {{1, 1, 1}};
  const auto &c = asset.materials[index].baseColor;
  return {{linearToSrgb(c[0]), linearToSrgb(c[1]), linearToSrgb(c[2])}};
}

RenderScene prepareRender(const Asset &asset, const RenderOptions &o,
                          const int *canceled) {
  for (double v : o.position) require(std::isfinite(v), "Non-finite GLB position.");
  for (double v : o.rotation) require(std::isfinite(v), "Non-finite GLB rotation.");
  require(std::isfinite(o.scale) && o.scale > 0 &&
              std::isfinite(o.cameraDistance) && o.cameraDistance > 0 &&
              std::isfinite(o.nearClip) && std::isfinite(o.farClip) &&
              o.nearClip > 0 && o.farClip > o.nearClip,
          "GLB camera requires positive distances and Far Clip greater than Near Clip.");
  require(o.perspective ? (std::isfinite(o.fieldOfView) && o.fieldOfView > 0 && o.fieldOfView < 180)
                        : (std::isfinite(o.orthoHeight) && o.orthoHeight > 0),
          "Invalid GLB projection height or field of view.");
  require(o.animation == NoIndex || std::isfinite(o.sourceSeconds),
          "GLB animation time must be finite.");

  RenderScene out;
  require(o.colors.empty() || o.colors.size() == asset.materials.size() + 1,
          "GLB material color count does not match the asset.");
  for (const auto &color : o.colors)
    for (float v : color) require(std::isfinite(v) && v >= 0 && v <= 1,
                                 "Invalid GLB material color.");
  out.wireframe = o.wireframe;
  if (asset.scenes.empty()) return out;

  PoseResult evaluated;
  const Pose *pose = nullptr;
  if (o.animation != NoIndex) {
    evaluated = evaluatePose(asset, o.animation, o.sourceSeconds);
    if (!evaluated) throw std::runtime_error(evaluated.error);
    pose = &evaluated.pose;
    out.warnings.insert(out.warnings.end(), evaluated.warnings.begin(),
                        evaluated.warnings.end());
  }

  const int scene = asset.defaultScene == NoIndex ? 0 : asset.defaultScene;
  require(scene >= 0 && std::size_t(scene) < asset.scenes.size(), "Invalid GLB scene index.");
  if (asset.defaultScene == NoIndex)
    out.warnings.push_back("No default scene declared; rendering the first scene.");
  const Instance instance(o);
  const double factor = o.perspective ? ProjectionHeight / (2 * std::tan(o.fieldOfView * Pi / 360))
                                      : ProjectionHeight / o.orthoHeight;
  auto project = [&](Vec p) {
    const double k = o.perspective ? factor / -p.z : factor;
    ProjectedVertex v{p.x * k, p.y * k, o.perspective ? 1 / -p.z : p.z};
    require(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.depth) &&
                std::abs(v.x) < 1e12 && std::abs(v.y) < 1e12,
            "GLB projection exceeds the supported coordinate range.");
    return v;
  };
  bool skipped = false;
  std::vector<int> pending = asset.scenes[scene].roots;
  std::size_t visited = 0;
  while (!pending.empty()) {
    if (canceled && *canceled) return {};
    const int index = pending.back(); pending.pop_back();
    require(index >= 0 && std::size_t(index) < asset.nodes.size() &&
                ++visited <= asset.nodes.size(), "Invalid GLB scene hierarchy.");
    const Node &node = asset.nodes[index];
    const Matrix &nodeWorld = pose ? pose->nodes[index].world : node.world;
    pending.insert(pending.end(), node.children.begin(), node.children.end());
    if (node.mesh == NoIndex) continue;
    require(node.mesh >= 0 && std::size_t(node.mesh) < asset.meshes.size(), "Invalid GLB mesh index.");

    const SkinPose *skinPose = nullptr;
    if (pose && node.hasSkin) {
      const auto found = std::find_if(pose->skins.begin(), pose->skins.end(),
          [&](const SkinPose &candidate) { return candidate.node == index; });
      require(found != pose->skins.end(),
              "GLB animated skinned node has no evaluated skin binding.");
      skinPose = &*found;
    }

    for (const Primitive &p : asset.meshes[node.mesh].primitives) {
      require(p.material == NoIndex || (p.material >= 0 &&
                  std::size_t(p.material) < asset.materials.size()),
              "Invalid GLB material index.");
      const std::size_t material = p.material == NoIndex ? asset.materials.size() : p.material;
      const auto base = o.colors.empty() ? materialColor(asset, material) : o.colors[material];
      if (p.mode < 4 || p.mode > 6) { skipped = true; continue; }
      const auto pos = std::find_if(p.attributes.begin(), p.attributes.end(),
          [](const Attribute &a) { return a.semantic == "POSITION"; });
      require(pos != p.attributes.end() && pos->components == 3,
              "GLB triangle geometry has no positions.");

      const auto influences = skinPose ? influenceSets(p) : std::vector<InfluenceSet>();
      if (skinPose)
        require(!influences.empty(),
                "GLB animated skinned primitive has no joint/weight influences.");

      auto vertex = [&](std::size_t i) {
        if (!p.indices.empty()) i = p.indices.at(i);
        require(i < pos->values.size() / 3, "GLB triangle index is out of range.");
        const Vec basePosition{pos->values[i * 3], pos->values[i * 3 + 1],
                               pos->values[i * 3 + 2]};
        Vec local = basePosition;
        if (skinPose) {
          Vec weighted{0, 0, 0};
          double total = 0.0;
          for (const auto &set : influences) {
            for (int component = 0; component < 4; ++component) {
              const std::size_t at = i * 4 + component;
              const double weight = set.weights->values[at];
              if (!(weight > 0.0)) continue;
              const double jointValue = set.joints->values[at];
              const auto joint = std::size_t(std::llround(jointValue));
              require(std::isfinite(jointValue) && jointValue >= 0.0 &&
                          std::abs(jointValue - double(joint)) < 1e-6 &&
                          joint < skinPose->jointMatrices.size(),
                      "GLB skin influence references an invalid joint.");
              const Vec moved = transform(skinPose->jointMatrices[joint], basePosition);
              weighted.x += weight * moved.x;
              weighted.y += weight * moved.y;
              weighted.z += weight * moved.z;
              total += weight;
            }
          }
          require(total > 1e-12 && std::isfinite(total),
                  "GLB skinned vertex has no positive joint weight.");
          local = {weighted.x / total, weighted.y / total, weighted.z / total};
        }
        return instance(transform(nodeWorld, local));
      };
      const std::size_t count = p.indices.empty() ? p.vertexCount : p.indices.size();
      for (std::size_t i = 2; i < count; i += p.mode == 4 ? 3 : 1) {
        if ((i & 1023) == 0 && canceled && *canceled) return {};
        const Vec a = vertex(p.mode == 6 ? 0 : i - 2), b = vertex(i - 1), c = vertex(i);
        const Vec normal = cross(sub(b, a), sub(c, a));
        const double length = std::sqrt(dot(normal, normal));
        if (length < 1e-15) continue;
        const Vec view = o.perspective ? Vec{-(a.x + b.x + c.x), -(a.y + b.y + c.y), -(a.z + b.z + c.z)}
                                       : Vec{0, 0, 1};
        const double denominator = length * std::sqrt(dot(view, view));
        const double light = denominator > 0 && std::isfinite(denominator)
            ? std::abs(dot(normal, view)) / denominator : 0;
        const float gray = o.headlight ? float(0.2 + 0.6 * std::clamp(light, 0.0, 1.0)) : 0.75f;
        std::array<float, 3> color{{gray, gray, gray}};
        if (o.materialColors) {
          color = base;
          if (o.headlight)
            for (auto &v : color) v = linearToSrgb(srgbToLinear(v) * gray);
        }
        const auto polygon = clip(clip({a, b, c}, o.nearClip, true), o.farClip, false);
        for (std::size_t j = 1; j + 1 < polygon.size(); ++j) {
          // Include both old and new allocations during vector growth.
          require(out.triangles.size() < MemoryLimit / (3 * sizeof(RenderTriangle)),
                  "GLB projected geometry exceeds the 256 MiB render budget.");
          RenderTriangle triangle{{{project(polygon[0]), project(polygon[j]), project(polygon[j + 1])}},
                                  {{j == 1, true, j + 2 == polygon.size()}}, color};
          if (std::abs(edge(triangle.vertices[0], triangle.vertices[1],
                            triangle.vertices[2].x, triangle.vertices[2].y)) < 1e-12) continue;
          if (out.triangles.empty()) {
            const auto &v = triangle.vertices[0];
            out.bounds = {{v.x, v.y, v.x, v.y}};
          }
          for (const auto &v : triangle.vertices) {
            out.bounds[0] = std::min(out.bounds[0], v.x); out.bounds[1] = std::min(out.bounds[1], v.y);
            out.bounds[2] = std::max(out.bounds[2], v.x); out.bounds[3] = std::max(out.bounds[3], v.y);
          }
          out.triangles.push_back(triangle);
        }
      }
    }
  }
  if (skipped) out.warnings.push_back("Point/line primitives are not rendered; triangle surfaces only.");
  return out;
}

std::vector<ColorPixel> renderTile(const RenderScene &scene, const RenderTile &tile,
                                   const int *canceled) {
  require(tile.width >= 0 && tile.height >= 0, "Invalid GLB tile dimensions.");
  const std::size_t count = std::size_t(tile.width) * std::size_t(tile.height);
  struct Sample { double depth = -std::numeric_limits<double>::infinity(); ColorPixel color; };
  require(count <= MemoryLimit / (sizeof(ColorPixel) + 4 * sizeof(Sample)),
          "GLB tile exceeds the 256 MiB render budget; reduce the render tile size.");
  for (double value : tile.affine) require(std::isfinite(value), "Invalid GLB image affine.");
  require(std::isfinite(tile.x) && std::isfinite(tile.y), "Invalid GLB tile origin.");
  std::vector<ColorPixel> output(count);
  if (!count || scene.triangles.empty()) return output;
  std::vector<Sample> samples(count * 4);
  const auto &m = tile.affine;
  for (const auto &triangle : scene.triangles) {
    if (canceled && *canceled) return {};
    auto v = triangle.vertices;
    for (auto &p : v) {
      const double x = m[0] * p.x + m[1] * p.y + m[2] - tile.x;
      p.y = m[3] * p.x + m[4] * p.y + m[5] - tile.y; p.x = x;
      require(std::isfinite(p.x) && std::isfinite(p.y), "GLB image affine overflows.");
    }
    const double area = edge(v[0], v[1], v[2].x, v[2].y);
    if (std::abs(area) < 1e-12) continue;
    double x0 = std::min({v[0].x, v[1].x, v[2].x}), x1 = std::max({v[0].x, v[1].x, v[2].x});
    double y0 = std::min({v[0].y, v[1].y, v[2].y}), y1 = std::max({v[0].y, v[1].y, v[2].y});
    if (x1 < 0 || y1 < 0 || x0 >= tile.width || y0 >= tile.height) continue;
    const int left = int(std::max(0.0, std::floor(x0))), bottom = int(std::max(0.0, std::floor(y0)));
    const int right = int(std::min(double(tile.width - 1), std::floor(x1)));
    const int top = int(std::min(double(tile.height - 1), std::floor(y1)));
    const double lengths[] = {std::hypot(v[1].x - v[0].x, v[1].y - v[0].y),
                              std::hypot(v[2].x - v[1].x, v[2].y - v[1].y),
                              std::hypot(v[0].x - v[2].x, v[0].y - v[2].y)};
    for (int y = bottom; y <= top; ++y) {
      if (canceled && *canceled) return {};
      for (int x = left; x <= right; ++x) for (int s = 0; s < 4; ++s) {
        const double px = x + 0.25 + 0.5 * (s & 1), py = y + 0.25 + 0.5 * (s >> 1);
        const double e0 = edge(v[0], v[1], px, py), e1 = edge(v[1], v[2], px, py), e2 = edge(v[2], v[0], px, py);
        const double a = e1 / area, b = e2 / area, c = e0 / area;
        if (a < -1e-12 || b < -1e-12 || c < -1e-12) continue;
        const double depth = a * v[0].depth + b * v[1].depth + c * v[2].depth;
        Sample &sample = samples[(std::size_t(y) * tile.width + x) * 4 + s];
        const bool line = !scene.wireframe ||
            (triangle.edges[0] && std::abs(e0) <= 0.65 * lengths[0]) ||
            (triangle.edges[1] && std::abs(e1) <= 0.65 * lengths[1]) ||
            (triangle.edges[2] && std::abs(e2) <= 0.65 * lengths[2]);
        const double epsilon = 1e-10 * std::max(1.0, std::abs(depth));
        if (depth > sample.depth + epsilon) {
          sample = {depth, line ? ColorPixel{triangle.color[0], triangle.color[1], triangle.color[2], 1} : ColorPixel{}};
        } else if (scene.wireframe && line && std::abs(depth - sample.depth) <= epsilon) {
          sample.color = {triangle.color[0], triangle.color[1], triangle.color[2], 1};
        }
      }
    }
  }
  for (std::size_t i = 0; i < count; ++i) for (int s = 0; s < 4; ++s) {
    const auto &c = samples[i * 4 + s].color;
    output[i].r += c.r * 0.25f;
    output[i].g += c.g * 0.25f;
    output[i].b += c.b * 0.25f;
    output[i].alpha += c.alpha * 0.25f;
  }
  return output;
}
}  // namespace otglb
