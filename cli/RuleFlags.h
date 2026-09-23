#pragma once

#include <cerrno>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "cli/Args.h"
#include "engine/Config.h"

namespace bsr {
namespace cli {

/// The rules this engine had to assume, as command line settings.
///
/// docs/RULES.md lists eleven assumptions and names the field that changes
/// each. Until these flags existed, changing one meant editing C++ and
/// rebuilding, so nobody without a compiler could check a rule against their
/// own game. Every answer already prints the assumptions it used, so a flag
/// here and the line under the ranking describe the same thing.
struct RuleSetting {
  const char* flag;
  const char* values;
  const char* meaning;
};

inline const std::vector<RuleSetting>& ruleSettings() {
  static const std::vector<RuleSetting> settings = {
      {"--reload-turn", "keep|p1|dealer", "who acts first after a mid-round reload"},
      {"--saw-survives", "yes|no", "whether a sawed barrel survives a reload"},
      {"--clear-cuffs", "yes|no", "whether a reload releases handcuffs and jammers"},
      {"--items-per-load", "0 to 8", "items dealt to each seat at a reload"},
      {"--item-limit", "1 to 8", "how many items a seat may hold"},
      {"--med-success", "0 to 1", "the chance Expired Medicine works"},
      {"--med-heal", "0 to 8", "charges Expired Medicine returns on success"},
  };
  return settings;
}

inline bool isRuleSetting(const std::string& flag) {
  for (const RuleSetting& setting : ruleSettings()) {
    if (flag == setting.flag) return true;
  }
  return false;
}

/// A probability written the way a person writes one: 0, 1, 0.5, .35. Digits
/// and at most one point, nothing else, and inside [0, 1]. The shortcuts for
/// this return zero on a string that is not a number at all, which would turn
/// a typo into a certainty.
inline bool parseProbability(const std::string& text, double* out) {
  if (text.empty() || text.size() > 24) return false;
  int digits = 0;
  int points = 0;
  for (char c : text) {
    if (c == '.') {
      if (++points > 1) return false;
    } else if (c >= '0' && c <= '9') {
      ++digits;
    } else {
      return false;
    }
  }
  if (digits == 0) return false;
  errno = 0;
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (errno != 0 || end == text.c_str() || *end != '\0') return false;
  if (!(value >= 0.0 && value <= 1.0)) return false;
  *out = value;
  return true;
}

inline bool parseYesNo(const std::string& text, bool* out) {
  if (text == "yes" || text == "y" || text == "true" || text == "on") {
    *out = true;
    return true;
  }
  if (text == "no" || text == "n" || text == "false" || text == "off") {
    *out = false;
    return true;
  }
  return false;
}

/// Apply one setting. Returns false with a sentence saying what was wrong,
/// rather than a usage dump, because these are typed one at a time.
inline bool applyRuleSetting(const std::string& flag, const std::string& value, RuleConfig* config,
                             std::string* error) {
  const std::string shown = flag + " takes ";
  if (flag == "--reload-turn") {
    if (value == "keep") {
      config->reloadTurn = ReloadTurn::KeepCurrent;
    } else if (value == "p1") {
      config->reloadTurn = ReloadTurn::PlayerFirst;
    } else if (value == "dealer" || value == "p2") {
      config->reloadTurn = ReloadTurn::DealerFirst;
    } else {
      *error = shown + "keep, p1 or dealer, not " + value;
      return false;
    }
    return true;
  }
  if (flag == "--saw-survives" || flag == "--clear-cuffs") {
    bool yes = false;
    if (!parseYesNo(value, &yes)) {
      *error = shown + "yes or no, not " + value;
      return false;
    }
    if (flag == "--saw-survives") {
      config->sawSurvivesReload = yes;
    } else {
      config->reloadClearsCuffs = yes;
    }
    return true;
  }
  if (flag == "--med-success") {
    double chance = 0.0;
    if (!parseProbability(value, &chance)) {
      *error = shown + "a probability between 0 and 1, not " + value;
      return false;
    }
    config->medicineSuccess = chance;
    return true;
  }
  long number = 0;
  if (flag == "--items-per-load") {
    if (!parseWholeNumber(value, 0, 8, &number)) {
      *error = shown + "a number between 0 and 8, not " + value;
      return false;
    }
    config->itemsPerLoad = static_cast<std::uint8_t>(number);
    return true;
  }
  if (flag == "--item-limit") {
    if (!parseWholeNumber(value, 1, 8, &number)) {
      *error = shown + "a number between 1 and 8, not " + value;
      return false;
    }
    config->itemLimit = static_cast<std::uint8_t>(number);
    return true;
  }
  if (flag == "--med-heal") {
    if (!parseWholeNumber(value, 0, 8, &number)) {
      *error = shown + "a number between 0 and 8, not " + value;
      return false;
    }
    config->medicineHeal = static_cast<std::uint8_t>(number);
    return true;
  }
  *error = "there is no setting called " + flag;
  return false;
}

/// Apply a list of settings collected while reading the command line.
inline bool applyRuleSettings(const std::vector<std::pair<std::string, std::string>>& settings,
                              RuleConfig* config, std::string* error) {
  for (const std::pair<std::string, std::string>& setting : settings) {
    if (!applyRuleSetting(setting.first, setting.second, config, error)) return false;
  }
  return true;
}

inline std::string ruleSettingsHelp() {
  std::string text = "  Rules that this engine had to assume (docs/RULES.md)\n";
  for (const RuleSetting& setting : ruleSettings()) {
    std::string line = "    ";
    line += setting.flag;
    line += " ";
    line += setting.values;
    line += " ";
    while (line.size() < 34) line += " ";
    line += setting.meaning;
    text += line + "\n";
  }
  return text;
}

}  // namespace cli
}  // namespace bsr
