/// Whole positions: ordered hands, the dealer's stale list, phone reads nobody
/// saw the result of and the dealer's memory part-way through its turn, as
/// text and back, and the refusals for positions that cannot arise.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/Notation.h"
#include "engine/Position.h"

namespace bsr {
namespace {

Position parseOk(const std::string& text) {
  Position position;
  std::string error;
  EXPECT_TRUE(notation::parsePosition(text, &position, &error)) << error << " in: " << text;
  return position;
}

std::string refusal(const std::string& text) {
  Position position;
  std::string error;
  EXPECT_FALSE(notation::parsePosition(text, &position, &error))
      << "should have been refused: " << text;
  return error;
}

TEST(Position, PrintsInOneOrderAndKeepsTheHandsAsGiven) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1",
       "p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1"},
      {"turn=p2 p2=2/2 phoned=p2@4,6 p1=2/2[beer,mg,beer] tube=1L2B",
       "p1=2/2[beer,mg,beer] p2=2/2 tube=1L2B turn=p2 phoned=p2@6,4"},
      {"dealer=aim:p1,med sawed listcigs p1=2/2 p2=2/2 tube=1L1B turn=p2",
       "p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed listcigs dealer=aim:p1,med"},
      {"p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 known=p2:0L dealer=seen",
       "p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 known=p2:0L dealer=seen"},
      {"p1=2/2[cig] p2=2/3[med] tube=2L2B turn=p2 listcigs",
       "p1=2/2[cig] p2=2/3[med] tube=2L2B turn=p2 listcigs"},
      {"p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p1@4 phoned=p2@5",
       "p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p1@4 phoned=p2@5"},
      {"p1=2/2[mg,mg,mg,mg,mg,mg,mg,mg] p2=2/2 tube=1L1B turn=p1",
       "p1=2/2[mg,mg,mg,mg,mg,mg,mg,mg] p2=2/2 tube=1L1B turn=p1"},
  };
  for (const auto& [input, printed] : cases) {
    const Position first = parseOk(input);
    EXPECT_EQ(notation::printPosition(first), printed) << input;
    const Position second = parseOk(printed);
    EXPECT_EQ(notation::printPosition(second), printed);
    EXPECT_TRUE(first.state == second.state);
    EXPECT_TRUE(first.dealerMemory == second.dealerMemory);
    EXPECT_EQ(first.unseenReads.size(), second.unseenReads.size());
  }
}

TEST(Position, TheDealerMemoryTokensSetTheMemory) {
  const Position seen = parseOk("p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 known=p2:0L dealer=seen");
  EXPECT_TRUE(seen.dealerMemory.knows);
  EXPECT_EQ(seen.dealerMemory.known, Shell::Live);
  EXPECT_EQ(seen.dealerMemory.target, dealer::Target::Player);
  EXPECT_FALSE(seen.dealerMemory.usedMedicine);

  const Position aim = parseOk("p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed listcigs dealer=aim:p1,med");
  EXPECT_FALSE(aim.dealerMemory.knows);
  EXPECT_EQ(aim.dealerMemory.target, dealer::Target::Player);
  EXPECT_TRUE(aim.dealerMemory.usedMedicine);
  EXPECT_TRUE(aim.state.dealerListCigs);

  const Position believes = parseOk("p1=2/2 p2=2/2 tube=2L2B turn=p2 dealer=believes:B");
  EXPECT_TRUE(believes.dealerMemory.knows);
  EXPECT_EQ(believes.dealerMemory.known, Shell::Blank);
  EXPECT_EQ(believes.dealerMemory.target, dealer::Target::Self);

  const Position self = parseOk("p1=2/2 p2=2/2 tube=2L2B turn=p2 dealer=aim:self");
  EXPECT_FALSE(self.dealerMemory.knows);
  EXPECT_EQ(self.dealerMemory.target, dealer::Target::Self);
}

TEST(Position, ASeatHoldsAtMostEightItems) {
  EXPECT_EQ(refusal("p1=2/2[mg,mg,mg,mg,mg,mg,mg,mg,mg] p2=2/2 tube=1L1B turn=p1"),
            "a seat holds at most 8 items");
  GameState state;
  std::string error;
  EXPECT_FALSE(notation::parse("p1=2/2[mg,mg,mg,mg,mg,mg,mg,mg,mg] p2=2/2 tube=1L1B turn=p1",
                               &state, &error));
  EXPECT_EQ(error, "a seat holds at most 8 items");
  EXPECT_TRUE(
      notation::parse("p1=2/2[mg,mg,mg,mg,mg,mg,mg,mg] p2=2/2 tube=1L1B turn=p1", &state, &error))
      << error;
}

