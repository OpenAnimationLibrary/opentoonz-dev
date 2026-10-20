// Enable with -DWITH_CHANNEL_MAPPING_TEST=ON in the OpenToonz build.
// Uses explicit checks so release builds do not compile away verification.
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"

#include <cstdint>
#include <limits>
#include <set>
#include <type_traits>

static_assert(!std::is_convertible<int, TChannelId>::value,
              "Integers must not implicitly become channel IDs");
static_assert(!std::is_convertible<TChannelId, int>::value,
              "Channel IDs must not implicitly become integers");

int main() {
  TStageObjectTree tree;
  auto *object = tree.getStageObject(TStageObjectId::ColumnId(0));
  using S = TStageObject;
  const S::Channel expected[] = {
      S::T_Angle, S::T_X,      S::T_Y,     S::T_Z,      S::T_SO,    S::T_ScaleX,
      S::T_ScaleY, S::T_Scale, S::T_Path,  S::T_ShearX, S::T_ShearY};
  std::set<std::uint32_t> ids;
  for (std::uint32_t i = 0; i < 11; ++i) {
    const TChannelId id(10 + i);
    if (S::getChannelId(expected[i]) != id) return 1;
    auto *parameter = object->findChannel(id);
    if (!parameter || parameter != object->getParam(expected[i])) return 2;
    // An edit through the new API must reach the existing authoritative curve.
    parameter->setValue(3, 2.5);
    if (object->getParam(expected[i])->getValue(3) != 2.5) return 3;
  }
  for (const auto &descriptor : S::getChannelDescriptors()) {
    if (!ids.insert(descriptor.id.value()).second) return 4;
    if (!object->findChannel(descriptor.id)) return 5;
  }
  if (ids.size() != 11) return 6;
  for (std::uint32_t i = 0; i <= 50; ++i) {
    if (i >= 10 && i <= 20) continue;
    if (object->findChannel(TChannelId(i))) return 7;
  }
  if (object->findChannel(TChannelId(
          std::numeric_limits<std::uint32_t>::max()))) return 8;
  if (S::getChannelId(S::T_ChannelCount) != TChannelIds::Invalid) return 9;
  return 0;
}
