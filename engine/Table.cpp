#include "engine/Table.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "engine/Rules.h"

namespace bsr {
namespace {

constexpr int kPlayer = 0;
constexpr int kDealer = 1;

const char* shellWord(Shell shell) {
  return shell == Shell::Live ? "live" : "blank";
}

const char* shellShout(Shell shell) {
  return shell == Shell::Live ? "LIVE" : "blank";
}

/// The text with its first letter in capitals, except a seat name such as
/// "p1", which is always written in lower case.
std::string capitalised(std::string text) {
  const bool seatName =
      text.size() > 1 && text[0] == 'p' && std::isdigit(static_cast<unsigned char>(text[1])) != 0;
  if (!text.empty() && !seatName) {
    text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
  }
  return text;
}

/// Whether `seat` can tell what the chamber holds from what it has seen and
/// the counts. For the dealer this is the endless rules' deduction
/// (`FigureOutShell`, `dealer::deduces`).
bool canTellChamber(const Tube& tube, int seat) {
  if (tube.knows(seat, 0)) return true;
  if (tube.live == 0 || tube.blank == 0) return true;
  int live = tube.live;
  int blank = tube.blank;
  for (int i = 0; i < tube.size(); ++i) {
    if (!tube.knows(seat, i)) continue;
    if (tube.truth[i] == Shell::Live) --live;
    if (tube.truth[i] == Shell::Blank) --blank;
  }
  return live == 0 || blank == 0;
}

/// How the dealer knows the chamber without an item showing it, as a clause
/// with no full stop that names the dealer as `who`. It is called private only
/// when p1 could not tell the same thing from what p1 has seen.
std::string chamberKnowledge(const Tube& tube, Shell known, const std::string& who) {
  const std::string type = shellWord(known);
  if (tube.size() == 1) return "one shell is left, so " + who + " knows the chamber is " + type;
  if (tube.live == 0 || tube.blank == 0) {
    return "every shell left is " + type + ", so " + who + " knows the chamber is " + type;
  }
  if (canTellChamber(tube, kPlayer)) {
    return "both seats can tell from what they have seen that the chamber is " + type;
  }
  // A glass shows the chamber only for a shot or a Beer in the same turn, so a
  // chamber the dealer knew before this pass, and every shell past it that it
  // knows, was heard on a Burner Phone.
  if (tube.knows(kDealer, 0)) {
    return who + " heard this shell on a Burner Phone earlier and p1 did not, so only " + who +
           " knows it is " + type;
  }
  return "shells only " + who + " has heard on a Burner Phone, with the counts, show the " +
         "chamber is " + type;
}

/// The clause that goes before a story rules reason when the counts settle
/// the chamber: the story rules do not work the chamber out from the counts,
/// so a tube of one type with more than one shell in it still reads as
/// unknown to them.
std::string storyCountsLead(const Tube& tube, dealer::Brain brain) {
  const bool ignores =
      brain == dealer::Brain::Story && tube.size() > 1 && (tube.live == 0 || tube.blank == 0);
  return ignores ? "the story rules do not work the chamber out from the counts, so " : "";
}

/// What the script's coin decides: a shot with no target chosen, or, with a
/// saw in reach and no other item to use, whether to saw or shoot itself.
enum class CoinUse : std::uint8_t { Shot, SawSelf, Saw };

/// Why the coin came out as it did, as a clause with no full stop. The rule
/// is the dealer's `coin` (DealerIntelligence.gd 421-431). The endless rules
/// count the tube, which every seat can: more live than blank shells always
/// means p1 and the saw, fewer always means itself, and only even counts toss
/// a fair coin. The story rules always toss a fair coin.
std::string coinWhy(CoinUse use, const Tube& tube, dealer::Brain brain, bool self) {
  const bool shot = use == CoinUse::Shot;
  std::string always;
  std::string chose;
  switch (use) {
    case CoinUse::Shot:
      always = self ? "it always shoots itself" : "it always shoots p1";
      chose = self ? "shoot itself" : "shoot p1";
      break;
    case CoinUse::SawSelf:
      always = "it always shoots itself rather than saw";
      chose = "shoot itself rather than saw";
      break;
    case CoinUse::Saw:
      always = "it always saws the barrel and shoots p1";
      chose = "saw the barrel and shoot p1";
      break;
  }
  if (brain == dealer::Brain::Story) {
    std::string lead = storyCountsLead(tube, brain);
    if (lead.empty()) lead = "under the story rules, ";
    return lead + (shot ? "with no target" : "with no other item to use and a saw in reach") +
           ", a fair coin chose to " + chose;
  }
  const std::string lead =
      shot ? "with no target and " : "with no other item to use, a saw in reach and ";
  if (tube.live > tube.blank) return lead + "more live than blank shells, " + always;
  if (tube.live < tube.blank) return lead + "fewer live than blank shells, " + always;
  return lead + "as many live as blank shells, a fair coin chose to " + chose;
}

/// What made an item's rule fire, in the order the script checks them
/// (DealerIntelligence.gd 151-201).
std::string itemWhy(Item item, const Tube& tube, dealer::Brain brain) {
  switch (item) {
    case Item::MagnifyingGlass:
      return storyCountsLead(tube, brain) +
             "it does not know the chamber and more than one shell is left";
    case Item::Cigarettes:
      return "it is below its full charges";
    case Item::ExpiredMedicine:
      return "it is below its full charges and not on its last one, no cigarettes are within "
             "its reach, and it has not taken medicine this turn";
    case Item::Beer:
      return storyCountsLead(tube, brain) +
             "it does not know the chamber to be live and more than one shell is left";
    case Item::Handcuffs:
      return "p1 is free to be handcuffed and more than one shell is left";
    case Item::HandSaw:
      return "it knows the chamber is live and the barrel is not sawed";
    case Item::BurnerPhone:
      return "more than two shells are left";
    case Item::Inverter:
      return "it knows the chamber is blank";
    case Item::Adrenaline:
    case Item::Jammer:
    case Item::Remote:
      break;
  }
  return "its item rules chose it";
}

/// Why the dealer shoots a target it chose on an earlier pass of this turn.
/// The rule applied is that it keeps the target, so the clause leads with
/// that, then says what chose it and whether that shell is still in the
/// chamber. Both rule sets keep the target after a Beer
/// (DealerIntelligence.gd 170-176).
std::string keptTargetWhy(const std::string& when, bool racked, dealer::Brain brain) {
  std::string why = "it keeps the target it chose earlier this turn";
  if (when.empty()) return why;
  why += ", when " + when;
  if (racked) {
    why += ". A Beer has since racked that shell out, and the ";
    why += brain == dealer::Brain::Story
               ? "story rules keep both the target and what it learned after a Beer"
               : "endless rules keep the target after a Beer";
  }
  return why;
}

std::string chargesText(const PlayerState& player) {
  return std::to_string(static_cast<int>(player.hp)) + "/" +
         std::to_string(static_cast<int>(player.maxHp)) + " charges";
}

}  // namespace

Table::Table(const TableOptions& options) : options_(options), config_(options.config) {
  rng_.seed(options.seed);
  const int players = config_.mode == Mode::Multiplayer
                          ? std::max(2, std::min(options.players, static_cast<int>(kMaxPlayers)))
                          : 2;
  scripted_ = options.scriptedDealer && players == 2 && dealer::brainFor(config_, &brain_);
  if (options.charges < 0 || options.charges > 8) {
    throw std::logic_error("a seat starts with 1 to 8 charges");
  }
  switch (config_.mode) {
    case Mode::DoubleOrNothing:
      charges_ = options.charges > 0 ? options.charges : uniformInt(2, 4);
      break;
    case Mode::Story:
      charges_ = config_.charges;
      break;
    case Mode::Multiplayer:
      charges_ = options.charges > 0 ? options.charges : 4;
      break;
  }
  config_.charges = static_cast<std::uint8_t>(charges_);
  state_.playerCount = static_cast<std::uint8_t>(players);
  for (int seat = 0; seat < players; ++seat) {
    state_.players[seat].hp = static_cast<std::uint8_t>(charges_);
    state_.players[seat].maxHp = static_cast<std::uint8_t>(charges_);
  }
  state_.current = 0;
}

bool Table::fromPosition(const Position& position, const TableOptions& options, Table* out,
                         std::string* error) {
  Table table;
  table.options_ = options;
  table.config_ = options.config;
  table.rng_.seed(options.seed);
  const GameState& state = position.state;
  if (!position.unseenReads.empty()) {
    *error = "a table cannot start from phone reads whose result nobody at it saw";
    return false;
  }
  if (state.playerCount < 2 || state.playerCount > kMaxPlayers) {
    *error = "a table seats 2 to 4";
    return false;
  }
  if (table.config_.mode != Mode::Multiplayer && state.playerCount != 2) {
    *error = "this rule set seats exactly two";
    return false;
  }
  table.scripted_ = options.scriptedDealer && state.playerCount == 2 &&
                    dealer::brainFor(table.config_, &table.brain_);
  if (!(position.dealerMemory == dealer::Memory{})) {
    if (!table.scripted_) {
      *error = "only the scripted dealer carries a memory from one pass to the next";
      return false;
    }
    if (!dealer::validateMemory(position, table.brain_, error)) return false;
    table.memory_ = position.dealerMemory;
    table.midTurn_ = true;
  }
  table.state_ = state;
  table.charges_ = state.players[0].maxHp;
  table.config_.charges = static_cast<std::uint8_t>(table.charges_);
  table.loadNumber_ = 1;
  for (int seat = 0; seat < state.playerCount; ++seat) {
    for (int i = 0; i < state.players[seat].hand.size(); ++i) table.slots_[seat].push_back(i);
  }
  // A written chamber bit is what the writer saw, so it counts as public.
  table.publicChamber_ = state.tube.empty() ? 0 : state.tube.knownBy[0];
  table.applySkips();
  *out = table;
  return true;
}

std::uint32_t Table::draw() {
  return static_cast<std::uint32_t>(rng_());
}

int Table::uniformInt(int lo, int hi) {
  if (lo == hi) return lo;
  const std::uint64_t range = static_cast<std::uint64_t>(hi - lo) + 1;
  const std::uint64_t span = std::uint64_t{1} << 32;
  const std::uint64_t limit = span - span % range;
  std::uint64_t x = draw();
  while (x >= limit) x = draw();
  return lo + static_cast<int>(x % range);
}

double Table::unitReal() {
  return static_cast<double>(draw()) / 4294967296.0;
}

std::size_t Table::sample(const std::vector<double>& weights) {
  const double u = unitReal();
  double cumulative = 0.0;
  for (std::size_t i = 0; i < weights.size(); ++i) {
    cumulative += weights[i];
    if (u < cumulative) return i;
  }
  return weights.empty() ? 0 : weights.size() - 1;
}

std::string Table::name(int seat) const {
  if (scripted_ && seat == kDealer) return "the dealer";
  return "p" + std::to_string(seat + 1);
}

std::string Table::sentenceName(int seat) const {
  // Seat names such as p1 stay lower case at the start of a sentence.
  return scripted_ && seat == kDealer ? capitalised(name(seat)) : name(seat);
}

Step Table::next() const {
  if (state_.roundOver()) return Step::Over;
  if (state_.needsReload()) return Step::Load;
  if (scripted_ && state_.current == kDealer) return Step::Dealer;
  return Step::Choose;
}

std::vector<Item> Table::poolFor(int loadIndex, int charges) const {
  std::vector<Item> pool = config_.itemPool;
  if (config_.mode != Mode::Multiplayer && loadIndex == 0 && charges == 2) {
    pool.erase(std::remove(pool.begin(), pool.end(), Item::HandSaw), pool.end());
  }
  return pool;
}

std::pair<int, int> Table::loadedCounts() const {
  return {state_.tube.live - netFlip_, state_.tube.blank + netFlip_};
}

std::array<int, kMaxItemsPerSeat> Table::tray(int seat) const {
  std::array<int, kMaxItemsPerSeat> places{};
  places.fill(-1);
  if (seat < 0 || seat >= state_.playerCount) return places;
  const Hand& hand = state_.players[seat].hand;
  for (int i = 0; i < hand.size(); ++i) {
    places[static_cast<std::size_t>(slots_[seat][static_cast<std::size_t>(i)])] =
        itemIndex(hand.at[static_cast<std::size_t>(i)]);
  }
  return places;
}

int Table::slotOf(int seat, Item item, int ordinal) const {
  if (seat < 0 || seat >= state_.playerCount) return -1;
  const int index = state_.players[seat].hand.indexOfCopy(item, ordinal);
  return index < 0 ? -1 : slots_[seat][static_cast<std::size_t>(index)];
}

void Table::spend(int seat, int handIndex, int slot) {
  // `state_` still holds the hand the item leaves. The entries of its type
  // keep their places, apart from the one emptied, in the order they sit in
  // the hand.
  const Hand& hand = state_.players[seat].hand;
  std::vector<int>& slots = slots_[seat];
  const Item type = hand.at[static_cast<std::size_t>(handIndex)];
  std::vector<int> kept;
  for (int i = 0; i < hand.size(); ++i) {
    const int place = slots[static_cast<std::size_t>(i)];
    if (hand.at[static_cast<std::size_t>(i)] == type && place != slot) kept.push_back(place);
  }
  slots.erase(slots.begin() + handIndex);
  std::size_t next = 0;
  for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
    const int old = i < handIndex ? i : i + 1;
    if (hand.at[static_cast<std::size_t>(old)] == type && next < kept.size()) {
      slots[static_cast<std::size_t>(i)] = kept[next++];
    }
  }
}

