/// The rules this engine had to assume are settings, and until they could be
/// typed on a command line, checking one against a real game meant editing C++
/// and rebuilding. These tests cover the parsing of those settings, including
/// the values that must be refused: a probability outside zero to one, or a
/// count the engine cannot hold, would describe a game that cannot happen.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "cli/RuleFlags.h"

namespace bsr {
namespace {

using cli::applyRuleSetting;
using cli::applyRuleSettings;
using cli::isRuleSetting;
using cli::parseProbability;

RuleConfig applied(const std::string& flag, const std::string& value) {
  RuleConfig config;
  std::string error;
  EXPECT_TRUE(applyRuleSetting(flag, value, &config, &error)) << error;
  return config;
}

void refused(const std::string& flag, const std::string& value) {
  RuleConfig config;
  const RuleConfig before = config;
  std::string error;
  EXPECT_FALSE(applyRuleSetting(flag, value, &config, &error)) << flag << " " << value;
  EXPECT_FALSE(error.empty()) << "a refusal has to say what was wrong";
  EXPECT_EQ(config.reloadTurn, before.reloadTurn) << "a refused setting changes nothing";
  EXPECT_EQ(config.medicineSuccess, before.medicineSuccess);
  EXPECT_EQ(config.itemsPerLoad, before.itemsPerLoad);
}

TEST(RuleFlags, EverySettingReachesTheFieldItNames) {
  EXPECT_EQ(applied("--reload-turn", "keep").reloadTurn, ReloadTurn::KeepCurrent);
  EXPECT_EQ(applied("--reload-turn", "p1").reloadTurn, ReloadTurn::PlayerFirst);
  EXPECT_EQ(applied("--reload-turn", "dealer").reloadTurn, ReloadTurn::DealerFirst);
  EXPECT_EQ(applied("--reload-turn", "p2").reloadTurn, ReloadTurn::DealerFirst);

  EXPECT_TRUE(applied("--saw-survives", "yes").sawSurvivesReload);
  EXPECT_FALSE(applied("--saw-survives", "no").sawSurvivesReload);
  EXPECT_FALSE(applied("--clear-cuffs", "no").reloadClearsCuffs);
  EXPECT_TRUE(applied("--clear-cuffs", "yes").reloadClearsCuffs);

  EXPECT_EQ(applied("--items-per-load", "4").itemsPerLoad, 4);
  EXPECT_EQ(applied("--items-per-load", "0").itemsPerLoad, 0);
  EXPECT_EQ(applied("--item-limit", "3").itemLimit, 3);
  EXPECT_EQ(applied("--med-heal", "1").medicineHeal, 1);
  EXPECT_DOUBLE_EQ(applied("--med-success", "0.35").medicineSuccess, 0.35);
  EXPECT_DOUBLE_EQ(applied("--med-success", "1").medicineSuccess, 1.0);
  EXPECT_DOUBLE_EQ(applied("--med-success", "0").medicineSuccess, 0.0);
}

TEST(RuleFlags, TheItemCountTakesARangeBecauseTheGameRedrawsIt) {
  // Double or Nothing draws 1 to 5 items at every load, so the flag holds both
  // ends. A solved reload deals the middle of the range, rounded up.
  const RuleConfig range = applied("--items-per-load", "1-5");
  EXPECT_EQ(range.itemsPerLoad, 1);
  EXPECT_EQ(range.itemsPerLoadMax, 5);
  EXPECT_EQ(range.itemsDealtPerLoad(), 3);

  // A bare number is the same range with both ends equal, which is what the
  // flag meant before ranges existed.
  const RuleConfig fixed = applied("--items-per-load", "4");
  EXPECT_EQ(fixed.itemsPerLoad, 4);
  EXPECT_EQ(fixed.itemsPerLoadMax, 4);
  EXPECT_EQ(fixed.itemsDealtPerLoad(), 4);

  // Both ends the same is a fixed deal however it was typed, and a range that
  // starts and ends at zero deals nothing.
  EXPECT_EQ(applied("--items-per-load", "2-2").itemsDealtPerLoad(), 2);
  EXPECT_EQ(applied("--items-per-load", "0").itemsDealtPerLoad(), 0);

  // The line every answer carries names the range and the count it used, so
  // the rounding is never hidden behind one number.
  RuleConfig shown = RuleConfig::doubleOrNothing(4);
  EXPECT_NE(shown.describe().find("1 to 5 items dealt per load, modelled at 3"),
            std::string::npos)
      << shown.describe();
  EXPECT_NE(applied("--items-per-load", "2").describe().find("2 items dealt per load"),
            std::string::npos);
}

TEST(RuleFlags, AValueTheEngineCannotHoldIsRefused) {
  refused("--reload-turn", "sideways");
  refused("--reload-turn", "");
  refused("--saw-survives", "maybe");
  refused("--clear-cuffs", "1");
  refused("--items-per-load", "9");
  refused("--items-per-load", "-1");
  refused("--items-per-load", "abc");
  refused("--items-per-load", "5-1");
  refused("--items-per-load", "1-9");
  refused("--items-per-load", "1-");
  refused("--items-per-load", "1-2-3");
  refused("--item-limit", "0");
  refused("--med-heal", "9");
  refused("--med-success", "1.5");
  refused("--med-success", "-0.5");
  refused("--med-success", "half");
  refused("--med-success", "0.5.5");
  refused("--no-such-setting", "yes");
}

TEST(RuleFlags, AProbabilityIsDigitsAndAtMostOnePoint) {
  double value = -1.0;
  EXPECT_TRUE(parseProbability("0.5", &value));
  EXPECT_DOUBLE_EQ(value, 0.5);
  EXPECT_TRUE(parseProbability(".25", &value));
  EXPECT_DOUBLE_EQ(value, 0.25);
  EXPECT_TRUE(parseProbability("1.0", &value));
  EXPECT_DOUBLE_EQ(value, 1.0);

  EXPECT_FALSE(parseProbability("", &value));
  EXPECT_FALSE(parseProbability(".", &value)) << "a point with no digits is not a number";
  EXPECT_FALSE(parseProbability("1e-1", &value)) << "exponent notation is not accepted here";
  EXPECT_FALSE(parseProbability("50%", &value));
  EXPECT_FALSE(parseProbability("1.0001", &value));
  EXPECT_FALSE(parseProbability("nan", &value));
  EXPECT_FALSE(parseProbability("inf", &value));
}

TEST(RuleFlags, SettingsApplyInOrderAndStopAtTheFirstBadOne) {
  RuleConfig config;
  std::string error;
  const std::vector<std::pair<std::string, std::string>> good = {
      {"--reload-turn", "keep"}, {"--med-success", "0.4"}, {"--items-per-load", "3"}};
  ASSERT_TRUE(applyRuleSettings(good, &config, &error)) << error;
  EXPECT_EQ(config.reloadTurn, ReloadTurn::KeepCurrent);
  EXPECT_DOUBLE_EQ(config.medicineSuccess, 0.4);
  EXPECT_EQ(config.itemsPerLoad, 3);

  RuleConfig partial;
  const std::vector<std::pair<std::string, std::string>> mixed = {
      {"--reload-turn", "dealer"}, {"--item-limit", "99"}, {"--items-per-load", "1"}};
  EXPECT_FALSE(applyRuleSettings(mixed, &partial, &error));
  EXPECT_EQ(partial.reloadTurn, ReloadTurn::DealerFirst) << "what came before the error stands";
  EXPECT_EQ(partial.itemsPerLoad, RuleConfig().itemsPerLoad) << "what came after it does not";
}

TEST(RuleFlags, TheHelpTextNamesEverySettingThatCanBeTyped) {
  const std::string help = cli::ruleSettingsHelp();
  for (const cli::RuleSetting& setting : cli::ruleSettings()) {
    EXPECT_NE(help.find(setting.flag), std::string::npos) << setting.flag << " is undocumented";
    EXPECT_TRUE(isRuleSetting(setting.flag));
  }
  EXPECT_FALSE(isRuleSetting("--seat")) << "an ordinary option is not a rule setting";
  EXPECT_FALSE(isRuleSetting("--reload-turn=keep")) << "the value is a separate word";
}

/// The setting that docs/RULES.md calls the largest of the assumptions has to
/// reach the printed model line, since that line is what a reader checks.
TEST(RuleFlags, TheAssumptionLineFollowsTheSetting) {
  RuleConfig config = RuleConfig::doubleOrNothing(4);
  const std::string before = config.describe();
  std::string error;
  ASSERT_TRUE(applyRuleSetting("--reload-turn", "keep", &config, &error)) << error;
  const std::string after = config.describe();
  EXPECT_NE(before, after);
  EXPECT_NE(after.find("keeps the turn"), std::string::npos) << after;
}

}  // namespace
}  // namespace bsr
