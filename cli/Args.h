#pragma once

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>

namespace bsr {
namespace cli {

/// Read a whole number, refusing anything that is not all digits and anything
/// outside the range the caller can use. The standard shortcuts for this are
/// undefined out of range and cannot report a bad string at all, so a mistyped
/// option used to be silently replaced by whatever it wrapped to.
inline bool parseWholeNumber(const std::string& text, long low, long high, long* out) {
  if (text.empty() || text.size() > 18) return false;
  for (char c : text) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  errno = 0;
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || *end != '\0') return false;
  if (value < low || value > high) return false;
  *out = value;
  return true;
}

/// The value that follows a flag. Fails when the flag is last on the line or is
/// followed by another flag, rather than quietly swallowing it.
inline bool nextValue(int argc, char** argv, int* index, const std::string& flag,
                      std::string* out) {
  if (*index + 1 >= argc || argv[*index + 1][0] == '-') {
    std::cerr << flag << " needs a value\n";
    return false;
  }
  ++*index;
  *out = argv[*index];
  return true;
}

/// A seat named the way every command names one: p1, p2, and so on, counting
/// from one, or self and me for the seat to move. Returns the seat's index,
/// counting from zero. The digits are checked before the range is, so a number
/// too large for an int is refused rather than wrapped into a seat that is
/// really at the table.
inline bool parseSeatToken(const std::string& text, int playerCount, int currentSeat, int* seat) {
  if (text == "self" || text == "me") {
    *seat = currentSeat;
    return true;
  }
  if (text.size() >= 2 && (text[0] == 'p' || text[0] == 'P')) {
    long value = 0;
    if (parseWholeNumber(text.substr(1), 1, playerCount, &value)) {
      *seat = static_cast<int>(value) - 1;
      return true;
    }
  }
  return false;
}

/// A flag whose value is a whole number in a stated range.
inline bool nextNumber(int argc, char** argv, int* index, const std::string& flag, long low,
                       long high, long* out) {
  std::string text;
  if (!nextValue(argc, argv, index, flag, &text)) return false;
  if (!parseWholeNumber(text, low, high, out)) {
    std::cerr << flag << " takes a number between " << low << " and " << high << ", not " << text
              << "\n";
    return false;
  }
  return true;
}

}  // namespace cli
}  // namespace bsr
