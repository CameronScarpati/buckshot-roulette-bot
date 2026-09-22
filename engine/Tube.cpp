#include "engine/Tube.h"

namespace bsr {

std::uint8_t Tube::unresolvedLive() const {
  int resolved = 0;
  for (int i = 0; i < size(); ++i) {
    if (truth[i] == Shell::Live) ++resolved;
  }
  return static_cast<std::uint8_t>(live - resolved);
}

std::uint8_t Tube::unresolvedBlank() const {
  int resolved = 0;
  for (int i = 0; i < size(); ++i) {
    if (truth[i] == Shell::Blank) ++resolved;
  }
  return static_cast<std::uint8_t>(blank - resolved);
}

double Tube::liveProbability(int player, int offset) const {
  if (offset >= size()) return 0.0;
  if (knows(player, offset)) return truth[offset] == Shell::Live ? 1.0 : 0.0;
  if (offset == 0 && chamberInverted && truth[0] == Shell::Unknown) {
    // The shell is still an unresolved draw, but it fires as its opposite.
    Tube plain = *this;
    plain.chamberInverted = false;
    return 1.0 - plain.liveProbability(player, 0);
  }
  // Positions this player has not seen are exchangeable for this player, so the
  // chance is the live shells they cannot account for over the shells they
  // cannot account for.
  int seenLive = 0;
  int seen = 0;
  for (int i = 0; i < size(); ++i) {
    if (!knows(player, i)) continue;
    ++seen;
    if (truth[i] == Shell::Live) ++seenLive;
  }
  const int remaining = size() - seen;
  if (remaining <= 0) return 0.0;
  return static_cast<double>(live - seenLive) / static_cast<double>(remaining);
}

void Tube::resolve(int offset, Shell type, std::uint8_t observerMask) {
  truth[offset] = type;
  knownBy[offset] = static_cast<std::uint8_t>(knownBy[offset] | observerMask);
}

bool Tube::canFire(Shell type) const {
  if (empty() || type == Shell::Unknown) return false;
  if (truth[0] != Shell::Unknown) return truth[0] == type;
  const Shell drawn = chamberInverted ? (type == Shell::Live ? Shell::Blank : Shell::Live) : type;
  return (drawn == Shell::Live ? unresolvedLive() : unresolvedBlank()) > 0;
}

Shell Tube::resolveChamberDraw(Shell drawn, std::uint8_t observerMask) {
  Shell fires = drawn;
  if (chamberInverted) {
    fires = drawn == Shell::Live ? Shell::Blank : Shell::Live;
    // The physical shell changed type, so the public counts move with it.
    if (drawn == Shell::Live) {
      --live;
      ++blank;
    } else {
      ++live;
      --blank;
    }
    chamberInverted = false;
  }
  resolve(0, fires, observerMask);
  return fires;
}

void Tube::popChamber() {
  const int n = size();
  chamberInverted = false;
  if (truth[0] == Shell::Live) {
    if (live > 0) --live;
  } else if (truth[0] == Shell::Blank) {
    if (blank > 0) --blank;
  }
  for (int i = 0; i + 1 < n; ++i) {
    truth[i] = truth[i + 1];
    knownBy[i] = knownBy[i + 1];
  }
  const int newSize = size();
  for (int i = newSize; i < kMaxShells; ++i) {
    truth[i] = Shell::Unknown;
    knownBy[i] = 0;
  }
}

void Tube::invertChamber() {
  if (empty()) return;
  if (truth[0] == Shell::Live) {
    truth[0] = Shell::Blank;
    --live;
    ++blank;
  } else if (truth[0] == Shell::Blank) {
    truth[0] = Shell::Live;
    ++live;
    --blank;
  } else {
    chamberInverted = !chamberInverted;
  }
  // knownBy is unchanged: whoever could see the type before the inversion can
  // still name it afterwards, and whoever could not still cannot.
}

bool Tube::operator==(const Tube& other) const {
  if (live != other.live || blank != other.blank || sawed != other.sawed ||
      chamberInverted != other.chamberInverted) {
    return false;
  }
  for (int i = 0; i < kMaxShells; ++i) {
    if (truth[i] != other.truth[i] || knownBy[i] != other.knownBy[i]) return false;
  }
  return true;
}

}  // namespace bsr
