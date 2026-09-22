/// The tube is an information state, not a shuffled list, so these tests pin
/// the two things that can silently go wrong: the odds a seat should compute
/// from what it has seen, and the bookkeeping when a shell is resolved, fired
/// or inverted.

#include <gtest/gtest.h>

#include "engine/Tube.h"

namespace bsr {
namespace {

constexpr std::uint8_t kSeat0 = 1u << 0;
constexpr std::uint8_t kSeat1 = 1u << 1;
constexpr std::uint8_t kBoth = kSeat0 | kSeat1;

Tube makeTube(int live, int blank) {
  Tube tube;
  tube.live = static_cast<std::uint8_t>(live);
  tube.blank = static_cast<std::uint8_t>(blank);
  return tube;
}

TEST(Tube, OddsFollowTheCountsWhenNothingIsKnown) {
  const Tube tube = makeTube(1, 3);
  EXPECT_DOUBLE_EQ(tube.liveProbability(0, 0), 0.25);
  EXPECT_DOUBLE_EQ(tube.liveProbability(1, 0), 0.25);
  EXPECT_DOUBLE_EQ(tube.liveProbability(0, 3), 0.25);
}

TEST(Tube, AKnownShellIsCertainForItsObserverAndOnlyForThem) {
  Tube tube = makeTube(1, 3);
  tube.resolve(0, Shell::Live, kSeat0);
  EXPECT_DOUBLE_EQ(tube.liveProbability(0, 0), 1.0);
  // Seat 1 saw nothing, so for it every position is still exchangeable.
  EXPECT_DOUBLE_EQ(tube.liveProbability(1, 0), 0.25);
}

TEST(Tube, KnowledgeOfOnePositionSharpensTheRest) {
  Tube tube = makeTube(2, 2);
  tube.resolve(3, Shell::Live, kSeat0);
  // Seat 0 accounts for one live among four, leaving one live in three unseen.
  EXPECT_DOUBLE_EQ(tube.liveProbability(0, 0), 1.0 / 3.0);
  EXPECT_DOUBLE_EQ(tube.liveProbability(1, 0), 0.5);
}

TEST(Tube, ResolvedShellsLeaveTheUnresolvedPool) {
  Tube tube = makeTube(2, 2);
  EXPECT_EQ(tube.unresolvedLive(), 2);
  EXPECT_EQ(tube.unresolvedBlank(), 2);
  tube.resolve(1, Shell::Blank, kBoth);
  EXPECT_EQ(tube.unresolvedLive(), 2);
  EXPECT_EQ(tube.unresolvedBlank(), 1);
}

TEST(Tube, FiringShiftsKnowledgeDownAndDropsTheCount) {
  Tube tube = makeTube(2, 2);
  tube.resolve(0, Shell::Live, kBoth);
  tube.resolve(2, Shell::Blank, kSeat0);
  tube.popChamber();
  EXPECT_EQ(tube.live, 1);
  EXPECT_EQ(tube.blank, 2);
  EXPECT_EQ(tube.size(), 3);
  // What was offset 2 is now offset 1, still known to seat 0 alone.
  EXPECT_TRUE(tube.knows(0, 1));
  EXPECT_FALSE(tube.knows(1, 1));
  EXPECT_EQ(tube.truth[1], Shell::Blank);
  // Nothing may be known past the new end of the tube.
  for (int i = tube.size(); i < kMaxShells; ++i) {
    EXPECT_EQ(tube.truth[i], Shell::Unknown);
    EXPECT_EQ(tube.knownBy[i], 0);
  }
}

TEST(Tube, InvertingAKnownShellFlipsItAndMovesTheCounts) {
  Tube tube = makeTube(2, 2);
  tube.resolve(0, Shell::Live, kSeat0);
  tube.invertChamber();
  EXPECT_EQ(tube.truth[0], Shell::Blank);
  EXPECT_EQ(tube.live, 1);
  EXPECT_EQ(tube.blank, 3);
  EXPECT_DOUBLE_EQ(tube.liveProbability(0, 0), 0.0);
  EXPECT_FALSE(tube.chamberInverted);
}

TEST(Tube, InvertingAnUnseenShellInvertsItsOdds) {
  Tube tube = makeTube(1, 3);
  EXPECT_DOUBLE_EQ(tube.liveProbability(0, 0), 0.25);
  tube.invertChamber();
  EXPECT_TRUE(tube.chamberInverted);
  EXPECT_DOUBLE_EQ(tube.liveProbability(0, 0), 0.75);
  // The counts do not move until the shell is actually resolved.
  EXPECT_EQ(tube.live, 1);
  EXPECT_EQ(tube.blank, 3);
}

TEST(Tube, InvertingTwiceIsTheSameAsNotInverting) {
  Tube tube = makeTube(1, 3);
  tube.invertChamber();
  tube.invertChamber();
  EXPECT_FALSE(tube.chamberInverted);
  EXPECT_DOUBLE_EQ(tube.liveProbability(0, 0), 0.25);
}

TEST(Tube, ResolvingAnInvertedChamberComplementsTheDrawAndTheCounts) {
  Tube tube = makeTube(1, 3);
  tube.invertChamber();
  // The shell drawn from the pool is live, so after the inversion it fires blank
  // and the tube now holds one fewer live and one more blank.
  const Shell fired = tube.resolveChamberDraw(Shell::Live, kBoth);
  EXPECT_EQ(fired, Shell::Blank);
  EXPECT_EQ(tube.truth[0], Shell::Blank);
  EXPECT_EQ(tube.live, 0);
  EXPECT_EQ(tube.blank, 4);
  EXPECT_FALSE(tube.chamberInverted);
  EXPECT_EQ(tube.unresolvedLive(), 0);
  EXPECT_EQ(tube.unresolvedBlank(), 3);
}

TEST(Tube, ResolvingAPlainChamberLeavesTheCountsAlone) {
  Tube tube = makeTube(2, 2);
  const Shell fired = tube.resolveChamberDraw(Shell::Live, kBoth);
  EXPECT_EQ(fired, Shell::Live);
  EXPECT_EQ(tube.live, 2);
  EXPECT_EQ(tube.blank, 2);
  EXPECT_EQ(tube.unresolvedLive(), 1);
}

}  // namespace
}  // namespace bsr
