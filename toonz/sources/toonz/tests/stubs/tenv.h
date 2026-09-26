#pragma once

#include "testenvironment.h"

#include <set>
#include <string>

namespace TEnv {
inline TFilePath getConfigDir() {
  return TFilePath(TestEnvironment::under("config"));
}
inline TFilePath getStuffDir() { return TFilePath(TestEnvironment::root()); }
inline std::set<std::string> getRegisteredVariableNames() {
  return {"RecognizedOption"};
}
}  // namespace TEnv
