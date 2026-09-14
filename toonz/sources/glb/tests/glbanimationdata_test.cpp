#include "glbloader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;
void check(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}
void near(float a, float b) { check(std::abs(a - b) < 0.0001f, "numeric mismatch"); }
void word(Bytes &b, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) b.push_back((value >> (8 * i)) & 255);
}
Bytes floats(std::initializer_list<float> values) {
  Bytes out;
  for (float value : values) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    word(out, bits);
  }
  return out;
}
std::string array(const std::vector<std::string> &values) {
  std::string out = "[";
  for (const auto &value : values) out += (out.size() == 1 ? "" : ",") + value;
  return out + "]";
}
std::string replace(std::string text, const std::string &from, const std::string &to) {
  const auto at = text.find(from);
  check(at != std::string::npos, "fixture replacement not found: " + from);
  text.replace(at, from.size(), to);
  return text;
}
struct Fixture {
  Bytes bin;
  std::vector<std::string> views, accessors;
  int view(const Bytes &bytes) {
    while (bin.size() % 4) bin.push_back(0);
    views.push_back("{\"buffer\":0,\"byteOffset\":" + std::to_string(bin.size()) +
        ",\"byteLength\":" + std::to_string(bytes.size()) + "}");
    bin.insert(bin.end(), bytes.begin(), bytes.end());
    return int(views.size() - 1);
  }
  int accessor(int view, int component, std::size_t count, const char *type,
               const std::string &extra = "") {
    accessors.push_back("{\"componentType\":" + std::to_string(component) +
        ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"" +
        (view < 0 ? "" : ",\"bufferView\":" + std::to_string(view)) + extra + "}");
    return int(accessors.size() - 1);
  }
  Bytes glb(const std::string &fields) const {
    std::string json = "{\"asset\":{\"version\":\"2.0\"}";
    if (!bin.empty()) json += ",\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) +
        "}],\"bufferViews\":" + array(views) + ",\"accessors\":" + array(accessors);
    json += fields + "}";
    while (json.size() % 4) json += ' ';
    Bytes buffer = bin, out;
    while (buffer.size() % 4) buffer.push_back(0);
    word(out, 0x46546c67); word(out, 2);
    word(out, std::uint32_t(20 + json.size() + (buffer.empty() ? 0 : 8 + buffer.size())));
    word(out, std::uint32_t(json.size())); word(out, 0x4e4f534a);
    out.insert(out.end(), json.begin(), json.end());
    if (!buffer.empty()) {
      word(out, std::uint32_t(buffer.size())); word(out, 0x004e4942);
      out.insert(out.end(), buffer.begin(), buffer.end());
    }
    return out;
  }
};
struct Files {
  std::filesystem::path directory;
  int serial = 0;
  Files() {
    directory = std::filesystem::temp_directory_path() / ("otglb-animation-" +
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(directory), "create test directory");
  }
  ~Files() { std::error_code error; std::filesystem::remove_all(directory, error); }
  std::filesystem::path write(const Bytes &bytes) {
    const auto path = directory / std::filesystem::u8path(
        std::string(u8"keys-\u00e9-") + std::to_string(++serial) + ".glb");
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    check(bool(file), "write fixture");
    return path;
  }
};
void success(const otglb::Result &r) {
  check(bool(r), "load failed: " + r.error);
  check(r.status == otglb::Status::LoadedWithWarnings, "missing unevaluated-data warning");
}
void failure(const otglb::Result &r, otglb::Status status = otglb::Status::Invalid) {
  check(!r && !r.asset && r.status == status && !r.error.empty(),
        "expected failure, got " + std::string(otglb::statusName(r.status)) + ": " + r.error);
}
Fixture translation(std::initializer_list<float> times = {0, 1},
                    std::initializer_list<float> values = {0, 0, 0, 1, 2, 3}) {
  Fixture f;
  const auto first = std::isfinite(*times.begin()) ? *times.begin() : 0.0f;
  const auto last = std::isfinite(*(times.end() - 1)) ? *(times.end() - 1) : 1.0f;
  f.accessor(f.view(floats(times)), 5126, times.size(), "SCALAR",
      ",\"min\":[" + std::to_string(first) + "],\"max\":[" + std::to_string(last) + "]");
  f.accessor(f.view(floats(values)), 5126, values.size() / 3, "VEC3");
  return f;
}
const std::string graph = R"(,"nodes":[{},{}],"scenes":[{"nodes":[0,1]}],"scene":0)";
std::string clip(const std::string &interpolation = "LINEAR", const std::string &path = "translation") {
  return ",\"animations\":[{\"name\":\"Run\",\"samplers\":[{\"input\":0,\"output\":1,\"interpolation\":\"" +
      interpolation + "\"}],\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"" + path + "\"}}]}]";
}
const std::string skinGraph = R"(,"nodes":[{"mesh":0,"skin":0},{"children":[2]},{"translation":[0,2,0]}],"scenes":[{"nodes":[0,1]}],"scene":0,"skins":[{"name":"Rig","skeleton":1,"joints":[2,1],"inverseBindMatrices":3}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2}}]}])";
Fixture skin() {
  Fixture f;
  f.accessor(f.view(floats({0,0,0, 1,0,0, 0,1,0})), 5126, 3, "VEC3", ",\"min\":[0,0,0],\"max\":[1,1,0]");
  f.accessor(f.view({0,1,0,0, 0,1,0,0, 0,1,0,0}), 5121, 3, "VEC4");
  f.accessor(f.view({128,127,0,0, 128,127,0,0, 128,127,0,0}), 5121, 3, "VEC4", ",\"normalized\":true");
  f.accessor(f.view(floats({1,0,0,0, 0,1,0,0, 0,0,1,0, 3,-2,5,1,
                           1,0,0,0, 0,1,0,0, 0,0,1,0, 7,8,9,1})), 5126, 2, "MAT4");
  return f;
}
}  // namespace