void Table::applySkips() {
  while (!state_.roundOver() && !state_.needsReload()) {
    const int seat = state_.current;
    if (!rules::applyPendingSkip(&state_)) break;
    Event event;
    event.kind = Event::Kind::Skip;
    event.seat = seat;
    event.text = state_.playerCount > 2
                     ? name(seat) + " is jammed and loses this turn."
                     : sentenceName(seat) + " is handcuffed and loses this turn.";
    log_.push_back(event);
  }
}

void Table::noteSkips(const GameState& before, const GameState& after) {
  // A move that passes the turn hands it past a cuffed seat at once and marks
  // that seat as having lost a turn. When the move emptied the tube and the
  // reload hands the turn to a set seat, the skip cost nobody anything, so the
  // load says so instead.
  const bool reloadDecides = after.needsReload() && config_.reloadTurn != ReloadTurn::KeepCurrent;
  for (int seat = 0; seat < after.playerCount; ++seat) {
    if (before.players[seat].skipConsumed || !after.players[seat].skipConsumed) continue;
    if (reloadDecides) {
      unsaidSkips_ |= 1u << seat;
      continue;
    }
    Event event;
    event.kind = Event::Kind::Skip;
    event.seat = seat;
    event.text = after.playerCount > 2 ? name(seat) + " is jammed and loses this turn."
                                       : sentenceName(seat) + " is handcuffed and loses this turn.";
    log_.push_back(event);
  }
}

