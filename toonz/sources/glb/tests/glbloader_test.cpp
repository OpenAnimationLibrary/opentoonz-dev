#include "glbloader.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;
void check(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}
void near(float actual, float expected) {
  check(std::abs(actual - expected) < 0.0001f, "numeric mismatch: " +
        std::to_string(actual) + " != " + std::to_string(expected));
}
void word(Bytes &bytes, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) bytes.push_back((value >> (8 * i)) & 255);
}
Bytes floats(std::initializer_list<float> values) {
  Bytes bytes;
  for (float value : values) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    word(bytes, bits);
  }
  return bytes;
}
std::string array(const std::vector<std::string> &items) {
  std::string result = "[";
  for (const auto &item : items) result += (result.size() == 1 ? "" : ",") + item;
  return result + "]";
}
struct Fixture {
  Bytes bin;
  std::vector<std::string> views, accessors;
  std::vector<std::size_t> offsets;
  int view(const Bytes &bytes, int stride = 0) {
    while (bin.size() % 4) bin.push_back(0);
    offsets.push_back(bin.size());
    std::string entry = "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(bin.size()) + ",\"byteLength\":" + std::to_string(bytes.size());
    if (stride) entry += ",\"byteStride\":" + std::to_string(stride);
    views.push_back(entry + "}");
    bin.insert(bin.end(), bytes.begin(), bytes.end());
    return int(views.size() - 1);
  }
  int accessor(int viewIndex, int component, int count, const char *type,
               const std::string &extra = "") {
    std::string entry = "{\"componentType\":" + std::to_string(component) +
        ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"";
    if (viewIndex >= 0) entry += ",\"bufferView\":" + std::to_string(viewIndex);
    accessors.push_back(entry + extra + "}");
    return int(accessors.size() - 1);
  }
  std::string json(const std::string &fields = "") const {
    return "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" +
        std::to_string(bin.size()) + "}],\"bufferViews\":" + array(views) +
        ",\"accessors\":" + array(accessors) + fields + "}";
  }
  Bytes glb(std::string json) const {
    while (json.size() % 4) json += ' ';
    Bytes buffer = bin;
    while (buffer.size() % 4) buffer.push_back(0);
    Bytes bytes;
    word(bytes, 0x46546c67); word(bytes, 2);
    word(bytes, std::uint32_t(28 + json.size() + buffer.size()));
    word(bytes, std::uint32_t(json.size())); word(bytes, 0x4e4f534a);
    bytes.insert(bytes.end(), json.begin(), json.end());
    word(bytes, std::uint32_t(buffer.size())); word(bytes, 0x004e4942);
    bytes.insert(bytes.end(), buffer.begin(), buffer.end());
    return bytes;
  }
};
const std::string graph =
    ",\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0";
std::string mesh(const std::string &extra = "") {
  return ",\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}" + extra + "}]}]";
}
Fixture triangle() {
  Fixture f;
  f.accessor(f.view(floats({-1,0,0, 1,0,0, 0,2,0})), 5126, 3, "VEC3",
             ",\"min\":[-1,0,0],\"max\":[1,2,0]");
  return f;
}
std::string replace(std::string text, const std::string &from, const std::string &to) {
  auto pos = text.find(from);
  check(pos != std::string::npos, "fixture replacement missing: " + from);
  text.replace(pos, from.size(), to);
  return text;
}
struct Files {
  std::filesystem::path directory;
  int serial = 0;
  Files() {
    directory = std::filesystem::temp_directory_path() /
        ("otglb-test-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(directory), "create fixture directory");
  }
  ~Files() { std::error_code error; std::filesystem::remove_all(directory, error); }
  std::filesystem::path write(const Bytes &bytes, bool unicode = false) {
    auto path = directory / (unicode ? std::filesystem::u8path(u8"\u6a21\u578b-\u00e9.glb") :
        std::filesystem::path(std::to_string(++serial) + ".glb"));
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    check(bool(file), "write fixture");
    return path;
  }
};
Bytes read(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(stream), {});
}
const otglb::Attribute &attribute(const otglb::Primitive &p, const std::string &name) {
  for (const auto &a : p.attributes) if (a.semantic == name) return a;
  throw std::runtime_error("missing attribute " + name);
}
void bounds(const otglb::Bounds &b, std::initializer_list<float> values) {
  check(!b.empty, "empty bounds");
  auto it = values.begin();
  for (float value : b.minimum) near(value, *it++);
  for (float value : b.maximum) near(value, *it++);
}
void success(const otglb::Result &r, otglb::Status status = otglb::Status::Loaded) {
  check(bool(r) && r.status == status, std::string(otglb::statusName(r.status)) + ": " + r.error);
  check(r.error.empty(), "successful load has fatal error");
  if (status == otglb::Status::LoadedWithWarnings) check(!r.warnings.empty(), "missing warning");
}
void failure(const otglb::Result &r, otglb::Status status = otglb::Status::Invalid) {
  check(!r && !r.asset && r.status == status && !r.error.empty(),
        "unexpected failure status " + std::string(otglb::statusName(r.status)) + ": " + r.error);
}
}  // namespace

