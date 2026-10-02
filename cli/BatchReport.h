#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace bsr {
namespace cli {

/// A batch stops a round that has run this many moves without a winner. A
/// round can run a long way when both seats keep healing.
constexpr int kBatchMoveCap = 400;

/// The 95 percent Wilson score interval for a survival rate of `wins` in
/// `rounds`, as a line. Unlike the plain normal interval it stays inside 0 to
/// 1 and behaves near either end.
inline std::string wilsonLine(int wins, int rounds) {
  const double z = 1.959963984540054;
  const double n = static_cast<double>(rounds);
  const double rate = static_cast<double>(wins) / n;
  const double scale = 1.0 + z * z / n;
  const double centre = (rate + z * z / (2.0 * n)) / scale;
  const double half = z * std::sqrt(rate * (1.0 - rate) / n + z * z / (4.0 * n * n)) / scale;
  std::ostringstream out;
  out << "95 percent Wilson score interval for the survival rate: " << std::fixed
      << std::setprecision(1) << 100.0 * std::max(0.0, centre - half) << " to "
      << 100.0 * std::min(1.0, centre + half) << " percent\n";
  return out.str();
}

/// How often seat 1 survived a batch, one line each, with the Wilson interval
/// when `wilson` is set. A round that reached the move cap has no winner, so it
/// is left out of both the rate and the interval and counted on a line of its
/// own.
inline std::string survivalLines(const char* against, int won, int rounds, int capped,
                                 bool wilson) {
  const int finished = rounds - capped;
  if (finished <= 0) {
    return "no round finished within the " + std::to_string(kBatchMoveCap) + " move cap\n";
  }
  std::ostringstream out;
  out << "seat 1 " << against << " survived " << won << " of " << finished
      << (capped > 0 ? " finished rounds, " : " rounds, ") << std::fixed << std::setprecision(1)
      << (100.0 * static_cast<double>(won) / static_cast<double>(finished)) << " percent\n";
  if (wilson) out << wilsonLine(won, finished);
  if (capped > 0) {
    out << capped << " rounds reached the " << kBatchMoveCap
        << " move cap without a winner and are left out of the rate and the interval\n";
  }
  return out.str();
}

}  // namespace cli
}  // namespace bsr
