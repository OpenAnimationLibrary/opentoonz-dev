#pragma once

#include "bulletworld.h"
#include "toonz/tstageobject.h"

#include <array>

class TStageObjectTree;

namespace OTBullet {
struct SourceFrame {
  TAffine world, parent;
  TPointD center;
  double x = 0, y = 0, angle = 0;
};
struct Binding {
  TStageObjectP object;
  TStageObjectId parentId;
  std::vector<TStageObjectP> ancestors;  // Immediate parent through root.
  std::vector<SourceFrame> source;
};
struct ChannelEdit {
  TDoubleParamP channel, before, after;
};
using Bake = std::vector<ChannelEdit>;

// Captures authored transforms only. Path/pinned IK/cyclic animation and moving
// hook pivots are deliberately excluded from the first adapter.
bool capture(TStageObjectTree &tree, const std::vector<TStageObject *> &objects,
             int firstFrame, int frames, std::vector<Binding> &bindings,
             std::string &error);

// Samples retained authored controllers at the exact simulation substeps.
// Retained bodies cannot descend from a body whose channels will be baked.
// Failure leaves bodies unchanged. No scene is mutated.
bool prepareControllers(const std::vector<Binding> &bindings,
                        const Settings &settings, int firstFrame,
                        double metersPerInch, std::vector<Body> &bodies,
                        std::string &error);

// Preflights every frame before producing a patch. Only X/Y/rotation change;
// unrepresentable affine poses fail instead of silently dropping scale/shear.
bool makeBake(const std::vector<Binding> &bindings,
              const std::vector<Body> &bodies, const Clip &clip, int firstFrame,
              double metersPerInch, Bake &bake, std::string &error);
void applyBake(const Bake &bake, bool after);
}  // namespace OTBullet
