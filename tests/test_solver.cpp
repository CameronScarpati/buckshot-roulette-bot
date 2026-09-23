/// Solver values pinned to positions worked out by hand. Each test states the
/// arithmetic in its comment, so a failure says which step of the reasoning the
/// code stopped agreeing with rather than only that a number moved.

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

SolveOptions options(int reloadBudget = 0) {
  SolveOptions opts;
  opts.seat = 0;
  opts.reloadBudget = reloadBudget;
  return opts;
}

double valueOf(const SolveResult& result, const std::string& action) {
  for (const ActionValue& entry : result.ranked) {
    if (entry.action.describe(0) == action) return entry.value;
  }
  ADD_FAILURE() << "no move called " << action;
  return -1.0;
}

TEST(Solver, OneLiveOneBlankIsAnEvenChoice) {
  // 1L1B, both seats on their last charge, seat 1 to move.
  //   Shoot the opponent: live (1/2) wins at once; blank (1/2) leaves 1L0B and
  //   hands the turn over, and that certain live shell kills us. 1/2.
  //   Shoot self: live (1/2) loses; blank (1/2) keeps the turn with 1L0B, and
  //   the certain live shell wins. 1/2.
  const SolveResult result =
      solve(parse("p1=1/1 p2=1/1 tube=1L1B turn=p1"), RuleConfig::doubleOrNothing(1), options());
  EXPECT_NEAR(valueOf(result, "shoot p2"), 0.5, 1e-12);
  EXPECT_NEAR(valueOf(result, "shoot self"), 0.5, 1e-12);
  EXPECT_TRUE(result.hasTie());
  EXPECT_EQ(result.bestActions().size(), 2u);
}

TEST(Solver, ACertainLiveShellIsAWinAndShootingYourselfIsALoss) {
  const SolveResult result =
      solve(parse("p1=1/1 p2=1/1 tube=1L0B turn=p1"), RuleConfig::doubleOrNothing(1), options());
  EXPECT_NEAR(valueOf(result, "shoot p2"), 1.0, 1e-12);
  EXPECT_NEAR(valueOf(result, "shoot self"), 0.0, 1e-12);
  EXPECT_EQ(result.ranked.front().action.describe(0), "shoot p2");
}

TEST(Solver, TheSawTurnsACertainLiveIntoAKillOnTwoCharges) {
  // Two charges cannot be taken by one live shell, so the saw is the difference
  // between winning now and handing over the turn.
  const SolveResult sawed = solve(parse("p1=1/1 p2=2/2 tube=1L0B turn=p1 sawed"),
                                  RuleConfig::doubleOrNothing(2), options());
  EXPECT_NEAR(valueOf(sawed, "shoot p2"), 1.0, 1e-12);

  // Without the saw the shot leaves the opponent alive and empties the tube, so
  // the round runs past the reload budget and falls back to charges in hand:
  // one each, so one half.
  const SolveResult plain =
      solve(parse("p1=1/1 p2=2/2 tube=1L0B turn=p1"), RuleConfig::doubleOrNothing(2), options());
  EXPECT_NEAR(valueOf(plain, "shoot p2"), 0.5, 1e-12);
  EXPECT_TRUE(plain.truncated);
}

TEST(Solver, UsingTheSawFirstIsFoundWhenItIsTheOnlyWayToKill) {
  // 1L0B, the opponent has two charges: the saw makes the certain live lethal.
  const SolveResult result = solve(parse("p1=1/1[saw] p2=2/2 tube=1L0B turn=p1"),
                                   RuleConfig::doubleOrNothing(2), options());
  EXPECT_EQ(result.ranked.front().action.describe(0), "use Hand Saw");
  EXPECT_NEAR(result.ranked.front().value, 1.0, 1e-12);
}

TEST(Solver, HandcuffsTurnAnEvenPositionIntoAWin) {
  // 1L1B, both on one charge, and a pair of handcuffs.
  //   Cuff, then shoot the opponent: live (1/2) wins at once. Blank (1/2) hands
  //   the turn over, the cuffs eat it, the turn comes straight back with 1L0B,
  //   and the certain live shell wins. So cuffing wins outright.
  const SolveResult result = solve(parse("p1=1/1[cuff] p2=1/1 tube=1L1B turn=p1"),
                                   RuleConfig::doubleOrNothing(1), options());
  EXPECT_NEAR(valueOf(result, "use Handcuffs on p2"), 1.0, 1e-12);
  EXPECT_NEAR(valueOf(result, "shoot p2"), 0.5, 1e-12);
  EXPECT_EQ(result.ranked.front().action.describe(0), "use Handcuffs on p2");
}

