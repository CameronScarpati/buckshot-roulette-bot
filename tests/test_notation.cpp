/// The notation is what tests, transcripts and the advisor all speak, so it has
/// to round trip and it has to refuse positions that cannot exist.

#include <gtest/gtest.h>

#include "engine/Notation.h"

namespace bsr {
namespace {

GameState parseOk(const std::string& text) {
  GameState state;
  std::string error;
  EXPECT_TRUE(notation::parse(text, &state, &error)) << error << " in: " << text;
  return state;
}

void parseFails(const std::string& text) {
  GameState state;
  std::string error;
  EXPECT_FALSE(notation::parse(text, &state, &error)) << "should have been rejected: " << text;
  EXPECT_FALSE(error.empty());
}

TEST(Notation, ReadsEveryField) {
  const GameState state =
      parseOk("p1=3/4[saw,beer] p2=2/4[mg] tube=2L3B turn=p2 cuffed=p1 sawed known=p1:0L,2B");
  EXPECT_EQ(state.playerCount, 2);
  EXPECT_EQ(state.players[0].hp, 3);
  EXPECT_EQ(state.players[0].maxHp, 4);
  EXPECT_EQ(state.players[0].items[itemIndex(Item::HandSaw)], 1);
  EXPECT_EQ(state.players[0].items[itemIndex(Item::Beer)], 1);
  EXPECT_EQ(state.players[1].items[itemIndex(Item::MagnifyingGlass)], 1);
  EXPECT_EQ(state.tube.live, 2);
  EXPECT_EQ(state.tube.blank, 3);
  EXPECT_EQ(state.current, 1);
  EXPECT_TRUE(state.players[0].cuffed);
  EXPECT_TRUE(state.tube.sawed);
  EXPECT_TRUE(state.tube.knows(0, 0));
  EXPECT_EQ(state.tube.truth[0], Shell::Live);
  EXPECT_TRUE(state.tube.knows(0, 2));
  EXPECT_EQ(state.tube.truth[2], Shell::Blank);
  EXPECT_FALSE(state.tube.knows(1, 0));
}

TEST(Notation, RoundTrips) {
  const std::vector<std::string> positions = {
      "p1=3/4[saw,beer] p2=2/4[mg] tube=2L3B turn=p1",
      "p1=1/1 p2=1/1 tube=1L1B turn=p2",
      "p1=2/2[cig,cig,cuff] p2=2/2 tube=3L1B turn=p1 sawed",
      "p1=2/2 p2=2/2 p3=1/2 tube=2L2B turn=p3",
      "p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p1:0B,1L",
  };
  for (const std::string& text : positions) {
    const GameState first = parseOk(text);
    const std::string printed = notation::print(first);
    const GameState second = parseOk(printed);
    EXPECT_TRUE(first == second) << text << "\nprinted as: " << printed;
    EXPECT_EQ(printed, notation::print(second));
  }
}

TEST(Notation, ChargesDefaultToFull) {
  const GameState state = parseOk("p1=3 p2=2 tube=1L1B turn=p1");
  EXPECT_EQ(state.players[0].hp, 3);
  EXPECT_EQ(state.players[0].maxHp, 3);
  EXPECT_EQ(state.players[1].maxHp, 2);
}

TEST(Notation, RejectsPositionsThatCannotExist) {
  parseFails("p1=3/4 tube=1L1B turn=p1");                        // one seat
  parseFails("p1=5/4 p2=2/4 tube=1L1B turn=p1");                 // over maximum charges
  parseFails("p1=2/2 p2=2/2 tube=9L1B turn=p1");                 // tube too long
  parseFails("p1=2/2 p2=2/2 tube=1L1B turn=p3");                 // no such seat
  parseFails("p1=2/2 p2=2/2 tube=1L1B turn=p1 known=p1:5L");     // past the end
  parseFails("p1=2/2 p2=2/2 tube=1L1B turn=p1 known=p1:0L,1L");  // two lives in a 1L tube
  parseFails("p1=2/2 p2=2/2 tube=1L1B turn=p1 [saw]");           // stray token
  parseFails("p1=2/2 p2=2/2[nope] tube=1L1B turn=p1");           // no such item
  // A seat that is out cannot be to move while the round is still running.
  parseFails("p1=0/2 p2=2/2 p3=2/2 tube=1L1B turn=p1");
  parseFails("p1=2/2 p1=3/3 p2=2/2 tube=1L1B turn=p1");                   // seat given twice
  parseFails("p1=2/2 p2=2/2 tube=1L1B tube=2L2B turn=p1");                // tube given twice
  parseFails("p1=2/2 p2=2/2 tube=1L1B turn=p1 dir=cww");                  // dir typo
  parseFails("p1=300/300 p2=2/2 tube=1L1B turn=p1");                      // charges that would wrap
  parseFails("p1=2/2 p2=2/2 tube=1L3B turn=p1 known=p1:2147483648L");     // offset that would wrap
  parseFails("p1=2/2 p2=2/2 tube=1L1B turn=p1 known=p1:0L known=p2:0B");  // two answers, one shell
  parseFails("p1=2/2 p2=2/2 tube=1L1B turn=p1 inverted known=p1:0L");     // seen and inverted
  parseFails("p1=2/2 p2=2/2 tube=0L0B turn=p1 inverted");                 // nothing to invert
}

TEST(Notation, AFinishedRoundMayLeaveTheTurnOnTheSeatThatDied) {
  // This is what the engine produces after a fatal shot, so the notation has to
  // be able to say it.
  const GameState state = parseOk("p1=0/2 p2=2/2 tube=1L1B turn=p1");
  EXPECT_TRUE(state.roundOver());
  EXPECT_EQ(state.soleSurvivor(), 1);
}

TEST(Notation, CarriesTheFlagsThatDecideWhoMayBeRestrained) {
  // skipConsumed and cuffUsedThisTurn change which moves are legal, so a
  // printed position that dropped them parsed back into a different game.
  const GameState state = parseOk("p1=2/2[cuff] p2=2/2 tube=1L1B turn=p1 skipped=p2 restraintused");
  EXPECT_TRUE(state.players[1].skipConsumed);
  EXPECT_TRUE(state.cuffUsedThisTurn);
  const std::string printed = notation::print(state);
  EXPECT_NE(printed.find("skipped=p2"), std::string::npos);
  EXPECT_NE(printed.find("restraintused"), std::string::npos);
  EXPECT_TRUE(parseOk(printed) == state);
}

TEST(Notation, AcceptsItemNamesAsWellAsTokens) {
  const GameState state = parseOk("p1=2/2[handsaw,magnifyingglass] p2=2/2 tube=1L1B turn=p1");
  EXPECT_EQ(state.players[0].items[itemIndex(Item::HandSaw)], 1);
  EXPECT_EQ(state.players[0].items[itemIndex(Item::MagnifyingGlass)], 1);
}

TEST(Notation, TheBoardNamesWhatEachSeatKnows) {
  const GameState state = parseOk("p1=2/2[saw] p2=1/2 tube=1L1B turn=p1 known=p1:0L");
  const std::string board = notation::board(state);
  EXPECT_NE(board.find("Hand Saw"), std::string::npos);
  EXPECT_NE(board.find("shell 1 is live"), std::string::npos);
  EXPECT_NE(board.find("2 charges"), std::string::npos);
}

}  // namespace
}  // namespace bsr
