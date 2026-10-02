/// Moves on ordered hands: which uses and steals the game allows, which copy a
/// move takes, what a wasted use leaves behind, and the shot rules that depend
/// on the seat firing. Outcome lists may repeat a state, so they are compared
/// as a map from the printed state to its summed probability.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

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

RuleConfig config() {
  return RuleConfig::doubleOrNothing(4);
}

using Outcomes = std::map<std::string, double>;

Outcomes outcomesOf(const GameState& state, const Action& action, const RuleConfig& rules) {
  Outcomes out;
  for (const Outcome& outcome : rules::apply(state, action, rules)) {
    out[notation::print(outcome.state)] += outcome.probability;
  }
  return out;
}

void expectOutcomes(const Outcomes& actual, const Outcomes& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (const auto& [text, chance] : expected) {
    const auto found = actual.find(text);
    ASSERT_NE(found, actual.end()) << "missing " << text;
    EXPECT_NEAR(found->second, chance, 1e-12) << text;
  }
}

std::vector<std::string> texts(const GameState& state, const RuleConfig& rules) {
  std::vector<std::string> out;
  for (const Action& action : rules::legalActions(state, rules)) {
    out.push_back(action.describe(state.current));
  }
  return out;
}

const Action* find(const std::vector<Action>& actions, const GameState& state,
                   const std::string& text) {
  for (const Action& action : actions) {
    if (action.describe(state.current) == text) return &action;
  }
  return nullptr;
}

Outcomes applyText(const GameState& state, const std::string& text, const RuleConfig& rules) {
  const std::vector<Action> actions = rules::legalActions(state, rules);
  const Action* action = find(actions, state, text);
  EXPECT_NE(action, nullptr) << text << " is not offered";
  if (action == nullptr) return {};
  return outcomesOf(state, *action, rules);
}

TEST(Moves, EveryUseAndStealIsOfferedInAFixedOrder) {
  const GameState state = parse("p1=2/2[beer,adr,mg] p2=2/2[beer,mg] tube=1L1B turn=p1");
  const std::vector<std::string> expected = {
      "shoot self",
      "shoot p2",
      "use Magnifying Glass",
      "use Beer",
      "use Adrenaline",
      "steal Magnifying Glass from p2 and use it",
      "steal Beer from p2 and use it",
  };
  EXPECT_EQ(texts(state, config()), expected);
}

TEST(Moves, CigarettesAtFullChargesAreSpentForNothing) {
  const GameState state = parse("p1=2/2[cig] p2=2/2 tube=1L1B turn=p1");
  expectOutcomes(applyText(state, "use Cigarettes", config()),
                 {{"p1=2/2 p2=2/2 tube=1L1B turn=p1", 1.0}});
}

TEST(Moves, AGlassOnAChamberTheSeatKnowsIsSpentForNothing) {
  const GameState state = parse("p1=2/2[mg] p2=2/2 tube=1L1B turn=p1 known=p1:0L");
  expectOutcomes(applyText(state, "use Magnifying Glass", config()),
                 {{"p1=2/2 p2=2/2 tube=1L1B turn=p1 known=p1:0L", 1.0}});
}

TEST(Moves, AdrenalineOnItsOwnIsSpentAndAStealTakesTheItem) {
  const GameState state = parse("p1=2/2[adr] p2=2/2[cig] tube=1L1B turn=p1");
  expectOutcomes(applyText(state, "use Adrenaline", config()),
                 {{"p1=2/2 p2=2/2[cig] tube=1L1B turn=p1", 1.0}});
  expectOutcomes(applyText(state, "steal Cigarettes from p2 and use it", config()),
                 {{"p1=2/2 p2=2/2 tube=1L1B turn=p1", 1.0}});
}

