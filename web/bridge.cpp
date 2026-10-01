// The engine behind the web page: one round at the table and the solver,
// reached through a handful of functions that take plain values and return
// JSON text. It is built with Emscripten and embind (web/CMakeLists.txt) and
// runs in a Web Worker (web/js/engine-worker.js).
//
// Every function answers {"ok": value} or {"error": sentence}. The table
// refuses a call it cannot carry out by throwing, which a build without
// exception handling cannot catch, so every precondition is checked here
// first and a call that would fail comes back as an error instead.

#include <emscripten/bind.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/Config.h"
#include "engine/Items.h"
#include "engine/Notation.h"
#include "engine/Position.h"
#include "engine/Rules.h"
#include "engine/State.h"
#include "engine/Table.h"
#include "engine/Tube.h"
#include "solver/Solver.h"

namespace bsr {
namespace {

constexpr int kPlayer = 0;
constexpr int kDealer = 1;

/// The ranking of seat 1's moves in a round looks through no reloads and
/// stops after this many new positions. A watched round asks for one before
/// every move seat 1 makes. Most take milliseconds; with eight shells and
/// eight items a seat the search reaches the limit in about 6 seconds under
/// Node.
constexpr int kRankReloads = 0;
constexpr long long kRankNodeLimit = 2000000;

/// A written position is searched through at most one reload and stops after
/// this many new positions. Under Node the heaviest positions tried (eight
/// shells, four to eight items a seat, one reload) reached the limit in at
/// most 7.4 seconds with a 207 MB heap, and the README position finishes
/// within it, at 1,544,973 positions.
constexpr int kAdviseMaxReloads = 1;
constexpr long long kAdviseNodeLimit = 1600000;

/// Answers kept for positions asked about again, as when the page replays a
/// round after a change of mode. A full cache is emptied.
constexpr std::size_t kCacheLimit = 512;

/// The typographic apostrophe the page uses in a possessive.
constexpr const char* kApostrophe = "’";

constexpr const char* kNothingToRank = "There is no move to rank right now.";
constexpr const char* kDealerHasNoChoice =
    "The Dealer is to move and follows its script, so it has no choice to rank.";

/// Builds one JSON value as text, in the order it is written. The writer puts
/// in the commas and the colons.
class Json {
 public:
  void openObject() {
    separate();
    out_ += '{';
    fresh_.push_back(true);
  }
  void closeObject() {
    out_ += '}';
    fresh_.pop_back();
  }
  void openArray() {
    separate();
    out_ += '[';
    fresh_.push_back(true);
  }
  void closeArray() {
    out_ += ']';
    fresh_.pop_back();
  }
  void key(const char* name) {
    separate();
    quote(name);
    out_ += ':';
    keyed_ = true;
  }
  void text(const std::string& value) {
    separate();
    quote(value);
  }
  void integer(long long value) {
    separate();
    out_ += std::to_string(value);
  }
  void number(double value) {
    char digits[32];
    std::snprintf(digits, sizeof digits, "%.17g", value);
    separate();
    out_ += digits;
  }
  void boolean(bool value) {
    separate();
    out_ += value ? "true" : "false";
  }
  void null() {
    separate();
    out_ += "null";
  }
  const std::string& str() const { return out_; }

 private:
  void separate() {
    if (keyed_) {
      keyed_ = false;
      return;
    }
    if (fresh_.empty()) return;
    if (!fresh_.back()) out_ += ',';
    fresh_.back() = false;
  }
  void quote(const std::string& value) {
    out_ += '"';
    for (const char c : value) {
      const auto byte = static_cast<unsigned char>(c);
      if (c == '"' || c == '\\') {
        out_ += '\\';
        out_ += c;
      } else if (byte < 0x20) {
        char escaped[8];
        std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(byte));
        out_ += escaped;
      } else {
        out_ += c;
      }
    }
    out_ += '"';
  }

