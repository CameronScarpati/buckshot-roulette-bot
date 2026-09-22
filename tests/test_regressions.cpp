/// One test per defect found by reading this code adversarially. Each names
/// what went wrong, because a regression test whose only message is a number
/// tells the next reader nothing.

#include <gtest/gtest.h>

#include <string>

#include "engine/Notation.h"
#include "engine/Rules.h"
#include "solver/Solver.h"

namespace bsr {
namespace {

GameState parse(const std::string& text) {
  GameState state;
  std::string error;
  EXPECT_TRUE(notation::parse(text, &state, &error)) << error << " in: " << text;
  return state;
}

SolveOptions options(int seat = 0, int reloadBudget = 0) {
  SolveOptions opts;
  opts.seat = seat;
  opts.reloadBudget = reloadBudget;
  return opts;
}

bool listsMove(const SolveResult& result, const std::string& text) {
  for (const ActionValue& entry : result.ranked) {
    if (entry.action.describe(result.mover) == text) return true;
  }
  return false;
}

TEST(Regression, AMarkedInversionOnASeenChamberIsRefused) {
  // The inversion flag only means anything while the chamber is an unresolved
  // draw. Accepting both at once described a position where the shell fires as
  // the opposite of what the seat can see, and the shot then used the seen type.
  GameState state;
  std::string error;
  EXPECT_FALSE(
      notation::parse("p1=1/1 p2=1/1 tube=1L1B turn=p1 inverted known=p1:0B", &state, &error));
  EXPECT_NE(error.find("inverted"), std::string::npos);
}

TEST(Regression, TheOpponentCannotReadAShellThroughAdrenaline) {
  // Other seats are modelled as spending no magnifying glass, so that the
  // search never branches on knowledge the solved seat cannot see. Adrenaline
  // carries its payload in a second field, so the filter used to miss it and
  // the opponent read the chamber anyway.
  const SolveResult result = solve(parse("p1=1/1[adr] p2=1/1[mg] tube=1L1B turn=p1"),
                                   RuleConfig::doubleOrNothing(1), options(1));
  EXPECT_FALSE(listsMove(result, "steal Magnifying Glass from p2 and use it"));
  EXPECT_NEAR(result.value, 0.5, 1e-12) << "seat 2 has no way to resolve the tube here";
}

TEST(Regression, AStolenRestraintIsAimedByTheThief) {
  // The victim used to be whichever seat came first, which in three seats meant
  // the cuffs went back on the seat they were taken from.
  RuleConfig multi = RuleConfig::multiplayer(3, 2);
  SolveOptions opts = options(0);
  opts.opponent = OpponentModel::Paranoid;
  const SolveResult result =
      solve(parse("p1=2/2[adr] p2=2/2[jam] p3=2/2[saw] tube=2L1B turn=p1"), multi, opts);
  EXPECT_TRUE(listsMove(result, "steal Jammer from p2 and use it on p3"));
  EXPECT_TRUE(listsMove(result, "steal Jammer from p2 and use it on p2"));

  double onThird = -1.0;
  double onSource = -1.0;
  for (const ActionValue& entry : result.ranked) {
    const std::string text = entry.action.describe(result.mover);
    if (text == "steal Jammer from p2 and use it on p3") onThird = entry.value;
    if (text == "steal Jammer from p2 and use it on p2") onSource = entry.value;
  }
  EXPECT_GE(onThird, onSource) << "the thief should not be forced into the worse aim";
}

TEST(Regression, ARankingKnowsWhichSeatItBelongsTo) {
  // A seat that arrives handcuffed is skipped before anything is asked of it,
  // so the moves listed belong to the seat that actually acts. Reporting them
  // against the seat named by the position read them backwards.
  const SolveResult result = solve(parse("p1=2/2 p2=2/2 tube=1L0B turn=p1 cuffed=p1"),
                                   RuleConfig::doubleOrNothing(2), options(0));
  EXPECT_EQ(result.mover, 1);
  EXPECT_TRUE(listsMove(result, "shoot p1"));
  EXPECT_FALSE(listsMove(result, "shoot p2"))
      << "seat 2 is the one holding the gun, so it cannot shoot itself by that name";
}

TEST(Regression, AnOpponentRankingIsOrderedForTheOpponent) {
  // The value is always the solved seat's, but the order belongs to whoever is
  // holding the gun, so the front of the list is the worst outcome for us.
  const SolveResult result =
      solve(parse("p1=2/2 p2=2/2 tube=1L0B turn=p2"), RuleConfig::doubleOrNothing(2), options(0));
  EXPECT_EQ(result.mover, 1);
  ASSERT_GE(result.ranked.size(), 2u);
  EXPECT_LT(result.ranked.front().value, result.ranked.back().value);
  EXPECT_EQ(result.bestActions().size(), 1u) << "these two moves are not a tie";
}

TEST(Regression, ResolvingAnInvertedChamberMovesThePublicCounts) {
  // Recording the fired type without going through the draw left the counts
  // describing a tube that no longer existed.
  GameState state = parse("p1=4/4 p2=4/4 tube=2L2B turn=p1");
  state.tube.invertChamber();
  ASSERT_TRUE(state.tube.chamberInverted);
  // The shell that fires live must have been drawn as a blank.
  state.tube.resolveChamberDraw(Shell::Blank, 0x3);
  EXPECT_EQ(state.tube.truth[0], Shell::Live);
  EXPECT_EQ(state.tube.live, 3);
  EXPECT_EQ(state.tube.blank, 1);
  state.tube.popChamber();
  EXPECT_EQ(state.tube.live, 2);
  EXPECT_EQ(state.tube.blank, 1);
}

TEST(Regression, KnowledgeCannotExceedWhatTheTubeHolds) {
  // The parser refuses these. The advisor's reveal commands go through the same
  // check now, because a tube holding minus one live shell produced a negative
  // probability rather than an error.
  GameState state;
  std::string error;
  EXPECT_FALSE(notation::parse("p1=2/2 p2=2/2 tube=0L3B turn=p1 known=p1:1L", &state, &error));
  EXPECT_FALSE(notation::parse("p1=2/2 p2=2/2 tube=1L1B turn=p1 known=p1:0L,1L", &state, &error));
}

TEST(Regression, TheOpponentCannotActOnAShellOnlyWeHaveSeen) {
  // 1L1B, both on their last charge, the opponent to move, and we alone know
  // the chamber is live. The opponent cannot tell its two moves apart: from
  // what it can see each is worth one half to us. The shell is live, so
  // whichever it picks decides the round, and the position is worth the average
  // of the two, one half. Minimising over the position as it really is would
  // have handed it our private knowledge and valued this at zero.
  const SolveResult result = solve(parse("p1=1/1 p2=1/1 tube=1L1B turn=p2 known=p1:0L"),
                                   RuleConfig::doubleOrNothing(1), options(0));
  EXPECT_NEAR(result.value, 0.5, 1e-12);

  // Both consequences are still shown, because that is what we want to know.
  double shootUs = -1.0;
  double shootSelf = -1.0;
  for (const ActionValue& entry : result.ranked) {
    const std::string text = entry.action.describe(result.mover);
    if (text == "shoot p1") shootUs = entry.value;
    if (text == "shoot self") shootSelf = entry.value;
  }
  EXPECT_NEAR(shootUs, 0.0, 1e-12);
  EXPECT_NEAR(shootSelf, 1.0, 1e-12);
}

TEST(Regression, PrivateKnowledgeIsNeverWorseThanNone) {
  // Knowing something the opponent does not cannot hurt. Before the fix a
  // private reveal made the search value the position lower, because the
  // opponent was allowed to read it.
  const RuleConfig config = RuleConfig::doubleOrNothing(2);
  const double blind = solve(parse("p1=2/2 p2=2/2 tube=2L2B turn=p2"), config, options(0, 1)).value;
  const double informed =
      solve(parse("p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p1:1L"), config, options(0, 1)).value;
  EXPECT_GE(informed, blind - 1e-9);
}

TEST(Regression, EveryValueStaysAProbability) {
  // A sweep over positions that mix knowledge, restraints, inversion and items,
  // asserting the one invariant that covers every arithmetic slip at once.
  const std::vector<std::string> positions = {
      "p1=1/2[adr] p2=2/2[mg,cuff] tube=2L2B turn=p1",
      "p1=2/2[inv,beer] p2=1/2[saw] tube=1L3B turn=p1",
      "p1=1/1[med,cig] p2=1/2[phone] tube=3L1B turn=p2",
      "p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p1:0B,2L",
      "p1=1/2[cuff] p2=2/2[cuff] tube=1L1B turn=p1 cuffed=p2",
  };
  for (const std::string& text : positions) {
    for (int seat = 0; seat < 2; ++seat) {
      const SolveResult result =
          solve(parse(text), RuleConfig::doubleOrNothing(2), options(seat, 1));
      EXPECT_GE(result.value, 0.0) << text;
      EXPECT_LE(result.value, 1.0) << text;
      for (const ActionValue& entry : result.ranked) {
        EXPECT_GE(entry.value, 0.0) << entry.action.describe(result.mover) << " in " << text;
        EXPECT_LE(entry.value, 1.0) << entry.action.describe(result.mover) << " in " << text;
      }
    }
  }
}

}  // namespace
}  // namespace bsr
