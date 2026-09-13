#include "glbrenderer.h"
#include "glbfixture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace otglb;
namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void near(double a, double b) { check(std::abs(a - b) < 1e-5, "Numeric mismatch"); }
Asset triangle() {
  Asset a;
  Primitive p;
  p.vertexCount = 3; p.triangleCount = 1;
  p.attributes.push_back({"POSITION", 3, {-1, -1, 0, 1, -1, 0, 0, 1, 0}});
  a.meshes.push_back({"triangle", {p}, {}});
  Node n; n.mesh = 0;
  n.world = n.local = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  a.nodes.push_back(n);
  a.scenes.push_back({"scene", {0}, {}}); a.defaultScene = 0;
  return a;
}
RenderTile tile(int size = 80) {
  RenderTile t; t.width = t.height = size;
  t.affine = {{0.2, 0, double(size) / 2, 0, 0.2, double(size) / 2}};
  return t;
}
double coverage(const std::vector<ColorPixel> &pixels) {
  double sum = 0;
  for (const auto &p : pixels) {
    check(std::isfinite(p.r) && p.r >= 0 && p.r <= p.alpha &&
              p.alpha <= 1, "Invalid premultiplied output");
    sum += p.alpha;
  }
  return sum;
}
void equal(const std::vector<ColorPixel> &a, const std::vector<ColorPixel> &b) {
  check(a.size() == b.size(), "Image size differs");
  for (std::size_t i = 0; i < a.size(); ++i) {
    near(a[i].r, b[i].r); near(a[i].g, b[i].g); near(a[i].b, b[i].b); near(a[i].alpha, b[i].alpha);
  }
}
template <class F> void rejects(F f) {
  bool rejected = false;
  try { f(); } catch (const std::runtime_error &) { rejected = true; }
  check(rejected, "Expected an explicit failure");
}
}  // namespace

