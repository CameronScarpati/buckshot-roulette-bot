/// What the advised seat knows about what somebody else has seen.
///
/// A seat that has used a magnifying glass plays better than one that has not,
/// and the seat being advised can watch it happen without learning the answer.
/// The solver therefore has two things to get right at once: it must not read a
/// shell only the opponent has seen, and it must not forget that the opponent
/// has seen it. Every value below is worked out in the comment above it.

#include <gtest/gtest.h>

#include <string>

#include "engine/Notation.h"
#include "solver/Solver.h"

namespace bsr {
namespace {

GameState parse(const std::string& text) {
  GameState state;
  std::string error;
  EXPECT_TRUE(notation::parse(text, &state, &error)) << error << " in: " << text;
  return state;
}

SolveOptions advising(int seat = 0, int reloadBudget = 0) {
  SolveOptions options;
  options.seat = seat;
  options.reloadBudget = reloadBudget;
  return options;
}

double valueOf(const std::string& position, const SolveOptions& options) {
  return solve(parse(position), RuleConfig::doubleOrNothing(1), options).value;
}

TEST(OpponentKnowledge, LookingAtTheChamberTurnsAnEvenRoundIntoALostOne) {
  // 1L1B, both seats on their last charge, p2 to move, and this is p1's answer.
  //   Nobody has looked: p2 shooting p1 wins half the time and otherwise hands
  //   p1 a certain live shell, and p2 shooting itself is the same trade the
  //   other way round. Either way p1 survives half the time.
  //   p2 has looked at the chamber: whichever way it fell, p2 has a winning
  //   move. Live, and it fires at p1. Blank, and it fires at itself, keeps the
  //   turn, and the remaining certain live shell is p1's. 1/2 * 0 + 1/2 * 0.
  EXPECT_NEAR(valueOf("p1=1/1 p2=1/1 tube=1L1B turn=p2", advising()), 0.5, 1e-12);
  EXPECT_NEAR(valueOf("p1=1/1 p2=1/1 tube=1L1B turn=p2 known=p2:0L", advising()), 0.0, 1e-12);
}

TEST(OpponentKnowledge, TheAnswerCannotDependOnWhichShellTheOtherSeatSaw) {
  // The advised seat watched the magnifying glass come out and did not see the
  // answer, so naming the shell in the position must not change a thing it is
  // told. This is the property the whole design turns on: if these two ever
  // came apart, the solver would be reading a shell it has no right to.
  const std::string live = "p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p2:0L";
  const std::string blank = "p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p2:0B";
  EXPECT_NEAR(valueOf(live, advising()), valueOf(blank, advising()), 1e-12);

  // And the number itself: 1L2B, so the chamber p2 looked at is live a third of
  // the time. Live, and p2 fires it at p1, who is on one charge: 0. Blank, and
  // p2 is left choosing in 1L1B with nothing else known, which is worth 1/2 to
  // p1 whichever way p2 spends the blank. 1/3 * 0 + 2/3 * 1/2 = 1/3.
  EXPECT_NEAR(valueOf(live, advising()), 1.0 / 3.0, 1e-12);
}

TEST(OpponentKnowledge, ASeatStillReadsAShellItLookedAtItself) {
  // The same notation pointed at the advised seat is knowledge, not a guess.
  // Blind, 1L1B with p1 to move is a coin flip: either shot is worth 1/2.
  EXPECT_NEAR(valueOf("p1=1/1 p2=1/1 tube=1L1B turn=p1", advising()), 0.5, 1e-12);

  // Knowing the chamber makes it certain either way, and that is the point:
  // a live chamber goes at p2, who is on one charge, and the round ends 1.
  EXPECT_NEAR(valueOf("p1=1/1 p2=1/1 tube=1L1B turn=p1 known=p1:0L", advising()), 1.0, 1e-12);

  // A blank chamber goes at p1 itself, which costs nothing and keeps the turn.
  // One shell is left and one live is unaccounted for, so it is live by
  // elimination and p1 fires it at p2. Also 1.
  EXPECT_NEAR(valueOf("p1=1/1 p2=1/1 tube=1L1B turn=p1 known=p1:0B", advising()), 1.0, 1e-12);
}

TEST(OpponentKnowledge, TheResultSaysHowManyShellsItAveragedOver) {
  const SolveResult none =
      solve(parse("p1=2/2 p2=2/2 tube=2L2B turn=p1"), RuleConfig::doubleOrNothing(2), advising());
  EXPECT_EQ(none.opponentKnownShells, 0);
  EXPECT_FALSE(none.opponentKnowledgeDropped);

  const SolveResult two = solve(parse("p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p2:1L,3B"),
                                RuleConfig::doubleOrNothing(2), advising());
  EXPECT_EQ(two.opponentKnownShells, 2);
  EXPECT_FALSE(two.opponentKnowledgeDropped);

  // A shell the advised seat has seen for itself is not something to average
  // over, whoever else saw it too.
  const SolveResult mine = solve(parse("p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p1:1L"),
                                 RuleConfig::doubleOrNothing(2), advising());
  EXPECT_EQ(mine.opponentKnownShells, 0);
}

TEST(OpponentKnowledge, PastTheLimitTheOtherSeatIsTreatedAsHavingSeenNothing) {
  const std::string position = "p1=1/1 p2=1/1 tube=1L1B turn=p2 known=p2:0L";
  SolveOptions capped = advising();
  capped.opponentKnowledgeLimit = 0;
  const SolveResult result = solve(parse(position), RuleConfig::doubleOrNothing(1), capped);
  EXPECT_TRUE(result.opponentKnowledgeDropped);
  EXPECT_EQ(result.opponentKnownShells, 1);
  // Falling back means forgetting, which is the answer for a p2 that never
  // looked: the even round from the first test.
  EXPECT_NEAR(result.value, 0.5, 1e-12);
  EXPECT_NE(result.assumptions.find("treated as seen by nobody"), std::string::npos);
}

TEST(OpponentKnowledge, KnowledgeIsWorthSomethingToTheSeatThatHasIt) {
  // Not a theorem about every position, a check on these: an opponent that has
  // looked is never worse for it, so the advised seat's chance cannot go up.
  const std::string positions[] = {
      "p1=2/2 p2=2/2 tube=2L2B turn=p2",
      "p1=1/2 p2=2/2 tube=1L2B turn=p2",
      "p1=2/2[saw] p2=2/2[beer] tube=2L1B turn=p2",
  };
  for (const std::string& position : positions) {
    const double blind = valueOf(position, advising());
    const double looked = valueOf(position + " known=p2:0L", advising());
    EXPECT_LE(looked, blind + 1e-12) << position;
  }
}

TEST(OpponentKnowledge, TheReportedValueIsTheMoveTheAdvisedSeatWouldPlay) {
  // The rows already account for an opponent that has looked, so the value the
  // answer prints has to be the top row and not a number arrived at another
  // way. A position where the two could drift apart is one where the advised
  // seat is to move and the other seat is holding something back.
  const SolveResult result = solve(parse("p1=2/2[saw,mg] p2=2/2[beer,cuff] tube=2L2B turn=p1"
                                         " known=p2:1L"),
                                   RuleConfig::doubleOrNothing(2), advising(0, 1));
  ASSERT_FALSE(result.ranked.empty());
  EXPECT_NEAR(result.value, result.ranked.front().value, 1e-12);
}

}  // namespace
}  // namespace bsr
