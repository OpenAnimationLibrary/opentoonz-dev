#include "previewdrawingscheduler.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cassert>
#include <iostream>
#include <vector>

static void wait(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  std::vector<int> rendered, canceled;
  PreviewDrawingScheduler scheduler(
      &app,
      [&](int frame) {
        rendered.push_back(frame);
        return true;
      },
      [&](int frame) { canceled.push_back(frame); });
  scheduler.setEnabled(true);

  // Stroke input pauses all work; repeated requests collapse to the latest
  // foreground frame, which starts before the queued range after release.
  scheduler.setBusy(true);
  scheduler.requestForeground(5);
  scheduler.requestForeground(5);
  scheduler.requestBackground({1, 2, 2, 5});
  wait(20);
  assert(rendered.empty());
  scheduler.setBusy(false);
  assert(rendered.empty()); // still queued, never synchronous inside release
  wait(20);
  assert((rendered == std::vector<int>{5}));
  scheduler.completed(5);
  wait(100);
  assert(rendered.size() == 1);
  wait(500);
  assert((rendered == std::vector<int>{5, 1}));

  // Foreground preempts background. Interrupted background frames are kept,
  // deduplicated, and eventually resume after the foreground is ready.
  scheduler.requestForeground(6);
  wait(20);
  assert((canceled == std::vector<int>{1}));
  assert(rendered.back() == 6);
  scheduler.completed(1); // late completion of the canceled background job
  wait(20);
  assert(rendered.back() == 6);
  scheduler.completed(6);
  wait(600);
  assert(rendered.back() == 2);
  scheduler.completed(2);
  wait(20);
  assert(rendered.back() == 1);
  scheduler.completed(1);
  wait(20);
  assert((rendered == std::vector<int>{5, 1, 6, 2, 1}));

  // Starting another stroke cancels the running job. No resubmission happens
  // before release, including when the invalidated frame is requested again.
  scheduler.requestForeground(7);
  wait(20);
  scheduler.setBusy(true);
  scheduler.invalidate(7);
  scheduler.requestForeground(7);
  wait(20);
  assert(canceled.back() == 7);
  auto count = rendered.size();
  scheduler.setBusy(false);
  wait(20);
  assert(rendered.size() == count + 1 && rendered.back() == 7);
  scheduler.invalidate(7);
  wait(20);
  assert(rendered.back() == 7 && rendered.size() == count + 2);
  scheduler.completed(7);

  // Disabling / playback restores all queued frames rather than losing them.
  scheduler.requestBackground({8, 9});
  scheduler.setEnabled(false);
  assert(rendered[rendered.size() - 2] == 8 && rendered.back() == 9);
  scheduler.requestForeground(10);
  assert(rendered.back() == 10);

  // Closing/switching the scene discards pending work and stops the timer.
  scheduler.setEnabled(true);
  scheduler.requestForeground(11);
  scheduler.requestBackground({12});
  scheduler.clear();
  count = rendered.size();
  wait(600);
  assert(rendered.size() == count);

  // A foreground frame superseded before submission still belongs to the
  // requested range and must eventually render in the background.
  scheduler.requestForeground(13);
  scheduler.requestForeground(14);
  wait(20);
  assert(rendered.back() == 14);
  scheduler.completed(14);
  wait(600);
  assert(rendered.back() == 13);
  scheduler.completed(13);

  // A frame already cached (no render submitted) must not stall the queue.
  std::vector<int> cachedRequests;
  PreviewDrawingScheduler cached(
      &app,
      [&](int frame) {
        cachedRequests.push_back(frame);
        return false;
      },
      [](int) {});
  cached.setEnabled(true);
  cached.requestForeground(0);
  cached.requestBackground({1, 2});
  wait(1150);
  assert((cachedRequests == std::vector<int>{0, 1, 2}));
  std::cout << "Preview drawing scheduler tests passed\n";
}
