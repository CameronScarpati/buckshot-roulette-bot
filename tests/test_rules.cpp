/// Rule transitions, driven from written positions so that each test says what
/// it means. Probabilities are checked to sum to one on every branch set,
/// because a transition that loses probability mass would quietly bias the
/// solver rather than fail loudly.

#include <gtest/gtest.h>

#include <numeric>

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

double totalProbability(const std::vector<Outcome>& outcomes) {
  double total = 0.0;
  for (const Outcome& outcome : outcomes) total += outcome.probability;
  return total;
}

const Outcome& branchWhereShellIs(const std::vector<Outcome>& outcomes, Shell type) {
  for (const Outcome& outcome : outcomes) {
    if (outcome.shellType == type) return outcome;
  }
  ADD_FAILURE() << "no branch fired a shell of the expected type";
  return outcomes.front();
}

RuleConfig config() {
  return RuleConfig::doubleOrNothing(4);
}

bool contains(const std::vector<Action>& actions, const Action& wanted) {
  for (const Action& action : actions) {
    if (action == wanted) return true;
  }
  return false;
}

TEST(Rules, ShootingYourselfWithABlankKeepsTheTurn) {
  const GameState state = parse("p1=2/2 p2=2/2 tube=1L1B turn=p1");
  const std::vector<Outcome> outcomes = rules::apply(state, Action::shoot(0), config());
  ASSERT_EQ(outcomes.size(), 2u);
  EXPECT_DOUBLE_EQ(totalProbability(outcomes), 1.0);

  const Outcome& blank = branchWhereShellIs(outcomes, Shell::Blank);
  EXPECT_EQ(blank.state.current, 0);
  EXPECT_EQ(blank.state.players[0].hp, 2);
  EXPECT_EQ(blank.state.tube.size(), 1);

  const Outcome& live = branchWhereShellIs(outcomes, Shell::Live);
  EXPECT_EQ(live.state.current, 1);
  EXPECT_EQ(live.state.players[0].hp, 1);
}

TEST(Rules, ShootingSomebodyElsePassesTheTurnEitherWay) {
  const GameState state = parse("p1=2/2 p2=2/2 tube=1L1B turn=p1");
  const std::vector<Outcome> outcomes = rules::apply(state, Action::shoot(1), config());
  ASSERT_EQ(outcomes.size(), 2u);
  for (const Outcome& outcome : outcomes) EXPECT_EQ(outcome.state.current, 1);
  EXPECT_EQ(branchWhereShellIs(outcomes, Shell::Live).state.players[1].hp, 1);
  EXPECT_EQ(branchWhereShellIs(outcomes, Shell::Blank).state.players[1].hp, 2);
}

TEST(Rules, ASawedBarrelDealsTwoAndIsSpentByTheShotEitherWay) {
  const GameState state = parse("p1=2/2 p2=3/3 tube=1L1B turn=p1 sawed");
  const std::vector<Outcome> outcomes = rules::apply(state, Action::shoot(1), config());
  const Outcome& live = branchWhereShellIs(outcomes, Shell::Live);
  EXPECT_EQ(live.state.players[1].hp, 1);
  EXPECT_FALSE(live.state.tube.sawed);
  const Outcome& blank = branchWhereShellIs(outcomes, Shell::Blank);
  EXPECT_EQ(blank.state.players[1].hp, 3);
  EXPECT_FALSE(blank.state.tube.sawed) << "a blank still consumes the saw";
}

TEST(Rules, ASawedShotCannotDriveChargesBelowZero) {
  const GameState state = parse("p1=2/2 p2=1/3 tube=1L0B turn=p1 sawed");
  const std::vector<Outcome> outcomes = rules::apply(state, Action::shoot(1), config());
  ASSERT_EQ(outcomes.size(), 1u);
  EXPECT_EQ(outcomes.front().state.players[1].hp, 0);
  EXPECT_TRUE(outcomes.front().state.roundOver());
}

