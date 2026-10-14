#pragma once

#include <string>
#include <vector>

// Read-only discovery interface used by the FX Settings controls.  The FX
// keeps ownership of the EXR document; the editor only receives stable values
// and human-readable labels to store in ordinary TStringParam parameters.
enum class TFxAovChoiceKind { Part, Layer, Channel };

struct TFxAovChoice {
  std::wstring value;
  std::wstring label;
};

class TFxAovSource {
public:
  virtual ~TFxAovSource()                                             = default;
  virtual std::vector<TFxAovChoice> getAovChoices(TFxAovChoiceKind kind,
                                                  double frame) const = 0;
  virtual std::wstring getAovSummary(double frame) const              = 0;
};
