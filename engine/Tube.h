#pragma once

#include <array>
#include <cstdint>

#include "engine/IndexedArray.h"

namespace bsr {

constexpr int kMaxShells = 8;
constexpr int kMaxPlayers = 4;

/// What is objectively true about one chamber position, as far as the model has
/// resolved it. Positions start Unknown and become Live or Blank when something
/// forces the question: a shot, a reveal, an ejection or an inversion.
enum class Shell : std::uint8_t { Unknown = 0, Live = 1, Blank = 2 };

/// The shotgun's tube, as an information state rather than a shuffled sequence.
///
/// `live` and `blank` are the counts still in the tube, which every player sees
/// at load time and can track. `truth` carries the objective type of a position
/// once it has been resolved, and `knownBy` is a bitmask of the players who have
/// observed that position. Resolving lazily is what makes the representation
/// exact: an unresolved position is exchangeable with every other unresolved
/// position, so the chance of drawing live is the unresolved live count over the
/// unresolved total, conditioned on everything anyone has learned so far.
///
/// Position 0 is the chamber. Firing or ejecting shifts every position down by
/// one, which is why knowledge is stored by offset and not by absolute index.
struct Tube {
  std::uint8_t live = 0;
  std::uint8_t blank = 0;
  IndexedArray<Shell, kMaxShells> truth{};
  IndexedArray<std::uint8_t, kMaxShells> knownBy{};
  bool sawed = false;
  /// An Inverter flipped a chamber nobody had seen. The shell keeps its place in
  /// the unresolved pool, but whatever it turns out to be, it fires as the
  /// opposite. Only the chamber can carry this, and it clears when the chamber
  /// leaves.
  bool chamberInverted = false;

  constexpr std::uint8_t size() const { return static_cast<std::uint8_t>(live + blank); }
  constexpr bool empty() const { return size() == 0; }

  /// Live shells whose position nobody has resolved yet.
  std::uint8_t unresolvedLive() const;
  std::uint8_t unresolvedBlank() const;

  /// True when `player` has observed the type at `offset`.
  bool knows(int player, int offset) const {
    return truth[offset] != Shell::Unknown && (knownBy[offset] & (1u << player)) != 0;
  }

  /// Probability that the shell at `offset` is live, given what `player` knows.
  /// A resolved position that the player has seen returns 1.0 or 0.0.
  double liveProbability(int player, int offset) const;

  /// Mark `offset` as objectively `type` and record who saw it. Callers reach
  /// this through the chance branches in Rules, never directly from a policy.
  void resolve(int offset, Shell type, std::uint8_t observerMask);

  /// Remove the chambered shell and shift knowledge down one position.
  void popChamber();

  /// Flip the chamber. A type somebody has already pinned down flips outright,
  /// and the public counts move with it. A chamber nobody has seen keeps its
  /// place in the unresolved pool and records the flip instead, so the shell
  /// stays exchangeable while its odds invert.
  void invertChamber();

  /// Resolve the chamber to the shell drawn from the unresolved pool, honouring
  /// a pending inversion, and return the type it actually fires as.
  Shell resolveChamberDraw(Shell drawn, std::uint8_t observerMask);

  bool operator==(const Tube& other) const;
};

}  // namespace bsr
