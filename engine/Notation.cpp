#include "engine/Notation.h"

#include <cctype>
#include <sstream>
#include <vector>

namespace bsr {
namespace notation {
namespace {

std::vector<std::string> split(const std::string& text, char sep) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : text) {
    if (c == sep) {
      if (!current.empty()) parts.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

std::string lower(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

/// "p3" -> 2. Returns false for anything else.
bool parseSeat(const std::string& text, int playerCount, int* seat) {
  if (text.size() < 2 || text[0] != 'p') return false;
  const int value = std::atoi(text.c_str() + 1);
  if (value < 1 || value > playerCount) return false;
  *seat = value - 1;
  return true;
}

bool parseInt(const std::string& text, int* out) {
  if (text.empty()) return false;
  for (char c : text) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  *out = std::atoi(text.c_str());
  return true;
}

}  // namespace

bool parse(const std::string& text, GameState* state, std::string* error) {
  GameState result;
  result.playerCount = 0;
  int highestSeat = -1;
  int turnSeat = 0;
  bool sawed = false;
  bool inverted = false;
  std::vector<std::string> cuffTokens;
  std::vector<std::string> knownTokens;
  std::string turnToken;

  for (const std::string& rawToken : split(text, ' ')) {
    const std::string token = lower(rawToken);
    const std::size_t eq = token.find('=');
    const std::string key = eq == std::string::npos ? token : token.substr(0, eq);
    const std::string value = eq == std::string::npos ? "" : token.substr(eq + 1);

    if (key == "sawed") {
      sawed = true;
    } else if (key == "inverted") {
      inverted = true;
    } else if (key == "dir") {
      result.direction = (value == "ccw" || value == "-") ? -1 : 1;
    } else if (key == "turn") {
      turnToken = value;
    } else if (key == "cuffed") {
      for (const std::string& seat : split(value, ',')) cuffTokens.push_back(seat);
    } else if (key == "known") {
      knownTokens.push_back(value);
    } else if (key == "tube") {
      const std::size_t livePos = value.find('l');
      const std::size_t blankPos = value.find('b');
      if (livePos == std::string::npos || blankPos == std::string::npos || blankPos < livePos) {
        *error = "tube must look like tube=2L3B";
        return false;
      }
      int live = 0;
      int blank = 0;
      if (!parseInt(value.substr(0, livePos), &live) ||
          !parseInt(value.substr(livePos + 1, blankPos - livePos - 1), &blank)) {
        *error = "tube counts must be numbers, as in tube=2L3B";
        return false;
      }
      if (live + blank > kMaxShells) {
        *error = "a tube holds at most " + std::to_string(kMaxShells) + " shells";
        return false;
      }
      result.tube.live = static_cast<std::uint8_t>(live);
      result.tube.blank = static_cast<std::uint8_t>(blank);
    } else if (key.size() >= 2 && key[0] == 'p' &&
               std::isdigit(static_cast<unsigned char>(key[1]))) {
      const int seatNumber = std::atoi(key.c_str() + 1);
      if (seatNumber < 1 || seatNumber > kMaxPlayers) {
        *error = "seats run from p1 to p" + std::to_string(kMaxPlayers);
        return false;
      }
      const int seat = seatNumber - 1;
      highestSeat = std::max(highestSeat, seat);
      std::string charges = value;
      std::string itemList;
      const std::size_t bracket = value.find('[');
      if (bracket != std::string::npos) {
        charges = value.substr(0, bracket);
        const std::size_t close = value.find(']', bracket);
        itemList = value.substr(bracket + 1, close == std::string::npos
                                                 ? std::string::npos
                                                 : close - bracket - 1);
      }
      const std::size_t slash = charges.find('/');
      int hp = 0;
      int maxHp = 0;
      if (slash == std::string::npos) {
        if (!parseInt(charges, &hp)) {
          *error = "charges must look like p1=3/4";
          return false;
        }
        maxHp = hp;
      } else if (!parseInt(charges.substr(0, slash), &hp) ||
                 !parseInt(charges.substr(slash + 1), &maxHp)) {
        *error = "charges must look like p1=3/4";
        return false;
      }
      if (maxHp <= 0 || hp > maxHp) {
        *error = "a seat cannot hold more charges than its maximum";
        return false;
      }
      result.players[seat].hp = static_cast<std::uint8_t>(hp);
      result.players[seat].maxHp = static_cast<std::uint8_t>(maxHp);
      for (const std::string& itemToken : split(itemList, ',')) {
        Item item;
        if (!itemFromToken(itemToken, &item)) {
          *error = "no item is called " + itemToken;
          return false;
        }
        ++result.players[seat].items[itemIndex(item)];
      }
    } else if (!token.empty()) {
      *error = "unrecognised token " + rawToken;
      return false;
    }
  }

  if (highestSeat < 1) {
    *error = "a position needs at least two seats, as in p1=3/4 p2=3/4";
    return false;
  }
  result.playerCount = static_cast<std::uint8_t>(highestSeat + 1);
  for (int seat = 0; seat < result.playerCount; ++seat) {
    if (result.players[seat].maxHp == 0) {
      *error = "seat p" + std::to_string(seat + 1) + " has no charges; every seat between p1 and p" +
               std::to_string(result.playerCount) + " must be given some";
      return false;
    }
  }

  if (!turnToken.empty() && !parseSeat(turnToken, result.playerCount, &turnSeat)) {
    *error = "turn must name a seat, as in turn=p1";
    return false;
  }
  result.current = static_cast<std::uint8_t>(turnSeat);

  for (const std::string& seatToken : cuffTokens) {
    int seat = 0;
    if (!parseSeat(seatToken, result.playerCount, &seat)) {
      *error = "cuffed must name seats, as in cuffed=p2";
      return false;
    }
    result.players[seat].cuffed = true;
  }

  result.tube.sawed = sawed;
  result.tube.chamberInverted = inverted;

  for (const std::string& entry : knownTokens) {
    const std::size_t colon = entry.find(':');
    if (colon == std::string::npos) {
      *error = "known must look like known=p1:0L,2B";
      return false;
    }
    int seat = 0;
    if (!parseSeat(entry.substr(0, colon), result.playerCount, &seat)) {
      *error = "known must name a seat, as in known=p1:0L";
      return false;
    }
    for (const std::string& fact : split(entry.substr(colon + 1), ',')) {
      if (fact.size() < 2) {
        *error = "a known shell looks like 0L or 2B";
        return false;
      }
      int offset = 0;
      if (!parseInt(fact.substr(0, fact.size() - 1), &offset)) {
        *error = "a known shell looks like 0L or 2B";
        return false;
      }
      const char type = fact.back();
      if (offset >= result.tube.size()) {
        *error = "offset " + std::to_string(offset) + " is past the end of the tube";
        return false;
      }
      if (type != 'l' && type != 'b') {
        *error = "a known shell is L or B";
        return false;
      }
      result.tube.resolve(offset, type == 'l' ? Shell::Live : Shell::Blank,
                          static_cast<std::uint8_t>(1u << seat));
    }
  }

  if (result.tube.unresolvedLive() > result.tube.live ||
      result.tube.unresolvedBlank() > result.tube.blank) {
    *error = "the known shells do not fit the tube";
    return false;
  }
  int knownLive = 0;
  int knownBlank = 0;
  for (int i = 0; i < result.tube.size(); ++i) {
    if (result.tube.truth[i] == Shell::Live) ++knownLive;
    if (result.tube.truth[i] == Shell::Blank) ++knownBlank;
  }
  if (knownLive > result.tube.live || knownBlank > result.tube.blank) {
    *error = "more shells are known than the tube holds";
    return false;
  }
  if (!result.players[result.current].alive()) {
    *error = "the seat to move has no charges left";
    return false;
  }

  *state = result;
  return true;
}

std::string print(const GameState& state) {
  std::ostringstream out;
  for (int seat = 0; seat < state.playerCount; ++seat) {
    const PlayerState& player = state.players[seat];
    out << "p" << (seat + 1) << "=" << static_cast<int>(player.hp) << "/"
        << static_cast<int>(player.maxHp);
    if (player.itemCount() > 0) {
      out << "[";
      bool first = true;
      for (int k = 0; k < kItemCount; ++k) {
        for (int n = 0; n < player.items[k]; ++n) {
          if (!first) out << ",";
          out << itemToken(itemAt(k));
          first = false;
        }
      }
      out << "]";
    }
    out << " ";
  }
  out << "tube=" << static_cast<int>(state.tube.live) << "L" << static_cast<int>(state.tube.blank)
      << "B";
  out << " turn=p" << (static_cast<int>(state.current) + 1);
  if (state.tube.sawed) out << " sawed";
  if (state.tube.chamberInverted) out << " inverted";
  if (state.direction < 0) out << " dir=ccw";
  bool anyCuffed = false;
  for (int seat = 0; seat < state.playerCount; ++seat) {
    if (!state.players[seat].cuffed) continue;
    out << (anyCuffed ? "," : " cuffed=") << "p" << (seat + 1);
    anyCuffed = true;
  }
  for (int seat = 0; seat < state.playerCount; ++seat) {
    bool anyKnown = false;
    for (int i = 0; i < state.tube.size(); ++i) {
      if (!state.tube.knows(seat, i)) continue;
      out << (anyKnown ? "," : " known=p" + std::to_string(seat + 1) + ":") << i
          << (state.tube.truth[i] == Shell::Live ? "L" : "B");
      anyKnown = true;
    }
  }
  return out.str();
}

std::string board(const GameState& state) {
  std::ostringstream out;
  out << "tube: " << static_cast<int>(state.tube.live) << " live, "
      << static_cast<int>(state.tube.blank) << " blank";
  if (state.tube.sawed) out << ", barrel sawed";
  if (state.tube.chamberInverted) out << ", chamber inverted";
  out << "\n";
  for (int seat = 0; seat < state.playerCount; ++seat) {
    const PlayerState& player = state.players[seat];
    out << (seat == state.current ? "> " : "  ") << "p" << (seat + 1) << "  "
        << static_cast<int>(player.hp) << "/" << static_cast<int>(player.maxHp) << " charges";
    if (!player.alive()) out << "  out";
    if (player.cuffed) out << "  cuffed";
    if (player.itemCount() > 0) {
      out << "  items:";
      for (int k = 0; k < kItemCount; ++k) {
        if (player.items[k] == 0) continue;
        out << " " << itemName(itemAt(k));
        if (player.items[k] > 1) out << " x" << static_cast<int>(player.items[k]);
      }
    }
    bool anyKnown = false;
    for (int i = 0; i < state.tube.size(); ++i) {
      if (!state.tube.knows(seat, i)) continue;
      out << (anyKnown ? ", " : "  knows: ") << "shell " << (i + 1) << " is "
          << (state.tube.truth[i] == Shell::Live ? "live" : "blank");
      anyKnown = true;
    }
    out << "\n";
  }
  return out.str();
}

}  // namespace notation
}  // namespace bsr
