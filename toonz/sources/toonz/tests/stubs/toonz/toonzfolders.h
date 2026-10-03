#pragma once

#include "testenvironment.h"

namespace ToonzFolder {
inline TFilePath getMyModuleDir() {
  return TFilePath(TestEnvironment::under("profiles/layouts/settings." +
                                          TestEnvironment::user()));
}
inline TFilePath getProfileFolder() {
  return TFilePath(TestEnvironment::under("profiles"));
}
inline TFilePath getFxPresetFolder() {
  return TFilePath(TestEnvironment::under("fxs"));
}
inline TFilePath getLibraryFolder() {
  return TFilePath(TestEnvironment::under("library"));
}
}  // namespace ToonzFolder
