/// The scripted dealer, one rule at a time, and then through the solver.
///
/// A pass is checked directly: the branches it returns, their probabilities,
/// the memory each one leaves, and what was spent. The solver values at the end
/// are small enough to work out by hand, and each comment carries the working.
/// Line numbers refer to DealerIntelligence.gd in the pinned decompilation named
/// in engine/Dealer.h.

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "engine/Dealer.h"
#include "engine/Notation.h"
#include "solver/Solver.h"

#if defined(BSR_ADVISOR_PATH) && !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#endif

namespace bsr {
namespace {

using dealer::Brain;
using dealer::Branch;
using dealer::Memory;
using dealer::Reason;
using dealer::Target;

constexpr int kPlayer = 0;
constexpr int kDealer = 1;

GameState parse(const std::string& text) {
  GameState state;
  std::string error;
  EXPECT_TRUE(notation::parse(text, &state, &error)) << error << " in: " << text;
  return state;
}

RuleConfig configFor(Brain brain) {
  return brain == Brain::Story ? RuleConfig::storyRound(2) : RuleConfig::doubleOrNothing(4);
}

std::vector<Branch> pass(const GameState& state, Brain brain, const Memory& memory = Memory{}) {
  std::vector<Branch> branches = dealer::step(state, memory, configFor(brain), brain);
  double total = 0.0;
  for (const Branch& branch : branches) {
    EXPECT_GT(branch.probability, 0.0);
    total += branch.probability;
  }
  EXPECT_NEAR(total, 1.0, 1e-12) << "a pass must not lose probability";
  return branches;
}

double chanceOf(const std::vector<Branch>& branches, const std::function<bool(const Branch&)>& is) {
  double total = 0.0;
  for (const Branch& branch : branches) {
    if (is(branch)) total += branch.probability;
  }
  return total;
}

bool shotAt(const Branch& branch, int seat) {
  return branch.turnOver && branch.action.kind == Action::Kind::Shoot &&
         static_cast<int>(branch.action.target) == seat;
}

bool used(const Branch& branch, Item item) {
  if (branch.turnOver || branch.action.kind != Action::Kind::UseItem) return false;
  return branch.stolen ? branch.action.stolen == item : branch.action.item == item;
}

int held(const GameState& state, int seat, Item item) {
  return state.players[seat].items[itemIndex(item)];
}

SolveOptions againstTheDealer() {
  SolveOptions options;
  options.seat = 0;
  options.reloadBudget = 0;
  options.opponent = OpponentModel::Dealer;
  return options;
}

SolveResult solveAgainstTheDealer(const std::string& position, Brain brain) {
  return solve(parse(position), configFor(brain), againstTheDealer());
}

// ---------------------------------------------------------------------------
// Rules of a single pass
// ---------------------------------------------------------------------------

TEST(Dealer, AKnownLiveChamberIsNeverFiredIntoTheDealer) {
  for (const Brain brain : {Brain::Story, Brain::Endless}) {
    // 2L2B, so neither rule set can work the chamber out, and the glass is the
    // first item in the scan (lines 152-159).
    const std::vector<Branch> first = pass(parse("p1=2/2 p2=2/2[mg] tube=2L2B turn=p2"), brain);
    ASSERT_EQ(first.size(), 2u);
    for (const Branch& looked : first) {
      ASSERT_TRUE(used(looked, Item::MagnifyingGlass));
      EXPECT_NEAR(looked.probability, 0.5, 1e-12);
      EXPECT_EQ(held(looked.state, kDealer, Item::MagnifyingGlass), 0);
      EXPECT_TRUE(looked.state.tube.knows(kDealer, 0));
      EXPECT_FALSE(looked.state.tube.knows(kPlayer, 0)) << "the glass is private";
      EXPECT_TRUE(looked.memory.knows);
      EXPECT_EQ(looked.memory.known, looked.state.tube.truth[0]);
      const Target aim = looked.memory.known == Shell::Live ? Target::Player : Target::Self;
      EXPECT_EQ(looked.memory.target, aim);

      const std::vector<Branch> second = pass(looked.state, brain, looked.memory);
      if (looked.memory.known == Shell::Live) {
        EXPECT_NEAR(chanceOf(second, [](const Branch& b) { return shotAt(b, kPlayer); }), 1.0,
                    1e-12);
        EXPECT_NEAR(chanceOf(second, [](const Branch& b) { return shotAt(b, kDealer); }), 0.0,
                    1e-12);
      } else {
        EXPECT_NEAR(chanceOf(second, [](const Branch& b) { return shotAt(b, kDealer); }), 1.0,
                    1e-12);
      }
      for (const Branch& shot : second) EXPECT_EQ(shot.reason, Reason::KnownTarget);
    }
  }
}

TEST(Dealer, TheStoryCoinIsFair) {
  // Lines 421-431: the story rules ignore the tube, so even 3L1B is an even
  // choice between the two seats.
  for (const char* tube : {"tube=3L1B", "tube=1L3B", "tube=2L2B"}) {
    const std::vector<Branch> branches =
        pass(parse(std::string("p1=2/2 p2=2/2 turn=p2 ") + tube), Brain::Story);
    EXPECT_NEAR(chanceOf(branches, [](const Branch& b) { return shotAt(b, kPlayer); }), 0.5, 1e-12)
        << tube;
    EXPECT_NEAR(chanceOf(branches, [](const Branch& b) { return shotAt(b, kDealer); }), 0.5, 1e-12)
        << tube;
    for (const Branch& shot : branches) {
      EXPECT_EQ(shot.reason, Reason::Coin);
      EXPECT_EQ(shot.memory, Memory{}) << "the memory is discarded after a shot";
    }
  }
}

TEST(Dealer, TheEndlessCoinFollowsTheCounts) {
  const auto atPlayer = [](const Branch& b) { return shotAt(b, kPlayer); };
  const auto atSelf = [](const Branch& b) { return shotAt(b, kDealer); };
  // More live shells: always the player. More blanks: always itself.
  const std::vector<Branch> liveHeavy =
      pass(parse("p1=2/2 p2=2/2 tube=3L1B turn=p2"), Brain::Endless);
  EXPECT_NEAR(chanceOf(liveHeavy, atPlayer), 1.0, 1e-12);
  const std::vector<Branch> blankHeavy =
      pass(parse("p1=2/2 p2=2/2 tube=1L3B turn=p2"), Brain::Endless);
  EXPECT_NEAR(chanceOf(blankHeavy, atSelf), 1.0, 1e-12);
  // A tie is the only fair flip.
  const std::vector<Branch> even = pass(parse("p1=2/2 p2=2/2 tube=2L2B turn=p2"), Brain::Endless);
  EXPECT_NEAR(chanceOf(even, atPlayer), 0.5, 1e-12);
  EXPECT_NEAR(chanceOf(even, atSelf), 0.5, 1e-12);
}

TEST(Dealer, TheSawFallbackIsACoinInTheStoryRules) {
  // Lines 203-215: nothing in the scan applies (the saw needs a known live
  // shell), so the saw is a coin flip. Heads, it saws and aims at the player;
  // tails, it shoots itself with the saw still in hand.
  const std::vector<Branch> branches =
      pass(parse("p1=2/2 p2=2/2[saw] tube=2L2B turn=p2"), Brain::Story);
  EXPECT_NEAR(chanceOf(branches, [](const Branch& b) { return used(b, Item::HandSaw); }), 0.5,
              1e-12);
  EXPECT_NEAR(chanceOf(branches, [](const Branch& b) { return shotAt(b, kDealer); }), 0.5, 1e-12);
  for (const Branch& branch : branches) {
    if (branch.turnOver) {
      EXPECT_EQ(branch.reason, Reason::SawCoinSelf);
      EXPECT_EQ(held(branch.state, kDealer, Item::HandSaw), 1);
      continue;
    }
    EXPECT_EQ(branch.reason, Reason::SawCoin);
    EXPECT_TRUE(branch.state.tube.sawed);
    EXPECT_EQ(held(branch.state, kDealer, Item::HandSaw), 0);
    EXPECT_EQ(branch.memory.target, Target::Player);
    // The next pass fires the sawed barrel at the player.
    const std::vector<Branch> next = pass(branch.state, Brain::Story, branch.memory);
    EXPECT_NEAR(chanceOf(next, [](const Branch& b) { return shotAt(b, kPlayer); }), 1.0, 1e-12);
  }
}

TEST(Dealer, TheEndlessRulesAlwaysSawWhenLiveShellsOutnumberBlanks) {
  const std::vector<Branch> branches =
      pass(parse("p1=2/2 p2=2/2[saw] tube=2L1B turn=p2"), Brain::Endless);
  ASSERT_EQ(branches.size(), 1u);
  EXPECT_TRUE(used(branches.front(), Item::HandSaw));
  EXPECT_TRUE(branches.front().state.tube.sawed);

  // And with more blanks the same coin always says shoot itself.
  const std::vector<Branch> blanks =
      pass(parse("p1=2/2 p2=2/2[saw] tube=1L2B turn=p2"), Brain::Endless);
  EXPECT_NEAR(chanceOf(blanks, [](const Branch& b) { return shotAt(b, kDealer); }), 1.0, 1e-12);
}

TEST(Dealer, TheSawFallbackCanReachThePlayersSawThroughAdrenaline) {
  const std::vector<Branch> branches =
      pass(parse("p1=2/2[saw] p2=2/2[adr] tube=2L1B turn=p2"), Brain::Endless);
  ASSERT_EQ(branches.size(), 1u);
  const Branch& sawed = branches.front();
  EXPECT_TRUE(sawed.stolen);
  EXPECT_TRUE(used(sawed, Item::HandSaw));
  EXPECT_TRUE(sawed.state.tube.sawed);
  EXPECT_EQ(held(sawed.state, kDealer, Item::Adrenaline), 0);
  EXPECT_EQ(held(sawed.state, kPlayer, Item::HandSaw), 0);
}

TEST(Dealer, AStaleTargetSurvivesABeerInBothRuleSets) {
  // Lines 170-176: the glass shows a blank and aims the dealer at itself; the
  // Beer then ejects that blank. The endless rules forget the shell but not the
  // aim, and the story rules forget neither.
  for (const Brain brain : {Brain::Story, Brain::Endless}) {
    const std::vector<Branch> looked =
        pass(parse("p1=2/2 p2=2/2[mg,beer] tube=2L2B turn=p2"), brain);
    const Branch* blank = nullptr;
    for (const Branch& branch : looked) {
      if (branch.memory.known == Shell::Blank) blank = &branch;
    }
    ASSERT_NE(blank, nullptr);
    EXPECT_EQ(blank->memory.target, Target::Self);

    const std::vector<Branch> drank = pass(blank->state, brain, blank->memory);
    ASSERT_EQ(drank.size(), 1u) << "the ejected shell was already known";
    const Branch& beer = drank.front();
    ASSERT_TRUE(used(beer, Item::Beer));
    EXPECT_TRUE(beer.shellFired);
    EXPECT_EQ(beer.shellType, Shell::Blank);
    EXPECT_EQ(beer.state.tube.live, 2);
    EXPECT_EQ(beer.state.tube.blank, 1);
    EXPECT_EQ(beer.state.current, kDealer) << "a Beer does not pass the turn";
    EXPECT_EQ(beer.memory.target, Target::Self);
    if (brain == Brain::Endless) {
      EXPECT_FALSE(beer.memory.knows);
      EXPECT_EQ(beer.memory.known, Shell::Unknown);
    } else {
      EXPECT_TRUE(beer.memory.knows);
      EXPECT_EQ(beer.memory.known, Shell::Blank);
    }

    // 2L1B would send an endless coin at the player, but the stale aim wins.
    const std::vector<Branch> shot = pass(beer.state, brain, beer.memory);
    EXPECT_NEAR(chanceOf(shot, [](const Branch& b) { return shotAt(b, kDealer); }), 1.0, 1e-12);
    for (const Branch& branch : shot) EXPECT_EQ(branch.reason, Reason::KnownTarget);
  }
}

TEST(Dealer, WithOneShellLeftBothRuleSetsKnowIt) {
  // Lines 106-112, on every pass and in both rule sets. The glass is not spent
  // on the last shell.
  for (const Brain brain : {Brain::Story, Brain::Endless}) {
    const std::vector<Branch> live = pass(parse("p1=2/2 p2=2/2[mg] tube=1L0B turn=p2"), brain);
    ASSERT_EQ(live.size(), 1u);
    EXPECT_TRUE(shotAt(live.front(), kPlayer));
    EXPECT_EQ(live.front().reason, Reason::LastShell);
    EXPECT_EQ(held(live.front().state, kDealer, Item::MagnifyingGlass), 1);

    const std::vector<Branch> blank = pass(parse("p1=2/2 p2=2/2 tube=0L1B turn=p2"), brain);
    ASSERT_EQ(blank.size(), 1u);
    EXPECT_TRUE(shotAt(blank.front(), kDealer));
    EXPECT_EQ(blank.front().reason, Reason::LastShell);
  }
}

TEST(Dealer, TheEndlessRulesDeduceTheChamberFromWhatTheDealerHasSeen) {
  const auto atPlayer = [](const Branch& b) { return shotAt(b, kPlayer); };
  const auto atSelf = [](const Branch& b) { return shotAt(b, kDealer); };
  // D1: the dealer has seen the chamber.
  const std::vector<Branch> seen =
      pass(parse("p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:0B"), Brain::Endless);
  EXPECT_NEAR(chanceOf(seen, atSelf), 1.0, 1e-12);
  for (const Branch& branch : seen) EXPECT_EQ(branch.reason, Reason::Deduced);

  // D2: one type left in the tube.
  const std::vector<Branch> oneType =
      pass(parse("p1=2/2 p2=2/2 tube=2L0B turn=p2"), Brain::Endless);
  EXPECT_NEAR(chanceOf(oneType, atPlayer), 1.0, 1e-12);
  for (const Branch& branch : oneType) EXPECT_EQ(branch.reason, Reason::Deduced);

  // D3: the shells it has seen account for every blank, so the chamber is live.
  const std::vector<Branch> blanks =
      pass(parse("p1=2/2 p2=2/2 tube=1L2B turn=p2 known=p2:1B,2B"), Brain::Endless);
  EXPECT_NEAR(chanceOf(blanks, atPlayer), 1.0, 1e-12);
  // ... or every live one, so the chamber is blank.
  const std::vector<Branch> lives =
      pass(parse("p1=2/2 p2=2/2 tube=1L2B turn=p2 known=p2:2L"), Brain::Endless);
  EXPECT_NEAR(chanceOf(lives, atSelf), 1.0, 1e-12);
  for (const Branch& branch : lives) {
    EXPECT_EQ(branch.reason, Reason::Deduced);
    EXPECT_EQ(branch.shellType, Shell::Blank);
  }

  // Shells only the player has seen are not the dealer's to count: 1L2B with
  // nothing the dealer has seen is a blank-heavy coin, which says itself.
  const std::vector<Branch> notItsOwn =
      pass(parse("p1=2/2 p2=2/2 tube=1L2B turn=p2 known=p1:1B,2B"), Brain::Endless);
  EXPECT_NEAR(chanceOf(notItsOwn, atSelf), 1.0, 1e-12);
  for (const Branch& branch : notItsOwn) EXPECT_EQ(branch.reason, Reason::Coin);
}

TEST(Dealer, TheStoryRulesDeduceNothing) {
  // The same three positions under the story rules are fair coins.
  for (const char* position :
       {"p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:0B", "p1=2/2 p2=2/2 tube=2L0B turn=p2",
        "p1=2/2 p2=2/2 tube=1L2B turn=p2 known=p2:1B,2B"}) {
    const std::vector<Branch> branches = pass(parse(position), Brain::Story);
    EXPECT_NEAR(chanceOf(branches, [](const Branch& b) { return shotAt(b, kPlayer); }), 0.5, 1e-12)
        << position;
    for (const Branch& branch : branches) EXPECT_EQ(branch.reason, Reason::Coin) << position;
  }
}

TEST(Dealer, AnUnseenInversionBecomesTheShellItFiresAs) {
  // A written chamber inverted while nobody could see it. 1L2B: the drawn shell
  // is live one time in three and then fires blank, leaving 0L3B, which the
  // endless rules read as all blank; otherwise it fires live, leaving 2L1B, and
  // the coin sends it at the player.
  const std::vector<Branch> branches =
      pass(parse("p1=2/2 p2=2/2 tube=1L2B turn=p2 inverted"), Brain::Endless);
  EXPECT_NEAR(chanceOf(branches, [](const Branch& b) { return shotAt(b, kDealer); }), 1.0 / 3.0,
              1e-12);
  EXPECT_NEAR(chanceOf(branches, [](const Branch& b) { return shotAt(b, kPlayer); }), 2.0 / 3.0,
              1e-12);
  for (const Branch& branch : branches) {
    EXPECT_EQ(branch.shellType, shotAt(branch, kDealer) ? Shell::Blank : Shell::Live);
  }
}

TEST(Dealer, TheInverterWritesALiveShellAndMovesTheCounts) {
  // Lines 195-201. The dealer has seen a blank chamber, so it inverts it and
  // aims at the player.
  GameState state = parse("p1=2/2 p2=2/2[inv] tube=1L2B turn=p2 known=p2:0B");
  Memory memory;
  memory.knows = true;
  memory.known = Shell::Blank;
  memory.target = Target::Self;
  const std::vector<Branch> branches = pass(state, Brain::Endless, memory);
  ASSERT_EQ(branches.size(), 1u);
  const Branch& inverted = branches.front();
  ASSERT_TRUE(used(inverted, Item::Inverter));
  EXPECT_EQ(inverted.state.tube.truth[0], Shell::Live);
  EXPECT_EQ(inverted.state.tube.live, 2);
  EXPECT_EQ(inverted.state.tube.blank, 1);
  EXPECT_FALSE(inverted.state.tube.chamberInverted);
  EXPECT_EQ(inverted.memory.known, Shell::Live);
  EXPECT_EQ(inverted.memory.target, Target::Player);
  EXPECT_EQ(held(inverted.state, kDealer, Item::Inverter), 0);
}

TEST(Dealer, TheInverterFollowsTheScriptNotTheEnginesItem) {
  // The story rules keep a blank in memory after a Beer has ejected it, so the
  // dealer can invert a chamber it has not seen. The script writes a live shell
  // whatever was there. The engine's Inverter would leave an unseen chamber
  // pending instead, which is not what happens here: it is drawn from the pool,
  // seen by the dealer, and live in both branches.
  Memory memory;
  memory.knows = true;
  memory.known = Shell::Blank;
  memory.target = Target::Self;
  const std::vector<Branch> branches =
      pass(parse("p1=2/2 p2=2/2[inv] tube=1L1B turn=p2"), Brain::Story, memory);
  ASSERT_EQ(branches.size(), 2u);
  for (const Branch& branch : branches) {
    ASSERT_TRUE(used(branch, Item::Inverter));
    EXPECT_NEAR(branch.probability, 0.5, 1e-12);
    EXPECT_FALSE(branch.state.tube.chamberInverted);
    EXPECT_EQ(branch.state.tube.truth[0], Shell::Live);
    EXPECT_TRUE(branch.state.tube.knows(kDealer, 0));
    EXPECT_FALSE(branch.state.tube.knows(kPlayer, 0));
  }
  // Drawn live, nothing changes; drawn blank, it becomes the second live.
  int counts[3] = {0, 0, 0};
  for (const Branch& branch : branches) ++counts[branch.state.tube.live];
  EXPECT_EQ(counts[1], 1);
  EXPECT_EQ(counts[2], 1);
}

TEST(Dealer, ThePhonePicksEveryPositionPastTheChamberEvenly) {
  // Lines 187-194. Five shells, so positions 1 to 4 a quarter each. Position 2
  // is already pinned down (the player saw it) and position 3 the dealer has
  // already seen, and both are still in the draw. The engine's own phone would
  // skip position 3, which is why the dealer does not use it.
  const GameState state = parse("p1=2/2 p2=2/2[phone] tube=3L2B turn=p2 known=p1:2L known=p2:3B");
  const std::vector<Branch> branches = pass(state, Brain::Endless);
  double byPosition[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
  double liveAtOne = 0.0;
  for (const Branch& branch : branches) {
    ASSERT_TRUE(used(branch, Item::BurnerPhone));
    EXPECT_EQ(held(branch.state, kDealer, Item::BurnerPhone), 0);
    EXPECT_EQ(branch.memory.knows, false) << "the phone does not change the memory";
    EXPECT_EQ(branch.memory.target, Target::None);
    int named = 3;  // the one position whose record cannot change
    for (int offset = 1; offset < 5; ++offset) {
      if (branch.state.tube.knownBy[offset] != state.tube.knownBy[offset]) named = offset;
    }
    byPosition[named] += branch.probability;
    if (named == 1 && branch.state.tube.truth[1] == Shell::Live) liveAtOne += branch.probability;
    EXPECT_TRUE(branch.state.tube.knows(kDealer, named));
  }
  for (int offset = 1; offset < 5; ++offset) EXPECT_NEAR(byPosition[offset], 0.25, 1e-12);
  // Position 1 is a draw from the two live and one blank nobody has placed.
  EXPECT_NEAR(liveAtOne, 0.25 * 2.0 / 3.0, 1e-12);
}

TEST(Dealer, ThePhoneNeedsMoreThanTwoShells) {
  const std::vector<Branch> branches =
      pass(parse("p1=2/2 p2=2/2[phone] tube=1L1B turn=p2"), Brain::Endless);
  for (const Branch& branch : branches) {
    EXPECT_TRUE(branch.turnOver);
    EXPECT_EQ(held(branch.state, kDealer, Item::BurnerPhone), 1);
  }
}

TEST(Dealer, MedicineIsUsedAtMostOncePerTurn) {
  // Lines 165-169. 2 of 5 charges and two medicines: the first one heals to 4
  // or costs one, and after that the second is left alone even though the
  // dealer is still hurt.
  const std::vector<Branch> first =
      pass(parse("p1=2/2 p2=2/5[med,med] tube=2L2B turn=p2"), Brain::Endless);
  ASSERT_EQ(first.size(), 2u);
  for (const Branch& branch : first) {
    ASSERT_TRUE(used(branch, Item::ExpiredMedicine));
    EXPECT_NEAR(branch.probability, 0.5, 1e-12);
    EXPECT_TRUE(branch.memory.usedMedicine);
    const int hp = branch.state.players[kDealer].hp;
    EXPECT_TRUE(hp == 4 || hp == 1) << hp;
  }
  for (const Branch& branch : first) {
    for (const Branch& next : pass(branch.state, Brain::Endless, branch.memory)) {
      EXPECT_TRUE(next.turnOver);
      EXPECT_EQ(held(next.state, kDealer, Item::ExpiredMedicine), 1);
    }
  }
}

TEST(Dealer, MedicineIsNeverTakenOnOneCharge) {
  const std::vector<Branch> branches =
      pass(parse("p1=2/2 p2=1/4[med] tube=2L2B turn=p2"), Brain::Endless);
  for (const Branch& branch : branches) {
    EXPECT_TRUE(branch.turnOver);
    EXPECT_EQ(held(branch.state, kDealer, Item::ExpiredMedicine), 1);
  }
}

TEST(Dealer, CigarettesWithinReachBlockMedicine) {
  // The dealer's own cigarettes come before medicine in the scan, so they are
  // smoked first, and the medicine follows once they are gone.
  const std::vector<Branch> own =
      pass(parse("p1=2/2 p2=2/4[med,cig] tube=2L2B turn=p2"), Brain::Endless);
  ASSERT_EQ(own.size(), 1u);
  ASSERT_TRUE(used(own.front(), Item::Cigarettes));
  EXPECT_EQ(own.front().state.players[kDealer].hp, 3);
  const std::vector<Branch> then = pass(own.front().state, Brain::Endless, own.front().memory);
  EXPECT_NEAR(chanceOf(then, [](const Branch& b) { return used(b, Item::ExpiredMedicine); }), 1.0,
              1e-12);

  // The player's cigarettes, with Adrenaline to reach them, block the dealer's
  // own medicine even though medicine comes first in the scan: the dealer
  // steals the cigarettes instead.
  const std::vector<Branch> stealable =
      pass(parse("p1=2/2[cig] p2=2/4[med,adr] tube=2L2B turn=p2"), Brain::Endless);
  ASSERT_EQ(stealable.size(), 1u);
  EXPECT_TRUE(stealable.front().stolen);
  EXPECT_TRUE(used(stealable.front(), Item::Cigarettes));
  EXPECT_EQ(held(stealable.front().state, kDealer, Item::ExpiredMedicine), 1);
}

TEST(Dealer, AStolenItemCostsAdrenalineAndThePlayersCopyAndWorksForTheDealer) {
  // Lines 243-256. The stolen glass shows the chamber to the dealer.
  const std::vector<Branch> glass =
      pass(parse("p1=2/2[mg] p2=2/2[adr] tube=2L2B turn=p2"), Brain::Endless);
  ASSERT_EQ(glass.size(), 2u);
  for (const Branch& branch : glass) {
    EXPECT_TRUE(branch.stolen);
    EXPECT_TRUE(used(branch, Item::MagnifyingGlass));
    EXPECT_EQ(branch.action.item, Item::Adrenaline);
    EXPECT_EQ(held(branch.state, kDealer, Item::Adrenaline), 0);
    EXPECT_EQ(held(branch.state, kPlayer, Item::MagnifyingGlass), 0);
    EXPECT_TRUE(branch.state.tube.knows(kDealer, 0));
    EXPECT_FALSE(branch.state.tube.knows(kPlayer, 0));
    EXPECT_TRUE(branch.memory.knows);
  }

  // Stolen handcuffs go on the player, not on the dealer.
  const std::vector<Branch> cuffs =
      pass(parse("p1=2/2[cuff] p2=2/2[adr] tube=2L2B turn=p2"), Brain::Endless);
  ASSERT_EQ(cuffs.size(), 1u);
  EXPECT_TRUE(cuffs.front().stolen);
  EXPECT_TRUE(used(cuffs.front(), Item::Handcuffs));
  EXPECT_TRUE(cuffs.front().state.players[kPlayer].cuffed);
  EXPECT_FALSE(cuffs.front().state.players[kDealer].cuffed);
  EXPECT_EQ(held(cuffs.front().state, kPlayer, Item::Handcuffs), 0);
  EXPECT_EQ(held(cuffs.front().state, kDealer, Item::Adrenaline), 0);

  // Stolen cigarettes heal the dealer.
  const std::vector<Branch> cig =
      pass(parse("p1=1/2[cig] p2=1/2[adr] tube=2L2B turn=p2"), Brain::Endless);
  ASSERT_EQ(cig.size(), 1u);
  EXPECT_EQ(cig.front().state.players[kDealer].hp, 2);
  EXPECT_EQ(cig.front().state.players[kPlayer].hp, 1);
}

TEST(Dealer, HandcuffsOnlyWhenThePlayerCanBeCuffedAndMoreThanOneShellIsLeft) {
  // Lines 177-180.
  const std::vector<Branch> free =
      pass(parse("p1=2/2 p2=2/2[cuff] tube=2L2B turn=p2"), Brain::Endless);
  ASSERT_EQ(free.size(), 1u);
  ASSERT_TRUE(used(free.front(), Item::Handcuffs));
  EXPECT_TRUE(free.front().state.players[kPlayer].cuffed);
  EXPECT_TRUE(free.front().state.cuffUsedThisTurn);
  EXPECT_EQ(held(free.front().state, kDealer, Item::Handcuffs), 0);

  for (const char* position : {"p1=2/2 p2=2/2[cuff] tube=2L2B turn=p2 cuffed=p1",
                               "p1=2/2 p2=2/2[cuff] tube=2L2B turn=p2 skipped=p1",
                               "p1=2/2 p2=2/2[cuff] tube=1L0B turn=p2"}) {
    for (const Branch& branch : pass(parse(position), Brain::Endless)) {
      EXPECT_TRUE(branch.turnOver) << position;
      EXPECT_EQ(held(branch.state, kDealer, Item::Handcuffs), 1) << position;
    }
  }
}

TEST(Dealer, TheItemListFromThePreviousPassStillHoldsThePlayersCigarettes) {
  // Pass 1: the dealer holds Adrenaline, so the player's cigarettes are within
  // reach and block its own medicine; it steals the glass and spends its only
  // Adrenaline. Pass 2: the list it reads was built while it held Adrenaline,
  // so the cigarettes still block the medicine, and it goes on to shoot.
  const GameState start = parse("p1=2/2[mg,cig] p2=2/4[adr,med] tube=2L2B turn=p2");
  const std::vector<Branch> first = pass(start, Brain::Endless);
  ASSERT_EQ(first.size(), 2u);
  for (const Branch& branch : first) {
    ASSERT_TRUE(used(branch, Item::MagnifyingGlass));
    EXPECT_TRUE(branch.stolen);
    EXPECT_EQ(held(branch.state, kDealer, Item::Adrenaline), 0);
    EXPECT_EQ(branch.memory.adrenalineList, dealer::AdrenalineList::True);
    for (const Branch& next : pass(branch.state, Brain::Endless, branch.memory)) {
      EXPECT_TRUE(next.turnOver);
      EXPECT_EQ(next.action.kind, Action::Kind::Shoot);
      EXPECT_EQ(held(next.state, kDealer, Item::ExpiredMedicine), 1);
    }
  }
}

TEST(Dealer, APassNeedsTheDealerToMove) {
  EXPECT_TRUE(dealer::step(parse("p1=2/2 p2=2/2 tube=2L2B turn=p1"), Memory{},
                           RuleConfig::doubleOrNothing(2), Brain::Endless)
                  .empty());
}

TEST(Dealer, EachRuleSetHasItsBrain) {
  Brain brain = Brain::Story;
  EXPECT_TRUE(dealer::brainFor(RuleConfig::doubleOrNothing(4), &brain));
  EXPECT_EQ(brain, Brain::Endless);
  for (int round = 1; round <= 3; ++round) {
    EXPECT_TRUE(dealer::brainFor(RuleConfig::storyRound(round), &brain));
    EXPECT_EQ(brain, Brain::Story);
  }
  EXPECT_FALSE(dealer::brainFor(RuleConfig::multiplayer(3, 4), &brain));
}

// ---------------------------------------------------------------------------
// Through the solver, at a reload budget of zero
// ---------------------------------------------------------------------------

TEST(DealerSolve, AnEvenTubeIsAnEvenRoundInBothRuleSets) {
  // 1L1B, no items. Shoot itself (1/2): live kills it; blank keeps the gun on
  // 1L0B, which it knows is live and fires at p1. 1/2. Shoot p1 (1/2): live
  // kills p1; blank hands p1 a certain live. 1/2. The endless coin is fair on a
  // tie, so both rule sets give 1/2.
  for (const Brain brain : {Brain::Story, Brain::Endless}) {
    const SolveResult result = solveAgainstTheDealer("p1=1/1 p2=1/1 tube=1L1B turn=p2", brain);
    EXPECT_NEAR(result.value, 0.5, 1e-12);
    EXPECT_TRUE(result.ranked.empty());
    EXPECT_EQ(result.mover, kDealer);
  }
}

TEST(DealerSolve, TwoLiveOneBlankFavoursTheEndlessDealer) {
  // Endless: more live, so it shoots p1. Live (2/3) kills p1; blank (1/3) hands
  // p1 2L0B and p1 wins. 1/3.
  EXPECT_NEAR(solveAgainstTheDealer("p1=1/1 p2=1/1 tube=2L1B turn=p2", Brain::Endless).value,
              1.0 / 3.0, 1e-12);
}

TEST(DealerSolve, TwoLiveOneBlankUnderTheStoryCoin) {
  // Story: shoot p1 (1/2) is 1/3 as above. Shoot itself (1/2): live (2/3) kills
  // it; blank (1/3) starts a fresh turn on 2L0B with a fair coin, 1/2. So
  // 2/3 + 1/3 * 1/2 = 5/6, and the value is 1/2 * 1/3 + 1/2 * 5/6 = 7/12.
  EXPECT_NEAR(solveAgainstTheDealer("p1=1/1 p2=1/1 tube=2L1B turn=p2", Brain::Story).value,
              7.0 / 12.0, 1e-12);
}

TEST(DealerSolve, PinnedBlanksAreADeductionOnlyForTheEndlessRules) {
  // Both blanks written for both seats, so the dealer's memory is exactly
  // those two. Endless: the chamber must be the live one, and it goes at p1. 0.
  // Story: no deduction, a fair coin over a certain live. 1/2.
  const std::string position = "p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p1:1B,2B known=p2:1B,2B";
  EXPECT_NEAR(solveAgainstTheDealer(position, Brain::Endless).value, 0.0, 1e-12);
  EXPECT_NEAR(solveAgainstTheDealer(position, Brain::Story).value, 0.5, 1e-12);
}

TEST(DealerSolve, ACuffedPlayerAtTheRootHandsTheDealerItsTurn) {
  // p1 is skipped before anything is asked, and the rest is the endless 2L1B
  // position: 1/3, with no moves listed.
  const SolveResult result =
      solveAgainstTheDealer("p1=1/1 p2=1/1 tube=2L1B turn=p1 cuffed=p1", Brain::Endless);
  EXPECT_NEAR(result.value, 1.0 / 3.0, 1e-12);
  EXPECT_TRUE(result.ranked.empty());
  EXPECT_EQ(result.mover, kDealer);
}

TEST(DealerSolve, TheDealerDoesNotCuffAPlayerWhoseTurnWasJustSkipped) {
  // The same position with handcuffs in the dealer's hand. p1 has just lost a
  // turn, so the cuffs stay on the table and the value is still 1/3. Cuffing
  // p1 again would make it 0: the blank that hands p1 the gun would hand it
  // straight back.
  const SolveResult result =
      solveAgainstTheDealer("p1=1/1 p2=1/1[cuff] tube=2L1B turn=p1 cuffed=p1", Brain::Endless);
  EXPECT_NEAR(result.value, 1.0 / 3.0, 1e-12);
  EXPECT_TRUE(result.ranked.empty());
}

TEST(DealerSolve, AShellOnlyTheDealerHasSeenIsRedrawnFromThePlayersView) {
  // p1 has not seen offset 1, so it is blank 2/3 of the time and live 1/3, and
  // the dealer remembers it either way. Blank: 1L1B left elsewhere, no
  // deduction, a blank-heavy coin says itself; live (1/2) kills it, blank
  // (1/2) leads to a seen blank, another self shot, and the last live at p1.
  // 1/2. Live: every live is accounted for, so the chamber is blank, it shoots
  // itself, and the next turn knows the live one. 0. So 2/3 * 1/2 = 1/3.
  const SolveResult result =
      solveAgainstTheDealer("p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p2:1B", Brain::Endless);
  EXPECT_NEAR(result.value, 1.0 / 3.0, 1e-12);
  EXPECT_EQ(result.opponentKnownShells, 1);
  EXPECT_FALSE(result.opponentKnowledgeDropped);
  // The written type is only a consistency check.
  EXPECT_NEAR(
      solveAgainstTheDealer("p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p2:1L", Brain::Endless).value,
      1.0 / 3.0, 1e-12);
}

TEST(DealerSolve, EveryShotClearsTheSawEvenABlankIntoItself) {
  // 1L2B sawed, both on two charges. The blank-heavy coin says itself. Live
  // (1/3) takes both its charges: 1. Blank (2/3): the saw clears, and a fresh
  // turn on 1L1B is a fair coin. Itself: live leaves it on one and hands p1 a
  // blank, then the boundary scores 2 of 3; blank keeps the gun, and the last
  // live puts p1 on one, 1 of 3; so 1/2. p1: live puts p1 on one with a blank
  // to come, 1/3; blank hands p1 the live, 2/3; so 1/2. 1/3 + 2/3 * 1/2 = 2/3.
  const SolveResult result =
      solveAgainstTheDealer("p1=2/2 p2=2/2 tube=1L2B turn=p2 sawed", Brain::Endless);
  EXPECT_NEAR(result.value, 2.0 / 3.0, 1e-12);
  EXPECT_TRUE(result.truncated) << "the empty tube is scored by charges in hand";
}

TEST(DealerSolve, TheAdvisedSeatStillChoosesItsOwnMoves) {
  // p1 to move on 1L1B: either shot is worth 1/2 against this dealer, as
  // against any seat that must fire a certain shell the way it is told.
  const SolveResult result =
      solveAgainstTheDealer("p1=1/1 p2=1/1 tube=1L1B turn=p1", Brain::Endless);
  ASSERT_EQ(result.ranked.size(), 2u);
  EXPECT_NEAR(result.value, 0.5, 1e-12);
  EXPECT_NEAR(result.ranked.front().value, 0.5, 1e-12);
  EXPECT_EQ(result.mover, kPlayer);
}

TEST(DealerSolve, TheAnswerNamesTheDealerAndItsApproximations) {
  const SolveResult result = solveAgainstTheDealer("p1=1/1 p2=1/1 tube=1L1B turn=p2", Brain::Story);
  EXPECT_NE(result.assumptions.find("dealer script"), std::string::npos);
  EXPECT_NE(result.assumptions.find("story rules"), std::string::npos);
  EXPECT_NE(result.assumptions.find("fixed order"), std::string::npos);
  EXPECT_NE(result.assumptions.find("clears a sawed barrel"), std::string::npos);
  EXPECT_NE(result.assumptions.find("Four approximations"), std::string::npos);
  // A root with the dealer to move is the start of its turn, and the answer
  // has to say so: a position part-way through one loses what it decided.
  EXPECT_NE(result.assumptions.find("start of its turn"), std::string::npos);
  EXPECT_EQ(result.assumptions.find("Expired Medicine"), std::string::npos)
      << "the medicine departure cannot arise at the heal floors the modes use";
  const SolveResult endless =
      solveAgainstTheDealer("p1=1/1 p2=1/1 tube=1L1B turn=p2", Brain::Endless);
  EXPECT_NE(endless.assumptions.find("endless rules"), std::string::npos);
}

TEST(DealerSolve, AHealFloorAboveTwoAddsTheMedicineDeparture) {
  RuleConfig config = RuleConfig::doubleOrNothing(4);
  config.healFloor = 3;
  const SolveResult result =
      solve(parse("p1=4/4 p2=4/4 tube=1L1B turn=p2"), config, againstTheDealer());
  EXPECT_NE(result.assumptions.find("Five approximations"), std::string::npos)
      << result.assumptions;
  EXPECT_NE(result.assumptions.find("Expired Medicine always costs it a charge"), std::string::npos)
      << result.assumptions;
}

TEST(DealerSolve, TheStoryDealerIgnoresWhatItSawBeforeItsTurn) {
  // Only the endless rules deduce from earlier sightings, so the story answer
  // is the same with or without them, and the note has to say so rather than
  // suggest the dealer plays on them.
  const SolveResult seen =
      solveAgainstTheDealer("p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:0L", Brain::Story);
  const SolveResult unseen = solveAgainstTheDealer("p1=2/2 p2=2/2 tube=2L2B turn=p2", Brain::Story);
  EXPECT_NEAR(seen.value, unseen.value, 1e-12);
  EXPECT_NE(seen.assumptions.find("does not change the answer"), std::string::npos)
      << seen.assumptions;
  EXPECT_EQ(seen.assumptions.find("remembers which"), std::string::npos);

  const SolveResult endless =
      solveAgainstTheDealer("p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:0L", Brain::Endless);
  EXPECT_NE(endless.assumptions.find("remembers which"), std::string::npos) << endless.assumptions;
}

// ---------------------------------------------------------------------------
// Where the dealer can be the opponent
// ---------------------------------------------------------------------------

TEST(DealerSolve, OnlyATwoSeatSingleplayerTableAdvisingP1IsAccepted) {
  const GameState two = parse("p1=2/2 p2=2/2 tube=2L2B turn=p1");
  const GameState three = parse("p1=2/2 p2=2/2 p3=2/2 tube=2L2B turn=p1");
  SolveOptions options = againstTheDealer();
  std::string reason;
  EXPECT_TRUE(dealerSupported(two, RuleConfig::doubleOrNothing(2), options, &reason));
  EXPECT_TRUE(dealerSupported(two, RuleConfig::storyRound(3), options, &reason));

  EXPECT_FALSE(dealerSupported(three, RuleConfig::doubleOrNothing(2), options, &reason));
  EXPECT_NE(reason.find("two seats"), std::string::npos) << reason;
  EXPECT_FALSE(dealerSupported(two, RuleConfig::multiplayer(2, 2), options, &reason));
  EXPECT_NE(reason.find("multiplayer"), std::string::npos) << reason;
  options.seat = 1;
  EXPECT_FALSE(dealerSupported(two, RuleConfig::doubleOrNothing(2), options, &reason));
  EXPECT_NE(reason.find("p1"), std::string::npos) << reason;

  // solve() refuses rather than answering some other question.
  const SolveResult refused = solve(two, RuleConfig::doubleOrNothing(2), options);
  EXPECT_TRUE(refused.refused);
  EXPECT_TRUE(refused.ranked.empty());
  EXPECT_EQ(refused.nodes, 0);
}

#if defined(BSR_ADVISOR_PATH) && !defined(_WIN32)

struct AdvisorRun {
  int code = -1;
  std::string output;
  std::string errors;
};

std::string slurp(const std::string& path) {
  std::ifstream in(path);
  std::string text;
  std::string line;
  while (std::getline(in, line)) text += line + "\n";
  return text;
}

/// Run the advisor with `arguments`, feeding it `input` as a narrated session
/// when there is any, and collect its exit code and both of its streams. ctest
/// runs every test in a process of its own, possibly side by side, so the files
/// carry the process id.
AdvisorRun advisor(const std::string& arguments, const std::string& input = "") {
  const std::string stem =
      ::testing::TempDir() + "bsr_dealer_advisor_" + std::to_string(getpid()) + "_";
  const std::string inFile = stem + "in.txt";
  const std::string outFile = stem + "out.txt";
  const std::string errorFile = stem + "err.txt";
  {
    std::ofstream in(inFile);
    in << input;
  }
  const std::string command = std::string("'") + BSR_ADVISOR_PATH + "' " + arguments + " <'" +
                              inFile + "' >'" + outFile + "' 2>'" + errorFile + "'";
  const int status = std::system(command.c_str());
  AdvisorRun run;
  if (status != -1 && WIFEXITED(status)) run.code = WEXITSTATUS(status);
  run.output = slurp(outFile);
  run.errors = slurp(errorFile);
  std::remove(inFile.c_str());
  std::remove(outFile.c_str());
  std::remove(errorFile.c_str());
  return run;
}

/// The chance printed by the answer that starts at `from` in a session's
/// output, as text.
std::string chanceAfter(const std::string& output, std::size_t from) {
  const std::string label = "Your chance of being the last player standing: ";
  const std::size_t at = output.find(label, from);
  if (at == std::string::npos) return "";
  return output.substr(at + label.size(), 6);
}

int lineCount(const std::string& text) {
  int lines = 0;
  for (char c : text) {
    if (c == '\n') ++lines;
  }
  return lines;
}

TEST(DealerAdvisor, TheOpponentFlagIsValidatedLikeEveryOtherFlag) {
  const std::string position = "--position 'p1=1/1 p2=1/1 tube=2L1B turn=p2' --reloads 0 --json";
  EXPECT_EQ(advisor(position + " --opponent dealer").code, 0);
  EXPECT_EQ(advisor(position + " --opponent solver").code, 0);
  EXPECT_EQ(advisor(position + " --opponent optimal").code, 0);
  EXPECT_EQ(advisor(position + " --opponent dealer --mode story").code, 0);
  EXPECT_EQ(advisor(position + " --opponent dealer --mode story1").code, 0);
  EXPECT_EQ(advisor(position + " --opponent dealer --mode story3").code, 0);

  const AdvisorRun unknown = advisor(position + " --opponent minimax");
  EXPECT_EQ(unknown.code, 2);
  EXPECT_NE(unknown.errors.find("--opponent takes solver or dealer, not minimax"),
            std::string::npos)
      << unknown.errors;
}

TEST(DealerAdvisor, SolverAndItsOlderNameOptimalGiveTheSameAnswer) {
  const std::string position =
      "--position 'p1=2/2[saw] p2=2/2[mg] tube=2L2B turn=p1' --reloads 0 --json";
  const AdvisorRun plain = advisor(position);
  const AdvisorRun solver = advisor(position + " --opponent solver");
  const AdvisorRun optimal = advisor(position + " --opponent optimal");
  ASSERT_EQ(plain.code, 0) << plain.errors;
  ASSERT_EQ(solver.code, 0) << solver.errors;
  ASSERT_EQ(optimal.code, 0) << optimal.errors;
  EXPECT_NE(solver.output.find("\"opponent\": \"solver\""), std::string::npos) << solver.output;
  EXPECT_EQ(solver.output, plain.output);
  EXPECT_EQ(optimal.output, solver.output);

  // The interactive command takes both names too, and reports the new one.
  const AdvisorRun session =
      advisor("--opponent dealer",
              "opponent optimal\nopponent\nopponent dealer\nopponent solver\n"
              "opponent\n");
  EXPECT_EQ(session.output.find("opponent takes"), std::string::npos) << session.output;
  std::size_t reports = 0;
  for (std::size_t at = session.output.find("p2 plays to minimise your chance (solver).");
       at != std::string::npos;
       at = session.output.find("p2 plays to minimise your chance (solver).", at + 1)) {
    ++reports;
  }
  EXPECT_EQ(reports, 2u) << session.output;
}

TEST(DealerAdvisor, ATableTheDealerDoesNotPlayIsRefusedWithOneLine) {
  const std::string twoSeats = "--position 'p1=1/1 p2=1/1 tube=2L1B turn=p2' --reloads 0";
  const std::string threeSeats = "--position 'p1=1/1 p2=1/1 p3=1/1 tube=2L1B turn=p2' --reloads 0";
  for (const std::string& arguments :
       {threeSeats + " --opponent dealer", twoSeats + " --opponent dealer --mode mp",
        twoSeats + " --opponent dealer --mode multiplayer",
        twoSeats + " --opponent dealer --seat 2"}) {
    const AdvisorRun run = advisor(arguments);
    EXPECT_EQ(run.code, 1) << arguments;
    EXPECT_EQ(lineCount(run.errors), 1) << arguments << "\n" << run.errors;
  }
  // The same tables are fine for the minimising opponent.
  EXPECT_EQ(advisor(twoSeats + " --seat 2 --json").code, 0);
}

TEST(DealerAdvisor, TheJsonAnswerListsNoMovesWhenTheDealerIsToMove) {
  const AdvisorRun run = advisor(
      "--position 'p1=1/1 p2=1/1 tube=2L1B turn=p1 cuffed=p1' --reloads 0 --json "
      "--opponent dealer");
  ASSERT_EQ(run.code, 0);
  const std::string& line = run.output;
  EXPECT_NE(line.find("\"value\": 0.333333333333"), std::string::npos) << line;
  EXPECT_NE(line.find("\"actions\": []"), std::string::npos) << line;
  // What tells this apart from a position with no move at all.
  EXPECT_NE(line.find("\"mover\": \"p2\", \"opponent\": \"dealer\", \"refused\": false"),
            std::string::npos)
      << line;

  const AdvisorRun optimal =
      advisor("--position 'p1=1/1 p2=1/1 tube=2L1B turn=p1' --reloads 0 --json");
  EXPECT_NE(optimal.output.find("\"mover\": \"p1\", \"opponent\": \"solver\""), std::string::npos)
      << optimal.output;
  const AdvisorRun paranoid =
      advisor("--position 'p1=1/1 p2=1/1 p3=1/1 tube=2L1B turn=p1' --reloads 0 --json --mode mp");
  EXPECT_NE(paranoid.output.find("\"opponent\": \"paranoid\""), std::string::npos)
      << paranoid.output;
}

TEST(DealerAdvisor, AFinishedRoundOrAnEmptyTubeIsNotTheDealersMove) {
  const AdvisorRun over =
      advisor("--position 'p1=0/2 p2=1/2 tube=1L1B turn=p2' --reloads 0 --opponent dealer");
  EXPECT_EQ(over.code, 0);
  EXPECT_NE(over.output.find("The round is over: p2 is the last player standing."),
            std::string::npos)
      << over.output;
  EXPECT_EQ(over.output.find("plays by its script"), std::string::npos) << over.output;
  EXPECT_EQ(chanceAfter(over.output, 0), "0.0000");

  // After a reload p1 acts first, whoever held the gun when the tube ran dry.
  for (const std::string turn : {"p1", "p2"}) {
    const AdvisorRun empty = advisor("--position 'p1=1/2 p2=1/2 tube=0L0B turn=" + turn +
                                     "' --reloads 0 --opponent dealer");
    EXPECT_EQ(empty.code, 0);
    EXPECT_NE(empty.output.find("The tube is empty and a reload is due (after a reload p1 acts "
                                "first)"),
              std::string::npos)
        << empty.output;
    EXPECT_EQ(empty.output.find("plays by its script"), std::string::npos) << empty.output;
    EXPECT_EQ(empty.output.find("No legal move"), std::string::npos) << empty.output;
    EXPECT_EQ(chanceAfter(empty.output, 0), "0.5000");
    EXPECT_NE(empty.output.find("1 state examined."), std::string::npos) << empty.output;
  }
}

TEST(DealerAdvisor, APositionPartWayThroughADealerTurnSaysSo) {
  const AdvisorRun start =
      advisor("--position 'p1=2/2 p2=2/2 tube=1L2B turn=p2' --reloads 0 --opponent dealer");
  EXPECT_NE(start.output.find("Its turn is taken to start here."), std::string::npos)
      << start.output;
  // Only the dealer's own turn leaves a sawed barrel or a cuffed p1 behind it.
  for (const std::string extra : {" sawed", " cuffed=p1"}) {
    const AdvisorRun middle = advisor("--position 'p1=2/2 p2=2/2 tube=1L2B turn=p2" + extra +
                                      "' --reloads 0 --opponent dealer");
    EXPECT_NE(middle.output.find("looks like the middle of a dealer turn"), std::string::npos)
        << extra << "\n"
        << middle.output;
  }
}

TEST(DealerAdvisor, ANarratedDealerItemMarksTheTurnAsUnderWayUntilItShoots) {
  const AdvisorRun run = advisor("--reloads 0 --opponent dealer",
                                 "set p1=2/4 p2=3/4[cuff] tube=2L2B turn=p2\n"
                                 "advise\n"
                                 "use cuff p1\n"
                                 "advise\n"
                                 "shot p1 blank\n"
                                 "advise\n");
  ASSERT_EQ(run.code, 0);
  const std::string note = "part-way through its turn";
  const std::size_t first = run.output.find("Advising seat");
  const std::size_t second = run.output.find("Advising seat", first + 1);
  const std::size_t third = run.output.find("Advising seat", second + 1);
  ASSERT_NE(third, std::string::npos) << run.output;
  const std::size_t found = run.output.find(note);
  EXPECT_GT(found, second) << run.output;
  EXPECT_LT(found, third) << "only the answer between the cuff and the shot carries it\n"
                          << run.output;
  EXPECT_EQ(run.output.find(note, third), std::string::npos) << run.output;
}

TEST(DealerAdvisor, TheDealersGlassCanBeRecordedUnseenAndTheAnswerIsTheSame) {
  const std::string position = "set p1=2/4[saw] p2=3/4[mg,beer,cuff] tube=2L2B turn=p2\n";
  std::vector<std::string> chances;
  for (const std::string shown : {"live", "blank", "unseen"}) {
    std::string input = position;
    input += "mg " + shown + "\nadvise\nshot self blank\nstate\n";
    const AdvisorRun run = advisor("--reloads 0 --opponent dealer", input);
    ASSERT_EQ(run.code, 0);
    chances.push_back(chanceAfter(run.output, 0));
    // A type only the dealer saw gives way to the shell that actually came out.
    EXPECT_NE(run.output.find("p1=2/4[saw] p2=3/4[beer,cuff] tube=2L1B turn=p2"), std::string::npos)
        << shown << "\n"
        << run.output;
    EXPECT_EQ(run.output.find("contradicts"), std::string::npos) << shown << "\n" << run.output;
    if (shown == "unseen") {
      EXPECT_NE(run.output.find("knows: shell 1 (unseen by you)"), std::string::npos) << run.output;
      EXPECT_EQ(run.output.find("knows: shell 1 is"), std::string::npos) << run.output;
    }
  }
  EXPECT_EQ(chances[0], "0.3056");
  EXPECT_EQ(chances[1], chances[0]);
  EXPECT_EQ(chances[2], chances[0]);

  const AdvisorRun refused = advisor("--opponent dealer", position + "use mg\n");
  EXPECT_NE(refused.output.find("The dealer's glass is private: type mg unseen"), std::string::npos)
      << refused.output;
  const AdvisorRun own = advisor("", "set p1=2/4[mg] p2=3/4 tube=2L2B turn=p1\nmg unseen\n");
  EXPECT_NE(own.output.find("You saw what your own glass showed"), std::string::npos) << own.output;
}

TEST(DealerAdvisor, AnUnseenTypeNeverMovesTheCounts) {
  // The dealer's glass and then its Inverter on a chamber p1 never saw. The
  // record cannot flip a type it only guessed without moving the counts by the
  // guess, so the chamber goes back to the pool with the flip pending, and the
  // shot then settles it: a blank turned live leaves 2L1B.
  const AdvisorRun inverted = advisor("--reloads 0",
                                      "set p1=2/4 p2=3/4[mg,inv] tube=2L2B turn=p2\n"
                                      "mg unseen\nuse inv\nstate\nshot p1 live\nstate\n");
  EXPECT_NE(inverted.output.find("p1=2/4 p2=3/4 tube=2L2B turn=p2 inverted\n"), std::string::npos)
      << inverted.output;
  EXPECT_NE(inverted.output.find("p1=1/4 p2=3/4 tube=2L1B turn=p1\n"), std::string::npos)
      << inverted.output;

  // A phone's shell pinned to the only live shell has to give way when that
  // live shell turns up in the chamber first.
  const AdvisorRun phoned = advisor("--reloads 0",
                                    "set p1=2/4 p2=3/4[phone] tube=1L2B turn=p2\n"
                                    "phone 3 unseen\nshot p1 live\nstate\n");
  EXPECT_NE(phoned.output.find("p1=1/4 p2=3/4 tube=0L2B turn=p1 known=p2:1B\n"), std::string::npos)
      << phoned.output;
  EXPECT_EQ(phoned.output.find("contradicts"), std::string::npos) << phoned.output;

  // A type the advised seat saw itself still stands.
  const AdvisorRun seen = advisor("",
                                  "set p1=2/4[mg] p2=3/4 tube=1L2B turn=p1\nmg live\n"
                                  "shot p2 blank\n");
  EXPECT_NE(seen.output.find("That contradicts the chamber"), std::string::npos) << seen.output;
}

TEST(DealerAdvisor, TheOpponentCommandSaysWhichModelIsInForce) {
  const AdvisorRun run =
      advisor("", "opponent\nopponent minimax\nopponent dealer\nopponent\nrules\n");
  EXPECT_NE(run.output.find("p2 plays to minimise your chance (solver). Type opponent dealer"),
            std::string::npos)
      << run.output;
  EXPECT_NE(run.output.find("opponent takes solver or dealer, not minimax"), std::string::npos)
      << run.output;
  EXPECT_NE(run.output.find("p2 plays by the game's dealer script (dealer). Type opponent solver"),
            std::string::npos)
      << run.output;
  EXPECT_NE(run.output.find("opponent: dealer script"), std::string::npos) << run.output;
  EXPECT_EQ(run.output.find("I do not know the command"), std::string::npos) << run.output;
}

TEST(DealerAdvisor, ItemsOutsideThePoolAreStillUsedAndTheWarningSaysSo) {
  // Story stage 2 never deals Adrenaline or an Inverter, but a seat holding one
  // still uses it, the dealer's script included.
  const AdvisorRun run = advisor(
      "--position 'p1=2/4 p2=2/4[adr,inv] tube=2L1B turn=p2' --mode story2 --reloads 0 "
      "--opponent dealer");
  EXPECT_EQ(run.code, 0);
  EXPECT_NE(run.errors.find("never deal Adrenaline, Inverter, so no reload brings one, but a seat "
                            "already holding one still uses it"),
            std::string::npos)
      << run.errors;
  EXPECT_EQ(run.errors.find("no move that uses it"), std::string::npos) << run.errors;
}

TEST(DealerAdvisor, HelpNamesEveryModeAndEveryFlag) {
  const AdvisorRun run = advisor("--help");
  EXPECT_EQ(run.code, 0);
  EXPECT_NE(run.output.find("[--mode don|story1|story2|story3|mp]"), std::string::npos)
      << run.output;
  for (const std::string flag : {"--seat", "--reloads", "--opponent", "--position", "--json"}) {
    EXPECT_NE(run.output.find(flag + " "), std::string::npos) << flag;
  }
  EXPECT_NE(run.output.find("mg live|blank|unseen"), std::string::npos) << run.output;
}

#endif

}  // namespace
}  // namespace bsr
