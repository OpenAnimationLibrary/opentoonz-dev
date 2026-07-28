#pragma once

#include <QWidget>

namespace ExperimentalSvg {

enum class OpenMode {
  Cancel,
  OpenExperimentalSvgLevel,
  ConvertToToonzVector
};

class OpenModeDialog final {
public:
  static OpenMode ask(QWidget *parent = nullptr);
};

}  // namespace ExperimentalSvg
