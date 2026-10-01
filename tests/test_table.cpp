/// The table that plays a round: what a seed draws, in what order, the items
/// the dealer's script picks from its ordered hand, the places items take on
/// the table, and what each seat may know of the position.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "engine/Notation.h"
#include "engine/Position.h"
#include "engine/Table.h"

namespace bsr {
namespace {

TableOptions doubleOrNothing(std::uint32_t seed, int charges = 0) {
  TableOptions options;
  options.config = RuleConfig::doubleOrNothing(0);
  options.seed = seed;
  options.charges = charges;
  return options;
}

Table firstLoad(const TableOptions& options) {
  Table table(options);
  EXPECT_EQ(table.next(), Step::Load);
  table.load();
  return table;
}

/// The printed form of a position, so that two positions compare by text.
std::string canonical(const std::string& text) {
  Position position;
  std::string error;
  EXPECT_TRUE(notation::parsePosition(text, &position, &error)) << error << " in: " << text;
  return notation::printPosition(position);
}

Table fromText(const std::string& text, const TableOptions& options) {
  Position position;
  std::string error;
  EXPECT_TRUE(notation::parsePosition(text, &position, &error)) << error << " in: " << text;
  Table table(options);
  EXPECT_TRUE(Table::fromPosition(position, options, &table, &error)) << error;
  return table;
}

std::vector<const Event*> eventsOf(const Table& table, Event::Kind kind) {
  std::vector<const Event*> out;
  for (const Event& event : table.log()) {
    if (event.kind == kind) out.push_back(&event);
  }
  return out;
}

/// The engine's own draw of an integer from a fresh generator, written out
/// again here so that the test does not lean on the code it checks.
int uniformInt(std::mt19937* rng, int lo, int hi) {
  if (lo == hi) return lo;
  const std::uint64_t range = static_cast<std::uint64_t>(hi - lo) + 1;
  const std::uint64_t span = std::uint64_t{1} << 32;
  const std::uint64_t limit = span - span % range;
  std::uint64_t x = (*rng)();
  while (x >= limit) x = (*rng)();
  return lo + static_cast<int>(x % range);
}

TEST(Table, TheGeneratorIsTheStandardMersenneTwister) {
  std::mt19937 standard(5489);
  EXPECT_EQ(standard(), 3499211612u);
  std::mt19937 one(1);
  EXPECT_EQ(one(), 1791095845u);
}

TEST(Table, PinnedSeedsDealTheSameFirstLoad) {
  const std::vector<std::pair<std::uint32_t, std::string>> pinned = {
      {1, "p1=3/3[phone,inv] p2=3/3[beer,cig] tube=2L3B turn=p1"},
      {2, "p1=2/2[mg,inv,cuff] p2=2/2[cig,cuff,mg] tube=2L3B turn=p1"},
      {4, "p1=3/3[cig,cuff,phone,med,saw] p2=3/3[adr,mg,saw,med,cig] tube=4L4B turn=p1"},
      {6, "p1=4/4[cig,inv,mg,cig,cuff] p2=4/4[beer,mg,phone,inv,inv] tube=1L1B turn=p1"},
      {7, "p1=2/2[inv,cuff,cuff] p2=2/2[med,med,phone] tube=4L4B turn=p1"},
  };
  for (const auto& [seed, text] : pinned) {
    const Table table = firstLoad(doubleOrNothing(seed));
    Position position;
    position.state = table.state();
    EXPECT_EQ(notation::printPosition(position), canonical(text)) << "seed " << seed;
  }
}

TEST(Table, DoubleOrNothingDrawsTheChargesForTheRound) {
  EXPECT_EQ(Table(doubleOrNothing(1)).charges(), 3);
  EXPECT_EQ(Table(doubleOrNothing(2)).charges(), 2);
  EXPECT_EQ(Table(doubleOrNothing(5)).charges(), 4);
  EXPECT_EQ(Table(doubleOrNothing(5)).state().players[1].maxHp, 4);
}

TEST(Table, GivenChargesTakeNoDraw) {
  const Table table = firstLoad(doubleOrNothing(1, 3));
  EXPECT_EQ(table.charges(), 3);
  std::mt19937 fresh(1);
  const int total = uniformInt(&fresh, 2, 8);
  const int live = std::max(1, total / 2);
  EXPECT_EQ(table.state().tube.live, live);
  EXPECT_EQ(table.state().tube.blank, total - live);
  EXPECT_EQ(table.state().tube.live, 1);
  EXPECT_EQ(table.state().tube.blank, 1);
}

TEST(Table, TheFirstLoadAtTwoChargesDealsNoHandSaw) {
  const Table table(doubleOrNothing(1, 2));
  const std::vector<Item> first = table.poolFor(0, 2);
  EXPECT_EQ(first.size(), 8u);
  EXPECT_EQ(std::count(first.begin(), first.end(), Item::HandSaw), 0);
  EXPECT_EQ(table.poolFor(1, 2).size(), 9u);
  EXPECT_EQ(table.poolFor(0, 3).size(), 9u);
}

TEST(Table, TheLoadEventCarriesTheDeal) {
  const Table table = firstLoad(doubleOrNothing(4));
  const std::vector<const Event*> loads = eventsOf(table, Event::Kind::Load);
  ASSERT_EQ(loads.size(), 1u);
  const Event& load = *loads[0];
  EXPECT_EQ(load.live, 4);
  EXPECT_EQ(load.blank, 4);
  EXPECT_EQ(load.dealt, 5);
  EXPECT_EQ(load.first, 0);
  for (int seat = 0; seat < 2; ++seat) {
    const Hand& hand = table.state().players[seat].hand;
    ASSERT_EQ(load.dealtItems[static_cast<std::size_t>(seat)].size(),
              static_cast<std::size_t>(hand.size()));
    for (int i = 0; i < hand.size(); ++i) {
      EXPECT_EQ(load.dealtItems[static_cast<std::size_t>(seat)][static_cast<std::size_t>(i)],
                hand.at[static_cast<std::size_t>(i)]);
    }
  }
  EXPECT_EQ(load.text.rfind("The gun is loaded with 4 live and 4 blank.\n", 0), 0u) << load.text;
  EXPECT_EQ(table.loadNumber(), 1);
}

TEST(Table, ADealFillsTheLowestFreePlaces) {
  const Table table = firstLoad(doubleOrNothing(2));
  const std::array<int, kMaxItemsPerSeat> places = table.tray(0);
  EXPECT_EQ(places[0], itemIndex(Item::MagnifyingGlass));
  EXPECT_EQ(places[1], itemIndex(Item::Inverter));
  EXPECT_EQ(places[2], itemIndex(Item::Handcuffs));
  EXPECT_EQ(places[3], -1);
  EXPECT_EQ(table.slotOf(0, Item::Handcuffs, 0), 2);
  EXPECT_EQ(table.slotOf(0, Item::Handcuffs, 1), -1);
}

TEST(Table, TheDealerReadsItsItemsInTheOrderTheySit) {
  TableOptions options = doubleOrNothing(1);
  Table table = fromText("p1=2/2 p2=2/2[beer,mg] tube=1L2B turn=p2", options);
  ASSERT_EQ(table.next(), Step::Dealer);
  table.dealerPass();
  const std::vector<const Event*> items = eventsOf(table, Event::Kind::Item);
  ASSERT_FALSE(items.empty());
  EXPECT_EQ(items.front()->item, Item::Beer);
}

TEST(Table, TheDealerListKeepsCigarettesItCouldReach) {
  Table table = fromText("p1=2/3[cig] p2=2/3[adr,mg] tube=2L2B turn=p2", doubleOrNothing(1));
  ASSERT_EQ(table.next(), Step::Dealer);
  table.dealerPass();
  EXPECT_TRUE(table.state().dealerListCigs);
}

TEST(Table, AChamberTheDealerWorkedOutStaysItsOwn) {
  Table table =
      fromText("p1=2/2 p2=2/2[cuff] tube=1L1B known=p1:0L known=p2:1B turn=p2", doubleOrNothing(1));
  table.dealerPass();
  ASSERT_TRUE(table.state().tube.knows(1, 0));
  ASSERT_TRUE(table.dealerMidTurn());
  const Position seen = table.view(0);
  EXPECT_TRUE(seen.state.tube.knows(0, 0));
  EXPECT_EQ(seen.state.tube.knownBy[0] & 2u, 0u);
  EXPECT_EQ(notation::printPosition(seen).find("dealer="), std::string::npos);
  EXPECT_TRUE(seen.dealerMemory == dealer::Memory{});
}

TEST(Table, AChamberTheDealerLookedAtIsSeenToBeKnown) {
  Table table = fromText("p1=2/2 p2=2/2[mg] tube=1L2B known=p1:0L turn=p2", doubleOrNothing(1));
  table.dealerPass();
  ASSERT_TRUE(table.state().tube.knows(1, 0));
  const Position seen = table.view(0);
  EXPECT_NE(seen.state.tube.knownBy[0] & 2u, 0u);
}

TEST(Table, TheDealerPhoneBecomesAReadNobodySaw) {
  Table table = fromText("p1=2/2 p2=2/2[phone] tube=2L3B turn=p2", doubleOrNothing(1));
  table.dealerPass();
  const Position seen = table.view(0);
  EXPECT_NE(notation::printPosition(seen).find("phoned=p2@5"), std::string::npos)
      << notation::printPosition(seen);
  for (int offset = 1; offset < seen.state.tube.size(); ++offset) {
    EXPECT_EQ(seen.state.tube.knownBy[offset] & 2u, 0u) << "offset " << offset;
  }
  const std::vector<const Event*> learned = eventsOf(table, Event::Kind::Learned);
  ASSERT_EQ(learned.size(), 1u);
  EXPECT_EQ(learned[0]->privateTo, 1);
  EXPECT_GE(learned[0]->offset, 1);
  EXPECT_TRUE(table.state().tube.knows(1, learned[0]->offset));
}

TEST(Table, TheDealerWorksOutTheChamberBeforeItsBeerRacksItOut) {
  // The dealer heard that shell 2 is live, so the counts leave a blank in the
  // chamber. It works that out as its pass begins (DealerIntelligence.gd
  // 96-104), before its item rules drink the Beer (170-175), so the log names
  // the chamber before the Beer racks it out.
  Table table = fromText("p1=2/2 p2=2/2[beer] tube=1L1B known=p2:1L turn=p2", doubleOrNothing(1));
  table.dealerPass();
  const std::vector<Event>& log = table.log();
  ASSERT_EQ(log.size(), 3u);
  EXPECT_EQ(log[0].kind, Event::Kind::Learned);
  EXPECT_TRUE(log[0].deduced);
  EXPECT_EQ(log[0].seat, 1);
  EXPECT_EQ(log[0].privateTo, 1);
  EXPECT_EQ(log[0].offset, 0);
  EXPECT_EQ(log[0].shell, Shell::Blank);
  EXPECT_EQ(log[0].text,
            "Shells only the dealer has heard on a Burner Phone, with the counts, show the "
            "chamber is blank.");
  EXPECT_EQ(log[1].kind, Event::Kind::Item);
  EXPECT_EQ(log[1].item, Item::Beer);
  EXPECT_EQ(log[1].text, "The dealer uses its Beer.\nThe shell it racked out was blank.");
  EXPECT_EQ(log[2].kind, Event::Kind::Rule);
  EXPECT_EQ(table.state().tube.size(), 1);
  EXPECT_EQ(table.state().tube.truth[0], Shell::Live);
}

TEST(Table, TheDealerWorksOutABlankBeforeItsInverterMakesItLive) {
  // The Inverter rule fires on a chamber the dealer knows is blank and writes
  // it live (DealerIntelligence.gd 195-201): blank is logged before the use,
  // live after it.
  Table table = fromText("p1=2/2 p2=2/2[inv] tube=1L1B known=p2:1L turn=p2", doubleOrNothing(1));
  table.dealerPass();
  const std::vector<Event>& log = table.log();
  ASSERT_EQ(log.size(), 4u);
  EXPECT_EQ(log[0].kind, Event::Kind::Learned);
  EXPECT_TRUE(log[0].deduced);
  EXPECT_EQ(log[0].offset, 0);
  EXPECT_EQ(log[0].shell, Shell::Blank);
  EXPECT_EQ(log[1].kind, Event::Kind::Item);
  EXPECT_EQ(log[1].item, Item::Inverter);
  EXPECT_EQ(log[1].text, "The dealer uses its Inverter.\nIt flips the chamber.");
  EXPECT_EQ(log[2].kind, Event::Kind::Learned);
  EXPECT_FALSE(log[2].deduced);
  EXPECT_EQ(log[2].privateTo, 1);
  EXPECT_EQ(log[2].offset, 0);
  EXPECT_EQ(log[2].shell, Shell::Live);
  EXPECT_EQ(log[2].text, "Seen only by the dealer: the chamber was blank and is now live.");
  EXPECT_EQ(log[3].kind, Event::Kind::Rule);
  EXPECT_EQ(table.state().tube.truth[0], Shell::Live);
  EXPECT_EQ(table.state().tube.live, 2);
}

TEST(Table, TheStoryDealerKnowsTheLastShellBeforeItsInverter) {
  // Both rule sets know the last shell as the pass begins
  // (DealerIntelligence.gd 106-112).
  TableOptions options;
  options.config = RuleConfig::storyRound(2);
  options.seed = 1;
  Table table = fromText("p1=2/4 p2=2/4[inv] tube=0L1B turn=p2", options);
  table.dealerPass();
  const std::vector<Event>& log = table.log();
  ASSERT_EQ(log.size(), 4u);
  EXPECT_EQ(log[0].kind, Event::Kind::Learned);
  EXPECT_TRUE(log[0].deduced);
  EXPECT_EQ(log[0].offset, 0);
  EXPECT_EQ(log[0].shell, Shell::Blank);
  EXPECT_EQ(log[0].text, "One shell is left, so the dealer knows the chamber is blank.");
  EXPECT_EQ(log[1].kind, Event::Kind::Item);
  EXPECT_EQ(log[1].item, Item::Inverter);
  EXPECT_EQ(log[2].kind, Event::Kind::Learned);
  EXPECT_EQ(log[2].shell, Shell::Live);
  EXPECT_EQ(log[2].text, "Seen only by the dealer: the chamber was blank and is now live.");
  EXPECT_EQ(log[3].kind, Event::Kind::Rule);
  EXPECT_EQ(table.state().tube.truth[0], Shell::Live);
}

/// Play rounds of `options` from seeds 1 to `seeds`, with items before shots,
/// and check that every shell learned at the chamber names the shell the
/// chamber held at its place in the log. Returns how many were worked out
/// before an item use.
int checkChamberShells(TableOptions options, std::uint32_t seeds) {
  int worked = 0;
  for (std::uint32_t seed = 1; seed <= seeds; ++seed) {
    options.seed = seed;
    Table table(options);
    for (int guard = 0; guard < 400 && table.next() != Step::Over; ++guard) {
      const std::size_t from = table.log().size();
      switch (table.next()) {
        case Step::Load:
          table.load();
          break;
        case Step::Dealer:
          table.dealerPass();
          break;
        case Step::Choose: {
          const std::vector<Action> actions = table.legal();
          table.play(
              actions[actions.size() - 1 - static_cast<std::size_t>(guard) % 2 % actions.size()]);
          break;
        }
        case Step::Over:
          break;
      }
      const std::vector<Event>& log = table.log();
      std::size_t firstItem = log.size();
      std::string text;
      for (std::size_t i = from; i < log.size(); ++i) {
        if (log[i].kind == Event::Kind::Item && firstItem == log.size()) firstItem = i;
        text += log[i].text + "\n";
      }
      for (std::size_t i = from; i < log.size(); ++i) {
        const Event& event = log[i];
        if (event.kind != Event::Kind::Learned || event.offset != 0) continue;
        if (i > firstItem) {
          EXPECT_FALSE(event.deduced) << "seed " << seed << " step " << guard;
          EXPECT_EQ(event.shell, table.state().tube.truth[0])
              << "seed " << seed << " step " << guard;
          continue;
        }
        // Worked out before the item it led to, about the chamber the pass
        // began with: the shell a Beer racked out, the blank an Inverter made
        // live, or else the chamber still there.
        EXPECT_TRUE(event.deduced) << "seed " << seed << " step " << guard;
        if (i + 1 != firstItem) {
          ADD_FAILURE() << "no item use follows, seed " << seed << " step " << guard;
          continue;
        }
        const Event& use = log[firstItem];
        const Item used = use.steal ? use.stolen : use.item;
        Shell began = table.state().tube.truth[0];
        if (used == Item::Beer) {
          began =
              text.find("racked out was LIVE") != std::string::npos ? Shell::Live : Shell::Blank;
        }
        if (used == Item::Inverter) began = Shell::Blank;
        EXPECT_EQ(event.shell, began) << "seed " << seed << " step " << guard;
        ++worked;
      }
    }
  }
  return worked;
}

TEST(Table, AShellLearnedAtTheChamberNamesTheChamberWhereItIsLogged) {
  EXPECT_GT(checkChamberShells(doubleOrNothing(0), 60), 0);
  TableOptions story;
  story.config = RuleConfig::storyRound(3);
  EXPECT_GT(checkChamberShells(story, 30), 0);
}

TEST(Table, AnyAdrenalineInTheTrayMayBeTheOneSpent) {
  TableOptions options = doubleOrNothing(1);
  options.scriptedDealer = false;
  Table table = fromText("p1=2/2[adr,mg,adr] p2=2/2 tube=1L1B turn=p1", options);
  const int second = table.slotOf(0, Item::Adrenaline, 1);
  ASSERT_EQ(second, 2);
  table.play(Action::adrenalineAlone(0), second);
  const std::array<int, kMaxItemsPerSeat> places = table.tray(0);
  EXPECT_EQ(places[2], -1);
  EXPECT_EQ(places[0], itemIndex(Item::Adrenaline));
  EXPECT_EQ(places[1], itemIndex(Item::MagnifyingGlass));
  const Hand& hand = table.state().players[0].hand;
  ASSERT_EQ(hand.size(), 2);
  EXPECT_EQ(hand.at[0], Item::MagnifyingGlass);
  EXPECT_EQ(hand.at[1], Item::Adrenaline);
}

TEST(Table, AnyCopyInARunMayBeTheOneSpent) {
  TableOptions options = doubleOrNothing(1);
  options.scriptedDealer = false;
  Table table = fromText("p1=2/2[beer,beer,mg] p2=2/2 tube=1L2B turn=p1", options);
  EXPECT_THROW(table.play(Action::use(Item::Beer), 2), std::logic_error);
  table.play(Action::use(Item::Beer), 1);
  const std::array<int, kMaxItemsPerSeat> places = table.tray(0);
  EXPECT_EQ(places[0], itemIndex(Item::Beer));
  EXPECT_EQ(places[1], -1);
  EXPECT_EQ(places[2], itemIndex(Item::MagnifyingGlass));
}

TEST(Table, OwnReadsStayKnownAndOtherSeatsReadsAreUnseen) {
  TableOptions options = doubleOrNothing(3);
  options.scriptedDealer = false;
  Table table = fromText("p1=2/2[phone] p2=2/2 tube=2L3B turn=p1", options);
  table.play(Action::use(Item::BurnerPhone));
  const std::vector<const Event*> learned = eventsOf(table, Event::Kind::Learned);
  ASSERT_EQ(learned.size(), 1u);
  const int offset = learned[0]->offset;
  ASSERT_GE(offset, 1);
  const Position mine = table.view(0);
  EXPECT_TRUE(mine.state.tube.knows(0, offset));
  EXPECT_TRUE(mine.unseenReads.empty());
  const Position theirs = table.view(1);
  EXPECT_FALSE(theirs.state.tube.knows(0, offset));
  ASSERT_EQ(theirs.unseenReads.size(), 1u);
  EXPECT_EQ(theirs.unseenReads[0].seat, 0);
  EXPECT_EQ(theirs.unseenReads[0].sizeAtUse, 5);
}

TEST(Table, AnInverterOnAnUnseenChamberLeavesTheCountsAsLoaded) {
  TableOptions options = doubleOrNothing(1);
  options.scriptedDealer = false;
  // p2 has seen the chamber; p1 has not, and flips it.
  Table table = fromText("p1=2/2[inv] p2=2/2 tube=1L2B known=p2:0B turn=p1", options);
  table.play(Action::use(Item::Inverter));
  EXPECT_EQ(table.state().tube.live, 2);
  EXPECT_EQ(table.loadedCounts(), std::make_pair(1, 2));
  const Position seen = table.view(0);
  EXPECT_EQ(seen.state.tube.live, 1);
  EXPECT_EQ(seen.state.tube.blank, 2);
  EXPECT_TRUE(seen.state.tube.chamberInverted);
  EXPECT_EQ(seen.state.tube.truth[0], Shell::Unknown);
  EXPECT_FALSE(seen.state.tube.pinnedFlip);
  // The table's own record says the counts moved with the flip, and p1 is
  // handed back the same counts from it as the view gives.
  EXPECT_TRUE(table.state().tube.pinnedFlip);
  Tube held = table.state().tube;
  held.unflipFor(0);
  EXPECT_EQ(held.live, 1);
  EXPECT_EQ(held.blank, 2);
  EXPECT_TRUE(held.chamberInverted);
}

TEST(Table, CallsOutOfTurnAreRefused) {
  Table table(doubleOrNothing(1));
  EXPECT_THROW(table.dealerPass(), std::logic_error);
  EXPECT_THROW(table.legal(), std::logic_error);
  table.load();
  EXPECT_THROW(table.load(), std::logic_error);
  EXPECT_THROW(table.play(Action::use(Item::Remote)), std::logic_error);
}

TEST(Table, AWrittenPositionWithUnseenReadsIsRefused) {
  Position position;
  std::string error;
  ASSERT_TRUE(
      notation::parsePosition("p1=2/2 p2=2/2 tube=2L3B turn=p1 phoned=p2@5", &position, &error));
  const TableOptions options = doubleOrNothing(1);
  Table table(options);
  EXPECT_FALSE(Table::fromPosition(position, options, &table, &error));
  EXPECT_FALSE(error.empty());
}

/// Play a whole round with each seat shooting itself first, and return every
/// event's text.
std::string playOut(const TableOptions& options) {
  Table table(options);
  std::string out;
  for (int guard = 0; guard < 1000 && table.next() != Step::Over; ++guard) {
    switch (table.next()) {
      case Step::Load:
        table.load();
        break;
      case Step::Dealer:
        table.dealerPass();
        break;
      case Step::Choose: {
        const std::vector<Action> actions = table.legal();
        table.play(actions[static_cast<std::size_t>(guard) % actions.size()]);
        break;
      }
      case Step::Over:
        break;
    }
  }
  for (const Event& event : table.log()) out += event.text + "\n";
  EXPECT_EQ(table.next(), Step::Over);
  return out;
}

/// Whether every seat's tray holds exactly the items in its hand.
bool trayMatchesHand(const Table& table) {
  for (int seat = 0; seat < table.state().playerCount; ++seat) {
    const Hand& hand = table.state().players[seat].hand;
    const std::array<int, kMaxItemsPerSeat> places = table.tray(seat);
    for (int k = 0; k < kItemCount; ++k) {
      if (std::count(places.begin(), places.end(), k) != hand.count(itemAt(k))) return false;
    }
    for (int ordinal = 0; ordinal < hand.size(); ++ordinal) {
      const Item item = hand.at[static_cast<std::size_t>(ordinal)];
      const int slot = table.slotOf(seat, item, hand.ordinalAt(ordinal));
      if (slot < 0 || places[static_cast<std::size_t>(slot)] != itemIndex(item)) return false;
    }
  }
  return true;
}

TEST(Table, TheSameSeedPlaysTheSameRound) {
  for (std::uint32_t seed = 1; seed <= 8; ++seed) {
    const TableOptions options = doubleOrNothing(seed);
    EXPECT_EQ(playOut(options), playOut(options)) << "seed " << seed;
  }
  TableOptions story;
  story.config = RuleConfig::storyRound(2);
  story.seed = 3;
  EXPECT_EQ(playOut(story), playOut(story));
  TableOptions crowd;
  crowd.config = RuleConfig::multiplayer(3);
  crowd.players = 3;
  crowd.seed = 9;
  EXPECT_EQ(playOut(crowd), playOut(crowd));
}

TEST(Table, TheTrayHoldsTheHandAndEveryViewIsAPosition) {
  for (std::uint32_t seed = 1; seed <= 60; ++seed) {
    Table table(doubleOrNothing(seed));
    for (int guard = 0; guard < 400 && table.next() != Step::Over; ++guard) {
      switch (table.next()) {
        case Step::Load:
          table.load();
          break;
        case Step::Dealer:
          table.dealerPass();
          break;
        case Step::Choose: {
          // Items before shots, so that the hands are worked through.
          const std::vector<Action> actions = table.legal();
          table.play(
              actions[actions.size() - 1 - static_cast<std::size_t>(guard) % 2 % actions.size()]);
          break;
        }
        case Step::Over:
          break;
      }
      ASSERT_TRUE(trayMatchesHand(table)) << "seed " << seed << " step " << guard;
      for (int seat = 0; seat < 2; ++seat) {
        const std::string text = notation::printPosition(table.view(seat));
        Position position;
        std::string error;
        EXPECT_TRUE(notation::parsePosition(text, &position, &error)) << error << " in: " << text;
      }
    }
  }
}

TEST(Table, AShotReportsTheChargesItTook) {
  TableOptions options;
  options.config = RuleConfig::storyRound(3);
  options.scriptedDealer = false;
  // Stage 3 leaves a seat at or above the floor on one charge at least.
  Table table = fromText("p1=5/5 p2=2/5 tube=1L0B sawed turn=p1", options);
  table.play(Action::shoot(1));
  const std::vector<const Event*> shots = eventsOf(table, Event::Kind::Shot);
  ASSERT_EQ(shots.size(), 1u);
  EXPECT_EQ(shots[0]->shell, Shell::Live);
  EXPECT_EQ(shots[0]->damage, 1);
  EXPECT_EQ(table.state().players[1].hp, 1);
}

}  // namespace
}  // namespace bsr