int main() {
  int passed = 0;
  auto run = [&](const char *name, auto test) {
    test(); ++passed; std::cout << "PASS " << name << '\n';
  };
  try {
    run("GLB load to visible antialiased grayscale pixels", [] {
      const auto path = std::filesystem::temp_directory_path() /
          ("otglb-render-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".glb");
      struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove(p, e); } } cleanup{path};
      const auto bytes = triangleGlb();
      { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()); }
      const auto loaded = load(path); check(bool(loaded), "Fixture failed to load");
      const auto scene = prepareRender(*loaded.asset, {});
      check(scene.triangles.size() == 1, "Triangle count changed");
      near(scene.bounds[0], -100); near(scene.bounds[3], 100);
      const auto image = renderTile(scene, tile());
      near(coverage(image), 800);
      near(image[40 * 80 + 40].r, .75); near(image[0].alpha, 0);
      check(std::any_of(image.begin(), image.end(), [](ColorPixel p) { return p.alpha > 0 && p.alpha < 1; }), "No edge antialiasing");
      std::ifstream f(path, std::ios::binary);
      const std::vector<unsigned char> after((std::istreambuf_iterator<char>(f)), {});
      check(bytes == after, "Source model changed");
    });
    run("orthographic, perspective and instance transforms", [] {
      auto a = triangle(); RenderOptions o;
      o.position = {{2, 3, 0}}; o.scale = 2; o.rotation[2] = 90;
      auto s = prepareRender(a, o);
      near(s.bounds[0], 0); near(s.bounds[2], 400); near(s.bounds[1], 100); near(s.bounds[3], 500);
      o = {}; o.perspective = true; o.fieldOfView = 90;
      s = prepareRender(a, o); near(s.bounds[0], -50);
      o.cameraDistance = 20; s = prepareRender(a, o); near(s.bounds[0], -25);
      o = {}; o.rotation[0] = 60;
      s = prepareRender(a, o); near(s.bounds[1], -50); near(s.bounds[3], 50);
      o = {}; o.rotation[1] = 60;
      s = prepareRender(a, o); near(s.bounds[0], -50); near(s.bounds[2], 50);
    });
    run("near and far clipping including camera crossings", [] {
      auto a = triangle(); RenderOptions o;
      o.nearClip = 11; check(prepareRender(a, o).triangles.empty(), "Near clip ignored");
      o = {}; o.farClip = 9; check(prepareRender(a, o).triangles.empty(), "Far clip ignored");
      o = {}; o.perspective = true; o.cameraDistance = 1; o.nearClip = .5;
      a.meshes[0].primitives[0].attributes[0].values[2] = 2;
      const auto s = prepareRender(a, o);
      check(s.triangles.size() == 2, "Crossing triangle was not clipped to a quad");
      for (auto &t : s.triangles) for (auto &v : t.vertices)
        check(v.depth <= 2.000001 && v.depth > 0, "Invalid clipped depth");
      check(coverage(renderTile(s, tile())) > 0, "Clipped geometry disappeared");
    });
    run("depth ordering independent of draw order and headlight", [] {
      auto a = triangle(); auto s = prepareRender(a, {});
      auto front = s.triangles[0]; front.color = {{.3f, .3f, .3f}};
      for (auto &v : front.vertices) v.depth = -5;
      s.triangles.push_back(front); const auto first = renderTile(s, tile());
      std::reverse(s.triangles.begin(), s.triangles.end());
      equal(first, renderTile(s, tile())); near(first[40 * 80 + 40].r, .3);
      RenderOptions o; o.headlight = true;
      near(prepareRender(a, o).triangles[0].color[0], .8);
      o.rotation[0] = 60; near(prepareRender(a, o).triangles[0].color[0], .5);
    });
    run("wireframe keeps interiors transparent and hides occluded edges", [] {
      auto s = prepareRender(triangle(), {}); const auto solid = renderTile(s, tile());
      s.wireframe = true; const auto wire = renderTile(s, tile());
      check(coverage(wire) > 0 && coverage(wire) < coverage(solid) / 4, "Wireframe filled faces");
      near(wire[40 * 80 + 40].alpha, 0);
      auto back = s.triangles[0];
      for (auto &v : back.vertices) { v.x *= .3; v.y *= .3; v.depth -= 2; }
      s.triangles.push_back(back); equal(wire, renderTile(s, tile()));
    });
    run("tile seams, affine rotation/shear/reflection and concurrent tiles", [] {
      auto s = prepareRender(triangle(), {}); auto t = tile();
      t.affine = {{-.19, .04, 40.125, .07, .22, 39.75}};
      const auto full = renderTile(s, t);
      auto left = t, right = t; left.width = 37; right.width = 43; right.x = 37;
      std::vector<ColorPixel> l, r;
      std::thread worker([&] { l = renderTile(s, left); }); r = renderTile(s, right); worker.join();
      for (int y = 0; y < 80; ++y) for (int x = 0; x < 80; ++x) {
        const auto p = x < 37 ? l[y * 37 + x] : r[y * 43 + x - 37];
        near(p.r, full[y * 80 + x].r); near(p.alpha, full[y * 80 + x].alpha);
      }
    });
    run("default scene, shared instances and triangle topology", [] {
      auto a = triangle(); auto n = a.nodes[0]; n.world[12] = 4; a.nodes.push_back(n);
      a.scenes.push_back({"second", {1}, {}}); a.defaultScene = 1;
      auto s = prepareRender(a, {}); near(s.bounds[0], 300);
      a.defaultScene = NoIndex; s = prepareRender(a, {}); near(s.bounds[0], -100);
      check(!s.warnings.empty(), "Missing default scene was not reported");
      a.scenes[0].roots.push_back(1); check(prepareRender(a, {}).triangles.size() == 2, "Instance omitted");
      a = triangle(); auto &p = a.meshes[0].primitives[0];
      p.attributes[0].values = {-1,-1,0, 1,-1,0, -1,1,0, 1,1,0}; p.vertexCount = 4; p.mode = 5;
      check(prepareRender(a, {}).triangles.size() == 2, "Strip omitted");
      p.mode = 6; p.indices = {0, 1, 3, 2};
      check(prepareRender(a, {}).triangles.size() == 2, "Indexed fan omitted");
      p.mode = 1; s = prepareRender(a, {});
      check(s.triangles.empty() && !s.warnings.empty(), "Unsupported topology not reported");
    });
    run("material RGB, linear factors, overrides and grayscale compatibility", [] {
      auto a = triangle();
      a.materials.resize(2);
      a.materials[0].baseColor = {{1, 0, 0, .2f}};
      a.materials[1].baseColor = {{0, .21404114f, 0, 1}};
      a.meshes[0].primitives[0].material = 0;
      RenderOptions o; o.materialColors = true;
      auto image = renderTile(prepareRender(a, o), tile());
      const int center = 40 * 80 + 40;
      near(image[center].r, 1); near(image[center].g, 0);
      near(image[center].b, 0); near(image[center].alpha, 1);
      a.meshes[0].primitives[0].material = 1;
      image = renderTile(prepareRender(a, o), tile());
      near(image[center].r, 0); near(image[center].g, .5);
      for (const auto &p : image) {
        near(p.r, 0); near(p.b, 0); near(p.g, .5 * p.alpha);
      }
      o.colors = {{{1, 0, 0}}, {{.25f, .5f, 1}}, {{1, 1, 1}}};
      image = renderTile(prepareRender(a, o), tile());
      near(image[center].r, .25); near(image[center].g, .5); near(image[center].b, 1);
      auto other = o; other.colors[1][0] = .3f;
      check(!(o == other), "Color override missing from cache identity");
      o.headlight = true;
      image = renderTile(prepareRender(a, o), tile());
      near(image[center].g, linearToSrgb(srgbToLinear(.5f) * .8f));
      o.materialColors = false;
      RenderOptions gray; gray.headlight = true;
      equal(renderTile(prepareRender(a, o), tile()),
            renderTile(prepareRender(a, gray), tile()));
      o = {}; o.materialColors = true;
      a.meshes[0].primitives[0].material = NoIndex;
      image = renderTile(prepareRender(a, o), tile());
      near(image[center].r, 1); near(image[center].g, 1); near(image[center].b, 1);
      o.colors.resize(1); rejects([&] { prepareRender(a, o); });
    });
    run("invalid controls, tile budget, empty geometry and cancellation", [] {
      auto a = triangle(); RenderOptions o; o.farClip = o.nearClip;
      rejects([&] { prepareRender(a, o); }); o = {}; o.scale = std::numeric_limits<double>::quiet_NaN();
      rejects([&] { prepareRender(a, o); });
      auto s = prepareRender(a, {}); auto t = tile(); t.width = t.height = 100000;
      rejects([&] { renderTile(s, t); });
      int canceled = 1; check(prepareRender(a, {}, &canceled).triangles.empty(), "Prepare cancellation ignored");
      check(renderTile(s, tile(), &canceled).empty(), "Render cancellation ignored");
      a.scenes.clear(); check(prepareRender(a, {}).triangles.empty(), "Mesh-only asset invented instances");
    });
    std::cout << passed << " renderer groups passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n'; return 1;
  }
}