TEST(Rules, ACuffedSeatIsSkippedOnceAndTheCuffsComeOff) {
  const GameState state = parse("p1=2/2 p2=2/2 tube=0L1B turn=p1 cuffed=p2");
  const std::vector<Outcome> outcomes = rules::apply(state, Action::shoot(1), config());
  ASSERT_EQ(outcomes.size(), 1u);
  const GameState& after = outcomes.front().state;
  EXPECT_EQ(after.current, 0) << "the skip hands the turn straight back";
  EXPECT_FALSE(after.players[1].cuffed);
  EXPECT_TRUE(after.players[1].skipConsumed);
}

TEST(Rules, ASkippedSeatCannotBeCuffedAgainBeforeItPlays) {
  GameState state = parse("p1=2/2[cuff] p2=2/2 tube=0L2B turn=p1 cuffed=p2");
  const std::vector<Outcome> outcomes = rules::apply(state, Action::shoot(1), config());
  const GameState& after = outcomes.front().state;
  ASSERT_EQ(after.current, 0);
  ASSERT_TRUE(after.players[1].skipConsumed);
  const std::vector<Action> actions = rules::legalActions(after, config());
  EXPECT_FALSE(contains(actions, Action::useOn(Item::Handcuffs, 1)))
      << "the opponent is owed a turn before a second pair of cuffs";
}

TEST(Rules, OnlyOnePairOfCuffsPerTurn) {
  const GameState state = parse("p1=2/2[cuff,cuff] p2=2/2 tube=1L1B turn=p1");
  const std::vector<Outcome> outcomes =
      rules::apply(state, Action::useOn(Item::Handcuffs, 1), config());
  ASSERT_EQ(outcomes.size(), 1u);
  const GameState& after = outcomes.front().state;
  EXPECT_TRUE(after.players[1].cuffed);
  EXPECT_TRUE(after.cuffUsedThisTurn);
  EXPECT_EQ(after.current, 0) << "using an item does not end the turn";
  const std::vector<Action> actions = rules::legalActions(after, config());
  EXPECT_FALSE(contains(actions, Action::useOn(Item::Handcuffs, 1)));
}

TEST(Rules, AMagnifyingGlassTellsOnlyItsUser) {
  const GameState state = parse("p1=2/2[mg] p2=2/2 tube=1L1B turn=p1");
  const std::vector<Outcome> outcomes =
      rules::apply(state, Action::use(Item::MagnifyingGlass), config());
  ASSERT_EQ(outcomes.size(), 2u);
  EXPECT_DOUBLE_EQ(totalProbability(outcomes), 1.0);
  for (const Outcome& outcome : outcomes) {
    EXPECT_TRUE(outcome.state.tube.knows(0, 0));
    EXPECT_FALSE(outcome.state.tube.knows(1, 0));
    EXPECT_EQ(outcome.state.current, 0);
    EXPECT_EQ(outcome.state.players[0].items[itemIndex(Item::MagnifyingGlass)], 0);
  }
}

TEST(Rules, BeerEjectsInPublicAndShortensTheTube) {
  const GameState state = parse("p1=2/2[beer] p2=2/2 tube=1L1B turn=p1");
  const std::vector<Outcome> outcomes = rules::apply(state, Action::use(Item::Beer), config());
  ASSERT_EQ(outcomes.size(), 2u);
  for (const Outcome& outcome : outcomes) {
    EXPECT_EQ(outcome.state.tube.size(), 1);
    EXPECT_EQ(outcome.state.current, 0);
    EXPECT_TRUE(outcome.shellFired);
  }
  EXPECT_EQ(branchWhereShellIs(outcomes, Shell::Live).state.tube.live, 0);
  EXPECT_EQ(branchWhereShellIs(outcomes, Shell::Blank).state.tube.blank, 0);
}

TEST(Rules, CigarettesHealOneAndAreNotOfferedAtFullCharges) {
  const GameState wounded = parse("p1=1/3[cig] p2=2/2 tube=1L1B turn=p1");
  const std::vector<Outcome> outcomes =
      rules::apply(wounded, Action::use(Item::Cigarettes), config());
  ASSERT_EQ(outcomes.size(), 1u);
  EXPECT_EQ(outcomes.front().state.players[0].hp, 2);

  const GameState full = parse("p1=3/3[cig] p2=2/2 tube=1L1B turn=p1");
  EXPECT_FALSE(contains(rules::legalActions(full, config()), Action::use(Item::Cigarettes)));
}

