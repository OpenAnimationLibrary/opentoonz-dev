#pragma once

#include <QEventLoop>
#include <QTimer>

#include <chrono>
#include <future>

namespace LutGenerator {

// Keep the polling timer scoped to the wait. Retrieving the result invalidates
// the future, and error/save dialogs may start another Qt event loop afterward.
template <typename Result>
void waitForTask(std::future<Result> &task) {
  QEventLoop loop;
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
    if (task.wait_for(std::chrono::milliseconds(0)) ==
        std::future_status::ready)
      loop.quit();
  });
  timer.start(25);
  loop.exec();
  timer.stop();
}

}  // namespace LutGenerator
