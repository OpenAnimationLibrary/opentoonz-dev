#pragma once

#include <QString>

class CommandManager {
public:
  static CommandManager *instance() {
    static CommandManager value;
    return &value;
  }
  void *getAction(const char *id, bool) {
    return QString::fromUtf8(id) == "MI_Open" ? this : nullptr;
  }
};