void Table::noteOver() {
  if (!state_.roundOver()) return;
  Event event;
  event.kind = Event::Kind::Over;
  event.seat = state_.soleSurvivor();
  if (event.seat < 0) {
    event.text = "Nobody is left standing.";
  } else {
    event.text = sentenceName(event.seat) + " wins the round.";
  }
  log_.push_back(event);
}

void Table::afterAction(const GameState& before, const GameState& after) {
  if (after.tube.size() < before.tube.size()) {
    // The chamber left the tube: what anyone knew about it went with it, and
    // every read now names a shell one place nearer the chamber.
    publicChamber_ = 0;
    deducedChamber_ = false;
    netFlip_ = 0;
    for (Read& read : reads_) read.offset = read.offset > 0 ? read.offset - 1 : -1;
  } else {
    netFlip_ += static_cast<int>(after.tube.live) - static_cast<int>(before.tube.live);
  }
  noteSkips(before, after);
}

int Table::phoneOffset(const GameState& before, const GameState& after, int seat, bool anyOffset,
                       int rank) const {
  const int size = before.tube.size();
  for (int offset = 1; offset < size; ++offset) {
    if (after.tube.knows(seat, offset) && !before.tube.knows(seat, offset)) return offset;
  }
  // The phone named a shell its user already knew. The branches for those
  // shells leave identical states, in the order of their offsets.
  const std::array<double, kMaxShells> weights =
      rules::phoneOffsetWeights(seat, before.playerCount, size);
  int seen = 0;
  for (int offset = 1; offset < size; ++offset) {
    const bool possible = anyOffset || weights[static_cast<std::size_t>(offset)] > 0.0;
    if (!possible || !before.tube.knows(seat, offset)) continue;
    if (seen++ == rank) return offset;
  }
  return -1;
}

