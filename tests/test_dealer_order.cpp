/// The scripted dealer on ordered hands: which copy its scan reaches first,
/// which copy a use or a steal removes, the item list it keeps from the
/// previous pass, and a pass that starts part-way through its turn with a given
/// memory. Line numbers refer to DealerIntelligence.gd in the pinned
/// decompilation named in engine/Dealer.h.

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

#include "engine/Dealer.h"
#include "engine/Notation.h"

namespace bsr {
namespace {

using dealer::Brain;
using dealer::Branch;
using dealer::Memory;
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

bool stole(const Branch& branch, Item item) {
  return branch.stolen && used(branch, item);
}

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

bool startsWith(const std::string& text, const std::string& prefix) {
  return text.rfind(prefix, 0) == 0;
}

Memory believesBlank() {
  Memory memory;
  memory.knows = true;
  memory.known = Shell::Blank;
  memory.target = Target::Self;
  return memory;
}

Memory aimsAtItself() {
  Memory memory;
  memory.target = Target::Self;
  return memory;
}

Memory tookMedicine() {
  Memory memory;
  memory.usedMedicine = true;
  return memory;
}

// ---------------------------------------------------------------------------
// The scan walks the hand in the order it was dealt
// ---------------------------------------------------------------------------

TEST(DealerOrder, TheScanTakesTheFirstUsableItemInHandOrder) {
  // With one live in three the dealer wants both the Beer and the glass; the
  // scan stops at whichever comes first in its hand (lines 118-149).
  const std::vector<Branch> beerFirst =
      pass(parse("p1=2/2 p2=2/2[beer,mg] tube=1L2B turn=p2"), Brain::Endless);
  ASSERT_FALSE(beerFirst.empty());
  for (const Branch& branch : beerFirst) EXPECT_TRUE(used(branch, Item::Beer));

  const std::vector<Branch> glassFirst =
      pass(parse("p1=2/2 p2=2/2[mg,beer] tube=1L2B turn=p2"), Brain::Endless);
  ASSERT_FALSE(glassFirst.empty());
  for (const Branch& branch : glassFirst) EXPECT_TRUE(used(branch, Item::MagnifyingGlass));
}

TEST(DealerOrder, AdrenalineScansThePlayersHandInItsOrder) {
  const std::vector<Branch> branches =
      pass(parse("p1=2/2[beer,mg] p2=2/2[adr] tube=1L2B turn=p2"), Brain::Endless);
  ASSERT_FALSE(branches.empty());
  for (const Branch& branch : branches) {
    EXPECT_TRUE(stole(branch, Item::Beer));
    EXPECT_TRUE(startsWith(notation::print(branch.state), "p1=2/2[mg] p2=2/2 "))
        << notation::print(branch.state);
  }
}

TEST(DealerOrder, AUseRemovesTheFirstCopy) {
  const std::vector<Branch> branches =
      pass(parse("p1=2/2 p2=2/2[cuff,beer,cuff] tube=1L2B turn=p2"), Brain::Endless);
  ASSERT_FALSE(branches.empty());
  for (const Branch& branch : branches) {
    const std::string text = notation::print(branch.state);
    EXPECT_TRUE(used(branch, Item::Handcuffs));
    EXPECT_TRUE(contains(text, "p2=2/2[beer,cuff] ")) << text;
    EXPECT_TRUE(contains(text, "cuffed=p1")) << text;
  }
}

TEST(DealerOrder, AStealRemovesTheFirstCopy) {
  const std::vector<Branch> branches =
      pass(parse("p1=2/2[beer,mg,beer] p2=2/2[adr] tube=1L2B turn=p2"), Brain::Endless);
  ASSERT_FALSE(branches.empty());
  for (const Branch& branch : branches) {
    EXPECT_TRUE(stole(branch, Item::Beer));
    EXPECT_TRUE(startsWith(notation::print(branch.state), "p1=2/2[mg,beer] "))
        << notation::print(branch.state);
  }
}

// ---------------------------------------------------------------------------
// The item list kept from the previous pass
// ---------------------------------------------------------------------------

TEST(DealerOrder, AStaleListWithCigarettesBlocksMedicine) {
  // The list still names the player's Cigarettes, so the medicine check fails
  // and the pass goes to the coin (lines 196-203, 421-431).
  const std::vector<Branch> stale =
      pass(parse("p1=2/3[cig] p2=2/3[med] tube=2L2B turn=p2 listcigs"), Brain::Endless);
  EXPECT_NEAR(chanceOf(stale, [](const Branch& b) { return used(b, Item::ExpiredMedicine); }), 0.0,
              1e-12);
  EXPECT_NEAR(chanceOf(stale, [](const Branch& b) { return shotAt(b, kDealer); }), 0.5, 1e-12);
  EXPECT_NEAR(chanceOf(stale, [](const Branch& b) { return shotAt(b, kPlayer); }), 0.5, 1e-12);
  for (const int seat : {kPlayer, kDealer}) {
    for (const Shell shell : {Shell::Live, Shell::Blank}) {
      EXPECT_NEAR(chanceOf(stale,
                           [&](const Branch& b) {
                             return shotAt(b, seat) && b.shellFired && b.shellType == shell;
                           }),
                  0.25, 1e-12);
    }
  }

  // Without the stale list the dealer takes the medicine.
  const std::vector<Branch> fresh =
      pass(parse("p1=2/3[cig] p2=2/3[med] tube=2L2B turn=p2"), Brain::Endless);
  ASSERT_EQ(fresh.size(), 2u);
  EXPECT_NEAR(chanceOf(fresh,
                       [](const Branch& b) {
                         return used(b, Item::ExpiredMedicine) &&
                                startsWith(notation::print(b.state), "p1=2/3[cig] p2=3/3 ");
                       }),
              0.5, 1e-12);
  EXPECT_NEAR(chanceOf(fresh,
                       [](const Branch& b) {
                         return used(b, Item::ExpiredMedicine) &&
                                startsWith(notation::print(b.state), "p1=2/3[cig] p2=1/3 ");
                       }),
              0.5, 1e-12);
}

TEST(DealerOrder, APassThatSeesThePlayersCigarettesLeavesTheList) {
  // Adrenaline puts the player's hand in the scan, so the list now names the
  // Cigarettes, and the glass is the first usable item (lines 118-149).
  const std::vector<Branch> looked =
      pass(parse("p1=2/3[cig] p2=2/3[adr,mg] tube=2L2B turn=p2"), Brain::Endless);
  ASSERT_FALSE(looked.empty());
  for (const Branch& branch : looked) {
    EXPECT_TRUE(used(branch, Item::MagnifyingGlass));
    EXPECT_FALSE(branch.stolen);
    EXPECT_TRUE(branch.state.dealerListCigs);
    EXPECT_TRUE(contains(notation::print(branch.state), " listcigs"))
        << notation::print(branch.state);

    // The next pass steals the Cigarettes, since the dealer is below its
    // maximum, and the list it leaves no longer names any.
    const std::vector<Branch> next = pass(branch.state, Brain::Endless, branch.memory);
    ASSERT_FALSE(next.empty());
    for (const Branch& after : next) {
      EXPECT_TRUE(stole(after, Item::Cigarettes));
      EXPECT_FALSE(after.state.dealerListCigs);
      const std::string text = notation::print(after.state);
      EXPECT_FALSE(contains(text, "listcigs")) << text;
      EXPECT_TRUE(startsWith(text, "p1=2/3 p2=3/3 ")) << text;
    }
  }
}

// ---------------------------------------------------------------------------
// A pass that starts part-way through the turn
// ---------------------------------------------------------------------------

TEST(DealerOrder, ABeliefInABlankShootsItselfAtOnce) {
  const GameState state = parse("p1=2/2 p2=2/2[saw] tube=2L2B turn=p2");
  const std::vector<Branch> believing = pass(state, Brain::Story, believesBlank());
  EXPECT_NEAR(chanceOf(believing, [](const Branch& b) { return shotAt(b, kDealer); }), 1.0, 1e-12);

  // From the start of the turn the story coin decides first, and the saw goes
  // with a shot at the player (lines 421-431).
  const std::vector<Branch> fresh = pass(state, Brain::Story);
  EXPECT_NEAR(chanceOf(fresh, [](const Branch& b) { return shotAt(b, kDealer); }), 0.5, 1e-12);
  EXPECT_NEAR(chanceOf(fresh,
                       [](const Branch& b) {
                         return used(b, Item::HandSaw) && b.memory.target == Target::Player;
                       }),
              0.5, 1e-12);
}

TEST(DealerOrder, AnAimAtItselfSkipsTheCoin) {
  const GameState state = parse("p1=2/2 p2=2/2 tube=2L2B turn=p2");
  const std::vector<Branch> aimed = pass(state, Brain::Endless, aimsAtItself());
  EXPECT_NEAR(chanceOf(aimed, [](const Branch& b) { return shotAt(b, kDealer); }), 1.0, 1e-12);

  const std::vector<Branch> fresh = pass(state, Brain::Endless);
  EXPECT_NEAR(chanceOf(fresh, [](const Branch& b) { return shotAt(b, kDealer); }), 0.5, 1e-12);
  EXPECT_NEAR(chanceOf(fresh, [](const Branch& b) { return shotAt(b, kPlayer); }), 0.5, 1e-12);
}

TEST(DealerOrder, MedicineIsTakenOnceATurn) {
  const GameState state = parse("p1=2/2 p2=2/3[med,med] tube=2L2B turn=p2");
  const std::vector<Branch> after = pass(state, Brain::Endless, tookMedicine());
  EXPECT_NEAR(chanceOf(after, [](const Branch& b) { return used(b, Item::ExpiredMedicine); }), 0.0,
              1e-12);
  EXPECT_NEAR(chanceOf(after, [](const Branch& b) { return shotAt(b, kDealer); }), 0.5, 1e-12);
  EXPECT_NEAR(chanceOf(after, [](const Branch& b) { return shotAt(b, kPlayer); }), 0.5, 1e-12);

  const std::vector<Branch> fresh = pass(state, Brain::Endless);
  EXPECT_NEAR(chanceOf(fresh, [](const Branch& b) { return used(b, Item::ExpiredMedicine); }), 1.0,
              1e-12);
}

}  // namespace
}  // namespace bsr
