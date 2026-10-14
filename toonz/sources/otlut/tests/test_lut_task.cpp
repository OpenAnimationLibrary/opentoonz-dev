#include "../../toonzqt/lutgeneratorwait.h"
#include "palette_fit.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <iostream>
#include <stdexcept>

namespace {
// Error messages and save dialogs run nested event loops after future::get().
// Exercise that sequence using the production polling helper.
void dialogEventLoop() {
  QEventLoop loop;
  QTimer::singleShot(75, &loop, &QEventLoop::quit);
  loop.exec();
}
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    for (bool fail : {true, false, true, false}) {
      auto work = std::async(std::launch::async, [fail] {
        std::vector<otlut::ColorPair> pairs = {
            {{1, 0, 0}, {0, 1, 0}, 0.1f, "change"}};
        if (fail) pairs.push_back({{1, 0, 0}, {1, 0, 0}, 0.1f, "preserve"});
        return otlut::fitLutFromColorPairs(pairs, 33);
      });
      LutGenerator::waitForTask(work);
      bool caught = false;
      try {
        work.get();
      } catch (const std::invalid_argument &) {
        caught = true;
      }
      if (caught != fail || work.valid())
        throw std::runtime_error("Unexpected background task result");
      dialogEventLoop();
    }
    std::cout << "Error, retry and post-result dialog event loops passed.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