  std::string out_;
  std::vector<bool> fresh_;
  bool keyed_ = false;
};

/// The answer to a call that could not be carried out.
std::string failure(const std::string& sentence) {
  Json json;
  json.openObject();
  json.key("error");
  json.text(sentence);
  json.closeObject();
  return json.str();
}

/// The round the page is playing.
struct Round {
  std::string mode;
  std::string stageLabel;
  std::uint32_t seed = 0;
  bool solverSeat = false;
  std::unique_ptr<Table> table;
};

Round current;
std::unordered_map<std::string, SolveResult> rankings;
std::unordered_map<std::string, SolveResult> answers;

void remember(std::unordered_map<std::string, SolveResult>* cache, const std::string& key,
              const SolveResult& result) {
  if (cache->size() >= kCacheLimit) cache->clear();
  (*cache)[key] = result;
}

/// How the texts name each seat, and the seat they speak to as "you", if any:
/// seat 1 in a round a person plays, and the mover in a written position.
struct Names {
  std::string seat1;
  std::string seat2;
  int addressedSeat = -1;
};

Names roundNames(const Round& round) {
  return Names{round.solverSeat ? "the solver" : "you", "the Dealer",
               round.solverSeat ? -1 : kPlayer};
}

const char* seatId(int seat) {
  return seat == kPlayer ? "p1" : "p2";
}

std::string seatName(int seat, const Names& names) {
  return seat == kPlayer ? names.seat1 : names.seat2;
}

/// Whether the texts speak to `seat` as "you".
bool addressed(int seat, const Names& names) {
  return seat == names.addressedSeat;
}

std::string possessive(int seat, const Names& names) {
  if (addressed(seat, names)) return "your";
  return seatName(seat, names) + kApostrophe + "s";
}

std::string capitalised(std::string text) {
  if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
  return text;
}

const char* shellWord(Shell shell) {
  return shell == Shell::Live ? "live" : "blank";
}

void replaceAll(std::string* text, const std::string& from, const std::string& to) {
  for (std::size_t at = text->find(from); at != std::string::npos;
       at = text->find(from, at + to.size())) {
    text->replace(at, from.size(), to);
  }
}

/// Replace `from` where it stands as a word of its own.
void replaceWord(std::string* text, const std::string& from, const std::string& to) {
  const auto partOfWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
  for (std::size_t at = text->find(from); at != std::string::npos;) {
    const std::size_t end = at + from.size();
    const bool alone = (at == 0 || !partOfWord((*text)[at - 1])) &&
                       (end == text->size() || !partOfWord((*text)[end]));
    if (alone) {
      text->replace(at, from.size(), to);
      at = text->find(from, at + to.size());
    } else {
      at = text->find(from, end);
    }
  }
}

/// The table's narration, in the words the page uses for each seat: its lines
/// joined into one paragraph, the Dealer named with a capital and seat 1 named
/// as the page names it.
std::string pageText(std::string text, const Names& names) {
  for (char& c : text) {
    if (c == '\n') c = ' ';
  }
  replaceAll(&text, "The dealer", "The Dealer");
  replaceAll(&text, "the dealer", "the Dealer");
  replaceWord(&text, "p1's", possessive(kPlayer, names));
  replaceWord(&text, "p1 is", addressed(kPlayer, names) ? "you are" : names.seat1 + " is");
  replaceWord(&text, "p1", names.seat1);
  return capitalised(text);
}

/// The sentences of a solver's assumptions, one per entry.
std::vector<std::string> sentences(std::string text) {
  replaceAll(&text, "The dealer", "The Dealer");
  replaceAll(&text, "the dealer", "the Dealer");
  std::vector<std::string> out;
  std::string sentence;
  for (std::size_t i = 0; i < text.size(); ++i) {
    sentence += text[i];
    const bool ends = text[i] == '.' && i + 2 < text.size() && text[i + 1] == ' ' &&
                      std::isupper(static_cast<unsigned char>(text[i + 2])) != 0;
    if (ends || i + 1 == text.size()) {
      const std::size_t start = sentence.find_first_not_of(' ');
      if (start != std::string::npos) out.push_back(sentence.substr(start));
      sentence.clear();
    }
  }
  return out;
}

bool rulesFor(const std::string& mode, RuleConfig* config, std::string* label) {
  if (mode == "don") {
    *config = RuleConfig::doubleOrNothing();
    *label = "Double or Nothing";
    return true;
  }
  for (int stage = 1; stage <= 3; ++stage) {
    if (mode == "story" + std::to_string(stage)) {
      *config = RuleConfig::storyRound(stage);
      *label = "Story, stage " + std::to_string(stage);
      return true;
    }
  }
  return false;
}

/// The index of `wanted` in `legal`, matching the copy it names when it can
/// and any copy of the same move otherwise, or -1.
int findLegal(const std::vector<Action>& legal, const Action& wanted) {
  for (std::size_t i = 0; i < legal.size(); ++i) {
    if (legal[i] == wanted) return static_cast<int>(i);
  }
  for (std::size_t i = 0; i < legal.size(); ++i) {
    Action same = wanted;
    same.copy = legal[i].copy;
    if (legal[i] == same) return static_cast<int>(i);
  }
  return -1;
}

/// Where a move takes its item from: the tray slot of the mover's copy, the
/// victim's slot for a steal, or -1 for a shot. With no table, as in a written
/// position, a seat's slots are its items in the order written.
int moveSlot(const Action& action, int mover, const GameState& state, const Table* table) {
  const auto slotOf = [&](int seat, Item item, int ordinal) {
    if (table != nullptr) return table->slotOf(seat, item, ordinal);
    return state.players[seat].hand.indexOfCopy(item, ordinal);
  };
  if (action.kind == Action::Kind::Shoot) return -1;
  if (action.isSteal()) return slotOf(action.stealFrom, action.stolen, action.copy);
  if (action.isAdrenalineAlone()) return slotOf(mover, Item::Adrenaline, 0);
  return slotOf(mover, action.item, action.copy);
}

std::string moveLabel(const Action& action, int mover, int slot, const Names& names) {
  const int target = static_cast<int>(action.target);
  if (action.kind == Action::Kind::Shoot) {
    if (target == mover) return addressed(mover, names) ? "Shoot yourself" : "Shoot itself";
    return "Shoot " + seatName(target, names);
  }
  if (action.isAdrenalineAlone()) return "Use Adrenaline and take nothing";
  const std::string where =
      action.named && slot >= 0 ? " in slot " + std::to_string(slot + 1) : std::string();
  if (action.isSteal()) {
    return "Use Adrenaline to take " + possessive(action.stealFrom, names) + " " +
           itemName(action.stolen) + where;
  }
  std::string label = std::string("Use the ") + itemName(action.item) + where;
  if (itemNeedsTarget(action.item)) label += " on " + seatName(target, names);
  return label;
}

void writeMove(Json& json, int id, const Action& action, int mover, const GameState& state,
               const Table& table, const Names& names) {
  const int slot = moveSlot(action, mover, state, &table);
  json.openObject();
  json.key("id");
  json.text(std::to_string(id));
  json.key("kind");
  json.text(action.kind == Action::Kind::Shoot ? "shoot" : "item");
  json.key("label");
  json.text(moveLabel(action, mover, slot, names));
  if (action.kind == Action::Kind::UseItem) {
    json.key("item");
    json.text(itemToken(action.isSteal() ? action.stolen : action.item));
  }
  // A steal is aimed at the seat it takes from, which in two seats is also
  // the one a stolen restraint holds.
  if (action.isSteal()) {
    json.key("target");
    json.text(seatId(action.stealFrom));
  } else if (action.kind == Action::Kind::Shoot ||
             (itemNeedsTarget(action.item) && !action.isAdrenalineAlone())) {
    json.key("target");
    json.text(seatId(action.target));
  }
  if (slot >= 0) {
    json.key("slot");
    json.integer(slot);
  }
  if (action.isSteal()) {
    json.key("from");
    json.text(seatId(action.stealFrom));
  }
  json.closeObject();
}

void writeSeat(Json& json, int seat, const Round& round) {
  const Table& table = *round.table;
  const PlayerState& player = table.state().players[seat];
  json.openObject();
  json.key("id");
  json.text(seatId(seat));
  json.key("name");
  json.text(seat == kDealer ? "Dealer" : round.solverSeat ? "Solver" : "You");
  json.key("charges");
  json.integer(player.hp);
  json.key("max");
  json.integer(player.maxHp);
  json.key("faded");
  json.integer(table.config().healFloor - 1);
  json.key("items");
  json.openArray();
  for (const int item : table.tray(seat)) {
    if (item < 0) {
      json.null();
    } else {
      json.text(itemToken(itemAt(item)));
    }
  }
  json.closeArray();
  json.key("hand");
  json.openArray();
  for (int i = 0; i < player.hand.size(); ++i) {
    json.text(itemToken(player.hand.at[static_cast<std::size_t>(i)]));
  }
  json.closeArray();
  json.key("restraint");
  if (player.cuffed) {
    json.text("cuffed");
  } else if (player.skipConsumed) {
    json.text("lost a turn");
  } else {
    json.null();
  }
  json.closeObject();
}

/// The tube with its counts by the type each shell was loaded as, and each
/// shell's type once some seat has seen it.
void writeTube(Json& json, const Table& table) {
  const Tube& tube = table.state().tube;
  const std::pair<int, int> counts = table.loadedCounts();
  json.openObject();
  json.key("live");
  json.integer(counts.first);
  json.key("blank");
  json.integer(counts.second);
  json.key("total");
  json.integer(counts.first + counts.second);
  json.key("sawed");
  json.boolean(tube.sawed);
  json.key("shells");
  json.openArray();
  for (int offset = 0; offset < tube.size(); ++offset) {
    json.openObject();
    json.key("offset");
    json.integer(offset);
    json.key("known");
    const bool seen = tube.truth[offset] != Shell::Unknown && tube.knownBy[offset] != 0;
    if (seen) {
      json.text(shellWord(tube.truth[offset]));
    } else {
      json.null();
    }
    json.key("knownBy");
    json.openArray();
    for (int seat = 0; seat < table.state().playerCount; ++seat) {
      if (tube.knows(seat, offset)) json.text(seatId(seat));
    }
    json.closeArray();
    json.closeObject();
  }
  json.closeArray();
  json.closeObject();
}

void writeView(Json& json, const Round& round) {
  const Table& table = *round.table;
  const GameState& state = table.state();
  const Step step = table.next();
  const Names names = roundNames(round);
  json.openObject();
  json.key("mode");
  json.text(round.mode);
  json.key("stageLabel");
  json.text(round.stageLabel);
  json.key("seed");
  json.integer(round.seed);
  json.key("seats");
  json.openArray();
  writeSeat(json, kPlayer, round);
  writeSeat(json, kDealer, round);
  json.closeArray();
  json.key("tube");
  writeTube(json, table);
  json.key("toMove");
  if (step == Step::Choose || step == Step::Dealer) {
    json.text(seatId(state.current));
  } else {
    json.null();
  }
  json.key("over");
  json.boolean(step == Step::Over);
  json.key("winner");
  if (step == Step::Over && state.soleSurvivor() >= 0) {
    json.text(seatId(state.soleSurvivor()));
  } else {
    json.null();
  }
  json.key("loadNumber");
  json.integer(table.loadNumber());
  json.key("legal");
  json.openArray();
  if (step == Step::Choose && state.current == kPlayer) {
    const std::vector<Action> legal = table.legal();
    for (std::size_t i = 0; i < legal.size(); ++i) {
      writeMove(json, static_cast<int>(i), legal[i], kPlayer, state, table, names);
    }
  }
  json.closeArray();
  json.key("notation");
  json.text(notation::printPosition(table.view(kPlayer)));
  json.closeObject();
}

/// The shell a Beer racked out, as the table narrated it. The counts give the
/// same answer unless an Inverter flipped a chamber nobody had seen.
Shell rackedShell(const Event& event, const GameState& before, const GameState& after) {
  if (event.text.find("LIVE") != std::string::npos) return Shell::Live;
  if (event.text.find("blank") != std::string::npos) return Shell::Blank;
  const bool liveLeft = after.tube.live < before.tube.live;
  const bool flipped = before.tube.chamberInverted;
  return liveLeft != flipped ? Shell::Live : Shell::Blank;
}

/// What an item event says after "X uses the Item.", from what the table
/// shows every seat. Only a person in seat 1 reads its own results here; any
/// other seat's private result travels in a learned event.
std::string itemText(const Event& event, const std::vector<Event>& batch, const GameState& before,
                     const GameState& after, const Names& names) {
  const int actor = event.seat;
  const bool mine = addressed(actor, names);
  const auto learnedBy = [&](Item item) -> const Event* {
    for (const Event& other : batch) {
      if (other.kind == Event::Kind::Learned && other.seat == actor && other.item == item) {
        return &other;
      }
    }
    return nullptr;
  };
  if (event.item == Item::Adrenaline) {
    if (!event.steal) return "Nothing is taken.";
    const std::string what = possessive(event.target, names) + " " + itemName(event.stolen);
    return mine ? "You take " + what + " and use it now."
                : "It takes " + what + " and uses it now.";
  }
  const PlayerState& was = before.players[actor];
  const PlayerState& now = after.players[actor];
  switch (event.item) {
    case Item::MagnifyingGlass: {
      const Event* seen = learnedBy(Item::MagnifyingGlass);
      if (mine && seen != nullptr && seen->shell != Shell::Unknown) {
        return std::string("The chamber is ") + shellWord(seen->shell) + ".";
      }
      return "Only " + seatName(actor, names) + " sees the shell in the chamber.";
    }
    case Item::Beer:
      return std::string("The racked shell is ") + shellWord(rackedShell(event, before, after)) +
             ".";
    case Item::Cigarettes:
      if (now.hp > was.hp) return "One charge comes back.";
      if (was.hp >= was.maxHp) return "Healing does nothing at full charges.";
      return "Healing does nothing on the last charge in this stage.";
    case Item::ExpiredMedicine: {
      if (now.hp < was.hp) return "It fails, and one charge is lost.";
      const int healed = now.hp - was.hp;
      if (healed <= 0) return "It works, but no charge can come back.";
      return healed == 1 ? "It works: one charge comes back." : "It works: two charges come back.";
    }
    case Item::Handcuffs:
      return capitalised(seatName(event.target, names)) +
             (addressed(event.target, names) ? " are" : " is") +
             " handcuffed and will lose the next turn.";
    case Item::HandSaw:
      return "The barrel is sawed for the next shot.";
    case Item::BurnerPhone: {
      if (before.tube.size() < 2) {
        return "No shell is left past the chamber, so the phone names nothing.";
      }
      if (!mine) return "The phone names one later shell to " + seatName(actor, names) + " only.";
      const Event* heard = learnedBy(Item::BurnerPhone);
      if (heard != nullptr && heard->offset > 0 && heard->shell != Shell::Unknown) {
        return "Shell " + std::to_string(heard->offset + 1) + " is " + shellWord(heard->shell) +
               (before.tube.knows(actor, heard->offset) ? ", which you already knew." : ".");
      }
      return "The phone names a shell you already knew.";
    }
    case Item::Inverter:
      if (addressed(kPlayer, names) && !after.tube.empty() && after.tube.knows(kPlayer, 0)) {
        return std::string("The chamber is flipped. It is now ") + shellWord(after.tube.truth[0]) +
               ".";
      }
      return "The chamber is flipped.";
    case Item::Adrenaline:
    case Item::Jammer:
    case Item::Remote:
      break;
  }
  return std::string("The ") + itemName(event.item) + " is used.";
}

void writeEvent(Json& json, const Event& event, const std::vector<Event>& batch,
                const GameState& before, const GameState& after, const Names& names) {
  json.openObject();
  switch (event.kind) {
    case Event::Kind::Load:
      json.key("kind");
      json.text("load");
      json.key("live");
      json.integer(event.live);
      json.key("blank");
      json.integer(event.blank);
      json.key("dealt");
      json.openObject();
      for (int seat = kPlayer; seat <= kDealer; ++seat) {
        json.key(seatId(seat));
        json.openArray();
        for (const Item item : event.dealtItems[static_cast<std::size_t>(seat)]) {
          json.text(itemToken(item));
        }
        json.closeArray();
      }
      json.closeObject();
      json.key("first");
      json.text(seatId(event.first));
      break;
    case Event::Kind::Shot:
      json.key("kind");
      json.text("shot");
      json.key("by");
      json.text(seatId(event.seat));
      json.key("target");
      json.text(seatId(event.target));
      json.key("shell");
      json.text(shellWord(event.shell));
      json.key("damage");
      json.integer(event.damage);
      break;
    case Event::Kind::Item:
      json.key("kind");
      json.text("item");
      json.key("by");
      json.text(seatId(event.seat));
      json.key("item");
      json.text(itemToken(event.item));
      if (event.target >= 0) {
        json.key("target");
        json.text(seatId(event.target));
      }
      json.key("text");
      json.text(itemText(event, batch, before, after, names));
      break;
    case Event::Kind::Learned:
      json.key("kind");
      json.text("learned");
      json.key("by");
      json.text(seatId(event.seat));
      json.key("offset");
      json.integer(event.offset);
      json.key("shell");
      json.text(shellWord(event.shell));
      json.key("private");
      json.boolean(true);
      break;
    case Event::Kind::Rule:
      json.key("kind");
      json.text("rule");
      json.key("by");
      json.text(seatId(kDealer));
      json.key("text");
      json.text(pageText(event.text, names));
      break;
    case Event::Kind::Skip:
      json.key("kind");
      json.text("skip");
      json.key("seat");
      json.text(seatId(event.seat));
      break;
    case Event::Kind::Over:
      json.key("kind");
      json.text("over");
      json.key("winner");
      if (event.seat >= 0) {
        json.text(seatId(event.seat));
      } else {
        json.null();
      }
      break;
  }
  json.closeObject();
}

/// The events the table logged from `from` on. A learned event that names no
/// shell (a phone that named a shell its user already knew) is left out, and
/// the Dealer's rule comes straight after the item or shot it explains.
std::string stepResult(const Round& round, std::size_t from, const GameState& before) {
  const std::vector<Event>& log = round.table->log();
  std::vector<Event> batch;
  for (std::size_t i = from; i < log.size(); ++i) {
    const Event& event = log[i];
    if (event.kind == Event::Kind::Learned && (event.offset < 0 || event.shell == Shell::Unknown)) {
      continue;
    }
    if (event.kind == Event::Kind::Rule) {
      std::size_t at = batch.size();
      while (at > 0 && batch[at - 1].kind == Event::Kind::Learned) --at;
      batch.insert(batch.begin() + static_cast<std::ptrdiff_t>(at), event);
      continue;
    }
    batch.push_back(event);
  }
  const Names names = roundNames(round);
  const GameState& after = round.table->state();
  Json json;
  json.openObject();
  json.key("ok");
  json.openObject();
  json.key("events");
  json.openArray();
  for (const Event& event : batch) writeEvent(json, event, batch, before, after, names);
  json.closeArray();
  json.key("view");
  writeView(json, round);
  json.closeObject();
  json.closeObject();
  return json.str();
}

/// The solver's ranking of seat 1's moves in the round, from what seat 1 has
/// seen, against the Dealer's script. It reads the table and never draws from
/// its generator.
SolveResult roundRanking(const Round& round) {
  const Position view = round.table->view(kPlayer);
  const std::string key = round.mode + "|" + notation::printPosition(view);
  const auto found = rankings.find(key);
  if (found != rankings.end()) return found->second;
  SolveOptions options;
  options.seat = kPlayer;
  options.opponent = OpponentModel::Dealer;
  options.reloadBudget = kRankReloads;
  options.nodeLimit = kRankNodeLimit;
  const SolveResult result = solve(view, round.table->config(), options);
  remember(&rankings, key, result);
  return result;
}

struct RankedMove {
  std::string id;
  std::string label;
  double win = 0.0;
};

/// A solver's assumptions as sentences, with the sentence that says the search
/// stopped at its node limit set apart, since the page shows it above the
/// moves rather than among the assumptions.
struct Notes {
  std::vector<std::string> assumptions;
  std::string stopped;
};

Notes notesOf(const SolveResult& result) {
  Notes notes{sentences(result.assumptions), std::string()};
  // The solver adds that sentence last, after the search (solver/Solver.cpp).
  if (result.nodeLimitHit && !notes.assumptions.empty()) {
    notes.stopped = notes.assumptions.back();
    notes.assumptions.pop_back();
  }
  return notes;
}

std::string rankingResult(int mover, bool scripted, const std::string& refused,
                          const std::vector<RankedMove>& moves, const Notes& notes) {
  Json json;
  json.openObject();
  json.key("ok");
  json.openObject();
  json.key("mover");
  json.text(seatId(mover));
  json.key("opponent");
  json.text(scripted ? "dealer" : "solver");
  json.key("refused");
  if (refused.empty()) {
    json.null();
  } else {
    json.text(refused);
  }
  json.key("moves");
  json.openArray();
  for (const RankedMove& move : moves) {
    json.openObject();
    json.key("id");
    json.text(move.id);
    json.key("label");
    json.text(move.label);
    // A chance, kept within 0 and 1: the sum over a search's branches can
    // land a rounding step outside.
    json.key("win");
    json.number(std::clamp(move.win, 0.0, 1.0));
    json.closeObject();
  }
  json.closeArray();
  json.key("stopped");
  if (notes.stopped.empty()) {
    json.null();
  } else {
    json.text(notes.stopped);
  }
  json.key("assumptions");
  json.openArray();
  for (const std::string& sentence : notes.assumptions) json.text(sentence);
  json.closeArray();
  json.closeObject();
  json.closeObject();
  return json.str();
}

std::string refusal(int mover, bool scripted, const std::string& why) {
  return rankingResult(mover, scripted, why, {}, Notes{});
}

/// Why the solver would not answer, as a sentence.
std::string notSolved(const SolveResult& result) {
  std::string why = result.assumptions;
  const std::string lead = "Not solved: ";
  if (why.compare(0, lead.size(), lead) == 0) why = why.substr(lead.size());
  return capitalised(why);
}

std::string noRound() {
  return failure("No round has been started.");
}

/// Why seat 1 cannot move now, or empty when it can.
std::string notSeatOnesMove(const Table& table) {
  switch (table.next()) {
    case Step::Over:
      return "The round is over.";
    case Step::Load:
      return "The tube is empty, so the gun is loaded next.";
    case Step::Dealer:
      return "The Dealer is to move.";
    case Step::Choose:
      break;
  }
  return table.state().current == kPlayer ? "" : "Seat 1 is not to move.";
}

/// Whether `slot` holds a copy that `action` may spend: any Adrenaline of the
/// mover for an Adrenaline, or any copy in the run of adjacent copies the
/// action names for another item. A shot takes nothing from the tray.
bool slotFits(const Table& table, const Action& action, int slot) {
  if (action.kind == Action::Kind::Shoot || slot < 0 || slot >= kMaxItemsPerSeat) return false;
  const int seat = table.state().current;
  if (action.item == Item::Adrenaline) {
    return table.tray(seat)[static_cast<std::size_t>(slot)] == itemIndex(Item::Adrenaline);
  }
  const Hand& hand = table.state().players[seat].hand;
  const int first = hand.indexOfCopy(action.item, action.copy);
  if (first < 0) return false;
  int ordinal = action.copy;
  for (int i = first; i < hand.size() && hand.at[static_cast<std::size_t>(i)] == action.item;
       ++i, ++ordinal) {
    if (table.slotOf(seat, action.item, ordinal) == slot) return true;
  }
  return false;
}

/// A move id is the decimal index of the move in the legal list.
int parseMoveId(const std::string& id, std::size_t count) {
  if (id.empty() || id.size() > 3) return -1;
  int index = 0;
  for (const char c : id) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) return -1;
    index = index * 10 + (c - '0');
  }
  return static_cast<std::size_t>(index) < count ? index : -1;
}

