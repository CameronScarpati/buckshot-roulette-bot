#include "engine/Position.h"

#include <map>

#include "engine/Rules.h"

namespace bsr {

std::vector<std::pair<std::uint8_t, double>> readMasks(const Position& position, int seat) {
  const GameState& state = position.state;
  const int size = state.tube.size();
  std::map<std::uint8_t, double> masks;
  masks[0] = 1.0;
  for (const UnseenRead& read : position.unseenReads) {
    if (read.seat != seat) continue;
    const int atUse = read.sizeAtUse;
    const int gone = atUse - size;
    const std::array<double, kMaxShells> weights =
        rules::phoneOffsetWeights(seat, state.playerCount, atUse);
    std::map<std::uint8_t, double> next;
    for (const auto& [mask, chance] : masks) {
      for (int pick = 1; pick < atUse && pick < kMaxShells; ++pick) {
        const double w = weights[static_cast<std::size_t>(pick)];
        if (w <= 0.0) continue;
        const int offset = pick - gone;
        const std::uint8_t named =
            offset >= 0 ? static_cast<std::uint8_t>(mask | (1u << offset)) : mask;
        next[named] += chance * w;
      }
    }
    masks = std::move(next);
  }
  std::vector<std::pair<std::uint8_t, double>> out;
  for (const auto& [mask, chance] : masks) {
    if (chance > 0.0) out.emplace_back(mask, chance);
  }
  return out;
}

std::vector<ReadExpansion> expandReads(const Position& position, int advisedSeat) {
  std::vector<ReadExpansion> out(1);
  for (int seat = 0; seat < position.state.playerCount; ++seat) {
    if (seat == advisedSeat) continue;
    const std::vector<std::pair<std::uint8_t, double>> masks = readMasks(position, seat);
    std::vector<ReadExpansion> next;
    next.reserve(out.size() * masks.size());
    for (const ReadExpansion& base : out) {
      for (const auto& [mask, chance] : masks) {
        ReadExpansion entry = base;
        entry.weight *= chance;
        for (int offset = 0; offset < kMaxShells; ++offset) {
          if (((mask >> offset) & 1u) == 0u) continue;
          entry.extraObservers[static_cast<std::size_t>(offset)] = static_cast<std::uint8_t>(
              entry.extraObservers[static_cast<std::size_t>(offset)] | (1u << seat));
        }
        next.push_back(entry);
      }
    }
    out = std::move(next);
  }
  return out;
}

}  // namespace bsr