TEST(Position, RefusesPhoneReadsThatCannotHaveHappened) {
  const std::string shape = "phoned must look like phoned=p2@5 or phoned=p2@5,4";
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2"), shape);
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2@x"), shape);
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p1 phoned=p2@5 phoned=p2@4"),
            "phoned is given twice for p2");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=0L1B turn=p1 phoned=p2@1"),
            "a phone read names a tube of 2 to 8 shells");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p1 phoned=p2@9"),
            "a phone read names a tube of 2 to 8 shells");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2@3"),
            "a phone read names a tube at least as large as the one in the position");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p1 phoned=p2@8,8,8,8,8,8,8,8,8"),
            "a seat makes at most 8 phone reads in one load");
  EXPECT_TRUE(
      parseOk("p1=2/2 p2=2/2 tube=1L1B turn=p1 phoned=p2@8,8,8,8,8,8,8,8").unseenReads.size() ==
      8u);
}

TEST(Position, RefusesDealerMemoriesTheScriptCannotLeave) {
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 p3=2/2 tube=1L1B turn=p1 listcigs"),
            "listcigs and dealer need exactly two seats");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 p3=2/2 tube=1L1B turn=p2 dealer=med"),
            "listcigs and dealer need exactly two seats");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=foo"),
            "dealer must be seen, believes:B, aim:self or aim:p1, optionally followed by med, as "
            "in dealer=seen,med");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=believes:L"),
            "dealer=believes:L cannot happen: the dealer never drinks a Beer on a shell it saw was "
            "live");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed dealer=seen,aim:p1"),
            "dealer names at most one of seen, believes:B, aim:self and aim:p1");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=med dealer=med"),
            "dealer is given twice");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p1 dealer=med"), "dealer needs p2 to move");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p2 cuffed=p2 dealer=med"),
            "a cuffed seat cannot be in the middle of its turn");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=seen"),
            "dealer=seen needs known=p2:0L or known=p2:0B");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p2 known=p1:0L dealer=seen"),
            "dealer=seen needs known=p2:0L or known=p2:0B");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=aim:p1"),
            "dealer=aim:p1 needs the barrel sawed");
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=0L0B turn=p2 dealer=med"),
            "dealer needs shells left in the tube");
}

TEST(Position, ASawedBarrelGoesOnlyWithAMemoryAimedAtThePlayer) {
  const std::string sawed =
      "the dealer saws only when it aims at p1, so a sawed barrel cannot go with this dealer "
      "memory";
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed dealer=aim:self"), sawed);
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed dealer=believes:B"), sawed);
  EXPECT_EQ(refusal("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed known=p2:0B dealer=seen"), sawed);
  parseOk("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed known=p2:0L dealer=seen");
  parseOk("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed dealer=aim:p1,med");
  parseOk("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed dealer=med");
}

TEST(Position, AStateOnlyParseRefusesWhatAStateCannotHold) {
  const std::string message = "phoned and dealer are read only where a whole position is expected";
  for (const char* text : {"p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2@5",
                           "p1=2/2 p2=2/2 tube=2L2B turn=p2 dealer=med"}) {
    GameState state;
    std::string error;
    EXPECT_FALSE(notation::parse(text, &state, &error)) << text;
    EXPECT_EQ(error, message);
  }
  // listcigs is part of the state, so both parsers read it.
  GameState state;
  std::string error;
  ASSERT_TRUE(notation::parse("p1=2/2[cig] p2=2/3 tube=2L2B turn=p2 listcigs", &state, &error))
      << error;
  EXPECT_TRUE(state.dealerListCigs);
  EXPECT_EQ(notation::print(state), "p1=2/2[cig] p2=2/3 tube=2L2B turn=p2 listcigs");
}