// ---------------------------------------------------------------------------
// The functions the worker calls.
// ---------------------------------------------------------------------------

/// Start a round. `charges` is 2, 3 or 4 in Double or Nothing, or 0 to let the
/// seed draw it; the story stages set their own.
std::string newRound(const std::string& mode, double seed, int charges, const std::string& seat) {
  RuleConfig config;
  std::string label;
  if (!rulesFor(mode, &config, &label)) {
    return failure("The mode is one of don, story1, story2 and story3.");
  }
  if (!(seed >= 0.0 && seed <= 4294967295.0) || std::floor(seed) != seed) {
    return failure("The seed is a whole number from 0 to 4294967295.");
  }
  if (mode == "don" && charges != 0 && (charges < 2 || charges > 4)) {
    return failure("Double or Nothing starts each seat on 2, 3 or 4 charges.");
  }
  if (seat != "human" && seat != "solver") {
    return failure("Seat 1 is played by a human or by the solver.");
  }
  TableOptions options;
  options.config = config;
  options.seed = static_cast<std::uint32_t>(seed);
  options.charges = mode == "don" ? charges : 0;
  current.mode = mode;
  current.stageLabel = label;
  current.seed = options.seed;
  current.solverSeat = seat == "solver";
  current.table = std::make_unique<Table>(options);
  Json json;
  json.openObject();
  json.key("ok");
  writeView(json, current);
  json.closeObject();
  return json.str();
}