TEST(Rules, ExpiredMedicineIsATossUpThatCanKillItsUser) {
  const GameState state = parse("p1=1/4[med] p2=2/2 tube=1L1B turn=p1");
  const std::vector<Outcome> outcomes =
      rules::apply(state, Action::use(Item::ExpiredMedicine), config());
  ASSERT_EQ(outcomes.size(), 2u);
  EXPECT_DOUBLE_EQ(totalProbability(outcomes), 1.0);
  bool sawHeal = false;
  bool sawDeath = false;
  for (const Outcome& outcome : outcomes) {
    EXPECT_DOUBLE_EQ(outcome.probability, 0.5);
    if (outcome.state.players[0].hp == 3) sawHeal = true;
    if (outcome.state.players[0].hp == 0) sawDeath = true;
  }
  EXPECT_TRUE(sawHeal);
  EXPECT_TRUE(sawDeath);
}

TEST(Rules, AdrenalineTakesAnItemAndSpendsItImmediately) {
  const GameState state = parse("p1=2/2[adr] p2=2/2[saw] tube=1L1B turn=p1");
  const std::vector<Outcome> outcomes =
      rules::apply(state, Action::steal(1, Item::HandSaw), config());
  ASSERT_EQ(outcomes.size(), 1u);
  const GameState& after = outcomes.front().state;
  EXPECT_TRUE(after.tube.sawed);
  EXPECT_EQ(after.players[0].items[itemIndex(Item::Adrenaline)], 0);
  EXPECT_EQ(after.players[1].items[itemIndex(Item::HandSaw)], 0);
  EXPECT_EQ(after.current, 0);
}

TEST(Rules, AdrenalineCannotTakeAnotherAdrenaline) {
  const GameState state = parse("p1=2/2[adr] p2=2/2[adr] tube=1L1B turn=p1");
  for (const Action& action : rules::legalActions(state, config())) {
    EXPECT_FALSE(action.kind == Action::Kind::UseItem && action.item == Item::Adrenaline &&
                 action.stolen == Item::Adrenaline);
  }
}

TEST(Rules, ABurnerPhoneNamesOneFuturePositionPrivately) {
  const GameState state = parse("p1=2/2[phone] p2=2/2 tube=2L2B turn=p1");
  const std::vector<Outcome> outcomes =
      rules::apply(state, Action::use(Item::BurnerPhone), config());
  EXPECT_DOUBLE_EQ(totalProbability(outcomes), 1.0);
  for (const Outcome& outcome : outcomes) {
    int knownToUser = 0;
    for (int i = 0; i < outcome.state.tube.size(); ++i) {
      if (outcome.state.tube.knows(0, i)) ++knownToUser;
      EXPECT_FALSE(outcome.state.tube.knows(1, i));
    }
    EXPECT_EQ(knownToUser, 1);
    EXPECT_FALSE(outcome.state.tube.knows(0, 0)) << "the phone never names the chamber";
  }
}

TEST(Rules, TheRemoteReversesTurnOrderAndOnlyExistsInMultiplayer) {
  const RuleConfig multi = RuleConfig::multiplayer(3, 2);
  const GameState state = parse("p1=2/2[rem] p2=2/2 p3=2/2 tube=1L1B turn=p1");
  const std::vector<Outcome> outcomes = rules::apply(state, Action::use(Item::Remote), multi);
  ASSERT_EQ(outcomes.size(), 1u);
  EXPECT_EQ(outcomes.front().state.direction, -1);
  EXPECT_FALSE(contains(rules::legalActions(state, config()), Action::use(Item::Remote)))
      << "no remote outside multiplayer";
}

TEST(Rules, MultiplayerLetsYouPickWhoYouShoot) {
  const RuleConfig multi = RuleConfig::multiplayer(3, 2);
  const GameState state = parse("p1=2/2 p2=2/2 p3=2/2 tube=1L1B turn=p1");
  const std::vector<Action> actions = rules::legalActions(state, multi);
  EXPECT_TRUE(contains(actions, Action::shoot(0)));
  EXPECT_TRUE(contains(actions, Action::shoot(1)));
  EXPECT_TRUE(contains(actions, Action::shoot(2)));
  EXPECT_FALSE(contains(actions, Action::useOn(Item::Handcuffs, 1)));
}

