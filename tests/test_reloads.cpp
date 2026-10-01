/// What a reload does to the table: the saw it keeps, and the deal the solver
/// models, appended after what each seat already holds.

#include <gtest/gtest.h>

#include <string>

#include "engine/Notation.h"
#include "engine/Rules.h"

namespace bsr {
namespace {

GameState parse(const std::string& text) {
  GameState state;
  std::string error;
  EXPECT_TRUE(notation::parse(text, &state, &error)) << error << " in: " << text;
  return state;
}

bool startsWith(const std::string& text, const std::string& prefix) {
  return text.rfind(prefix, 0) == 0;
}

TEST(Reloads, ASawedBarrelSurvivesAReloadExceptInMultiplayer) {
  // A Beer on the last shell starts the next load without touching the saw
  // (ItemInteraction.gd 130-141; RoundManager.gd 196-234).
  const GameState state = parse("p1=2/2 p2=2/2 tube=0L0B turn=p1 sawed");
  const std::vector<Outcome> don =
      rules::reloadOutcomes(state, RuleConfig::doubleOrNothing(4), false);
  ASSERT_EQ(don.size(), 7u);
  for (const Outcome& outcome : don) {
    EXPECT_NE(notation::print(outcome.state).find(" sawed"), std::string::npos);
  }

  const GameState table = parse("p1=2/2 p2=2/2 p3=2/2 tube=0L0B turn=p1 sawed");
  const std::vector<Outcome> multi =
      rules::reloadOutcomes(table, RuleConfig::multiplayer(3, 2), false);
  ASSERT_FALSE(multi.empty());
  for (const Outcome& outcome : multi) {
    EXPECT_EQ(notation::print(outcome.state).find(" sawed"), std::string::npos);
  }
}

TEST(Reloads, ARestraintKeptThroughAReloadStillEndsAtTheSeatsNextTurn) {
  // The game clears restraints at a load, and a rule set that keeps them still
  // frees a seat once its next turn starts (RoundManager.gd 308-325): p1 lost
  // its last turn and moves first after the load, so that turn is behind it,
  // while p2 stays restrained.
  RuleConfig config = RuleConfig::doubleOrNothing(2);
  config.reloadClearsCuffs = false;
  const std::vector<Outcome> outcomes =
      rules::reloadOutcomes(parse("p1=2/2 p2=2/2 tube=0L0B turn=p2 skipped=p1"), config, false);
  ASSERT_FALSE(outcomes.empty());
  for (const Outcome& outcome : outcomes) {
    EXPECT_EQ(outcome.state.current, 0);
    EXPECT_FALSE(outcome.state.players[0].skipConsumed);
    const std::string printed = notation::print(outcome.state);
    EXPECT_EQ(printed.find("skipped="), std::string::npos) << printed;
    GameState again;
    std::string error;
    EXPECT_TRUE(notation::parse(printed, &again, &error)) << error << " in: " << printed;
  }

  for (const Outcome& outcome :
       rules::reloadOutcomes(parse("p1=2/2 p2=2/2 tube=0L0B turn=p1 cuffed=p2"), config, false)) {
    EXPECT_TRUE(outcome.state.players[1].cuffed) << notation::print(outcome.state);
  }
}

TEST(Reloads, DoubleOrNothingDealsFourItemsAfterWhatEachSeatHolds) {
  const RuleConfig don = RuleConfig::doubleOrNothing(4);
  for (const Outcome& outcome :
       rules::reloadOutcomes(parse("p1=2/2 p2=2/2 tube=0L0B turn=p1"), don, true)) {
    EXPECT_TRUE(startsWith(notation::print(outcome.state),
                           "p1=2/2[mg,beer,cig,cuff] p2=2/2[beer,cig,cuff,saw] "))
        << notation::print(outcome.state);
  }

  // The deal stops at eight items.
  for (const Outcome& outcome : rules::reloadOutcomes(
           parse("p1=2/2[saw,saw,saw,saw,saw,saw] p2=2/2 tube=0L0B turn=p1"), don, true)) {
    EXPECT_TRUE(
        startsWith(notation::print(outcome.state), "p1=2/2[saw,saw,saw,saw,saw,saw,mg,beer] "))
        << notation::print(outcome.state);
  }
}

TEST(Reloads, TheModelLineNamesTheDealAndTheSaw) {
  const std::string line = RuleConfig::doubleOrNothing(4).describe();
  EXPECT_NE(line.find("2 to 5 items dealt per load, modelled at 4"), std::string::npos) << line;
  EXPECT_NE(line.find("a sawed barrel survives a reload"), std::string::npos) << line;
}

}  // namespace
}  // namespace bsr