/// Play seat 1's move `id` from the view's legal list. `slot`, when not -1,
/// names the slot of seat 1's tray the move spends: a copy of its item, or
/// for an Adrenaline move any Adrenaline. Otherwise an item move spends the
/// copy its `slot` names and an Adrenaline move the first Adrenaline.
std::string act(const std::string& id, int slot) {
  if (!current.table) return noRound();
  Table& table = *current.table;
  const std::string blocked = notSeatOnesMove(table);
  if (!blocked.empty()) return failure(blocked);
  const std::vector<Action> legal = table.legal();
  const int index = parseMoveId(id, legal.size());
  if (index < 0) return failure("The move is not one seat 1 may make now.");
  const Action& action = legal[static_cast<std::size_t>(index)];
  int traySlot = -1;
  if (slot != -1) {
    if (!slotFits(table, action, slot)) {
      return failure("The tray slot does not hold an item this move may use.");
    }
    traySlot = slot;
  } else if (action.kind == Action::Kind::UseItem && !action.isSteal()) {
    traySlot = moveSlot(action, kPlayer, table.state(), &table);
  }
  const std::size_t from = table.log().size();
  const GameState before = table.state();
  table.play(action, traySlot);
  return stepResult(current, from, before);
}

/// Take the next step that is not a person's choice: a load, one pass of the
/// Dealer's turn, or the solver's move for seat 1.
std::string advance() {
  if (!current.table) return noRound();
  Table& table = *current.table;
  const std::size_t from = table.log().size();
  const GameState before = table.state();
  switch (table.next()) {
    case Step::Over:
      break;
    case Step::Load:
      table.load();
      break;
    case Step::Dealer:
      table.dealerPass();
      break;
    case Step::Choose: {
      if (!current.solverSeat) {
        return failure("Seat 1 is to move. Call act with one of view.legal.");
      }
      const SolveResult result = roundRanking(current);
      const std::vector<Action> legal = table.legal();
      const int index = result.ranked.empty() ? -1 : findLegal(legal, result.ranked.front().action);
      if (index < 0) return failure("The solver found no move to play.");
      table.play(legal[static_cast<std::size_t>(index)]);
      break;
    }
  }
  return stepResult(current, from, before);
}

