#pragma once

#include "testenvironment.h"

namespace TSystem {
inline QString getUserName() { return TestEnvironment::user(); }
}  // namespace TSystem
