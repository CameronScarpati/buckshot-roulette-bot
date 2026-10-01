#include "engine/Notation.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "engine/Dealer.h"

namespace bsr {
namespace notation {
namespace {

/// A seat cannot hold more charges than this. The real game never comes close;
/// the cap exists so that a typo is refused rather than wrapped around.
constexpr int kMaxCharges = 64;
/// A seat makes at most this many phone reads in one load: each read spends an
/// item, and a seat holds at most eight.
constexpr int kMaxReadsPerSeat = kMaxItemsPerSeat;

const char* const kPhonedShape = "phoned must look like phoned=p2@5 or phoned=p2@5,4";
const char* const kDealerShape =
    "dealer must be seen, believes:B, aim:self or aim:p1, optionally followed by med, as in "
    "dealer=seen,med";

/// The part of the dealer's memory a `dealer=` token names before the position
/// around it is known.
enum class Core : std::uint8_t { None, Seen, BelievesBlank, AimSelf, AimPlayer };

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

/// Parse a non-negative integer, refusing anything that is not all digits and
/// anything that would not survive the conversion. Silent overflow here used to
/// turn a long offset into a negative one, which then indexed off the front of
/// the tube.
bool parseInt(const std::string& text, int* out) {
  if (text.empty() || text.size() > 9) return false;
  for (char c : text) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  errno = 0;
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || *end != '\0') return false;
  if (value < 0 || value > 1000000) return false;
  *out = static_cast<int>(value);
  return true;
}

/// "p3" -> 2. Returns false for anything else.
bool parseSeat(const std::string& text, int playerCount, int* seat) {
  if (text.size() < 2 || text[0] != 'p') return false;
  int value = 0;
  if (!parseInt(text.substr(1), &value)) return false;
  if (value < 1 || value > playerCount) return false;
  *seat = value - 1;
  return true;
}

/// Read `dealer=` after the equals sign: one core, optionally followed by med,
/// or med alone.
bool parseDealer(const std::string& value, Core* core, bool* med, std::string* error) {
  const std::vector<std::string> parts = split(value, ',');
  if (parts.empty()) {
    *error = kDealerShape;
    return false;
  }
  for (const std::string& part : parts) {
    Core named = Core::None;
    if (part == "seen") {
      named = Core::Seen;
    } else if (part == "believes:b") {
      named = Core::BelievesBlank;
    } else if (part == "aim:self") {
      named = Core::AimSelf;
    } else if (part == "aim:p1") {
      named = Core::AimPlayer;
    } else if (part == "believes:l") {
      *error =
          "dealer=believes:L cannot happen: the dealer never drinks a Beer on a shell it saw was "
          "live";
      return false;
    } else if (part != "med") {
      *error = kDealerShape;
      return false;
    }
    if (named == Core::None) {
      if (*med) {
        *error = kDealerShape;
        return false;
      }
      *med = true;
      continue;
    }
    if (*core != Core::None) {
      *error = "dealer names at most one of seen, believes:B, aim:self and aim:p1";
      return false;
    }
    if (*med) {
      // The core comes first, as in dealer=seen,med.
      *error = kDealerShape;
      return false;
    }
    *core = named;
  }
  return true;
}

/// Read `phoned=` after the equals sign: a seat, then the tube sizes at which
/// it used a phone.
bool parsePhoned(const std::string& value, int* seatNumber, std::vector<int>* sizes,
                 std::string* error) {
  const std::size_t at = value.find('@');
  if (at == std::string::npos || value.size() < 2 || value[0] != 'p' ||
      !parseInt(value.substr(1, at - 1), seatNumber) || *seatNumber < 1 ||
      *seatNumber > kMaxPlayers) {
    *error = kPhonedShape;
    return false;
  }
  const std::string list = value.substr(at + 1);
  const std::vector<std::string> parts = split(list, ',');
  if (parts.empty() || list.front() == ',' || list.back() == ',' ||
      list.find(",,") != std::string::npos) {
    *error = kPhonedShape;
    return false;
  }
  for (const std::string& part : parts) {
    int size = 0;
    if (!parseInt(part, &size)) {
      *error = kPhonedShape;
      return false;
    }
    if (size < 2 || size > kMaxShells) {
      *error = "a phone read names a tube of 2 to 8 shells";
      return false;
    }
    sizes->push_back(size);
  }
  if (static_cast<int>(sizes->size()) > kMaxReadsPerSeat) {
    *error = "a seat makes at most 8 phone reads in one load";
    return false;
  }
  return true;
}

/// Everything `parse` and `parsePosition` share. With `whole` unset, the tokens
/// that only a whole position carries are refused.
bool parseText(const std::string& text, bool whole, Position* position, std::string* error) {
  GameState result;
  result.playerCount = 0;
  int highestSeat = -1;
  int turnSeat = 0;
  bool sawed = false;
  bool inverted = false;
  bool restraintUsed = false;
  bool tubeGiven = false;
  bool turnGiven = false;
  bool dirGiven = false;
  IndexedArray<bool, kMaxPlayers> seatGiven{};
  std::vector<std::string> cuffTokens;
  std::vector<std::string> skippedTokens;
  std::vector<std::string> knownTokens;
  std::string turnToken;
  bool listCigs = false;
  bool dealerGiven = false;
  Core core = Core::None;
  bool med = false;
  IndexedArray<bool, kMaxPlayers> phonedGiven{};
  std::vector<UnseenRead> reads;

  for (const std::string& rawToken : split(text, ' ')) {
    const std::string token = lower(rawToken);
    const std::size_t eq = token.find('=');
    const std::string key = eq == std::string::npos ? token : token.substr(0, eq);
    const std::string value = eq == std::string::npos ? "" : token.substr(eq + 1);

    if ((key == "phoned" || key == "dealer") && !whole) {
      *error = "phoned and dealer are read only where a whole position is expected";
      return false;
    }
    if (key == "sawed") {
      sawed = true;
    } else if (key == "listcigs") {
      listCigs = true;
    } else if (key == "dealer") {
      if (dealerGiven) {
        *error = "dealer is given twice";
        return false;
      }
      dealerGiven = true;
      if (!parseDealer(value, &core, &med, error)) return false;
    } else if (key == "phoned") {
      int seatNumber = 0;
      std::vector<int> sizes;
      if (!parsePhoned(value, &seatNumber, &sizes, error)) return false;
      if (phonedGiven[seatNumber - 1]) {
        *error = "phoned is given twice for p" + std::to_string(seatNumber);
        return false;
      }
      phonedGiven[seatNumber - 1] = true;
      std::sort(sizes.begin(), sizes.end(), [](int a, int b) { return a > b; });
      for (int size : sizes) {
        UnseenRead read;
        read.seat = static_cast<std::uint8_t>(seatNumber - 1);
        read.sizeAtUse = static_cast<std::uint8_t>(size);
        reads.push_back(read);
      }
    } else if (key == "inverted") {
      inverted = true;
    } else if (key == "restraintused") {
      restraintUsed = true;
    } else if (key == "dir") {
      if (dirGiven) {
        *error = "dir is given twice";
        return false;
      }
      dirGiven = true;
      if (value == "ccw" || value == "-") {
        result.direction = -1;
      } else if (value == "cw" || value == "+") {
        result.direction = 1;
      } else {
        *error = "dir must be cw or ccw";
        return false;
      }
    } else if (key == "turn") {
      if (turnGiven) {
        *error = "turn is given twice";
        return false;
      }
      turnGiven = true;
      turnToken = value;
    } else if (key == "cuffed") {
      for (const std::string& seat : split(value, ',')) cuffTokens.push_back(seat);
    } else if (key == "skipped") {
      for (const std::string& seat : split(value, ',')) skippedTokens.push_back(seat);
    } else if (key == "known") {
      knownTokens.push_back(value);
    } else if (key == "tube") {
      if (tubeGiven) {
        *error = "tube is given twice";
        return false;
      }
      tubeGiven = true;
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
      int seatNumber = 0;
      if (!parseInt(key.substr(1), &seatNumber) || seatNumber < 1 || seatNumber > kMaxPlayers) {
        *error = "seats run from p1 to p" + std::to_string(kMaxPlayers);
        return false;
      }
      const int seat = seatNumber - 1;
      if (seatGiven[seat]) {
        *error = "seat p" + std::to_string(seatNumber) + " is given twice";
        return false;
      }
      seatGiven[seat] = true;
      highestSeat = std::max(highestSeat, seat);
      std::string charges = value;
      std::string itemList;
      const std::size_t bracket = value.find('[');
      if (bracket != std::string::npos) {
        charges = value.substr(0, bracket);
        const std::size_t close = value.find(']', bracket);
        itemList = value.substr(
            bracket + 1, close == std::string::npos ? std::string::npos : close - bracket - 1);
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
      if (maxHp <= 0 || maxHp > kMaxCharges) {
        *error = "a seat holds between 1 and " + std::to_string(kMaxCharges) + " charges";
        return false;
      }
      if (hp > maxHp) {
        *error = "a seat cannot hold more charges than its maximum";
        return false;
      }
      result.players[seat].hp = static_cast<std::uint8_t>(hp);
      result.players[seat].maxHp = static_cast<std::uint8_t>(maxHp);
      Hand& hand = result.players[seat].hand;
      for (const std::string& itemToken : split(itemList, ',')) {
        Item item;
        if (!itemFromToken(itemToken, &item)) {
          *error = "no item is called " + itemToken;
          return false;
        }
        if (hand.size() >= kMaxItemsPerSeat) {
          *error = "a seat holds at most 8 items";
          return false;
        }
        hand.append(item);
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
    if (!seatGiven[seat]) {
      *error = "seat p" + std::to_string(seat + 1) + " is missing; every seat between p1 and p" +
               std::to_string(result.playerCount) + " must be given";
      return false;
    }
  }

  if (listCigs && result.playerCount != 2) {
    *error = "listcigs and dealer need exactly two seats";
    return false;
  }
  result.dealerListCigs = listCigs;

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
  for (const std::string& seatToken : skippedTokens) {
    int seat = 0;
    if (!parseSeat(seatToken, result.playerCount, &seat)) {
      *error = "skipped must name seats, as in skipped=p2";
      return false;
    }
    result.players[seat].skipConsumed = true;
  }

  result.tube.sawed = sawed;
  result.cuffUsedThisTurn = restraintUsed;

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
      if (offset < 0 || offset >= result.tube.size()) {
        *error = "offset " + std::to_string(offset) + " is past the end of the tube";
        return false;
      }
      if (type != 'l' && type != 'b') {
        *error = "a known shell is L or B";
        return false;
      }
      const Shell shell = type == 'l' ? Shell::Live : Shell::Blank;
      if (result.tube.truth[offset] != Shell::Unknown && result.tube.truth[offset] != shell) {
        *error = "shell " + std::to_string(offset + 1) + " is named as both live and blank";
        return false;
      }
      result.tube.resolve(offset, shell, static_cast<std::uint8_t>(1u << seat));
    }
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

  if (inverted) {
    if (result.tube.empty()) {
      *error = "an empty tube has no chamber to invert";
      return false;
    }
    if (result.tube.truth[0] != Shell::Unknown) {
      // The flag only means anything while the chamber is still an unresolved
      // draw. Once a seat has seen the chamber, an inversion has a definite
      // result, so the position should name the type it fires as.
      *error =
          "a chamber a seat has already seen cannot also be marked inverted; give the type it "
          "fires as";
      return false;
    }
    result.tube.chamberInverted = true;
  }

  // A round that is already decided may leave the turn on a seat that is out,
  // which is exactly what the engine produces after a fatal shot.
  if (!result.players[result.current].alive() && !result.roundOver()) {
    *error = "the seat to move has no charges left";
    return false;
  }

  for (const UnseenRead& read : reads) {
    if (read.seat >= result.playerCount) {
      *error = kPhonedShape;
      return false;
    }
    if (read.sizeAtUse < result.tube.size()) {
      *error = "a phone read names a tube at least as large as the one in the position";
      return false;
    }
  }

  Position out;
  out.state = result;
  out.unseenReads = reads;
  if (dealerGiven) {
    dealer::Memory& memory = out.dealerMemory;
    memory.usedMedicine = med;
    switch (core) {
      case Core::None:
        break;
      case Core::Seen:
        // The chamber the dealer saw is the one the position pins for p2. A
        // chamber it has not seen is refused below.
        memory.knows = true;
        memory.known = result.tube.knows(1, 0) ? result.tube.truth[0] : Shell::Unknown;
        memory.target = memory.known == Shell::Live ? dealer::Target::Player : dealer::Target::Self;
        break;
      case Core::BelievesBlank:
        memory.knows = true;
        memory.known = Shell::Blank;
        memory.target = dealer::Target::Self;
        break;
      case Core::AimSelf:
        memory.target = dealer::Target::Self;
        break;
      case Core::AimPlayer:
        memory.target = dealer::Target::Player;
        break;
    }
    if (result.playerCount != 2) {
      *error = "listcigs and dealer need exactly two seats";
      return false;
    }
    if (!dealer::checkMemory(out, error)) return false;
  }

  *position = out;
  return true;
}

/// The `dealer=` text for a memory, or empty for the memory a turn starts with.
std::string dealerToken(const Position& position) {
  const dealer::Memory& memory = position.dealerMemory;
  const Tube& tube = position.state.tube;
  std::string core;
  if (memory.knows && memory.known == Shell::Live) {
    core = "seen";
  } else if (memory.knows && memory.known == Shell::Blank) {
    core = tube.knows(1, 0) && tube.truth[0] == Shell::Blank ? "seen" : "believes:B";
  } else if (!memory.knows && memory.target == dealer::Target::Self) {
    core = "aim:self";
  } else if (!memory.knows && memory.target == dealer::Target::Player) {
    core = "aim:p1";
  }
  if (memory.usedMedicine) core += core.empty() ? "med" : ",med";
  return core.empty() ? core : "dealer=" + core;
}

}  // namespace

bool parse(const std::string& text, GameState* state, std::string* error) {
  Position position;
  if (!parseText(text, false, &position, error)) return false;
  *state = position.state;
  return true;
}

bool parsePosition(const std::string& text, Position* position, std::string* error) {
  return parseText(text, true, position, error);
}

std::string printPosition(const Position& position) {
  std::string out = print(position.state);
  for (int seat = 0; seat < position.state.playerCount; ++seat) {
    std::vector<int> sizes;
    for (const UnseenRead& read : position.unseenReads) {
      if (read.seat == seat) sizes.push_back(read.sizeAtUse);
    }
    if (sizes.empty()) continue;
    std::sort(sizes.begin(), sizes.end(), [](int a, int b) { return a > b; });
    out += " phoned=p" + std::to_string(seat + 1) + "@";
    for (std::size_t i = 0; i < sizes.size(); ++i) {
      if (i > 0) out += ",";
      out += std::to_string(sizes[i]);
    }
  }
  const std::string memory = dealerToken(position);
  if (!memory.empty()) out += " " + memory;
  return out;
}

std::string print(const GameState& state) {
  std::ostringstream out;
  for (int seat = 0; seat < state.playerCount; ++seat) {
    const PlayerState& player = state.players[seat];
    out << "p" << (seat + 1) << "=" << static_cast<int>(player.hp) << "/"
        << static_cast<int>(player.maxHp);
    if (player.itemCount() > 0) {
      out << "[";
      for (int i = 0; i < player.hand.size(); ++i) {
        if (i > 0) out << ",";
        out << itemToken(player.hand.at[static_cast<std::size_t>(i)]);
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
  if (state.cuffUsedThisTurn) out << " restraintused";
  if (state.direction < 0) out << " dir=ccw";
  bool anyCuffed = false;
  for (int seat = 0; seat < state.playerCount; ++seat) {
    if (!state.players[seat].cuffed) continue;
    out << (anyCuffed ? "," : " cuffed=") << "p" << (seat + 1);
    anyCuffed = true;
  }
  bool anySkipped = false;
  for (int seat = 0; seat < state.playerCount; ++seat) {
    if (!state.players[seat].skipConsumed) continue;
    out << (anySkipped ? "," : " skipped=") << "p" << (seat + 1);
    anySkipped = true;
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
  if (state.dealerListCigs) out << " listcigs";
  return out.str();
}

std::string board(const GameState& state, std::uint8_t untyped) {
  std::ostringstream out;
  out << "tube: " << static_cast<int>(state.tube.live) << " live, "
      << static_cast<int>(state.tube.blank) << " blank";
  if (state.tube.sawed) out << ", barrel sawed";
  if (state.tube.chamberInverted) out << ", chamber inverted";
  out << "\n";
  // A finished round has nobody to move, and a seat that is out has no turn
  // to lose. With more than two seats the restraint is a Jammer.
  const bool playing = !state.roundOver();
  const char* restrained = state.playerCount > 2 ? "  jammed" : "  cuffed";
  for (int seat = 0; seat < state.playerCount; ++seat) {
    const PlayerState& player = state.players[seat];
    const bool marked = playing && player.alive();
    out << (marked && seat == state.current ? "> " : "  ") << "p" << (seat + 1) << "  "
        << static_cast<int>(player.hp) << "/" << static_cast<int>(player.maxHp) << " charges";
    if (!player.alive()) out << "  out";
    if (marked && player.cuffed) out << restrained;
    if (marked && player.skipConsumed) out << "  lost a turn";
    if (player.itemCount() > 0) {
      // In the order the seat received them, with the copy named where the
      // type sits in more than one place, as moves name it.
      out << "  items: ";
      for (int i = 0; i < player.hand.size(); ++i) {
        const Item item = player.hand.at[static_cast<std::size_t>(i)];
        if (i > 0) out << ", ";
        out << itemName(item);
        if (player.hand.runs(item) > 1) out << " #" << (player.hand.ordinalAt(i) + 1);
      }
    }
    bool anyKnown = false;
    for (int i = 0; i < state.tube.size(); ++i) {
      if (!state.tube.knows(seat, i)) continue;
      out << (anyKnown ? ", " : "  knows: ") << "shell " << (i + 1);
      if (((untyped >> i) & 1u) != 0u) {
        out << " (unseen by you)";
      } else {
        out << " is " << (state.tube.truth[i] == Shell::Live ? "live" : "blank");
      }
      anyKnown = true;
    }
    out << "\n";
  }
  return out.str();
}

}  // namespace notation
}  // namespace bsr