void Table::recordRead(int seat, const GameState& before, int offset) {
  if (before.tube.size() < 2) return;
  Read read;
  read.seat = seat;
  read.sizeAtUse = before.tube.size();
  read.offset = offset;
  reads_.push_back(read);
}

void Table::markPublic(const GameState& after, int seat, Item item) {
  const std::uint8_t bit = static_cast<std::uint8_t>(1u << seat);
  if (item == Item::MagnifyingGlass)
    publicChamber_ = static_cast<std::uint8_t>(publicChamber_ | bit);
  // An Inverter on a chamber its user had seen is seen by every seat, and its
  // user still knows the chamber after it.
  if (item == Item::Inverter && !after.tube.empty() && after.tube.knows(seat, 0)) {
    publicChamber_ = static_cast<std::uint8_t>(publicChamber_ | bit);
    if (seat == kDealer) deducedChamber_ = false;
  }
}

Table::ItemText Table::describeItem(const GameState& before, const GameState& after,
                                    const Action& action, Shell racked, int thiefSlot,
                                    int victimSlot, int phoneAt, bool knewPhone) const {
  ItemText out;
  const int seat = before.current;
  const std::string who = name(seat);
  const std::string sentence = sentenceName(seat);

  if (action.isAdrenalineAlone()) {
    Event event;
    event.kind = Event::Kind::Item;
    event.seat = seat;
    event.item = Item::Adrenaline;
    event.stolen = Item::Adrenaline;
    event.slot = thiefSlot;
    event.text = sentence + " uses Adrenaline and takes nothing.";
    out.items.push_back(event);
    return out;
  }

  const bool steal = action.isSteal();
  const Item item = steal ? action.stolen : action.item;
  const bool aimed = itemNeedsTarget(item);
  std::string detail;
  switch (item) {
    case Item::MagnifyingGlass: {
      detail = "It looks into the chamber.";
      Event learned;
      learned.kind = Event::Kind::Learned;
      learned.seat = seat;
      learned.item = item;
      learned.offset = 0;
      learned.shell = after.tube.truth[0];
      learned.privateTo = seat;
      learned.text =
          "Seen only by " + who + ": the chamber is " + shellWord(after.tube.truth[0]) + ".";
      out.learned.push_back(learned);
      break;
    }
    case Item::Beer:
      detail = std::string("The shell it racked out was ") + shellShout(racked) + ".";
      break;
    case Item::Cigarettes:
    case Item::ExpiredMedicine: {
      const PlayerState& was = before.players[seat];
      const PlayerState& now = after.players[seat];
      if (item == Item::ExpiredMedicine) detail = now.hp < was.hp ? "It fails. " : "It works. ";
      detail += sentence + " is now on " + chargesText(now) + ".";
      if (item == Item::Cigarettes && now.hp == was.hp) {
        detail += was.hp >= was.maxHp ? "\nHealing does nothing at full charges."
                                      : "\nHealing does nothing on its last charge in this stage.";
      }
      break;
    }
    case Item::Handcuffs:
      detail = sentenceName(action.target) + " is handcuffed and will lose the next turn.";
      break;
    case Item::Jammer:
      detail = sentenceName(action.target) + " is jammed and will lose the next turn.";
      break;
    case Item::HandSaw:
      detail = "The barrel is sawed: a live shell on the next shot deals two charges.";
      break;
    case Item::BurnerPhone: {
      if (before.tube.size() < 2) {
        detail = "No shell is left past the chamber, so it hears nothing.";
        break;
      }
      detail = "It listens to the type of one shell past the chamber.";
      Event learned;
      learned.kind = Event::Kind::Learned;
      learned.seat = seat;
      learned.item = item;
      learned.privateTo = seat;
      learned.offset = phoneAt;
      if (phoneAt > 0) {
        learned.shell = after.tube.truth[phoneAt];
        learned.text = "Heard only by " + who + ": shell " + std::to_string(phoneAt + 1) + " is " +
                       shellWord(learned.shell) + (knewPhone ? ", which it already knew." : ".");
      } else {
        learned.text = "Heard only by " + who + ": a shell it already knew.";
      }
      out.learned.push_back(learned);
      break;
    }
    case Item::Inverter: {
      detail = "It flips the chamber.";
      if (scripted_ && seat == kDealer && after.tube.knows(kDealer, 0)) {
        Event learned;
        learned.kind = Event::Kind::Learned;
        learned.seat = seat;
        learned.item = item;
        learned.offset = 0;
        learned.shell = after.tube.truth[0];
        learned.privateTo = seat;
        learned.text = "Seen only by " + who + ": the chamber " +
                       (after.tube.live > before.tube.live ? "was blank and is now live"
                                                           : "was already live") +
                       ".";
        out.learned.push_back(learned);
      }
      break;
    }
    case Item::Remote:
      detail = "The turn order is reversed.";
      break;
    case Item::Adrenaline:
      break;
  }

  if (steal) {
    Event taken;
    taken.kind = Event::Kind::Item;
    taken.seat = seat;
    taken.target = action.stealFrom;
    taken.item = Item::Adrenaline;
    taken.stolen = item;
    taken.steal = true;
    taken.slot = thiefSlot;
    taken.text = sentence + " uses Adrenaline to take " + name(action.stealFrom) + "'s " +
                 itemName(item) + ", and uses it.";
    out.items.push_back(taken);
    Event used;
    used.kind = Event::Kind::Item;
    used.seat = seat;
    used.target = aimed ? action.target : -1;
    used.item = item;
    used.stolen = item;
    used.steal = true;
    used.slot = victimSlot;
    used.text = detail;
    out.items.push_back(used);
    return out;
  }
  Event used;
  used.kind = Event::Kind::Item;
  used.seat = seat;
  used.target = aimed ? action.target : -1;
  used.item = item;
  used.slot = thiefSlot;
  used.text = sentence + " uses its " + itemName(item) + ".\n" + detail;
  out.items.push_back(used);
  return out;
}

