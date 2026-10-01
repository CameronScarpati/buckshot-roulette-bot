/// Exact values for small positions, written as fractions so a failure shows
/// which number moved. Each position is one the command line can be asked
/// about, and the options match the flags that question would use.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "engine/Notation.h"
#include "engine/Position.h"
#include "solver/Solver.h"

namespace bsr {
namespace {

Position position(const std::string& text) {
  Position whole;
  std::string error;
  EXPECT_TRUE(notation::parsePosition(text, &whole, &error)) << error << " in: " << text;
  return whole;
}

SolveOptions optimal(int reloadBudget = 0) {
  SolveOptions options;
  options.seat = 0;
  options.reloadBudget = reloadBudget;
  return options;
}

SolveOptions dealer(int reloadBudget = 0) {
  SolveOptions options = optimal(reloadBudget);
  options.opponent = OpponentModel::Dealer;
  return options;
}

/// Double or Nothing as the command line sets it up for p1's charges, with a
/// fixed item count per load when `items` is not negative.
RuleConfig doubleOrNothing(const Position& at, int items = -1) {
  RuleConfig config = RuleConfig::doubleOrNothing(at.state.players[0].maxHp);
  if (items >= 0) {
    config.itemsPerLoad = static_cast<std::uint8_t>(items);
    config.itemsPerLoadMax = static_cast<std::uint8_t>(items);
  }
  return config;
}

SolveResult solveText(const std::string& text, const RuleConfig& config,
                      const SolveOptions& options) {
  return solve(position(text), config, options);
}

double valueOf(const SolveResult& result, const std::string& action) {
  for (const ActionValue& entry : result.ranked) {
    if (entry.action.describe(result.mover) == action) return entry.value;
  }
  ADD_FAILURE() << "no move called " << action;
  return -1.0;
}

std::vector<std::string> rowTexts(const SolveResult& result) {
  std::vector<std::string> texts;
  for (const ActionValue& entry : result.ranked) {
    texts.push_back(entry.action.describe(result.mover));
  }
  return texts;
}

constexpr double kTight = 1e-12;

// ---------------------------------------------------------------------------
// A sawed barrel across a reload
// ---------------------------------------------------------------------------

TEST(ExactValues, ASawedBarrelCarriedThroughAReloadOnOneCharge) {
  const std::string text = "p1=1/1[beer] p2=2/2 tube=0L1B turn=p1 sawed";
  RuleConfig config = doubleOrNothing(position(text), 0);
  const SolveResult result = solveText(text, config, optimal(1));
  EXPECT_NEAR(result.value, 501.0 / 980.0, kTight);
  ASSERT_EQ(result.ranked.size(), 3u);
  EXPECT_EQ(rowTexts(result), (std::vector<std::string>{"use Beer", "shoot self", "shoot p2"}));
  EXPECT_NEAR(result.ranked[0].value, 501.0 / 980.0, kTight);
  EXPECT_NEAR(result.ranked[1].value, 439.0 / 1470.0, kTight);
  EXPECT_NEAR(result.ranked[2].value, 439.0 / 1470.0, kTight);

  config.sawSurvivesReload = false;
  const SolveResult cleared = solveText(text, config, optimal(1));
  EXPECT_NEAR(cleared.value, 439.0 / 1470.0, kTight);
  EXPECT_NEAR(valueOf(cleared, "use Beer"), 123.0 / 490.0, kTight);
}

TEST(ExactValues, ASawedBarrelCarriedThroughAReloadOnTwoCharges) {
  const std::string text = "p1=2/2[beer] p2=2/2 tube=0L1B turn=p1 sawed";
  RuleConfig config = doubleOrNothing(position(text), 0);
  const SolveResult result = solveText(text, config, optimal(1));
  EXPECT_NEAR(result.value, 669.0 / 980.0, kTight);
  EXPECT_NEAR(valueOf(result, "use Beer"), 669.0 / 980.0, kTight);
  EXPECT_NEAR(valueOf(result, "shoot self"), 148.0 / 245.0, kTight);
  EXPECT_NEAR(valueOf(result, "shoot p2"), 148.0 / 245.0, kTight);

  config.sawSurvivesReload = false;
  const SolveResult cleared = solveText(text, config, optimal(1));
  EXPECT_NEAR(valueOf(cleared, "use Beer"), 2447.0 / 4410.0, kTight);
}

// ---------------------------------------------------------------------------
// The load table
// ---------------------------------------------------------------------------

TEST(ExactValues, DoubleOrNothingReloadsFromItsSevenLoads) {
  const std::string text = "p1=1/1 p2=1/1 tube=0L1B turn=p1";
  const SolveResult result = solveText(text, doubleOrNothing(position(text), 0), optimal(1));
  EXPECT_NEAR(result.value, 367.0 / 588.0, kTight);
  EXPECT_NEAR(valueOf(result, "shoot self"), 367.0 / 588.0, kTight);
  EXPECT_NEAR(valueOf(result, "shoot p2"), 367.0 / 588.0, kTight);
  // Reload states are remembered like any other. On one charge each, every
  // live shell ends the round and every load holds one, so no line outlasts
  // the one reload searched and nothing is cut short.
  EXPECT_FALSE(result.budgetReached);
  EXPECT_FALSE(result.nodeLimitHit);
  EXPECT_FALSE(result.truncated);
}

TEST(ExactValues, TheStoryLoadTableKeepsItsOwnValue) {
  RuleConfig config = RuleConfig::storyRound(2);
  config.itemsPerLoad = 0;
  config.itemsPerLoadMax = 0;
  const SolveResult result = solveText("p1=1/1 p2=1/1 tube=0L1B turn=p1", config, optimal(1));
  EXPECT_NEAR(result.value, 198283.0 / 308700.0, kTight);
}

// ---------------------------------------------------------------------------
// Items that change nothing are still offered
// ---------------------------------------------------------------------------

TEST(ExactValues, APhoneOnTheLastShellIsOfferedAndChangesNothing) {
  const std::string text = "p1=1/1[phone] p2=1/1 tube=1L0B turn=p1";
  const SolveResult result = solveText(text, doubleOrNothing(position(text)), optimal());
  EXPECT_NEAR(result.value, 1.0, kTight);
  EXPECT_EQ(rowTexts(result),
            (std::vector<std::string>{"shoot p2", "use Burner Phone", "shoot self"}));
  EXPECT_NEAR(result.ranked[0].value, 1.0, kTight);
  EXPECT_NEAR(result.ranked[1].value, 1.0, kTight);
  EXPECT_NEAR(result.ranked[2].value, 0.0, kTight);
}

// ---------------------------------------------------------------------------
// The stage 3 heal floor on shot damage
// ---------------------------------------------------------------------------

TEST(ExactValues, ASawedShotInStageThreeStopsAtTheFloor) {
  // p1 on two charges takes a sawed live and is left on the faded floor
  // rather than dead, so the round goes on to an even boundary.
  const std::string text = "p1=2/5 p2=1/5 tube=1L0B turn=p2 sawed";
  EXPECT_NEAR(solveText(text, RuleConfig::storyRound(3), optimal()).value, 0.5, kTight);
  EXPECT_NEAR(solveText(text, RuleConfig::storyRound(3), dealer()).value, 0.5, kTight);
}

TEST(ExactValues, StageThreeEvenUpsASawedCoin) {
  const SolveResult result =
      solveText("p1=2/5 p2=2/5 tube=1L1B turn=p2 sawed", RuleConfig::storyRound(3), optimal());
  EXPECT_NEAR(result.value, 0.5, kTight);
  EXPECT_NEAR(valueOf(result, "shoot p1"), 0.5, kTight);
  EXPECT_NEAR(valueOf(result, "shoot self"), 0.5, kTight);
}

TEST(ExactValues, OneChargeInStageThreeIsStillTheLast) {
  EXPECT_NEAR(
      solveText("p1=1/5 p2=1/5 tube=1L0B turn=p2", RuleConfig::storyRound(3), optimal()).value, 0.0,
      kTight);
}

TEST(ExactValues, DoubleOrNothingHasNoFloor) {
  const std::string text = "p1=2/2 p2=1/2 tube=1L0B turn=p2 sawed";
  EXPECT_NEAR(solveText(text, doubleOrNothing(position(text)), optimal()).value, 0.0, kTight);
}

// ---------------------------------------------------------------------------
// Phone reads the advised seat never saw
// ---------------------------------------------------------------------------

TEST(ExactValues, AnUnseenReadIsTheAverageOfTheShellsItCouldHaveNamed) {
  const std::string phoned = "p1=2/2[beer] p2=2/2 tube=2L3B turn=p1 phoned=p2@5";
  const RuleConfig config = doubleOrNothing(position(phoned));
  const SolveResult result = solveText(phoned, config, optimal());
  ASSERT_FALSE(result.refused) << result.assumptions;
  ASSERT_FALSE(result.ranked.empty());

  std::vector<SolveResult> each;
  for (int k = 1; k <= 4; ++k) {
    each.push_back(
        solveText("p1=2/2[beer] p2=2/2 tube=2L3B turn=p1 known=p2:" + std::to_string(k) + "B",
                  config, optimal()));
  }
  double meanValue = 0.0;
  for (const SolveResult& one : each) meanValue += one.value / 4.0;
  EXPECT_NEAR(result.value, meanValue, kTight);
  for (const ActionValue& row : result.ranked) {
    const std::string text = row.action.describe(0);
    double mean = 0.0;
    for (const SolveResult& one : each) mean += valueOf(one, text) / 4.0;
    EXPECT_NEAR(row.value, mean, kTight) << text;
  }
}

TEST(ExactValues, AnUnseenDealerReadIsAveragedTheSameWay) {
  const std::string phoned = "p1=2/2 p2=2/2[beer] tube=2L3B turn=p2 phoned=p2@5";
  const RuleConfig config = doubleOrNothing(position(phoned));
  const SolveResult result = solveText(phoned, config, dealer());
  ASSERT_FALSE(result.refused) << result.assumptions;
  double mean = 0.0;
  for (int k = 1; k <= 4; ++k) {
    mean += solveText("p1=2/2 p2=2/2[beer] tube=2L3B turn=p2 known=p2:" + std::to_string(k) + "B",
                      config, dealer())
                .value /
            4.0;
  }
  EXPECT_NEAR(result.value, mean, kTight);
}

TEST(ExactValues, InvertingAChamberOnlyTheOtherSeatSawIsWorthWhatItIsUnseen) {
  // p1's one item is an Inverter, so the chamber fires before p2 moves again,
  // and nothing p2 saw of it can change a move p2 makes. The flip shows
  // nobody the type (ItemInteraction.gd 165-171), so p1 cannot learn it from
  // the flip either: every row is worth the same whether or not p2 looked.
  struct Case {
    std::string text;
    SolveOptions options;
  };
  for (const Case& one : {Case{"p1=2/2[inv] p2=2/2 tube=2L2B turn=p1", optimal()},
                          Case{"p1=1/2[inv] p2=2/2 tube=3L1B turn=p1", dealer()}}) {
    const RuleConfig config = doubleOrNothing(position(one.text));
    const SolveResult unseen = solveText(one.text, config, one.options);
    for (const char* seen : {"0L", "0B"}) {
      const SolveResult looked = solveText(one.text + " known=p2:" + seen, config, one.options);
      ASSERT_EQ(looked.ranked.size(), unseen.ranked.size()) << one.text << " " << seen;
      for (const ActionValue& row : unseen.ranked) {
        const std::string text = row.action.describe(0);
        EXPECT_NEAR(valueOf(looked, text), row.value, kTight)
            << one.text << " " << seen << ": " << text;
      }
    }
    // A read p2 made at five shells, one shot ago, named each of the four
    // shells left a quarter of the time. Naming the chamber is worth what
    // nobody looking is, and naming one of the other three is a read made at
    // four shells.
    const SolveResult atFive = solveText(one.text + " phoned=p2@5", config, one.options);
    const SolveResult atFour = solveText(one.text + " phoned=p2@4", config, one.options);
    ASSERT_EQ(atFive.ranked.size(), unseen.ranked.size()) << one.text;
    for (const ActionValue& row : unseen.ranked) {
      const std::string text = row.action.describe(0);
      EXPECT_NEAR(valueOf(atFive, text), row.value / 4.0 + 3.0 * valueOf(atFour, text) / 4.0,
                  kTight)
          << one.text << ": " << text;
    }
  }
  const std::string text = "p1=2/2[inv] p2=2/2 tube=2L2B turn=p1";
  const RuleConfig config = doubleOrNothing(position(text));
  EXPECT_NEAR(valueOf(solveText(text, config, optimal()), "use Inverter"), 5.0 / 9.0, kTight);
  EXPECT_NEAR(valueOf(solveText(text + " known=p2:0L", config, optimal()), "use Inverter"),
              5.0 / 9.0, kTight);
  EXPECT_NEAR(valueOf(solveText(text + " phoned=p2@4", config, optimal()), "use Inverter"),
              29.0 / 54.0, kTight);
  EXPECT_NEAR(valueOf(solveText(text + " phoned=p2@5", config, optimal()), "use Inverter"),
              13.0 / 24.0, kTight);
}

// ---------------------------------------------------------------------------
// Hand order and the dealer's list
// ---------------------------------------------------------------------------

TEST(ExactValues, HandOrderDoesNotMatterAgainstTheSolver) {
  const std::string first = "p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1";
  const std::string second = "p1=2/4[mg,saw] p2=4/4[cuff,beer] tube=2L3B turn=p1";
  const RuleConfig config = doubleOrNothing(position(first));
  const SolveResult a = solveText(first, config, optimal());
  const SolveResult b = solveText(second, config, optimal());
  EXPECT_EQ(a.value, b.value);
  EXPECT_EQ(a.nodes, b.nodes);
  ASSERT_EQ(a.ranked.size(), b.ranked.size());
  for (const ActionValue& row : a.ranked) {
    EXPECT_EQ(row.value, valueOf(b, row.action.describe(0))) << row.action.describe(0);
  }
}

TEST(ExactValues, TheDealersListMeansNothingToTheSolver) {
  const std::string text = "p1=2/3[cig] p2=2/3[med] tube=2L2B turn=p1";
  const RuleConfig config = doubleOrNothing(position(text));
  const SolveResult plain = solveText(text, config, optimal());
  const SolveResult listed = solveText(text + " listcigs", config, optimal());
  EXPECT_EQ(plain.value, listed.value);
  EXPECT_EQ(plain.nodes, listed.nodes);
  ASSERT_EQ(plain.ranked.size(), listed.ranked.size());
  for (std::size_t i = 0; i < plain.ranked.size(); ++i) {
    EXPECT_EQ(plain.ranked[i].action.describe(0), listed.ranked[i].action.describe(0));
    EXPECT_EQ(plain.ranked[i].value, listed.ranked[i].value);
  }
}

TEST(ExactValues, MergingP1sHandOrderAgainstTheDealerChangesNoValue) {
  // p1's order is read by the dealer only while it holds Adrenaline, so the
  // search may sort it when the dealer holds none and no reload deals it one.
  // The values have to agree whether it does or not.
  struct Case {
    std::string text;
    int reloads;
  };
  const std::vector<Case> cases = {
      {"p1=3/4[saw,beer,mg] p2=3/4[mg,beer] tube=2L2B turn=p2", 0},
      {"p1=3/4[saw] p2=3/4[mg,beer] tube=2L2B turn=p2", 1},
      {"p1=2/2[beer,mg,beer] p2=2/2[cuff] tube=2L3B turn=p1", 0},
      {"p1=2/2[cig,beer,cig] p2=2/2[med,saw] tube=1L2B turn=p2 known=p2:0B", 0},
      {"p1=2/2[beer,mg,beer] p2=2/2[adr] tube=1L2B turn=p2", 0},
  };
  for (const Case& one : cases) {
    const RuleConfig config = doubleOrNothing(position(one.text));
    SolveOptions merged = dealer(one.reloads);
    SolveOptions kept = merged;
    kept.mergePlayerHandOrder = false;
    const SolveResult a = solveText(one.text, config, merged);
    const SolveResult b = solveText(one.text, config, kept);
    ASSERT_FALSE(a.refused) << a.assumptions;
    EXPECT_NEAR(a.value, b.value, kTight) << one.text;
    EXPECT_LE(a.nodes, b.nodes) << one.text;
    ASSERT_EQ(a.ranked.size(), b.ranked.size()) << one.text;
    for (std::size_t i = 0; i < a.ranked.size(); ++i) {
      EXPECT_EQ(a.ranked[i].action.describe(0), b.ranked[i].action.describe(0)) << one.text;
      EXPECT_NEAR(a.ranked[i].value, b.ranked[i].value, kTight) << one.text;
    }
  }
}

TEST(ExactValues, ABlindChoiceCountsEachKindOfMoveOnce) {
  // p2 has looked at a shell p1 has not, so p1 picks evenly between moves it
  // cannot tell apart. Two Beers are one kind of move wherever they sit in the
  // hand, so the order the hand was dealt in cannot change the average. Both
  // positions have a choice where Beer ties with another move, and counting
  // the two Beers of the first hand as two moves would weigh it twice.
  SolveOptions options = dealer();
  options.mergePlayerHandOrder = false;
  struct Case {
    std::string charges;
    std::string tail;
  };
  const std::vector<Case> cases = {
      {"p1=1/1", " p2=1/1 tube=3L3B turn=p2 known=p2:4L"},
      {"p1=2/2", " p2=2/2[saw,beer] tube=2L3B turn=p1 known=p2:3L"},
  };
  for (const Case& one : cases) {
    const RuleConfig config = doubleOrNothing(position(one.charges + one.tail));
    const SolveResult apart = solveText(one.charges + "[beer,mg,beer]" + one.tail, config, options);
    const SolveResult together =
        solveText(one.charges + "[beer,beer,mg]" + one.tail, config, options);
    ASSERT_FALSE(apart.refused) << apart.assumptions;
    EXPECT_NEAR(apart.value, together.value, kTight) << one.tail;
  }
}

// ---------------------------------------------------------------------------
// Dealer memory at the root
// ---------------------------------------------------------------------------

TEST(ExactValues, ADealerThatSawTheChamberIsAveragedOverWhatItSaw) {
  const RuleConfig config = RuleConfig::storyRound(2);
  const double whole =
      solveText("p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 known=p2:0L dealer=seen", config, dealer())
          .value;
  const double live = solveText(
                          "p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 known=p1:0L known=p2:0L "
                          "dealer=seen",
                          config, dealer())
                          .value;
  const double blank = solveText(
                           "p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 known=p1:0B known=p2:0B "
                           "dealer=seen",
                           config, dealer())
                           .value;
  EXPECT_NEAR(whole, 0.5 * live + 0.5 * blank, kTight);
}

TEST(ExactValues, DealerMemoryIsRefusedWhereItCannotArise) {
  const std::string believes = "p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 dealer=believes:B";
  const SolveResult underDon = solveText(believes, doubleOrNothing(position(believes)), dealer());
  EXPECT_TRUE(underDon.refused);
  EXPECT_EQ(underDon.assumptions, "Not solved: dealer=believes:B happens only in story mode");

  const SolveResult underSolver = solveText(believes, RuleConfig::storyRound(2), optimal());
  EXPECT_TRUE(underSolver.refused);
  EXPECT_EQ(underSolver.assumptions,
            "Not solved: dealer describes the scripted dealer; use --opponent dealer");

  const std::string aim = "p1=2/2 p2=2/2 tube=2L2B turn=p2 dealer=aim:self";
  const SolveResult aimInStory = solveText(aim, RuleConfig::storyRound(2), dealer());
  EXPECT_TRUE(aimInStory.refused);
  EXPECT_EQ(aimInStory.assumptions,
            "Not solved: dealer=aim:self happens only in double or nothing");
}

TEST(ExactValues, ReadsByTheAdvisedSeatAreRefused) {
  const std::string text = "p1=2/2[beer] p2=2/2 tube=2L3B turn=p1 phoned=p1@5";
  const SolveResult result = solveText(text, doubleOrNothing(position(text)), optimal());
  EXPECT_TRUE(result.refused);
  EXPECT_EQ(result.assumptions,
            "Not solved: phoned names p1, the seat being advised, which saw where its own phone "
            "looked; give known=p1 instead");
}

TEST(ExactValues, TheChamberTheDealerSawIsKeptPastTheLimit) {
  // Five other shells the dealer looked at are more than the limit of four,
  // so they are treated as seen by nobody, but the chamber it saw is still
  // drawn from p1's pool and stays pinned, with the memory following it.
  const std::string over = "p1=2/2 p2=2/2 tube=4L4B turn=p2 known=p2:0L,1L,2B,3B,4L,5B dealer=seen";
  const RuleConfig config = doubleOrNothing(position(over));
  const SolveResult result = solveText(over, config, dealer());
  ASSERT_FALSE(result.refused) << result.assumptions;
  EXPECT_TRUE(result.opponentKnowledgeDropped);
  const double live =
      solveText("p1=2/2 p2=2/2 tube=4L4B turn=p2 known=p1:0L known=p2:0L dealer=seen", config,
                dealer())
          .value;
  const double blank =
      solveText("p1=2/2 p2=2/2 tube=4L4B turn=p2 known=p1:0B known=p2:0B dealer=seen", config,
                dealer())
          .value;
  EXPECT_NEAR(result.value, 0.5 * live + 0.5 * blank, kTight);
}

TEST(ExactValues, ASawedBarrelShowsEverySeatTheChamberTheDealerSaw) {
  // A dealer that has seen the chamber saws only a live one
  // (DealerIntelligence.gd 181 and 203-215), so p1 knows the sawed shot is
  // live. On two charges it takes both: 0, at any budget and under either rule
  // set, with no shell left for the answer to average over.
  for (const std::string tube : {"1L2B", "2L2B"}) {
    const std::string text =
        "p1=2/2 p2=2/2 tube=" + tube + " turn=p2 sawed known=p2:0L dealer=seen";
    for (const int reloads : {0, 2}) {
      const SolveResult result = solveText(text, doubleOrNothing(position(text)), dealer(reloads));
      ASSERT_FALSE(result.refused) << result.assumptions;
      EXPECT_NEAR(result.value, 0.0, kTight) << tube << " at " << reloads;
      EXPECT_EQ(result.opponentKnownShells, 0) << tube << " at " << reloads;
    }
    EXPECT_NEAR(solveText(text, RuleConfig::storyRound(2), dealer()).value, 0.0, kTight) << tube;
  }

  // On three charges p1 is left on one with 1L2B and nobody knowing the order.
  // Shooting the dealer: live (1/3) leaves two blanks the dealer works out and
  // fires into itself, and the empty tube scores 1 of 3; blank (2/3) hands the
  // dealer a fair coin on 1L1B, where either aim is worth 1/2 * 1/3, so 1/6.
  // That is 1/9 + 1/9 = 2/9, above shooting itself, 2/3 * 1/6 = 1/9.
  const std::string spare = "p1=3/3 p2=3/3 tube=2L2B turn=p2 sawed known=p2:0L dealer=seen";
  const SolveResult result = solveText(spare, doubleOrNothing(position(spare)), dealer());
  ASSERT_FALSE(result.refused) << result.assumptions;
  EXPECT_NEAR(result.value, 2.0 / 9.0, kTight);
}

// ---------------------------------------------------------------------------
// The node limit
// ---------------------------------------------------------------------------

TEST(ExactValues, TheNodeLimitKeepsEveryValueItReached) {
  const std::string text = "p1=1/1[mg,beer] p2=1/1[beer] tube=2L2B turn=p1";
  const RuleConfig config = doubleOrNothing(position(text));
  const SolveResult unlimited = solveText(text, config, optimal());
  ASSERT_GT(unlimited.nodes, 1);
  EXPECT_FALSE(unlimited.nodeLimitHit);

  SolveOptions atLimit = optimal();
  atLimit.nodeLimit = unlimited.nodes;
  const SolveResult exact = solveText(text, config, atLimit);
  EXPECT_EQ(exact.value, unlimited.value);
  EXPECT_FALSE(exact.nodeLimitHit);
  EXPECT_EQ(exact.nodes, unlimited.nodes);

  SolveOptions short1 = optimal();
  short1.nodeLimit = unlimited.nodes - 1;
  const SolveResult stopped = solveText(text, config, short1);
  EXPECT_TRUE(stopped.nodeLimitHit);
  EXPECT_TRUE(stopped.truncated);
  // Two Beers can empty this tube with both seats alive, so a line does reach
  // the reload budget, limit or no limit. The two flags are kept apart.
  EXPECT_TRUE(unlimited.budgetReached);
  EXPECT_EQ(stopped.budgetReached, unlimited.budgetReached);
  EXPECT_NE(stopped.assumptions.find("The search stopped early; these chances may be off."),
            std::string::npos);

  // Without the Beers every live shell ends the round before the tube runs
  // dry, so a stop at the node limit is the only thing cutting it short.
  const std::string dry = "p1=1/1[mg] p2=1/1 tube=2L2B turn=p1";
  const SolveResult whole = solveText(dry, config, optimal());
  ASSERT_GT(whole.nodes, 1);
  EXPECT_FALSE(whole.truncated);
  SolveOptions short2 = optimal();
  short2.nodeLimit = whole.nodes - 1;
  const SolveResult cut = solveText(dry, config, short2);
  EXPECT_TRUE(cut.nodeLimitHit);
  EXPECT_TRUE(cut.truncated);
  EXPECT_FALSE(cut.budgetReached);
}

TEST(ExactValues, PastTheNodeLimitAPositionReachedTwoWaysIsCountedOnce) {
  // Three seats on one charge each in 1L1B, p1 to move, no reload searched.
  // Each of p1's three shots is live or blank, which makes six outcomes but
  // only five positions: a blank at p2 and a blank at p3 both hand the gun to
  // p2 with the live left. With a limit of one, only the first is searched,
  // p1 shooting itself with the live. Nothing below it counts, since each
  // shot from there empties the tube and is scored at the reload budget. The
  // other four are scored by charges in hand, and the one met twice is
  // counted once.
  const std::string text = "p1=1/1 p2=1/1 p3=1/1 tube=1L1B turn=p1";
  SolveOptions limited = optimal();
  limited.nodeLimit = 1;
  const SolveResult result = solveText(text, RuleConfig::multiplayer(3, 1), limited);
  EXPECT_TRUE(result.nodeLimitHit);
  EXPECT_EQ(result.nodes, 5);
  // Scored by charges in hand, a live at another seat leaves p1 holding half
  // the charges at the table and a blank a third, so either shot at another
  // seat is worth 1/2 * 1/2 + 1/2 * 1/3 = 5/12. Shooting self is worth
  // 1/2 * 0 + 1/2 * 1/3 = 1/6, where the searched live is worth nothing, p1
  // being out. Scoring the position met twice once leaves both of those shots
  // where they were.
  EXPECT_NEAR(result.value, 5.0 / 12.0, kTight);
  EXPECT_NEAR(valueOf(result, "shoot self"), 1.0 / 6.0, kTight);
  EXPECT_NEAR(valueOf(result, "shoot p2"), 5.0 / 12.0, kTight);
  EXPECT_NEAR(valueOf(result, "shoot p3"), 5.0 / 12.0, kTight);
}

// ---------------------------------------------------------------------------
// A dealer-seat blank into itself keeps the saw
// ---------------------------------------------------------------------------

TEST(ExactValues, ASolverSecondSeatKeepsTheSawAfterABlankIntoItself) {
  // p2 sawed on 1L1B, both seats on two charges. Shooting p1: live (1/2)
  // takes both of p1's charges, 0; blank (1/2) clears the saw and hands p1 the
  // live, which leaves p2 on one, and the empty tube scores 2 of 3. So 1/3.
  // Shooting itself: live (1/2) takes both of p2's charges, 1; blank (1/2)
  // keeps the turn and the saw, and the certain live then takes both of p1's,
  // 0. So 1/2, and p2 shoots p1.
  const std::string text = "p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed";
  const SolveResult result = solveText(text, doubleOrNothing(position(text)), optimal());
  EXPECT_NEAR(result.value, 1.0 / 3.0, kTight);
  EXPECT_NEAR(valueOf(result, "shoot p1"), 1.0 / 3.0, kTight);
  EXPECT_NEAR(valueOf(result, "shoot self"), 0.5, kTight);
}

TEST(ExactValues, TheDealerFiresAKnownBlankIntoItselfAndKeepsTheSaw) {
  // Both seats know the chamber is blank. The dealer fires it into itself,
  // keeps the turn and the saw, and the last shell is a sawed live into p1.
  const std::string text = "p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed known=p1:0B known=p2:0B";
  EXPECT_NEAR(solveText(text, doubleOrNothing(position(text)), dealer()).value, 0.0, kTight);
}

}  // namespace
}  // namespace bsr
