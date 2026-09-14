#pragma once

#include "glbrenderer.h"
#include "trasterfx.h"

#include <memory>

// Internal contract for FX nodes that can provide an unevaluated 3D render
// result to another 3D-aware FX. It is deliberately separate from raster
// compute so lighting can be applied before the model is flattened to pixels.
class T3DRenderSource {
public:
  virtual ~T3DRenderSource() = default;

  virtual std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig *lighting = nullptr) const = 0;
};

// Normal OpenToonz input port with an additional runtime type check. At present
// GLB Model is the only production FX implementing T3DRenderSource, so this
// prevents accidentally connecting an ordinary raster source.
class T3DSourcePort final : public TRasterFxPort {
public:
  void setFx(TFx *fx) override {
    if (fx && !dynamic_cast<T3DRenderSource *>(fx))
      throw TException("Fx: 3D source port requires a compatible 3D model FX");
    TRasterFxPort::setFx(fx);
  }

  T3DRenderSource *source() const {
    return dynamic_cast<T3DRenderSource *>(getFx());
  }
};
