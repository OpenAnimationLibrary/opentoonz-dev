#pragma once

#ifndef T_CHANNEL_ID_INCLUDED
#define T_CHANNEL_ID_INCLUDED

#include <cstdint>

// Owner-relative identity, independent of labels, enum order and enabled state.
// No implicit conversion to/from integers: frame/column indices are not IDs.
class TChannelId {
  std::uint32_t m_value;

public:
  constexpr TChannelId() : m_value(0) {}
  explicit constexpr TChannelId(std::uint32_t value) : m_value(value) {}
  constexpr std::uint32_t value() const { return m_value; }
  constexpr bool operator==(TChannelId other) const {
    return m_value == other.m_value;
  }
  constexpr bool operator!=(TChannelId other) const {
    return m_value != other.m_value;
  }
};

static_assert(sizeof(TChannelId) == sizeof(std::uint32_t),
              "Channel identity must occupy 32 bits");

namespace TChannelIds {
// Permanent assignments. Never renumber or reuse an assigned ID.
// 0: invalid; 1-9: reserved/unmapped; 21-49: reserved for approved core roles.
constexpr TChannelId Invalid(0);
constexpr TChannelId Angle(10);
constexpr TChannelId X(11);
constexpr TChannelId Y(12);
constexpr TChannelId Z(13);
constexpr TChannelId StackingOrder(14);
constexpr TChannelId ScaleX(15);
constexpr TChannelId ScaleY(16);
constexpr TChannelId Scale(17);
constexpr TChannelId Path(18);
constexpr TChannelId ShearX(19);
constexpr TChannelId ShearY(20);
// Allocation boundary only. No dynamic channels are registered by this change.
constexpr TChannelId FirstDynamic(50);
}  // namespace TChannelIds

#endif
