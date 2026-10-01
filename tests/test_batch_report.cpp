/// The lines a batch of rounds ends with: how often seat 1 survived, and what
/// happens to rounds that reached the move cap without a winner.

#include <gtest/gtest.h>

#include <string>

#include "cli/BatchReport.h"

namespace bsr {
namespace {

TEST(BatchReport, WithNoCappedRoundTheLinesAreTheUsualOnes) {
  EXPECT_EQ(cli::survivalLines("(solver, against the scripted dealer)", 19, 20, 0, true),
            "seat 1 (solver, against the scripted dealer) survived 19 of 20 rounds, 95.0 percent\n"
            "95 percent Wilson score interval for the survival rate: 76.4 to 99.1 percent\n");
  EXPECT_EQ(cli::survivalLines("(solver, against the solver)", 7, 10, 0, false),
            "seat 1 (solver, against the solver) survived 7 of 10 rounds, 70.0 percent\n");
}

TEST(BatchReport, CappedRoundsAreLeftOutOfTheRate) {
  EXPECT_EQ(cli::survivalLines("(solver, against the solver)", 7, 10, 2, false),
            "seat 1 (solver, against the solver) survived 7 of 8 finished rounds, 87.5 percent\n"
            "2 rounds reached the 400 move cap without a winner and are left out of the rate "
            "and the interval\n");
}

TEST(BatchReport, TheIntervalIsOverTheFinishedRounds) {
  EXPECT_EQ(cli::survivalLines("(solver, against the scripted dealer)", 19, 21, 1, true),
            "seat 1 (solver, against the scripted dealer) survived 19 of 20 finished rounds, "
            "95.0 percent\n" +
                cli::wilsonLine(19, 20) +
                "1 rounds reached the 400 move cap without a winner and are left out of the rate "
                "and the interval\n");
}

TEST(BatchReport, ABatchWithNoFinishedRoundSaysSo) {
  EXPECT_EQ(cli::survivalLines("(solver, against the solver)", 0, 3, 3, false),
            "no round finished within the 400 move cap\n");
}

}  // namespace
}  // namespace bsr
