#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// A complete, self-contained triangle, shared by loader-to-renderer and FX tests.
inline std::vector<unsigned char> triangleGlb(float offset = 0, bool materials = false) {
  std::string json = "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":36}],"
      "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],\"accessors\":[{\"bufferView\":0,"
      "\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[" +
      std::to_string(offset - 1) + ",-1,0],\"max\":[" + std::to_string(offset + 1) +
      ",1,0]}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
      "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
  if (materials) {
    const auto pos = json.find("\"attributes\"");
    json.insert(pos, "\"material\":0,");
    json.insert(1, "\"materials\":[{\"name\":\"Body\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1]}},{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,1,0,1]}}],");
  }
  while (json.size() % 4) json += ' ';
  std::vector<unsigned char> bytes;
  auto word = [&](std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back((value >> (8 * i)) & 255);
  };
  word(0x46546c67); word(2); word(std::uint32_t(28 + json.size() + 36));
  word(std::uint32_t(json.size())); word(0x4e4f534a);
  bytes.insert(bytes.end(), json.begin(), json.end());
  word(36); word(0x004e4942);
  for (float value : {offset - 1, -1.0f, 0.0f, offset + 1, -1.0f, 0.0f,
                     offset, 1.0f, 0.0f}) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits)); word(bits);
  }
  return bytes;
}

// One skinned triangle and one joint. The named "Move" clip translates the
// joint from X=0 to X=1 over one second, so the whole triangle follows it.
inline std::vector<unsigned char> animatedSkinTriangleGlb() {
  std::string json =
      "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":192}],"
      "\"bufferViews\":["
      "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
      "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":12},"
      "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},"
      "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":64},"
      "{\"buffer\":0,\"byteOffset\":160,\"byteLength\":8},"
      "{\"buffer\":0,\"byteOffset\":168,\"byteLength\":24}],"
      "\"accessors\":["
      "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[-1,-1,0],\"max\":[1,1,0]},"
      "{\"bufferView\":1,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
      "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
      "{\"bufferView\":3,\"componentType\":5126,\"count\":1,\"type\":\"MAT4\"},"
      "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],\"max\":[1]},"
      "{\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],"
      "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2}}]}],"
      "\"nodes\":[{\"mesh\":0,\"skin\":0},{\"name\":\"Joint\"}],"
      "\"skins\":[{\"joints\":[1],\"inverseBindMatrices\":3}],"
      "\"animations\":[{\"name\":\"Move\",\"samplers\":[{\"input\":4,\"output\":5,\"interpolation\":\"LINEAR\"}],"
      "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}]}],"
      "\"scenes\":[{\"nodes\":[0,1]}],\"scene\":0}";
  while (json.size() % 4) json += ' ';

  std::vector<unsigned char> bin;
  auto appendFloat = [&](float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 4; ++i) bin.push_back((bits >> (8 * i)) & 255);
  };
  for (float value : {-1.0f,-1.0f,0.0f, 1.0f,-1.0f,0.0f, 0.0f,1.0f,0.0f})
    appendFloat(value);
  for (int i = 0; i < 3; ++i) {
    bin.push_back(0); bin.push_back(0); bin.push_back(0); bin.push_back(0);
  }
  for (int i = 0; i < 3; ++i)
    for (float value : {1.0f,0.0f,0.0f,0.0f}) appendFloat(value);
  for (int i = 0; i < 16; ++i) appendFloat(i % 5 == 0 ? 1.0f : 0.0f);
  appendFloat(0.0f); appendFloat(1.0f);
  for (float value : {0.0f,0.0f,0.0f, 1.0f,0.0f,0.0f}) appendFloat(value);

  std::vector<unsigned char> bytes;
  auto word = [&](std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back((value >> (8 * i)) & 255);
  };
  word(0x46546c67); word(2);
  word(std::uint32_t(28 + json.size() + bin.size()));
  word(std::uint32_t(json.size())); word(0x4e4f534a);
  bytes.insert(bytes.end(), json.begin(), json.end());
  word(std::uint32_t(bin.size())); word(0x004e4942);
  bytes.insert(bytes.end(), bin.begin(), bin.end());
  return bytes;
}
