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