int main() {
  Files files;
  int passed = 0, failed = 0;
  auto run = [&](const char *name, const std::function<void()> &test) {
    try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception &e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
  };
  auto load = [&](const Fixture &f, const std::string &fields) {
    return otglb::load(files.write(f.glb(fields)));
  };
  run("owned clip, final key, unchanged static TRS and shared sampler", [&] {
    auto fields = replace(clip(), R"("node":1)", R"("node":0)");
    fields = replace(fields, R"("channels":[)", R"("channels":[{"sampler":0,"target":{"node":1,"path":"translation"}},)");
    auto r = load(translation({0.25f, 1.5f}), fields + replace(graph, "[{},{}]",
        R"([{"translation":[4,5,6],"scale":[2,3,4]},{}])"));
    success(r);
    const auto &a = r.asset->animations.at(0);
    check(r.asset->animationCount == 1 && a.name == "Run", "clip identity");
    check(a.samplers.size() == 1 && a.channels.size() == 2, "shared sampler duplicated");
    near(a.firstKeyTime, 0.25f); near(a.lastKeyTime, 1.5f);
    check(a.samplers[0].times == std::vector<float>({0.25f,1.5f}), "time changed");
    check(a.samplers[0].values == std::vector<float>({0,0,0,1,2,3}), "values changed");
    check(a.channels[0].sampler == a.channels[1].sampler && a.channels[0].components == 3, "channel association");
    near(r.asset->nodes[0].translation[0], 4); near(r.asset->nodes[0].local[12], 4);
    near(r.asset->nodes[0].scale[2], 4); near(r.asset->nodes[1].rotation[3], 1);
    check(!r.asset->nodes[0].hasMatrix && r.asset->meshes.empty(), "hierarchy-only animation");
  });
  run("STEP, CUBICSPLINE tangents and independent named clips", [&] {
    auto f = translation({0,1}, {0,0,0, 1,2,3, 4,5,6, 7,8,9, 10,11,12, 0,0,0});
    auto r = load(f, clip("CUBICSPLINE") + graph);
    success(r);
    const auto &s = r.asset->animations[0].samplers[0];
    check(s.interpolation == otglb::AnimationInterpolation::CubicSpline && s.values.size() == 18, "cubic layout");
    near(s.values[3], 1); near(s.values[6], 4); near(s.values[9], 7); near(s.values[12], 10);
    auto clips = clip("STEP");
    clips = replace(clips, R"(]}])", R"(]},{"name":"Run","samplers":[{"input":0,"output":1}],"channels":[{"sampler":0,"target":{"node":1,"path":"scale"}}]}])");
    r = load(translation(), clips + graph);
    success(r);
    check(r.asset->animations.size() == 2 && r.asset->animations[0].name == r.asset->animations[1].name, "duplicate clip labels merged");
    check(r.asset->animations[0].samplers[0].interpolation == otglb::AnimationInterpolation::Step, "STEP changed");
    check(r.asset->animations[1].channels[0].path == otglb::AnimationPath::Scale, "scale target");
  });
  run("normalized quaternion accessor and zero cubic tangents", [&] {
    auto f = translation();
    f.accessors[1] = "{\"componentType\":5122,\"count\":2,\"type\":\"VEC4\",\"bufferView\":2,\"normalized\":true}";
    f.view({0,0,0,0,0,0,255,127, 0,0,255,127,0,0,0,0});
    auto r = load(f, clip("LINEAR", "rotation") + graph);
    success(r);
    near(r.asset->animations[0].samplers[0].values[3], 1);
    near(r.asset->animations[0].samplers[0].values[5], 1);
    Fixture q;
    q.accessor(q.view(floats({0,1})), 5126, 2, "SCALAR", ",\"min\":[0],\"max\":[1]");
    q.accessor(q.view(floats({0,0,0,0, 0,0,0,1, 0,0,0,0, 0,0,0,0, 0,1,0,0, 0,0,0,0})), 5126, 6, "VEC4");
    success(load(q, clip("CUBICSPLINE", "rotation") + graph));
    q.accessors[1] = replace(q.accessors[1], R"("count":6)", R"("count":2)");
    failure(load(q, clip("LINEAR", "rotation") + graph));
  });
  run("skin joint ordering, matrix layout, normalized weights and extra matrices", [&] {
    auto r = load(skin(), skinGraph);
    success(r);
    const auto &s = r.asset->skins.at(0);
    check(s.joints == std::vector<int>({2,1}) && s.skeleton == 1, "joint table reordered");
    check(s.hasInverseBindMatrices && s.inverseBindMatrices.size() == 2, "inverse binds absent");
    near(s.inverseBindMatrices[0][12], 3); near(s.inverseBindMatrices[0][13], -2);
    near(s.inverseBindMatrices[0][3], 0); near(s.inverseBindMatrices[1][14], 9);
    check(r.asset->nodes[0].hasSkin && r.asset->nodes[0].skin == 0, "skin association");
    near(r.asset->meshes[0].primitives[0].attributes[2].values[0], 128.0f/255.0f);
    // An unused skin may contain more inverse binds than joints. Preserve all.
    auto fields = replace(skinGraph, R"("joints":[2,1])", R"("joints":[2])");
    fields = replace(fields, R"(,"skin":0)", "");
    r = load(skin(), fields); success(r);
    check(r.asset->skins[0].inverseBindMatrices.size() == 2, "extra inverse bind discarded");
  });
  run("absent inverse binds produce identities; unused skins remain owned", [&] {
    auto r = load(skin(), replace(skinGraph, R"(,"inverseBindMatrices":3)", ""));
    success(r);
    const auto &s = r.asset->skins[0];
    check(!s.hasInverseBindMatrices && s.inverseBindMatrices.size() == 2, "identity fallback");
    for (const auto &m : s.inverseBindMatrices)
      for (int c = 0; c < 16; ++c) near(m[c], c % 5 == 0 ? 1.0f : 0.0f);
    Fixture empty;
    r = load(empty, graph + R"(,"skins":[{"joints":[1]}])");
    success(r);
    check(r.asset->skins.size() == 1 && r.asset->animations.empty(), "unused skin discarded");
  });
  run("sparse animation keys and sparse inverse bind matrices", [&] {
    auto f = translation();
    int index = f.view({1}), values = f.view(floats({9,8,7}));
    f.accessors[1] = replace(f.accessors[1], "\"bufferView\":1", "\"bufferView\":1,\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":" +
        std::to_string(index) + ",\"componentType\":5121},\"values\":{\"bufferView\":" + std::to_string(values) + "}}");
    auto r = load(f, clip() + graph);
    success(r); near(r.asset->animations[0].samplers[0].values[3], 9);
    f = skin();
    index = f.view({0,1});
    values = f.view(floats({1,0,0,0, 0,1,0,0, 0,0,1,0, 2,3,4,1,
                           1,0,0,0, 0,1,0,0, 0,0,1,0, 5,6,7,1}));
    f.accessors[3] = replace(f.accessors[3], ",\"bufferView\":3", ",\"sparse\":{\"count\":2,\"indices\":{\"bufferView\":" +
        std::to_string(index) + ",\"componentType\":5121},\"values\":{\"bufferView\":" + std::to_string(values) + "}}");
    r = load(f, skinGraph);
    success(r); near(r.asset->skins[0].inverseBindMatrices[1][14], 7);
  });
  run("morph weight channel data retained without morph evaluation", [&] {
    Fixture f;
    f.accessor(f.view(floats({0,1})), 5126, 2, "SCALAR", ",\"min\":[0],\"max\":[1]");
    f.accessor(f.view(floats({0,1, 1,0})), 5126, 4, "SCALAR");
    f.accessor(f.view(floats({0,0,0, 1,0,0, 0,1,0})), 5126, 3, "VEC3", ",\"min\":[0,0,0],\"max\":[1,1,0]");
    auto fields = clip("LINEAR", "weights") + replace(graph, "[{},{}]", "[{},{\"mesh\":0}]") +
        R"(,"meshes":[{"primitives":[{"attributes":{"POSITION":2},"targets":[{"POSITION":2},{"POSITION":2}]}]}])";
    auto r = load(f, fields); success(r);
    check(r.asset->animations[0].channels[0].components == 2, "morph scalar grouping");
    check(r.asset->animations[0].samplers[0].values == std::vector<float>({0,1,1,0}), "morph values");
    failure(load(f, replace(fields, R"(,{"POSITION":2})", "")));
  });
  run("missing node and optional extension target do not become TRS", [&] {
    auto fields = replace(clip(), R"("node":1,)", "");
    auto r = load(translation(), fields + graph); success(r);
    check(r.asset->animations[0].channels[0].node == otglb::NoIndex, "invented target");
    fields = replace(fields, R"("path":"translation")",
        R"("path":"pointer","extensions":{"KHR_animation_pointer":{"pointer":"/materials/0/emissiveFactor"}})");
    fields += graph + R"(,"materials":[{}],"extensionsUsed":["KHR_animation_pointer"])");
    r = load(translation(), fields); success(r);
    check(r.asset->animations[0].channels[0].path == otglb::AnimationPath::Unknown, "extension interpreted as TRS");
    failure(load(translation(), fields + R"(,"extensionsRequired":["KHR_animation_pointer"])"), otglb::Status::Unsupported);
  });
  run("invalid times, values, cardinality, references and duplicate targets", [&] {
    for (auto times : {std::vector<float>{-1,1}, {0,0}, {1,0}})
      failure(load(translation({times[0],times[1]}), clip() + graph));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    failure(load(translation({0,nan}), clip() + graph));
    failure(load(translation({0,1}, {0,0,0,nan,0,0}), clip() + graph));
    failure(load(translation({0}, {0,0,0}), clip("CUBICSPLINE") + graph));
    failure(load(translation(), clip("CUBICSPLINE") + graph));
    failure(load(translation(), clip("LINEAR", "rotation") + graph));
    failure(load(translation(), replace(clip(), R"("node":1)", R"("node":99)") + graph));
    failure(load(translation(), replace(clip(), R"("sampler":0)", R"("sampler":99)") + graph));
    failure(load(translation(), replace(clip(), R"("channels":[)", R"("channels":[{"sampler":0,"target":{"node":1,"path":"translation"}},)") + graph));
    failure(load(translation(), clip() + replace(graph, "[{},{}]", R"([{},{"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}])")));
  });
  run("invalid skin references, missing influence sets and matrix layouts", [&] {
    failure(load(skin(), replace(skinGraph, R"("joints":[2,1])", R"("joints":[2,2])")));
    failure(load(skin(), replace(skinGraph, R"("joints":[2,1])", R"("joints":[2,99])")));
    failure(load(skin(), replace(skinGraph, R"("skeleton":1)", R"("skeleton":0)")));
    failure(load(skin(), replace(skinGraph, R"(,"WEIGHTS_0":2)", "")));
    failure(load(skin(), replace(skinGraph, R"("inverseBindMatrices":3)", R"("inverseBindMatrices":0)")));
    auto f = skin();
    f.bin[36] = 2;
    failure(load(f, skinGraph));
    f = skin();
    f.accessors[3] = replace(f.accessors[3], R"("count":2)", R"("count":1)");
    failure(load(f, skinGraph));
    f = skin();
    // Inverse binds start after positions (36), joints (12), weights (12).
    const auto bad = floats({2});
    std::copy(bad.begin(), bad.end(), f.bin.begin() + 60 + 15 * 4);
    failure(load(f, skinGraph));
  });
  run("additional influence sets are retained and validated", [&] {
    auto f = skin();
    const int weights = f.view(floats({0.75f,0,0,0, 0.75f,0,0,0, 0.75f,0,0,0}));
    f.accessors[2] = "{\"componentType\":5126,\"count\":3,\"type\":\"VEC4\",\"bufferView\":" + std::to_string(weights) + "}";
    f.accessor(f.view({1,0,0,0, 1,0,0,0, 1,0,0,0}), 5121, 3, "VEC4");
    f.accessor(f.view(floats({0.25f,0,0,0, 0.25f,0,0,0, 0.25f,0,0,0})), 5126, 3, "VEC4");
    auto fields = replace(skinGraph, R"("WEIGHTS_0":2)", R"("WEIGHTS_0":2,"JOINTS_1":4,"WEIGHTS_1":5)");
    auto r = load(f, fields); success(r);
    const auto &attributes = r.asset->meshes[0].primitives[0].attributes;
    check(attributes.size() == 5 && attributes[3].semantic == "JOINTS_1", "influences truncated");
    near(attributes[4].values[0], 0.25f);
    failure(load(f, replace(fields, R"(,"WEIGHTS_1":5)", "")));
  });
  run("source preservation, owned lifetime and exact allocation boundary", [&] {
    const auto f = translation(); const auto bytes = f.glb(clip() + graph);
    const auto path = files.write(bytes);
    auto r = otglb::load(path); success(r);
    std::ifstream input(path, std::ios::binary);
    const Bytes after(std::istreambuf_iterator<char>(input), {});
    check(bytes == after, "source changed"); input.close();
    otglb::Limits limits; limits.decodedBytes = r.decodedBytes;
    success(otglb::load(path, limits));
    --limits.decodedBytes;
    failure(otglb::load(path, limits), otglb::Status::ResourceLimit);
    const auto owned = r.asset;
    r = {};
    std::filesystem::remove(path);
    near(owned->animations[0].samplers[0].values.back(), 3);
    near(owned->animations[0].samplers[0].times.back(), 1);
    failure(otglb::load(path), otglb::Status::IoError);
  });
  std::cout << passed << " passed, " << failed << " failed\n";
  return failed ? 1 : 0;
}
