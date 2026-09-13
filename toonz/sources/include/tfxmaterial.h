#pragma once

#include "tpixel.h"
#include "tparamset.h"
#include <string>
#include <vector>

// Optional read-only material discovery for FX editors. Asset ownership and
// loading stay with the source; animated overrides remain ordinary FX params.
struct TFxMaterial {
  std::string key;  // Persistence-safe identity, independent of the UI label.
  std::string name;
  TPixel32 color;
};

class TFxMaterialSource {
public:
  virtual ~TFxMaterialSource() = default;
  virtual std::vector<TFxMaterial> getMaterials() const = 0;
};

// A deep-copying dynamic set for scene-owned material overrides. Keeping this
// behavior here avoids changing TParamSet semantics for every existing FX.
class TFxMaterialParamSet final : public TParamSet {
public:
  TFxMaterialParamSet() : TParamSet("materialColors") {}
  ~TFxMaterialParamSet() override { removeAllParam(); }

  TParam *clone() const override {
    auto *result = new TFxMaterialParamSet;
    result->copy(const_cast<TFxMaterialParamSet *>(this));
    return result;
  }
  void loadData(TIStream &is) override {
    removeAllParam();
    TParamSet::loadData(is);
  }
  std::string getValueAlias(double frame, int precision) override {
    return getParamCount() ? TParamSet::getValueAlias(frame, precision) : "()";
  }
};
