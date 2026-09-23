#pragma once

#include "glbrenderer.h"
#include "trasterfx.h"
#include "toonz/tcolumnfx.h"

#include <memory>

// Internal contract for composable 3D FX. Lighting and material state are kept
// separate so either modifier can appear before or after the other in the FX
// Schematic. Downstream state wins when two nodes of the same kind are chained.
struct T3DRenderContext {
  bool hasLighting = false;
  otglb::LightingRig lighting;
  bool hasMaterial = false;
  otglb::MaterialRig material;
};

class T3DRenderSource {
public:
  virtual ~T3DRenderSource() = default;

  virtual std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const T3DRenderContext &context = T3DRenderContext()) const = 0;
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
