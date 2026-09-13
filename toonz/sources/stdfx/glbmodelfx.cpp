#include "stdfx.h"
#include "tfxparam.h"
#include "tnotanimatableparam.h"

// Framework only: these scene-owned settings describe a read-only GLB instance.
// No model is opened, changed, or rendered by this stage of the FX.
class GlbModelFx final : public TStandardZeraryFx {
  FX_PLUGIN_DECLARATION(GlbModelFx)

  TStringParamP m_modelFile;
  TDoubleParamP m_positionX, m_positionY, m_positionZ;
  TDoubleParamP m_rotationX, m_rotationY, m_rotationZ;
  TDoubleParamP m_scale;
  TIntEnumParamP m_projection;
  TDoubleParamP m_cameraDistance, m_fieldOfView, m_orthoSize;
  TDoubleParamP m_nearClip, m_farClip;
  TIntEnumParamP m_lighting, m_renderStyle;

public:
  GlbModelFx()
      : m_modelFile(L"")
      , m_positionX(0.0)
      , m_positionY(0.0)
      , m_positionZ(0.0)
      , m_rotationX(0.0)
      , m_rotationY(0.0)
      , m_rotationZ(0.0)
      , m_scale(100.0)
      , m_projection(new TIntEnumParam(0, "Orthographic"))
      , m_cameraDistance(10.0)
      , m_fieldOfView(45.0)
      , m_orthoSize(10.0)
      , m_nearClip(0.1)
      , m_farClip(1000.0)
      , m_lighting(new TIntEnumParam(0, "Unlit"))
      , m_renderStyle(new TIntEnumParam(0, "Solid")) {
    bindParam(this, "modelFile", m_modelFile);
    bindParam(this, "positionX", m_positionX);
    bindParam(this, "positionY", m_positionY);
    bindParam(this, "positionZ", m_positionZ);
    bindParam(this, "rotationX", m_rotationX);
    bindParam(this, "rotationY", m_rotationY);
    bindParam(this, "rotationZ", m_rotationZ);
    bindParam(this, "scale", m_scale);
    bindParam(this, "projection", m_projection);
    bindParam(this, "cameraDistance", m_cameraDistance);
    bindParam(this, "fieldOfView", m_fieldOfView);
    bindParam(this, "orthoSize", m_orthoSize);
    bindParam(this, "nearClip", m_nearClip);
    bindParam(this, "farClip", m_farClip);
    bindParam(this, "lighting", m_lighting);
    bindParam(this, "renderStyle", m_renderStyle);

    m_projection->addItem(1, "Perspective");
    m_lighting->addItem(1, "Headlight");
    m_renderStyle->addItem(1, "Wireframe");
    m_rotationX->setMeasureName("angle");
    m_rotationY->setMeasureName("angle");
    m_rotationZ->setMeasureName("angle");
    m_fieldOfView->setMeasureName("angle");
    m_scale->setValueRange(0.001, 100000.0);
    m_cameraDistance->setValueRange(0.001, 1000000.0);
    m_fieldOfView->setValueRange(1.0, 179.0);
    m_orthoSize->setValueRange(0.001, 1000000.0);
    m_nearClip->setValueRange(0.001, 1000000.0);
    m_farClip->setValueRange(0.001, 1000000.0);
    enableComputeInFloat(true);
  }

  bool isZerary() const override { return true; }

  // Do not claim visible geometry or allocate an infinite render region for a
  // framework-only node. Direct tile requests are also deterministically empty.
  bool doGetBBox(double, TRectD &bbox, const TRenderSettings &) override {
    bbox = TRectD();
    return false;
  }

  bool canHandle(const TRenderSettings &, double) override { return true; }

  void doCompute(TTile &tile, double, const TRenderSettings &) override {
    tile.getRaster()->clear();
  }
};

FX_PLUGIN_IDENTIFIER(GlbModelFx, "glbModelFx")