TEST(Rules, TurnOrderSkipsTheDeadAndFollowsTheDirection) {
  GameState state = parse("p1=2/2 p2=0/2 p3=2/2 tube=1L1B turn=p1");
  EXPECT_EQ(state.nextSeat(0), 2);
  state.direction = -1;
  EXPECT_EQ(state.nextSeat(0), 2);
  EXPECT_EQ(state.nextSeat(2), 0);
}

TEST(Rules, NoMagnifyingGlassWhenTheAnswerIsAlreadyCertain) {
  const GameState allLive = parse("p1=2/2[mg] p2=2/2 tube=2L0B turn=p1");
  EXPECT_FALSE(
      contains(rules::legalActions(allLive, config()), Action::use(Item::MagnifyingGlass)));
  const GameState known = parse("p1=2/2[mg] p2=2/2 tube=1L1B turn=p1 known=p1:0L");
  EXPECT_FALSE(contains(rules::legalActions(known, config()), Action::use(Item::MagnifyingGlass)));
  const GameState unknown = parse("p1=2/2[mg] p2=2/2 tube=1L1B turn=p1");
  EXPECT_TRUE(contains(rules::legalActions(unknown, config()), Action::use(Item::MagnifyingGlass)));
}

TEST(Rules, NoSawOnAnAlreadySawedBarrel) {
  const GameState state = parse("p1=2/2[saw] p2=2/2 tube=1L1B turn=p1 sawed");
  EXPECT_FALSE(contains(rules::legalActions(state, config()), Action::use(Item::HandSaw)));
}

TEST(Rules, EveryTransitionKeepsItsProbabilityMass) {
  const std::vector<std::string> positions = {
      "p1=2/2[mg,beer,cig,cuff,saw] p2=2/2[phone,inv,med] tube=2L2B turn=p1",
      "p1=1/3[phone] p2=2/2 tube=3L1B turn=p1",
      "p1=2/2[inv] p2=2/2 tube=1L3B turn=p1",
      "p1=2/2[med] p2=1/1 tube=1L1B turn=p1",
      "p1=2/2[adr] p2=2/2[beer] tube=2L1B turn=p1",
  };
  for (const std::string& text : positions) {
    const GameState state = parse(text);
    for (const Action& action : rules::legalActions(state, config())) {
      const std::vector<Outcome> outcomes = rules::apply(state, action, config());
      ASSERT_FALSE(outcomes.empty()) << action.describe(state.current) << " in " << text;
      EXPECT_NEAR(totalProbability(outcomes), 1.0, 1e-12)
          << action.describe(state.current) << " in " << text;
    }
  }
}

TEST(Rules, AReloadDistributionIsAProperDistribution) {
  double total = 0.0;
  int loads = 0;
  bool sawBlankHeavy = false;
  for (const auto& entry : rules::loadDistribution(config())) {
    const int live = std::get<0>(entry);
    const int blank = std::get<1>(entry);
    EXPECT_GE(live, 1);
    EXPECT_GE(blank, 1);
    EXPECT_LE(live + blank, kMaxShells);
    if (blank > live) sawBlankHeavy = true;
    total += std::get<2>(entry);
    ++loads;
  }
  EXPECT_NEAR(total, 1.0, 1e-12);
  EXPECT_GT(loads, 7);
  EXPECT_TRUE(sawBlankHeavy) << "blank heavy loads have to be possible";
}

TEST(Rules, AReloadClearsCuffsAndHandsTheTurnToSeatOne) {
  GameState state = parse("p1=2/2 p2=2/2 tube=1L0B turn=p2 cuffed=p1");
  state.tube.live = 0;
  state.tube.blank = 0;
  const std::vector<Outcome> outcomes = rules::reloadOutcomes(state, config(), true);
  ASSERT_FALSE(outcomes.empty());
  for (const Outcome& outcome : outcomes) {
    EXPECT_FALSE(outcome.state.players[0].cuffed);
    EXPECT_EQ(outcome.state.current, 0);
    EXPECT_FALSE(outcome.state.tube.sawed);
    EXPECT_GE(outcome.state.players[0].itemCount(), 1);
  }
}

}  // namespace
}  // namespace bsr