int main() {
  Files files;
  int failed = 0, passed = 0;
  auto run = [&](const char *name, const std::function<void()> &test) {
    try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception &error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
  };
  auto load = [&](const Fixture &f, const std::string &fields) {
    return otglb::load(files.write(f.glb(f.json(fields))));
  };
  run("interleaved attributes, normalized colors, material and image ownership", [&] {
    Fixture f;
    Bytes vertices;
    for (const auto &v : {floats({-1,0,0, 0,0,1, 0,0}),
                          floats({1,0,0, 0,0,1, 1,0}),
                          floats({0,2,0, 0,0,1, 0.5f,1})}) {
      vertices.insert(vertices.end(), v.begin(), v.end());
      vertices.insert(vertices.end(), {255,128,0,255});
    }
    int v = f.view(vertices, 36);
    f.accessor(v, 5126, 3, "VEC3", ",\"min\":[-1,0,0],\"max\":[1,2,0]");
    f.accessor(v, 5126, 3, "VEC3", ",\"byteOffset\":12");
    f.accessor(v, 5126, 3, "VEC2", ",\"byteOffset\":24");
    f.accessor(v, 5121, 3, "VEC4", ",\"byteOffset\":32,\"normalized\":true");
    f.accessor(f.view({0,0, 1,0, 2,0}), 5123, 3, "SCALAR");
    const Bytes png = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,
      0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,96,
      0,2,0,0,5,0,1,165,246,69,64,0,0,0,0,73,69,78,68,174,66,96,130};
    int imageView = f.view(png);
    auto fields = ",\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"COLOR_0\":3},\"indices\":4,\"material\":0}]}]" + graph +
      ",\"images\":[{\"bufferView\":" + std::to_string(imageView) + ",\"mimeType\":\"image/png\"}],\"samplers\":[{\"magFilter\":9728,\"wrapS\":33071}],\"textures\":[{\"source\":0,\"sampler\":0}],\"materials\":[{\"name\":\"paint\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.2,0.3,0.4,0.5],\"metallicFactor\":0.1,\"roughnessFactor\":0.8,\"baseColorTexture\":{\"index\":0}},\"alphaMode\":\"BLEND\",\"doubleSided\":true}]";
    auto r = load(f, replace(fields, "\"name\":\"paint\"", "\"name\":\"caf\\u00e9\""));
    success(r);
    const auto &p = r.asset->meshes.at(0).primitives.at(0);
    check(p.vertexCount == 3 && p.triangleCount == 1 && p.indices == std::vector<std::uint32_t>({0,1,2}), "geometry counts/indices");
    check(attribute(p, "POSITION").values == std::vector<float>({-1,0,0,1,0,0,0,2,0}), "position values");
    near(attribute(p, "NORMAL").values.at(8), 1);
    near(attribute(p, "TEXCOORD_0").values.at(4), 0.5f);
    near(attribute(p, "COLOR_0").values.at(1), 128.0f/255);
    check(p.material == 0 && r.asset->materials.at(0).name == u8"caf\u00e9", "material assignment/escaped Unicode name");
    const auto &m = r.asset->materials[0];
    near(m.baseColor[3], 0.5f); near(m.metallic, 0.1f); near(m.roughness, 0.8f);
    check(m.alphaMode == 2 && m.doubleSided && m.baseColorTexture.texture == 0, "material properties");
    check(r.asset->textures.at(0).image == 0 && r.asset->textures[0].magFilter == 9728 && r.asset->textures[0].wrapS == 33071, "texture association/sampler");
    check(r.asset->images.at(0).encoded == png && r.asset->images[0].mimeType == "image/png", "owned encoded image");
    bounds(p.bounds, {-1,0,0,1,2,0});
  });
  run("TRS order, column-major matrix, hierarchy and scene bounds", [&] {
    auto r = load(triangle(), mesh() + ",\"nodes\":[{\"translation\":[10,20,30],\"rotation\":[0,0,0.7071067812,0.7071067812],\"scale\":[2,3,4],\"children\":[1]},{\"mesh\":0,\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,1,2,3,1]}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0");
    success(r);
    const auto &n = r.asset->nodes.at(1);
    check(n.parent == 0 && r.asset->nodes[0].children == std::vector<int>({1}), "hierarchy");
    near(n.local[12], 1); near(n.local[13], 2); near(n.local[14], 3);
    near(n.world[12], 4); near(n.world[13], 22); near(n.world[14], 42);
    near(n.world[1], 2); near(n.world[4], -3);
    bounds(r.asset->scenes.at(0).bounds, {-2,20,42,4,24,42});
  });
  run("shared geometry, multiple scenes and default scene", [&] {
    auto r = load(triangle(), mesh() + ",\"nodes\":[{\"mesh\":0},{\"mesh\":0,\"translation\":[100,0,0]}],\"scenes\":[{\"nodes\":[0]},{\"nodes\":[1]}],\"scene\":1");
    success(r);
    check(r.asset->meshes.size() == 1 && r.asset->defaultScene == 1, "shared mesh/default scene");
    bounds(r.asset->scenes[0].bounds, {-1,0,0,1,2,0});
    bounds(r.asset->scenes[1].bounds, {99,0,0,101,2,0});
  });
  run("non-indexed triangle, triangle strip and fan topology", [&] {
    auto f = triangle();
    for (int mode : {4,5,6}) {
      auto r = load(f, mesh(",\"mode\":" + std::to_string(mode)) + graph);
      success(r);
      const auto &p = r.asset->meshes[0].primitives[0];
      check(p.mode == mode && p.indices.empty() && p.triangleCount == 1, "topology changed");
    }
  });
  run("sparse overrides over interleaved positions, and sparse indices without base", [&] {
    Fixture f;
    auto base = floats({-1,0,0,99, 1,0,0,99, 0,2,0,99});
    int baseView = f.view(base, 16), sparseIndex = f.view({1,2});
    int sparseValue = f.view(floats({1,0,0,0,3,0}));
    f.accessor(baseView, 5126, 3, "VEC3", ",\"min\":[-1,0,0],\"max\":[1,3,0],\"sparse\":{\"count\":2,\"indices\":{\"bufferView\":" + std::to_string(sparseIndex) + ",\"componentType\":5121},\"values\":{\"bufferView\":" + std::to_string(sparseValue) + "}}");
    int indices = f.view({1,2}), values = f.view({1,0,2,0});
    f.accessor(-1, 5123, 3, "SCALAR", ",\"sparse\":{\"count\":2,\"indices\":{\"bufferView\":" + std::to_string(indices) + ",\"componentType\":5121},\"values\":{\"bufferView\":" + std::to_string(values) + "}}");
    auto r = load(f, mesh(",\"indices\":1") + graph);
    success(r);
    const auto &p = r.asset->meshes[0].primitives[0];
    check(attribute(p, "POSITION").values == std::vector<float>({-1,0,0,1,0,0,0,3,0}), "sparse/interleaved values");
    check(p.indices == std::vector<std::uint32_t>({0,1,2}), "sparse indices");
    bounds(p.bounds, {-1,0,0,1,3,0});
    f.bin[f.offsets[sparseIndex] + 1] = 1;
    failure(load(f, mesh(",\"indices\":1") + graph));
    f.bin[f.offsets[sparseIndex] + 1] = 3;
    failure(load(f, mesh(",\"indices\":1") + graph));
  });
  run("invalid container, references, accessors, indices and hierarchy", [&] {
    auto f = triangle();
    auto json = f.json(mesh() + graph);
    Bytes truncated = f.glb(json); truncated.pop_back();
    failure(otglb::load(files.write(truncated)));
    for (const auto &change : std::vector<std::pair<std::string,std::string>>{
      {"\"POSITION\":0", "\"POSITION\":100"},
      {"\"count\":3", "\"count\":4"},
      {"\"byteOffset\":0", "\"byteOffset\":18446744073709551615"},
      {"\"mesh\":0", "\"mesh\":0,\"children\":[0]"}})
      failure(otglb::load(files.write(f.glb(replace(json, change.first, change.second)))));
    f.accessor(f.view({0,0,1,0,3,0}), 5123, 3, "SCALAR");
    failure(load(f, mesh(",\"indices\":1") + graph));
    f.bin[0] = 0; f.bin[1] = 0; f.bin[2] = 128; f.bin[3] = 127;
    failure(load(f, mesh() + graph));
    failure(load(triangle(), ",\"meshes\":[{\"primitives\":[{\"attributes\":{}}]}]" + graph));
  });
  run("required unsupported features and external resources", [&] {
    auto f = triangle();
    failure(load(f, mesh() + graph + ",\"extensionsUsed\":[\"VENDOR_required\"],\"extensionsRequired\":[\"VENDOR_required\"]"), otglb::Status::Unsupported);
    auto json = replace(f.json(mesh() + graph), "\"buffers\":[{", "\"buffers\":[{\"uri\":\"https://example.invalid/mesh.bin\",");
    failure(otglb::load(files.write(f.glb(json))), otglb::Status::Unsupported);
    failure(load(f, mesh() + graph + ",\"images\":[{\"uri\":\"relative.png\"}]"), otglb::Status::Unsupported);
  });
  run("optional material extensions, animations, skin and morph base geometry", [&] {
    auto f = triangle();
    f.accessor(f.view(floats({0,1})), 5126, 2, "SCALAR", ",\"min\":[0],\"max\":[1]");
    f.accessor(f.view(floats({0,0,0,1,0,0})), 5126, 2, "VEC3");
    f.accessor(f.view(Bytes(12, 0)), 5121, 3, "VEC4");
    f.accessor(f.view(floats({1,0,0,0,1,0,0,0,1,0,0,0})), 5126, 3, "VEC4");
    auto skinnedMesh = replace(mesh(",\"targets\":[{\"POSITION\":0}],\"material\":0"),
        "\"POSITION\":0", "\"POSITION\":0,\"JOINTS_0\":3,\"WEIGHTS_0\":4");
    auto r = load(f, skinnedMesh +
      ",\"nodes\":[{\"mesh\":0,\"skin\":0},{\"name\":\"joint\"}],\"scenes\":[{\"nodes\":[0,1]}],\"scene\":0,\"skins\":[{\"joints\":[1]}],\"animations\":[{\"samplers\":[{\"input\":1,\"output\":2}],\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}]}],\"extensionsUsed\":[\"KHR_materials_clearcoat\"],\"materials\":[{\"extensions\":{\"KHR_materials_clearcoat\":{\"clearcoatFactor\":1}}}]");
    success(r, otglb::Status::LoadedWithWarnings);
    check(r.asset->animationCount == 1 && r.asset->skinCount == 1 && r.asset->nodes[0].hasSkin && r.asset->meshes[0].primitives[0].hasMorphTargets, "deformation metadata");
    bounds(r.asset->meshes[0].bounds, {-1,0,0,1,2,0});
  });
  run("Unicode paths, nonexistent files, lifetime, repeated loading and unchanged source", [&] {
    auto f = triangle(); auto bytes = f.glb(f.json(mesh() + graph));
    auto path = files.write(bytes, true);
    auto first = otglb::load(path); success(first);
    auto second = otglb::load(path); success(second);
    check(read(path) == bytes, "source was modified");
    check(first.fileBytes == bytes.size() && first.decodedBytes > 0 && first.parserPeakBytes > 0, "load accounting");
    std::filesystem::remove(path);
    bounds(first.asset->meshes[0].bounds, {-1,0,0,1,2,0});
    failure(otglb::load(path), otglb::Status::IoError);
  });
  run("explicit resource limits and hierarchy depth", [&] {
    auto f = triangle(); auto path = files.write(f.glb(f.json(mesh() + graph)));
    for (int kind : {0,1,2}) {
      otglb::Limits limits;
      if (kind == 0) limits.fileBytes = 1;
      if (kind == 1) limits.parserBytes = 1;
      if (kind == 2) limits.decodedBytes = 1;
      failure(otglb::load(path, limits), otglb::Status::ResourceLimit);
    }
    otglb::Limits limits; limits.hierarchyDepth = 1;
    path = files.write(f.glb(f.json(mesh() + ",\"nodes\":[{\"children\":[1]},{\"children\":[2]},{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]")));
    failure(otglb::load(path, limits), otglb::Status::ResourceLimit);
  });
  run("larger scene retains shared meshes without a simplicity cap", [&] {
    Fixture f;
    Bytes positions, indices;
    for (int y = 0; y <= 64; ++y) for (int x = 0; x <= 64; ++x) {
      auto position = floats({float(x),float(y),0});
      positions.insert(positions.end(), position.begin(), position.end());
      if (x < 64 && y < 64) {
        std::uint32_t a = std::uint32_t(y * 65 + x);
        for (auto index : {a,a+1,a+65,a+1,a+66,a+65}) word(indices, index);
      }
    }
    f.accessor(f.view(positions), 5126, 65*65, "VEC3",
               ",\"min\":[0,0,0],\"max\":[64,64,0]");
    f.accessor(f.view(indices), 5125, 64*64*6, "SCALAR");
    std::vector<std::string> nodes, roots;
    for (int i = 0; i < 2048; ++i) {
      nodes.push_back("{\"mesh\":0,\"translation\":[" + std::to_string(i) + ",0,0]}");
      roots.push_back(std::to_string(i));
    }
    auto r = load(f, mesh(",\"indices\":1") + ",\"nodes\":" + array(nodes) + ",\"scenes\":[{\"nodes\":" + array(roots) + "}]");
    success(r);
    check(r.asset->nodes.size() == 2048 && r.asset->meshes.size() == 1, "shared scene geometry");
    check(r.asset->meshes[0].primitives[0].triangleCount == 8192, "dense mesh topology");
    bounds(r.asset->scenes[0].bounds, {0,0,0,2111,64,0});
    std::cout << "  8192 triangles, 2048 instances: " << r.loadMilliseconds << " ms, " << r.decodedBytes << " decoded bytes\n";
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