Event Table::describeShot(const GameState& before, const GameState& after, const Action& action,
                          Shell fired) const {
  const int seat = before.current;
  const int target = action.target;
  const bool self = target == seat;
  Event event;
  event.kind = Event::Kind::Shot;
  event.seat = seat;
  event.target = target;
  event.shell = fired;
  event.damage = static_cast<int>(before.players[target].hp) - after.players[target].hp;
  event.text = sentenceName(seat) + " shoots " + (self ? std::string("itself") : name(target)) +
               ".\nThe shell was " + shellShout(fired) + ".";
  // A blank fired at oneself hands the turn to nobody else, unless the tube is
  // now empty and the reload decides who moves.
  if (self && fired == Shell::Blank && !after.roundOver() && !after.needsReload() &&
      after.current == seat) {
    event.text += "\nA blank at itself lets " + name(seat) + " move again.";
  }
  return event;
}

void Table::load() {
  if (next() != Step::Load) throw std::logic_error("load is called only when the tube is empty");
  const GameState emptied = state_;
  const bool firstLoad = loadNumber_ == 0;

  int live = 1;
  int blank = 1;
  if (config_.mode == Mode::DoubleOrNothing) {
    // A total of 2 to 8 and half of it live, rounded down and at least one
    // (RoundManager.gd 146-152).
    const int total = uniformInt(2, kMaxShells);
    live = std::max(1, total / 2);
    blank = total - live;
  } else {
    const auto table = rules::loadDistribution(config_);
    std::vector<double> weights;
    weights.reserve(table.size());
    for (const auto& entry : table) weights.push_back(std::get<2>(entry));
    const auto& chosen = table[sample(weights)];
    live = std::get<0>(chosen);
    blank = std::get<1>(chosen);
  }

  Event event;
  event.kind = Event::Kind::Load;
  const int count = uniformInt(config_.itemsPerLoad,
                               std::max<int>(config_.itemsPerLoad, config_.itemsPerLoadMax));
  event.dealt = count;
  const std::vector<Item> pool = poolFor(loadNumber_, charges_);
  const int limit = std::min<int>(config_.itemLimit, kMaxItemsPerSeat);
  int placed = 0;
  for (int seat = 0; seat < state_.playerCount; ++seat) {
    PlayerState& player = state_.players[seat];
    if (!player.alive() || pool.empty()) continue;
    for (int i = 0; i < count; ++i) {
      if (player.hand.size() >= limit) break;
      const Item item =
          pool[static_cast<std::size_t>(uniformInt(0, static_cast<int>(pool.size()) - 1))];
      player.hand.append(item);
      std::vector<int>& slots = slots_[seat];
      int place = 0;
      while (std::find(slots.begin(), slots.end(), place) != slots.end()) ++place;
      slots.push_back(place);
      event.dealtItems[static_cast<std::size_t>(seat)].push_back(item);
      ++placed;
    }
  }
  rules::reloadInto(&state_, static_cast<std::uint8_t>(live), static_cast<std::uint8_t>(blank),
                    config_);

  reads_.clear();
  deducedChamber_ = false;
  publicChamber_ = 0;
  netFlip_ = 0;
  memory_ = dealer::Memory{};
  midTurn_ = false;
  aimWhen_.clear();
  aimFromChamber_ = false;
  chamberRacked_ = false;

  event.live = live;
  event.blank = blank;
  event.first = state_.current;
  event.text = "The gun is loaded with " + std::to_string(live) + " live and " +
               std::to_string(blank) + " blank.\n";
  event.text += placed > 0 ? "New items are dealt" : "No items are dealt";
  if (firstLoad || config_.reloadTurn != ReloadTurn::KeepCurrent) {
    event.text += ", and " + name(state_.current) + " moves first.";
  } else {
    event.text += ", and the turn stays with " + name(state_.current) + ".";
  }
  // Every seat the reload frees: one still restrained when the tube ran out,
  // or one whose lost turn the last move took. Either way it loses no turn.
  for (int seat = 0; seat < state_.playerCount; ++seat) {
    const PlayerState& now = state_.players[seat];
    const bool wasCuffed = emptied.players[seat].cuffed && !now.cuffed;
    const bool skipVoided = ((unsaidSkips_ >> seat) & 1u) != 0u && !now.skipConsumed;
    if (!(wasCuffed || skipVoided) || !now.alive()) continue;
    if (state_.playerCount > 2) {
      event.text += "\nThe reload frees " + name(seat) + " from the Jammer.";
    } else {
      event.text += "\nThe reload takes the handcuffs off " + name(seat) + ".";
    }
  }
  unsaidSkips_ = 0;
  ++loadNumber_;
  log_.push_back(event);
  applySkips();
}