TEST(Solver, AMagnifyingGlassIsWorthAWholeWinWhenItResolvesTheTube) {
  // 1L1B with a magnifying glass: whatever it shows, the right shot follows.
  //   Live (1/2): shoot the opponent and win.
  //   Blank (1/2): shoot yourself, keep the turn, then the certain live wins.
  const SolveResult result = solve(parse("p1=1/1[mg] p2=1/1 tube=1L1B turn=p1"),
                                   RuleConfig::doubleOrNothing(1), options());
  EXPECT_NEAR(valueOf(result, "use Magnifying Glass"), 1.0, 1e-12);
  EXPECT_NEAR(valueOf(result, "shoot p2"), 0.5, 1e-12);
}

TEST(Solver, MedicineIsATossUpAndRanksBelowACertainKill) {
  // Shooting the opponent with the certain live shell wins outright; the
  // medicine is a coin flip that can end the round on the spot.
  const SolveResult result = solve(parse("p1=1/2[med] p2=1/1 tube=1L0B turn=p1"),
                                   RuleConfig::doubleOrNothing(2), options());
  EXPECT_NEAR(valueOf(result, "shoot p2"), 1.0, 1e-12);
  EXPECT_NEAR(valueOf(result, "use Expired Medicine"), 0.5, 1e-12);
  EXPECT_EQ(result.ranked.front().action.describe(0), "shoot p2");
}

TEST(Solver, TheInverterFlipsABlankHeavyTubeIntoALikelyKill) {
  // 1L3B: the chamber is live one time in four. After an inverter it fires live
  // three times in four, which is the whole point of the item.
  const GameState before = parse("p1=1/1[inv] p2=1/1 tube=1L3B turn=p1");
  EXPECT_NEAR(before.tube.liveProbability(0, 0), 0.25, 1e-12);
  const SolveResult result = solve(before, RuleConfig::doubleOrNothing(1), options(1));
  const double inverted = valueOf(result, "use Inverter");
  const double plain = valueOf(result, "shoot p2");
  EXPECT_GT(inverted, plain);
  EXPECT_GT(inverted, 0.5);
}

TEST(Solver, ShootingAnEliminatedSeatIsNotOffered) {
  const RuleConfig multi = RuleConfig::multiplayer(3, 1);
  const SolveResult result =
      solve(parse("p1=1/1 p2=0/1 p3=1/1 tube=1L0B turn=p1"), multi, options());
  for (const ActionValue& entry : result.ranked) {
    EXPECT_NE(entry.action.describe(0), "shoot p2");
  }
  EXPECT_NEAR(valueOf(result, "shoot p3"), 1.0, 1e-12);
}

TEST(Solver, ThreeSeatsRankAKillAheadOfShootingYourself) {
  RuleConfig multi = RuleConfig::multiplayer(3, 1);
  SolveOptions opts = options();
  opts.opponent = OpponentModel::Paranoid;
  const SolveResult result = solve(parse("p1=1/1 p2=1/1 p3=1/1 tube=1L0B turn=p1"), multi, opts);
  // Killing one of the two leaves a fresh load against the survivor, valued at
  // charges in hand once the reload budget runs out: one each, so one half.
  EXPECT_NEAR(valueOf(result, "shoot p2"), 0.5, 1e-12);
  EXPECT_NEAR(valueOf(result, "shoot self"), 0.0, 1e-12);
}

TEST(Solver, ValuesAreProbabilitiesAndTheRankingIsSorted) {
  const SolveResult result = solve(parse("p1=2/3[mg,beer,saw] p2=2/3[cuff,cig] tube=2L2B turn=p1"),
                                   RuleConfig::doubleOrNothing(3), options(1));
  ASSERT_FALSE(result.ranked.empty());
  double previous = 2.0;
  for (const ActionValue& entry : result.ranked) {
    EXPECT_GE(entry.value, 0.0);
    EXPECT_LE(entry.value, 1.0);
    EXPECT_LE(entry.value, previous + 1e-12);
    previous = entry.value;
  }
  EXPECT_NEAR(result.value, result.ranked.front().value, 1e-12);
}

