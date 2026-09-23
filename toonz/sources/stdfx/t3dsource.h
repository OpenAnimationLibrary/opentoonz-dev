#pragma once

#include "glbrenderer.h"
#include "trasterfx.h"
#include "toonz/tcolumnfx.h"

#include <memory>

// Internal contract for FX nodes that can provide an unevaluated 3D render
// result to another 3D-aware FX. It is deliberately separate from raster
// compute so lighting can be applied before the model is flattened to pixels.
class T3DRenderSource {
public:
  virtual ~T3DRenderSource() = default;

  virtual std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig *lighting                   = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr) const = 0;
};

// The schematic connects a zerary column, whereas the render tree connects the
// underlying FX. Keep the original column connection for ownership, exposure
// and scene persistence; unwrap it only when checking/accessing the 3D source.
class T3DSourcePort final : public TRasterFxPort {
  static T3DRenderSource *resolve(TFx *fx) {
    if (auto *column = dynamic_cast<TZeraryColumnFx *>(fx))
      fx = column->getZeraryFx();
    return dynamic_cast<T3DRenderSource *>(fx);
  }

public:
  void setFx(TFx *fx) override {
    if (fx && !resolve(fx))
      throw TException("Fx: 3D source port requires a compatible 3D FX");
    TRasterFxPort::setFx(fx);
  }

  T3DRenderSource *source() const { return resolve(getFx()); }
};