void Table::dealerPass() {
  if (next() != Step::Dealer) {
    throw std::logic_error("dealerPass is called only when the scripted dealer is to move");
  }
  if (!midTurn_) {
    memory_ = dealer::Memory{};
    aimWhen_.clear();
    aimFromChamber_ = false;
    chamberRacked_ = false;
  }
  const GameState before = state_;
  const dealer::Memory memory = memory_;
  // The endless rules work the chamber out from what the dealer has seen and
  // the counts (DealerIntelligence.gd 96-104). Nobody else sees that happen.
  if (brain_ == dealer::Brain::Endless && !memory.knows && !before.tube.knows(kDealer, 0) &&
      dealer::deduces(before)) {
    deducedChamber_ = true;
  }
  const std::vector<dealer::Branch> branches = dealer::step(before, memory, config_, brain_);
  if (branches.empty()) throw std::logic_error("the dealer has no move in this position");
  std::vector<double> weights;
  weights.reserve(branches.size());
  for (const dealer::Branch& branch : branches) weights.push_back(branch.probability);
  const std::size_t index = sample(weights);
  const dealer::Branch& pass = branches[index];
  const GameState& after = pass.state;
  const Action& action = pass.action;

  if (action.kind == Action::Kind::Shoot) {
    const bool self = static_cast<int>(action.target) == kDealer;
    log_.push_back(describeShot(before, after, action, pass.shellType));
    std::string why;
    switch (pass.reason) {
      case dealer::Reason::Deduced:
      case dealer::Reason::LastShell:
        why = chamberKnowledge(before.tube, pass.shellType, "it");
        break;
      case dealer::Reason::KnownTarget:
        why = keptTargetWhy(aimWhen_, chamberRacked_, brain_);
        break;
      case dealer::Reason::SawCoinSelf:
        why = coinWhy(CoinUse::SawSelf, before.tube, brain_, true);
        break;
      case dealer::Reason::Coin:
      case dealer::Reason::Item:
      case dealer::Reason::SawCoin:
        why = coinWhy(CoinUse::Shot, before.tube, brain_, self);
        break;
    }
    Event rule;
    rule.kind = Event::Kind::Rule;
    rule.seat = kDealer;
    rule.text = capitalised(why) + ".";
    log_.push_back(rule);
  } else {
    const Item item = pass.stolen ? action.stolen : action.item;
    // The dealer pays with its first copy, or with its first Adrenaline and
    // p1's first copy (DealerIntelligence.gd 243-261).
    int thiefSlot = -1;
    int victimSlot = -1;
    if (pass.stolen) {
      const int own = before.players[kDealer].hand.indexOfCopy(Item::Adrenaline, 0);
      const int taken = before.players[kPlayer].hand.indexOfCopy(item, 0);
      thiefSlot = slots_[kDealer][static_cast<std::size_t>(own)];
      victimSlot = slots_[kPlayer][static_cast<std::size_t>(taken)];
      spend(kDealer, own, thiefSlot);
      spend(kPlayer, taken, victimSlot);
    } else {
      const int own = before.players[kDealer].hand.indexOfCopy(item, 0);
      thiefSlot = slots_[kDealer][static_cast<std::size_t>(own)];
      spend(kDealer, own, thiefSlot);
    }
    int phoneAt = -1;
    bool knewPhone = false;
    if (item == Item::BurnerPhone && before.tube.size() >= 2) {
      int rank = 0;
      for (std::size_t i = 0; i < index; ++i) {
        if (branches[i].state == after && branches[i].memory == pass.memory) ++rank;
      }
      phoneAt = phoneOffset(before, after, kDealer, true, rank);
      knewPhone = phoneAt < 0 || before.tube.knows(kDealer, phoneAt);
      recordRead(kDealer, before, phoneAt);
    }
    const ItemText text = describeItem(before, after, action, pass.shellType, thiefSlot, victimSlot,
                                       phoneAt, knewPhone);
    for (const Event& event : text.items) log_.push_back(event);

    // Whether the pass started by working out the chamber, which the endless
    // rules do whenever they can and both rule sets do with one shell left.
    // An item rule then acted, and what the dealer worked out is what the
    // chamber held when the pass began.
    const bool deduced =
        !memory.knows && pass.reason == dealer::Reason::Item &&
        ((brain_ == dealer::Brain::Endless && canTellChamber(before.tube, kDealer)) ||
         before.tube.size() == 1);
    Shell chamber = after.tube.truth[0];
    if (item == Item::Beer) chamber = pass.shellType;
    if (item == Item::Inverter) chamber = Shell::Blank;
    if (deduced) {
      Event learned;
      learned.kind = Event::Kind::Learned;
      learned.seat = kDealer;
      learned.offset = 0;
      learned.shell = chamber;
      learned.privateTo = kDealer;
      learned.text = capitalised(chamberKnowledge(before.tube, chamber, "the dealer")) + ".";
      log_.push_back(learned);
    }
    for (const Event& event : text.learned) log_.push_back(event);

    const auto aim = [this](const std::string& when, bool fromChamber) {
      aimWhen_ = when;
      aimFromChamber_ = fromChamber;
      chamberRacked_ = false;
    };
    if (item == Item::MagnifyingGlass) {
      aim(std::string("the glass showed the chamber was ") + shellWord(after.tube.truth[0]), true);
    }
    if (item == Item::Inverter) aim("the Inverter left the chamber live", true);
    if (pass.reason == dealer::Reason::SawCoin) {
      aim("it chose to saw the barrel and shoot p1", false);
    } else if (deduced && item != Item::Inverter && pass.memory.target != dealer::Target::None) {
      aim(std::string("it worked out that the chamber was ") + shellWord(chamber), true);
    }
    // A Beer moves the next shell into the chamber and leaves the target alone.
    if (item == Item::Beer && aimFromChamber_ && pass.memory.target != dealer::Target::None) {
      chamberRacked_ = true;
    }

    Event rule;
    rule.kind = Event::Kind::Rule;
    rule.seat = kDealer;
    rule.text = capitalised(pass.reason == dealer::Reason::SawCoin
                                ? coinWhy(CoinUse::Saw, before.tube, brain_, false)
                                : itemWhy(item, before.tube, brain_)) +
                ".";
    log_.push_back(rule);
  }

  state_ = after;
  memory_ = pass.memory;
  midTurn_ = !pass.turnOver;
  afterAction(before, state_);
  if (action.kind == Action::Kind::UseItem) {
    markPublic(state_, kDealer, pass.stolen ? action.stolen : action.item);
  }
  noteOver();
  applySkips();
}