/// Rank seat 1's moves in the round. The ids are those of the view's legal
/// list.
std::string rank() {
  if (!current.table) return refusal(kPlayer, true, kNothingToRank);
  const Table& table = *current.table;
  const Step step = table.next();
  if (step == Step::Dealer) return refusal(kDealer, true, kDealerHasNoChoice);
  if (step != Step::Choose || table.state().current != kPlayer) {
    return refusal(kPlayer, true, kNothingToRank);
  }
  const SolveResult result = roundRanking(current);
  if (result.refused) return refusal(kPlayer, true, notSolved(result));
  if (result.ranked.empty()) return refusal(kPlayer, true, kNothingToRank);
  // The search merges copies of an item that nothing can tell apart, so one
  // ranked move can stand for several entries of the legal list. Each entry
  // takes the value of the move it matches, exactly when it can.
  const std::vector<Action> legal = table.legal();
  std::vector<std::vector<int>> matches(result.ranked.size());
  std::vector<bool> covered(legal.size(), false);
  for (std::size_t r = 0; r < result.ranked.size(); ++r) {
    for (std::size_t i = 0; i < legal.size(); ++i) {
      if (!covered[i] && legal[i] == result.ranked[r].action) {
        matches[r].push_back(static_cast<int>(i));
        covered[i] = true;
        break;
      }
    }
  }
  for (std::size_t i = 0; i < legal.size(); ++i) {
    for (std::size_t r = 0; r < result.ranked.size() && !covered[i]; ++r) {
      Action same = result.ranked[r].action;
      same.copy = legal[i].copy;
      if (legal[i] == same) {
        matches[r].push_back(static_cast<int>(i));
        covered[i] = true;
      }
    }
    if (!covered[i]) return failure("The ranking leaves out a move in the legal list.");
  }
  const Names names = roundNames(current);
  std::vector<RankedMove> moves;
  for (std::size_t r = 0; r < result.ranked.size(); ++r) {
    for (const int index : matches[r]) {
      const Action& action = legal[static_cast<std::size_t>(index)];
      const int slot = moveSlot(action, kPlayer, table.state(), &table);
      moves.push_back(RankedMove{std::to_string(index), moveLabel(action, kPlayer, slot, names),
                                 result.ranked[r].value});
    }
  }
  return rankingResult(kPlayer, true, "", moves, notesOf(result));
}

