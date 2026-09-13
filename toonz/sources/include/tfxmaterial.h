#pragma once

#include "tpixel.h"
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