std::vector<Action> Table::legal() const {
  if (next() != Step::Choose) {
    throw std::logic_error("legal is called only when a seat chooses a move");
  }
  return rules::legalActions(state_, config_);
}

void Table::play(const Action& action, int traySlot) {
  if (next() != Step::Choose) {
    throw std::logic_error("play is called only when a seat chooses a move");
  }
  const std::vector<Action> actions = rules::legalActions(state_, config_);
  if (std::find(actions.begin(), actions.end(), action) == actions.end()) {
    throw std::logic_error("the move is not one the seat to move may make");
  }
  const GameState before = state_;
  const int seat = before.current;
  const Hand& hand = before.players[seat].hand;

  int thiefSlot = -1;
  int victimSlot = -1;
  int ownIndex = -1;
  int victimIndex = -1;
  if (action.kind == Action::Kind::Shoot) {
    if (traySlot != -1) throw std::logic_error("a shot takes nothing from the tray");
  } else if (action.item == Item::Adrenaline) {
    // Every Adrenaline a seat spends leaves its hand as the first copy, and
    // the slot it leaves may be any that holds one.
    ownIndex = hand.indexOfCopy(Item::Adrenaline, 0);
    thiefSlot = slots_[seat][static_cast<std::size_t>(ownIndex)];
    if (traySlot != -1) {
      const std::array<int, kMaxItemsPerSeat> places = tray(seat);
      if (traySlot < 0 || traySlot >= kMaxItemsPerSeat ||
          places[static_cast<std::size_t>(traySlot)] != itemIndex(Item::Adrenaline)) {
        throw std::logic_error("the tray slot does not hold an Adrenaline of the seat to move");
      }
      thiefSlot = traySlot;
    }
    if (action.isSteal()) {
      victimIndex = before.players[action.stealFrom].hand.indexOfCopy(action.stolen, action.copy);
      victimSlot = slots_[action.stealFrom][static_cast<std::size_t>(victimIndex)];
    }
  } else {
    ownIndex = hand.indexOfCopy(action.item, action.copy);
    thiefSlot = slots_[seat][static_cast<std::size_t>(ownIndex)];
    if (traySlot != -1) {
      bool found = false;
      for (int i = ownIndex; i < hand.size() && hand.at[static_cast<std::size_t>(i)] == action.item;
           ++i) {
        if (slots_[seat][static_cast<std::size_t>(i)] == traySlot) found = true;
      }
      if (!found) throw std::logic_error("the tray slot does not hold a copy this move may use");
      thiefSlot = traySlot;
    }
  }

  const std::vector<Outcome> outcomes = rules::apply(before, action, config_);
  std::vector<double> weights;
  weights.reserve(outcomes.size());
  for (const Outcome& outcome : outcomes) weights.push_back(outcome.probability);
  const std::size_t index = sample(weights);
  const Outcome& chosen = outcomes[index];
  const GameState& after = chosen.state;

  if (action.kind == Action::Kind::Shoot) {
    log_.push_back(describeShot(before, after, action, chosen.shellType));
  } else {
    const Item used = action.isSteal() ? action.stolen : action.item;
    int phoneAt = -1;
    bool knewPhone = false;
    if (used == Item::BurnerPhone && before.tube.size() >= 2) {
      int rank = 0;
      for (std::size_t i = 0; i < index; ++i) {
        if (outcomes[i].state == after) ++rank;
      }
      phoneAt = phoneOffset(before, after, seat, false, rank);
      knewPhone = phoneAt < 0 || before.tube.knows(seat, phoneAt);
      recordRead(seat, before, phoneAt);
    }
    if (victimIndex >= 0) spend(action.stealFrom, victimIndex, victimSlot);
    spend(seat, ownIndex, thiefSlot);
    const ItemText text = describeItem(before, after, action, chosen.shellType, thiefSlot,
                                       victimSlot, phoneAt, knewPhone);
    for (const Event& event : text.items) log_.push_back(event);
    for (const Event& event : text.learned) log_.push_back(event);
  }

  state_ = after;
  afterAction(before, state_);
  if (action.kind == Action::Kind::UseItem && !action.isAdrenalineAlone()) {
    markPublic(state_, seat, action.isSteal() ? action.stolen : action.item);
  }
  noteOver();
  applySkips();
}