/// Rank the moves of the seat to move in a written position, under Double or
/// Nothing rules with the charges p1 names. The ids are the moves' places in
/// the ranking. The round is not touched.
std::string advise(const std::string& text, const std::string& opponent, int reloads) {
  if (opponent != "solver" && opponent != "dealer") {
    return failure("The opponent is solver or dealer.");
  }
  const bool scripted = opponent == "dealer";
  Position position;
  std::string error;
  if (!notation::parsePosition(text, &position, &error)) {
    return refusal(kPlayer, scripted, "The position cannot be used: " + error + ".");
  }
  const GameState& state = position.state;
  const int turn = state.current;
  if (state.playerCount != 2) {
    return refusal(kPlayer, scripted,
                   "The position cannot be used: this page reads positions with two seats.");
  }
  if (scripted && turn == kDealer) {
    return refusal(turn, scripted,
                   std::string(kDealerHasNoChoice) +
                       " Choose the solver as the opponent, or give the turn to seat 1.");
  }
  if (reloads < 0 || reloads > kAdviseMaxReloads) {
    return refusal(turn, scripted,
                   "This page looks through no more than one reload. The command line advisor "
                   "can look further.");
  }
  if (state.tube.empty()) {
    return refusal(turn, scripted, "The tube is empty, so there is no move to rank.");
  }
  if (state.roundOver()) {
    return refusal(turn, scripted, "A seat has no charges left, so the round is over.");
  }
  const RuleConfig config = RuleConfig::doubleOrNothing(state.players[kPlayer].maxHp);
  const auto answer = [&](int seat) {
    const std::string key =
        opponent + "|" + std::to_string(reloads) + "|" + std::to_string(seat) + "|" + text;
    const auto found = answers.find(key);
    if (found != answers.end()) return found->second;
    SolveOptions options;
    options.seat = seat;
    options.opponent = scripted ? OpponentModel::Dealer : OpponentModel::Optimal;
    options.reloadBudget = reloads;
    options.nodeLimit = kAdviseNodeLimit;
    const SolveResult result = solve(position, config, options);
    remember(&answers, key, result);
    return result;
  };
  SolveResult result = answer(turn);
  if (result.refused) return refusal(turn, scripted, notSolved(result));
  if (result.ranked.empty()) {
    return refusal(result.mover, scripted,
                   "Seat 1 is handcuffed and loses this turn. The Dealer then follows its "
                   "script, so there is no choice to rank.");
  }
  // A handcuffed seat in turn= is skipped, so the other seat holds the gun.
  // Its moves are ranked by its own chance, from its own side of the table.
  if (result.mover != turn) {
    result = answer(result.mover);
    if (result.refused) return refusal(turn, scripted, notSolved(result));
  }
  const int mover = result.mover;
  const std::string other = scripted ? "the Dealer" : "the opponent";
  const Names names =
      mover == kPlayer ? Names{"you", other, kPlayer} : Names{other, "you", kDealer};
  std::vector<RankedMove> moves;
  for (std::size_t i = 0; i < result.ranked.size(); ++i) {
    const Action& action = result.ranked[i].action;
    const int slot = moveSlot(action, mover, state, nullptr);
    moves.push_back(RankedMove{std::to_string(i), moveLabel(action, mover, slot, names),
                               result.ranked[i].value});
  }
  return rankingResult(result.mover, scripted, "", moves, notesOf(result));
}

}  // namespace
}  // namespace bsr

EMSCRIPTEN_BINDINGS(bsr_engine) {
  emscripten::function("newRound", &bsr::newRound);
  emscripten::function("act", &bsr::act);
  emscripten::function("advance", &bsr::advance);
  emscripten::function("rank", &bsr::rank);
  emscripten::function("advise", &bsr::advise);
}