TEST(Moves, OnlyTheGamesOwnRefusalsRemoveAMove) {
  // No Adrenaline steals another.
  for (const std::string& text :
       texts(parse("p1=2/2[adr] p2=2/2[adr] tube=1L1B turn=p1"), config())) {
    EXPECT_EQ(text.find("steal"), std::string::npos) << text;
  }
  // No saw on a sawed barrel.
  const std::vector<std::string> sawed =
      texts(parse("p1=2/2[saw] p2=2/2 tube=1L1B turn=p1 sawed"), config());
  EXPECT_EQ(std::count(sawed.begin(), sawed.end(), "use Hand Saw"), 0);
  // No handcuffs on a seat already cuffed or owed a turn, and no second
  // restraint in one turn.
  for (const char* position : {"p1=2/2[cuff] p2=2/2 tube=1L1B turn=p1 cuffed=p2",
                               "p1=2/2[cuff] p2=2/2 tube=1L1B turn=p1 skipped=p2",
                               "p1=2/2[cuff] p2=2/2 tube=1L1B turn=p1 restraintused"}) {
    for (const std::string& text : texts(parse(position), config())) {
      EXPECT_EQ(text.find("Handcuffs"), std::string::npos) << position;
    }
  }
}

TEST(Moves, ATypeHeldInTwoPlacesNamesTheCopy) {
  const GameState split = parse("p1=2/2[beer,mg,beer] p2=2/2 tube=1L1B turn=p1");
  const std::vector<std::string> expected = {
      "shoot self", "shoot p2", "use Magnifying Glass", "use Beer #1", "use Beer #2",
  };
  EXPECT_EQ(texts(split, config()), expected);
  for (const auto& [text, chance] : applyText(split, "use Beer #2", config())) {
    EXPECT_EQ(text.rfind("p1=2/2[beer,mg] ", 0), 0u) << text;
    EXPECT_GT(chance, 0.0);
  }

  // Adjacent copies are one move: using either leaves the same hand.
  const GameState together = parse("p1=2/2[beer,beer,mg] p2=2/2 tube=1L1B turn=p1");
  const std::vector<Action> actions = rules::legalActions(together, config());
  const Action* beer = find(actions, together, "use Beer");
  ASSERT_NE(beer, nullptr);
  EXPECT_EQ(beer->copy, 0);
  EXPECT_FALSE(beer->named);
  EXPECT_EQ(find(actions, together, "use Beer #2"), nullptr);
}

TEST(Moves, ThePlayersPhoneNeverNamesTheLastOfEightShells) {
  // The player's phone picks 1 to n - 1 and moves a pick of 7 to 6
  // (BurnerPhone.gd 13-15). Offset 1 is already known here, so half the time
  // nothing new is learned.
  const GameState known = parse("p1=2/2[phone] p2=2/2 tube=1L2B turn=p1 known=p1:1B");
  expectOutcomes(applyText(known, "use Burner Phone", config()),
                 {{"p1=2/2 p2=2/2 tube=1L2B turn=p1 known=p1:1B", 0.5},
                  {"p1=2/2 p2=2/2 tube=1L2B turn=p1 known=p1:1B,2L", 0.25},
                  {"p1=2/2 p2=2/2 tube=1L2B turn=p1 known=p1:1B,2B", 0.25}});

  const GameState eight = parse("p1=2/2[phone] p2=2/2 tube=4L4B turn=p1");
  Outcomes expected;
  for (int offset = 1; offset <= 6; ++offset) {
    const double chance = offset == 6 ? 1.0 / 7.0 : 1.0 / 14.0;
    for (const char type : {'L', 'B'}) {
      expected["p1=2/2 p2=2/2 tube=4L4B turn=p1 known=p1:" + std::to_string(offset) + type] =
          chance;
    }
  }
  expectOutcomes(applyText(eight, "use Burner Phone", config()), expected);

  const GameState two = parse("p1=2/2[phone] p2=2/2 tube=1L1B turn=p1");
  expectOutcomes(applyText(two, "use Burner Phone", config()),
                 {{"p1=2/2 p2=2/2 tube=1L1B turn=p1 known=p1:1L", 0.5},
                  {"p1=2/2 p2=2/2 tube=1L1B turn=p1 known=p1:1B", 0.5}});
}

