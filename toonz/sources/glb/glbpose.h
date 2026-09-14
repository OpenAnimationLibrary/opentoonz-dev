#pragma once

#include "glbloader.h"

#include <array>
#include <string>
#include <vector>

namespace otglb {

struct PoseNode {
  Matrix local{}, world{};
  std::array<float, 3> translation{{0.0f, 0.0f, 0.0f}};
  std::array<float, 4> rotation{{0.0f, 0.0f, 0.0f, 1.0f}};
  std::array<float, 3> scale{{1.0f, 1.0f, 1.0f}};
};

// Joint matrices are in the skinned mesh node's local space, following glTF:
// inverse(meshWorld) * jointWorld * inverseBindMatrix.
struct SkinPose {
  int node = NoIndex;
  int skin = NoIndex;
  std::vector<Matrix> jointMatrices;
};

struct Pose {
  int animation = NoIndex;
  double sourceSeconds = 0.0;
  std::vector<PoseNode> nodes;
  std::vector<SkinPose> skins;
};

struct PoseResult {
  Pose pose;
  std::string error;
  std::vector<std::string> warnings;
  explicit operator bool() const { return error.empty(); }
};

// Pure, random-access evaluation. The Asset is never mutated and no playback
// clock is advanced. animation == NoIndex evaluates the authored/rest pose.
// Samplers clamp to their endpoint keys outside their own key range.
// Morph-weight channels are deliberately not applied until owned default weights
// and morph target deltas are added to the bridge.
PoseResult evaluatePose(const Asset &asset, int animation, double sourceSeconds);

}  // namespace otglb
