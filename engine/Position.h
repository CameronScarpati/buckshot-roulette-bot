#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/Dealer.h"
#include "engine/State.h"

namespace bsr {

/// A Burner Phone use whose result the reader of a position never saw: `seat`
/// used a phone when the tube held `sizeAtUse` shells. The game shows the
/// player only what the player's own phone names (BurnerPhone.gd 6), and the
/// dealer's phone writes only into its own memory (DealerIntelligence.gd
/// 187-194), so another seat's read is known to have happened but not where it
/// looked. Reads last until the next load.
struct UnseenRead {
  std::uint8_t seat = 0;
  std::uint8_t sizeAtUse = 0;

  bool operator==(const UnseenRead& other) const {
    return seat == other.seat && sizeAtUse == other.sizeAtUse;
  }
};

/// Everything a solve starts from: the state, the dealer's memory when it is in
/// the middle of its turn (a default memory is the memory a turn starts with),
/// and the phone reads nobody can see the result of.
struct Position {
  GameState state;
  dealer::Memory dealerMemory{};
  std::vector<UnseenRead> unseenReads;
};

/// One way the unseen reads can have fallen: its chance, and for each offset of
/// the current tube the seats whose read named it, one bit per seat.
struct ReadExpansion {
  double weight = 1.0;
  std::array<std::uint8_t, kMaxShells> extraObservers{};
};

/// The offsets of the current tube that `seat`'s unseen reads may have named,
/// as (mask, chance) pairs: bit j of a mask is set when some read named offset
/// j. A read made at a tube of n shells, when s shells are left, named offset
/// k - (n - s) if it picked k; a pick below n - s named a shell that has since
/// left the tube and adds nothing. The pick follows the seat's own phone
/// (rules::phoneOffsetWeights). Masks come in ascending order and masks with no
/// chance are left out. A seat with no reads gives the single pair (0, 1).
std::vector<std::pair<std::uint8_t, double>> readMasks(const Position& position, int seat);

/// Every combination of the seats' read masks, taking the seats in ascending
/// order and each seat's masks in ascending order, with the product of their
/// chances. Reads by `advisedSeat` are left out: that seat saw where its own
/// phone looked, so its reads belong in the tube's knowledge instead.
std::vector<ReadExpansion> expandReads(const Position& position, int advisedSeat);

}  // namespace bsr