TEST(Position, TheMemoryIsCheckedAgainstTheBrain) {
  std::string error;
  const Position believes = parseOk("p1=2/2 p2=2/2 tube=2L2B turn=p2 dealer=believes:B");
  EXPECT_TRUE(dealer::validateMemory(believes, dealer::Brain::Story, &error)) << error;
  EXPECT_FALSE(dealer::validateMemory(believes, dealer::Brain::Endless, &error));
  EXPECT_EQ(error, "dealer=believes:B happens only in story mode");

  const Position self = parseOk("p1=2/2 p2=2/2 tube=2L2B turn=p2 dealer=aim:self");
  EXPECT_TRUE(dealer::validateMemory(self, dealer::Brain::Endless, &error)) << error;
  EXPECT_FALSE(dealer::validateMemory(self, dealer::Brain::Story, &error));
  EXPECT_EQ(error, "dealer=aim:self happens only in double or nothing");

  // A blank the dealer saw is a memory either brain can hold.
  const Position seen = parseOk("p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:0B dealer=seen");
  EXPECT_TRUE(dealer::validateMemory(seen, dealer::Brain::Endless, &error)) << error;
  EXPECT_TRUE(dealer::validateMemory(seen, dealer::Brain::Story, &error)) << error;
}

void expectMasks(const std::string& text, int seat,
                 const std::vector<std::pair<int, double>>& expected) {
  const std::vector<std::pair<std::uint8_t, double>> masks = readMasks(parseOk(text), seat);
  ASSERT_EQ(masks.size(), expected.size()) << text;
  for (std::size_t i = 0; i < masks.size(); ++i) {
    EXPECT_EQ(masks[i].first, expected[i].first) << text << " entry " << i;
    EXPECT_NEAR(masks[i].second, expected[i].second, 1e-12) << text << " entry " << i;
  }
}

TEST(Position, AnUnseenReadSpreadsOverTheShellsItCouldHaveNamed) {
  // A read at five shells, two of which have since been fired: picks 2 to 4
  // are offsets 0 to 2 now, and pick 1 named a shell that has left.
  expectMasks("p1=2/2 p2=2/2 tube=1L2B turn=p1 phoned=p2@5", 1,
              {{0, 0.25}, {1, 0.25}, {2, 0.25}, {4, 0.25}});
  // Two reads by the dealer's seat, at five and four shells, with four left.
  expectMasks("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2@5,4", 1,
              {{2, 1.0 / 12.0},
               {3, 1.0 / 12.0},
               {4, 1.0 / 12.0},
               {5, 1.0 / 12.0},
               {6, 1.0 / 6.0},
               {8, 1.0 / 12.0},
               {9, 1.0 / 12.0},
               {10, 1.0 / 6.0},
               {12, 1.0 / 6.0}});
  // The player's phone at eight shells moves a pick of 7 to 6.
  expectMasks("p1=2/2 p2=2/2 tube=4L4B turn=p2 phoned=p1@8", 0,
              {{2, 1.0 / 7.0},
               {4, 1.0 / 7.0},
               {8, 1.0 / 7.0},
               {16, 1.0 / 7.0},
               {32, 1.0 / 7.0},
               {64, 2.0 / 7.0}});
  // A seat with no reads names nothing.
  expectMasks("p1=2/2 p2=2/2 tube=4L4B turn=p2 phoned=p1@8", 1, {{0, 1.0}});
}

TEST(Position, ReadExpansionsMarkTheSeatThatMadeEachRead) {
  const Position position = parseOk("p1=2/2 p2=2/2 tube=1L2B turn=p1 phoned=p2@5");
  const std::vector<ReadExpansion> expansions = expandReads(position, 0);
  ASSERT_EQ(expansions.size(), 4u);
  double total = 0.0;
  for (std::size_t i = 0; i < expansions.size(); ++i) {
    total += expansions[i].weight;
    EXPECT_NEAR(expansions[i].weight, 0.25, 1e-12);
  }
  EXPECT_NEAR(total, 1.0, 1e-12);
  // In mask order: nothing, offset 0, offset 1, offset 2, each seen by p2.
  EXPECT_EQ(expansions[0].extraObservers, (std::array<std::uint8_t, kMaxShells>{}));
  EXPECT_EQ(expansions[1].extraObservers[0], 2);
  EXPECT_EQ(expansions[2].extraObservers[1], 2);
  EXPECT_EQ(expansions[3].extraObservers[2], 2);

  // The advised seat's own reads are not expanded.
  const std::vector<ReadExpansion> own = expandReads(position, 1);
  ASSERT_EQ(own.size(), 1u);
  EXPECT_NEAR(own.front().weight, 1.0, 1e-12);
}

}  // namespace
}  // namespace bsr