TEST(Solver, TheSameQuestionGivesTheSameAnswerEveryTime) {
  const GameState state = parse("p1=2/3[mg,beer] p2=2/3[cuff,saw] tube=2L2B turn=p1");
  const RuleConfig config = RuleConfig::doubleOrNothing(3);
  const SolveResult first = solve(state, config, options(1));
  const SolveResult second = solve(state, config, options(1));
  ASSERT_EQ(first.ranked.size(), second.ranked.size());
  for (std::size_t i = 0; i < first.ranked.size(); ++i) {
    EXPECT_EQ(first.ranked[i].action.describe(0), second.ranked[i].action.describe(0));
    EXPECT_DOUBLE_EQ(first.ranked[i].value, second.ranked[i].value);
  }
  EXPECT_EQ(first.nodes, second.nodes);
}

TEST(Solver, TwoSeatsSplitTheRoundBetweenThem) {
  // One of two seats wins the round, so with no hidden knowledge anywhere the
  // two solves have to add to one. Nothing here reveals a shell privately and
  // the reload budget is zero, so no deal can hand anybody a magnifying glass.
  const GameState state = parse("p1=2/2[beer,saw] p2=2/2[cig,cuff] tube=2L2B turn=p1");
  const RuleConfig config = RuleConfig::doubleOrNothing(2);
  SolveOptions mine = options(0);
  SolveOptions theirs = options(0);
  theirs.seat = 1;
  const double us = solve(state, config, mine).value;
  const double them = solve(state, config, theirs).value;
  EXPECT_NEAR(us + them, 1.0, 1e-9);
}

TEST(Solver, EachSideIsFlatteredByTheOpponentModel) {
  // Once information items are in play the two solves are no longer the same
  // game: each one assumes the other seat spends no magnifying glass, so each
  // is a little optimistic and the pair adds to more than one. This is the
  // price of never reading a shell the solved seat cannot see, and it is
  // stated in every answer the advisor prints.
  const GameState state = parse("p1=2/2[mg,beer] p2=2/2[mg,saw] tube=2L2B turn=p1");
  const RuleConfig config = RuleConfig::doubleOrNothing(2);
  SolveOptions mine = options(0);
  SolveOptions theirs = options(0);
  theirs.seat = 1;
  const double us = solve(state, config, mine).value;
  const double them = solve(state, config, theirs).value;
  EXPECT_GE(us + them, 1.0 - 1e-9);
}

TEST(Solver, LookingThroughMoreReloadsChangesNothingThatIsAlreadyDecided) {
  const GameState state = parse("p1=1/1[saw] p2=2/2 tube=1L0B turn=p1");
  const RuleConfig config = RuleConfig::doubleOrNothing(2);
  const double shallow = solve(state, config, options(0)).ranked.front().value;
  const double deep = solve(state, config, options(2)).ranked.front().value;
  EXPECT_NEAR(shallow, 1.0, 1e-12);
  EXPECT_NEAR(deep, 1.0, 1e-12);
}

TEST(Solver, ACuffedSeatToMoveIsSkippedBeforeAnythingIsAsked) {
  // Seat 1 is cuffed and to move, so the turn belongs to seat 2 and the value
  // is read from seat 2's position, not seat 1's.
  const SolveResult result = solve(parse("p1=1/1 p2=1/1 tube=1L0B turn=p1 cuffed=p1"),
                                   RuleConfig::doubleOrNothing(1), options());
  EXPECT_NEAR(result.value, 0.0, 1e-12) << "the opponent holds a certain live shell";
}

TEST(Solver, AFinishedRoundIsWorthOneOrZero) {
  const RuleConfig config = RuleConfig::doubleOrNothing(2);
  GameState won = parse("p1=1/2 p2=1/2 tube=1L1B turn=p1");
  won.players[1].hp = 0;
  EXPECT_NEAR(solve(won, config, options()).value, 1.0, 1e-12);
  GameState lost = parse("p1=1/2 p2=1/2 tube=1L1B turn=p2");
  lost.players[0].hp = 0;
  EXPECT_NEAR(solve(lost, config, options()).value, 0.0, 1e-12);
}

}  // namespace
}  // namespace bsr