Position Table::view(int seat) const {
  if (seat < 0 || seat >= state_.playerCount) {
    throw std::logic_error("view is called with a seat at the table");
  }
  Position position;
  position.state = state_;
  Tube& tube = position.state.tube;
  const std::uint8_t mine = static_cast<std::uint8_t>(1u << seat);
  const int size = tube.size();
  if (size > 0 && netFlip_ != 0 && !tube.knows(seat, 0)) {
    // An Inverter flipped a chamber this seat has not seen. The counts it can
    // see are the ones loaded, with the flip still pending.
    tube.live = static_cast<std::uint8_t>(tube.live - netFlip_);
    tube.blank = static_cast<std::uint8_t>(tube.blank + netFlip_);
    tube.chamberInverted = true;
  }
  for (int offset = 0; offset < kMaxShells; ++offset) {
    if (offset >= size || !state_.tube.knows(seat, offset)) {
      tube.truth[offset] = Shell::Unknown;
      tube.knownBy[offset] = 0;
      continue;
    }
    std::uint8_t keep = mine;
    if (offset == 0) {
      for (int other = 0; other < state_.playerCount; ++other) {
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << other);
        if (other == seat || (tube.knownBy[0] & bit) == 0) continue;
        const bool isPublic = (publicChamber_ & bit) != 0 || size == 1;
        const bool deduced = other == kDealer && deducedChamber_;
        if (isPublic && !deduced) keep = static_cast<std::uint8_t>(keep | bit);
      }
    }
    tube.knownBy[offset] = keep;
  }
  for (const Read& read : reads_) {
    if (read.seat == seat) continue;
    UnseenRead unseen;
    unseen.seat = static_cast<std::uint8_t>(read.seat);
    unseen.sizeAtUse = static_cast<std::uint8_t>(read.sizeAtUse);
    position.unseenReads.push_back(unseen);
  }
  return position;
}

}  // namespace bsr