TEST(Moves, TheDealersSeatPhonePicksEveryLaterShellEvenly) {
  // Seat 2 at a two-seat table is the dealer's hand, whose phone picks 1 to
  // n - 1 evenly (DealerIntelligence.gd 187-194).
  const GameState eight = parse("p1=2/2 p2=2/2[phone] tube=4L4B turn=p2");
  Outcomes expected;
  for (int offset = 1; offset <= 7; ++offset) {
    for (const char type : {'L', 'B'}) {
      expected["p1=2/2 p2=2/2 tube=4L4B turn=p2 known=p2:" + std::to_string(offset) + type] =
          1.0 / 14.0;
    }
  }
  expectOutcomes(applyText(eight, "use Burner Phone", config()), expected);
}

TEST(Moves, AHitInTheFadedStageLeavesASeatAboveTheFloorOnOneCharge) {
  // Story stage 3, modelled as five charges with a floor of two: a seat at the
  // floor that takes two is cut to one rather than killed.
  const GameState state = parse("p1=2/5 p2=2/5 tube=1L0B turn=p2 sawed");
  expectOutcomes(outcomesOf(state, Action::shoot(0), RuleConfig::storyRound(3)),
                 {{"p1=1/5 p2=2/5 tube=0L0B turn=p1", 1.0}});
}

TEST(Moves, AFailedDoseInTheFadedStageIsPlainSubtraction) {
  const GameState state = parse("p1=2/5[med] p2=2/5 tube=1L1B turn=p1");
  expectOutcomes(
      applyText(state, "use Expired Medicine", RuleConfig::storyRound(3)),
      {{"p1=4/5 p2=2/5 tube=1L1B turn=p1", 0.5}, {"p1=1/5 p2=2/5 tube=1L1B turn=p1", 0.5}});
}

TEST(Moves, ABlankTheDealersSeatFiresIntoItselfKeepsTheSaw) {
  const GameState dealer = parse("p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed known=p2:0B");
  expectOutcomes(outcomesOf(dealer, Action::shoot(1), config()),
                 {{"p1=2/2 p2=2/2 tube=1L0B turn=p2 sawed", 1.0}});

  // Not in multiplayer, which has no dealer.
  const GameState table = parse("p1=2/2 p2=2/2 p3=2/2 tube=1L1B turn=p2 sawed known=p2:0B");
  for (const Outcome& outcome :
       rules::apply(table, Action::shoot(1), RuleConfig::multiplayer(3, 2))) {
    EXPECT_FALSE(outcome.state.tube.sawed);
  }
}

TEST(Moves, ABlankThePlayerFiresIntoItselfSpendsTheSaw) {
  const GameState player = parse("p1=2/2 p2=2/2 tube=1L1B turn=p1 sawed known=p1:0B");
  expectOutcomes(outcomesOf(player, Action::shoot(0), config()),
                 {{"p1=2/2 p2=2/2 tube=1L0B turn=p1", 1.0}});

  // The last shell leaves nothing for the saw to act on.
  const GameState last = parse("p1=2/2 p2=2/2 tube=0L1B turn=p2 sawed");
  const std::vector<Outcome> out = rules::apply(last, Action::shoot(1), config());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_TRUE(out.front().state.tube.empty());
  EXPECT_FALSE(out.front().state.tube.sawed);
}

TEST(Moves, ThePlayersMovesAndReloadsLeaveTheDealersListAlone) {
  // The dealer's list changes only on its own passes and on deals to it, so
  // nothing p1 does, smoking its own cigarettes included, touches the bit
  // (DealerIntelligence.gd 113-116; ItemManager.gd 368).
  const GameState state =
      parse("p1=2/3[cig,beer,mg,adr] p2=2/3[med,saw] tube=2L2B turn=p1 listcigs");
  for (const Action& action : rules::legalActions(state, config())) {
    for (const Outcome& outcome : rules::apply(state, action, config())) {
      EXPECT_TRUE(outcome.state.dealerListCigs) << action.describe(0);
    }
  }
  const GameState empty = parse("p1=2/3[cig] p2=2/3 tube=0L0B turn=p1 listcigs");
  for (const Outcome& outcome : rules::reloadOutcomes(empty, config(), true)) {
    EXPECT_TRUE(outcome.state.dealerListCigs);
  }
}

}  // namespace
}  // namespace bsr
