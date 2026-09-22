/// The command line's own parsing. Every numeric field a person can type used
/// to go through a shortcut that is undefined out of range and reports nothing
/// on a bad string, so a number too large for an int wrapped into a number that
/// passed the range check that followed it. A seat that was not at the table
/// then edited a seat that was.

#include <gtest/gtest.h>

#include <string>

#include "cli/Args.h"

namespace bsr {
namespace {

using cli::parseSeatToken;
using cli::parseWholeNumber;

TEST(Args, AWholeNumberIsDigitsAndNothingElse) {
  long value = -1;
  EXPECT_TRUE(parseWholeNumber("12", 1, 20, &value));
  EXPECT_EQ(value, 12);
  EXPECT_TRUE(parseWholeNumber("007", 1, 20, &value));
  EXPECT_EQ(value, 7);

  EXPECT_FALSE(parseWholeNumber("", 1, 20, &value));
  EXPECT_FALSE(parseWholeNumber("1zzz", 1, 20, &value)) << "trailing junk used to be ignored";
  EXPECT_FALSE(parseWholeNumber(" 3", 1, 20, &value));
  EXPECT_FALSE(parseWholeNumber("-5", 1, 20, &value));
  EXPECT_FALSE(parseWholeNumber("3.5", 1, 20, &value));
}

TEST(Args, ARangeIsCheckedOnTheNumberThatWasTyped) {
  long value = -1;
  EXPECT_FALSE(parseWholeNumber("0", 1, 20, &value));
  EXPECT_FALSE(parseWholeNumber("21", 1, 20, &value));
  EXPECT_TRUE(parseWholeNumber("1", 1, 20, &value));
  EXPECT_TRUE(parseWholeNumber("20", 1, 20, &value));

  // The defect this file exists for: 2^32 + 1 truncates to 1 in an int, and the
  // range check that followed the truncation then let it through as seat one.
  EXPECT_FALSE(parseWholeNumber("4294967297", 1, 4, &value));
  EXPECT_FALSE(parseWholeNumber("8589934594", 1, 4, &value));
  // Larger than any long, so the conversion itself has to report the failure.
  EXPECT_FALSE(parseWholeNumber("99999999999999999999999", 1, 4, &value));
}

TEST(Args, ASeatTokenNamesASeatThatIsActuallyAtTheTable) {
  int seat = -1;
  EXPECT_TRUE(parseSeatToken("p1", 2, 1, &seat));
  EXPECT_EQ(seat, 0) << "seats are typed from one and stored from zero";
  EXPECT_TRUE(parseSeatToken("p2", 2, 1, &seat));
  EXPECT_EQ(seat, 1);
  EXPECT_TRUE(parseSeatToken("P2", 2, 1, &seat)) << "upper case is accepted";
  EXPECT_EQ(seat, 1);

  EXPECT_FALSE(parseSeatToken("p3", 2, 1, &seat)) << "only two seats are at this table";
  EXPECT_FALSE(parseSeatToken("p0", 2, 1, &seat));
  EXPECT_FALSE(parseSeatToken("p", 2, 1, &seat));
  EXPECT_FALSE(parseSeatToken("x1", 2, 1, &seat));
  EXPECT_FALSE(parseSeatToken("p1zzz", 2, 1, &seat));
  EXPECT_FALSE(parseSeatToken("p4294967297", 2, 1, &seat))
      << "this used to wrap to 1 and edit seat one";
  EXPECT_FALSE(parseSeatToken("p4294967298", 2, 1, &seat))
      << "this used to wrap to 2 and hand the turn to seat two";
}

TEST(Args, TheSeatToMoveCanBeNamedWithoutItsNumber) {
  int seat = -1;
  EXPECT_TRUE(parseSeatToken("self", 3, 2, &seat));
  EXPECT_EQ(seat, 2);
  EXPECT_TRUE(parseSeatToken("me", 3, 0, &seat));
  EXPECT_EQ(seat, 0);
  EXPECT_FALSE(parseSeatToken("myself", 3, 0, &seat));
}

}  // namespace
}  // namespace bsr
