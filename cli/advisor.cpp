/// Position advisor. Type the position you are looking at, or narrate the round
/// as it happens, and get the legal moves ranked by the probability that you
/// are the last player standing, under the stated opponent model.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define BSR_ISATTY _isatty
#define BSR_FILENO _fileno
#else
#include <unistd.h>
#define BSR_ISATTY isatty
#define BSR_FILENO fileno
#endif

#include "cli/Args.h"
#include "cli/RuleFlags.h"
#include "engine/Dealer.h"
#include "engine/Notation.h"
#include "engine/Rules.h"
#include "solver/Solver.h"

namespace {

using namespace bsr;

/// The dealer's seat whenever the dealer is the opponent.
constexpr int kDealerSeat = 1;

/// What a narrated round knows that a position written as notation does not.
struct Narration {
  /// The dealer has used an item in the turn it is still taking. What it
  /// decided earlier in that turn is not part of a position, so an answer from
  /// here takes the turn as starting afresh, and says so.
  bool dealerMidTurn = false;
  /// Shells, one bit per offset, recorded as seen by another seat without the
  /// advised seat seeing what they were. Each is pinned to a type the counts
  /// allow, which changes nothing for the advised seat, and the board prints
  /// it without that type.
  std::uint8_t unseen = 0;
  /// What the dealer has settled in the turn it is still taking: the shell it
  /// saw, the seat it aims at, and whether it has taken Expired Medicine. It
  /// is followed only against the dealer's script, and a fresh memory is the
  /// one a turn starts with.
  dealer::Memory dealerMemory{};
  /// Burner Phone uses by other seats this load, whose reads the advised seat
  /// never heard.
  std::vector<UnseenRead> unseenReads;
};

/// A point `undo` returns to.
struct Snapshot {
  GameState state;
  Narration narration;
};

struct Session {
  GameState state;
  RuleConfig config = RuleConfig::doubleOrNothing(4);
  SolveOptions options;
  Narration narration;
  std::vector<Snapshot> history;
  /// Rule settings typed on the command line or with `rule`. Kept so that a
  /// later `mode` command, which rebuilds the configuration from a preset,
  /// does not quietly throw them away.
  std::vector<std::pair<std::string, std::string>> ruleSettings;
  /// Whether p2 plays the game's dealer script, set with `opponent`. Kept apart
  /// from `options.opponent`, which `mode` sets, so that neither command
  /// quietly undoes the other; it is checked against the table at `advise`.
  bool scriptedDealer = false;
};

/// Read an opponent model's name. Only the two a two-seat table can use are
/// named here; multiplayer picks its own when its mode is chosen. The
/// minimising opponent is "solver", as in play; its older name "optimal" is
/// still taken.
bool parseOpponent(const std::string& text, bool* scripted) {
  if (text == "dealer") {
    *scripted = true;
    return true;
  }
  if (text == "solver" || text == "optimal") {
    *scripted = false;
    return true;
  }
  return false;
}

/// Put the preset for a mode's name in force. Returns false, changing nothing,
/// for a name that is not a mode.
bool applyMode(Session* session, const std::string& mode) {
  if (mode == "don") {
    session->config = RuleConfig::doubleOrNothing(session->state.players[0].maxHp);
  } else if (mode.rfind("story", 0) == 0) {
    long round = 2;
    if (mode.size() > 5 && !cli::parseWholeNumber(mode.substr(5), 1, 3, &round)) return false;
    session->config = RuleConfig::storyRound(static_cast<int>(round));
  } else if (mode == "mp" || mode == "multiplayer") {
    session->config =
        RuleConfig::multiplayer(session->state.playerCount, session->state.players[0].maxHp);
    session->options.opponent = OpponentModel::Paranoid;
  } else {
    return false;
  }
  return true;
}

/// Rebuild the settings on top of whatever preset is now in force.
bool reapplySettings(Session* session) {
  std::string error;
  if (cli::applyRuleSettings(session->ruleSettings, &session->config, &error)) return true;
  std::cout << error << "\n";
  return false;
}

std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> words;
  std::istringstream stream(line);
  std::string word;
  while (stream >> word) words.push_back(word);
  return words;
}

bool parseSeat(const std::string& text, const GameState& state, int* seat) {
  return cli::parseSeatToken(text, state.playerCount, state.current, seat);
}

bool parseShell(const std::string& text, Shell* shell) {
  if (text == "live" || text == "l") {
    *shell = Shell::Live;
    return true;
  }
  if (text == "blank" || text == "b") {
    *shell = Shell::Blank;
    return true;
  }
  return false;
}

/// Whether the chamber can fire the observed type at all. A chamber that has
/// already been pinned down can only fire what it was pinned to, and one that
/// is still a draw needs the pool to hold a shell that would produce it, which
/// with an inversion pending is the opposite type.
bool chamberCanFire(const Tube& tube, Shell fired, std::string* why) {
  if (tube.canFire(fired)) return true;
  if (tube.empty()) {
    *why = "The tube is empty. Use load to start the next one.\n";
  } else if (tube.truth[0] != Shell::Unknown) {
    *why = "That contradicts the chamber, which is already recorded as ";
    *why += tube.truth[0] == Shell::Live ? "live" : "blank";
    *why += ".\n";
  } else {
    const Shell drawn =
        tube.chamberInverted ? (fired == Shell::Live ? Shell::Blank : Shell::Live) : fired;
    *why = "That contradicts the tube: it does not hold another ";
    *why += drawn == Shell::Live ? "live" : "blank";
    *why += " shell.\n";
  }
  return false;
}

/// A tube cannot hold more shells of a type than its counts allow. Any command
/// that records a fact about a shell has to answer this before it is believed.
bool tubeStillFits(const Tube& tube) {
  int live = 0;
  int blank = 0;
  for (int i = 0; i < tube.size(); ++i) {
    if (tube.truth[i] == Shell::Live) ++live;
    if (tube.truth[i] == Shell::Blank) ++blank;
  }
  return live <= tube.live && blank <= tube.blank;
}

std::string contradictionMessage(Shell shell) {
  return std::string("That contradicts the tube: it does not hold another ") +
         (shell == Shell::Live ? "live" : "blank") + " shell.\n";
}

std::uint8_t allSeats(const GameState& state) {
  return static_cast<std::uint8_t>((1u << state.playerCount) - 1u);
}

Shell opposite(Shell shell) {
  return shell == Shell::Live ? Shell::Blank : Shell::Live;
}

Snapshot snapshot(const Session& session) {
  return Snapshot{session.state, session.narration};
}

void restore(Session* session, const Snapshot& point) {
  session->state = point.state;
  session->narration = point.narration;
}

/// The session as a whole position, as `set` reads one and `state` prints it.
Position positionOf(const Session& session) {
  Position position;
  position.state = session.state;
  position.dealerMemory = session.narration.dealerMemory;
  position.unseenReads = session.narration.unseenReads;
  return position;
}

/// The `dealer=` token for a memory in `state`, or nothing for a fresh memory.
std::string memoryToken(const GameState& state, const dealer::Memory& memory) {
  Position position;
  position.state = state;
  position.dealerMemory = memory;
  const std::string whole = notation::printPosition(position);
  const std::string bare = notation::print(state);
  return whole.size() > bare.size() ? whole.substr(bare.size() + 1) : std::string();
}

/// The lines a board leaves out: the stale item list, the phone reads nobody
/// heard and the dealer's memory part-way through its turn.
std::string positionExtras(const Position& position) {
  std::ostringstream out;
  if (position.state.dealerListCigs) {
    out << "  the dealer's item list from its last pass holds p1's Cigarettes\n";
  }
  for (int seat = 0; seat < position.state.playerCount; ++seat) {
    std::vector<int> sizes;
    for (const UnseenRead& read : position.unseenReads) {
      if (read.seat == seat) sizes.push_back(read.sizeAtUse);
    }
    if (sizes.empty()) continue;
    std::sort(sizes.begin(), sizes.end(), [](int a, int b) { return a > b; });
    out << "  p" << (seat + 1) << " used a burner phone you did not hear, at tube size"
        << (sizes.size() == 1 ? " " : "s ");
    for (std::size_t i = 0; i < sizes.size(); ++i) out << (i > 0 ? ", " : "") << sizes[i];
    out << "\n";
  }
  const std::string token = memoryToken(position.state, position.dealerMemory);
  if (!token.empty()) out << "  the dealer is part-way through its turn: " << token << "\n";
  return out.str();
}

/// Whether the shell at `offset` has a type on record that the advised seat
/// has not seen. Another seat looked at it, and the type was either typed in
/// by somebody guessing or recorded as unseen.
bool typeUnseenByAdvised(const Session& session, int offset) {
  const Tube& tube = session.state.tube;
  return tube.truth[offset] != Shell::Unknown && !tube.knows(session.options.seat, offset);
}

/// Pin a shell whose type the advised seat never saw to the type it turned out
/// to be, keeping who saw it. Nothing about the advised seat's answer rested on
/// the old type, and the public counts never moved with it (an inversion
/// unpins such a shell first), so only the counts can refuse it. Returns false,
/// changing nothing, when they do.
bool repin(Session* session, int offset, Shell type) {
  Tube next = session->state.tube;
  next.truth[offset] = type;
  if (!tubeStillFits(next)) return false;
  session->state.tube = next;
  return true;
}

/// A shell of `type` is about to be drawn at `offset`, which nobody has pinned,
/// but every unpinned shell of that type may be held by pins the advised seat
/// never saw. Swap one of those to the other type, which leaves the counts as
/// they are. When nothing can be swapped the caller's own check speaks.
void makeRoomFor(Session* session, Shell type, int offset) {
  const Tube& tube = session->state.tube;
  const int free = type == Shell::Live ? tube.unresolvedLive() : tube.unresolvedBlank();
  if (free > 0) return;
  for (int other = 0; other < tube.size(); ++other) {
    if (other == offset || tube.truth[other] != type) continue;
    if (!typeUnseenByAdvised(*session, other)) continue;
    if (repin(session, other, opposite(type))) return;
  }
}

/// Before a shell fires or is shown as `fired`, bring the record into line with
/// it wherever only a type the advised seat never saw stands in the way.
/// Returns false, with the reason printed, when the counts rule it out.
bool fitChamber(Session* session, Shell fired) {
  const Tube& tube = session->state.tube;
  if (tube.empty()) return true;
  if (tube.truth[0] == Shell::Unknown) {
    makeRoomFor(session, tube.chamberInverted ? opposite(fired) : fired, 0);
    return true;
  }
  if (tube.truth[0] == fired || !typeUnseenByAdvised(*session, 0)) return true;
  if (repin(session, 0, fired)) return true;
  std::cout << contradictionMessage(fired);
  return false;
}

/// Show every seat a chamber that the dealer's script says is `type`. A type
/// only other seats saw gives way to it where the counts allow, and a flip the
/// advised seat did not see is first undone for it, as it now sees the chamber
/// itself. Returns false, changing nothing, with the reason in `why` when the
/// record rules it out.
bool showChamber(Session* session, Shell type, std::string* why) {
  const Snapshot point = snapshot(*session);
  session->state.tube.unflipFor(session->options.seat);
  const Tube& fitted = session->state.tube;
  if (fitted.truth[0] != Shell::Unknown && fitted.truth[0] != type &&
      typeUnseenByAdvised(*session, 0) && !repin(session, 0, type)) {
    restore(session, point);
    *why = contradictionMessage(type);
    return false;
  }
  if (fitted.truth[0] == Shell::Unknown) {
    makeRoomFor(session, fitted.chamberInverted ? opposite(type) : type, 0);
  }
  if (!chamberCanFire(session->state.tube, type, why)) {
    restore(session, point);
    return false;
  }
  Tube& tube = session->state.tube;
  if (tube.truth[0] == Shell::Unknown) {
    tube.resolveChamberDraw(tube.chamberInverted ? opposite(type) : type, allSeats(session->state));
  } else {
    tube.resolve(0, type, allSeats(session->state));
  }
  // Every seat has now seen the chamber, so nobody holds the counts from
  // before a flip.
  tube.pinnedFlip = false;
  if (!tubeStillFits(tube)) {
    restore(session, point);
    *why = contradictionMessage(type);
    return false;
  }
  session->narration.unseen = static_cast<std::uint8_t>(session->narration.unseen & ~1u);
  return true;
}

/// An inversion is about to flip a chamber whose type only other seats saw.
/// Flipping the type on record would move the public counts by whichever type
/// was recorded, which the advised seat cannot know, so the chamber goes back
/// to the unseen pool first and the flip is kept as pending, as for any shell
/// nobody has seen. What the other seats saw of it is lost, which the caller
/// says. Returns whether anything was unpinned.
bool unpinBeforeInversion(Session* session) {
  if (session->state.tube.empty() || !typeUnseenByAdvised(*session, 0)) return false;
  Tube& tube = session->state.tube;
  if (tube.pinnedFlip) {
    // The chamber was already flipped after another seat pinned it down, so
    // the counts go back to the ones the advised seat holds, with that flip
    // pending.
    tube.unflipFor(session->options.seat);
  } else {
    tube.truth[0] = Shell::Unknown;
    tube.knownBy[0] = 0;
  }
  session->narration.unseen = static_cast<std::uint8_t>(session->narration.unseen & ~1u);
  return true;
}

void sayUnpinned() {
  std::cout << "The chamber was inverted while only another seat knew its type, so it is kept "
               "as an inverted shell nobody has seen, and that seat no longer counts as "
               "knowing it.\n";
}

/// Bring the narration up to date after an event moved the position on from
/// `before`.
void afterEvent(Session* session, const GameState& before, bool itemUse) {
  Narration& noted = session->narration;
  const GameState& now = session->state;
  const int left = before.tube.size() - now.tube.size();
  if (left > 0) noted.unseen = static_cast<std::uint8_t>(noted.unseen >> left);
  noted.dealerMidTurn = itemUse && before.playerCount == 2 && before.current == kDealerSeat &&
                        now.current == kDealerSeat && !now.roundOver() && !now.needsReload();
}

/// Whether a seat's phone reads that nobody heard include any by `seat`.
bool hasUnseenReads(const Session& session, int seat) {
  for (const UnseenRead& read : session.narration.unseenReads) {
    if (read.seat == seat) return true;
  }
  return false;
}

/// The dealer's memory after one of its item uses, under the script's rules
/// for what each item leaves behind (DealerIntelligence.gd 151-215). `before`
/// is the position the item was used in, with any shell it showed already
/// recorded, and `shell` the type a Beer ejected. A memory the rules say
/// cannot arise from what was narrated gives way to a fresh one.
dealer::Memory nextMemory(const Session& session, const GameState& passStart,
                          const GameState& before, const Action& action, Shell shell,
                          bool* readsNote) {
  dealer::Brain brain = dealer::Brain::Endless;
  if (action.kind != Action::Kind::UseItem || !dealer::brainFor(session.config, &brain)) {
    return dealer::Memory{};
  }
  const GameState& now = session.state;
  dealer::Memory memory = session.narration.dealerMemory;
  const auto learn = [&memory](Shell type) {
    memory.knows = true;
    memory.known = type;
    memory.target = type == Shell::Live ? dealer::Target::Player : dealer::Target::Self;
  };
  // A chamber the dealer saw is whatever the record now says it is, which can
  // differ from a stand-in typed before the shell showed itself.
  if (memory.knows && before.tube.knows(kDealerSeat, 0)) learn(before.tube.truth[0]);
  const Item effect = action.isSteal() ? action.stolen : action.item;
  switch (effect) {
    case Item::MagnifyingGlass:
      if (now.tube.knows(kDealerSeat, 0)) learn(now.tube.truth[0]);
      break;
    case Item::Beer:
      if (memory.knows) {
        // The endless rules forget the shell and keep the target; the story
        // rules keep both (DealerIntelligence.gd 170-176).
        if (brain == dealer::Brain::Endless) {
          memory.knows = false;
          memory.known = Shell::Unknown;
        }
      } else if (brain == dealer::Brain::Endless && memory.target == dealer::Target::None &&
                 shell == Shell::Blank) {
        if (hasUnseenReads(session, kDealerSeat)) {
          *readsNote = true;
        } else {
          // Whether the dealer had worked the chamber out at the start of
          // this pass, from what it had seen before the Beer showed the shell
          // to everyone (DealerIntelligence.gd 96-104).
          GameState probe = before;
          probe.tube.knownBy[0] = passStart.tube.knownBy[0];
          if (dealer::deduces(probe)) memory.target = dealer::Target::Self;
        }
      }
      break;
    case Item::HandSaw:
      // A saw after a live chamber it saw keeps that; otherwise only the coin
      // saws, which aims at p1 without knowing the chamber (DealerIntelligence.gd
      // 181, 203-215).
      if (!(memory.knows && memory.known == Shell::Live)) {
        memory.knows = false;
        memory.known = Shell::Unknown;
        memory.target = dealer::Target::Player;
      }
      break;
    case Item::Inverter:
      if (now.tube.knows(kDealerSeat, 0)) learn(now.tube.truth[0]);
      break;
    case Item::ExpiredMedicine:
      memory.usedMedicine = true;
      break;
    default:
      break;
  }
  Position check;
  check.state = now;
  check.dealerMemory = memory;
  std::string ignored;
  if (dealer::validateMemory(check, brain, &ignored)) return memory;
  dealer::Memory medicineOnly;
  medicineOnly.usedMedicine = memory.usedMedicine;
  check.dealerMemory = medicineOnly;
  if (dealer::validateMemory(check, brain, &ignored)) return medicineOnly;
  return dealer::Memory{};
}

/// Bring the narration up to date after an event that started from
/// `passStart`: `before` is the same position with any shell the event showed
/// already recorded, and `shell` the type that left the tube, if one did. A
/// dealer pass also rewrites the stale item list (DealerIntelligence.gd
/// 113-149), and against the script the session follows the dealer's memory
/// until its turn ends.
void finishEvent(Session* session, const GameState& passStart, const GameState& before,
                 const Action& action, Shell shell) {
  const bool dealerPass =
      session->scriptedDealer && passStart.playerCount == 2 && passStart.current == kDealerSeat;
  bool readsNote = false;
  dealer::Memory memory{};
  if (dealerPass) {
    memory = nextMemory(*session, passStart, before, action, shell, &readsNote);
    session->state.dealerListCigs = dealer::listCigsAfterPass(passStart, session->state);
  }
  afterEvent(session, before, action.kind == Action::Kind::UseItem);
  const bool underWay = dealerPass && session->narration.dealerMidTurn;
  session->narration.dealerMemory = underWay ? memory : dealer::Memory{};
  if (underWay && readsNote) {
    std::cout << "The dealer may have worked out that shell from its phone reads; its memory "
                 "after the Beer is taken as fresh.\n";
  }
}

/// Whether `action` is a move the seat to move can make.
bool isLegal(const Session& session, const Action& action) {
  for (const Action& candidate : rules::legalActions(session.state, session.config)) {
    if (candidate == action) return true;
  }
  return false;
}

/// Take what an item use spends from the hands, for the uses whose effect is
/// recorded here rather than by the rules: a steal spends the thief's first
/// Adrenaline and the copy it took, any other use the copy it names.
void pay(GameState* state, const Action& action) {
  if (action.isSteal()) {
    state->players[state->current].hand.removeCopy(Item::Adrenaline, 0);
    state->players[action.stealFrom].hand.removeCopy(action.stolen, action.copy);
  } else {
    state->players[state->current].hand.removeCopy(action.item, action.copy);
  }
}

/// The board, with the shells the advised seat never saw printed without the
/// type they are pinned to.
void printBoard(const Session& session) {
  std::uint8_t untyped = 0;
  for (int offset = 0; offset < session.state.tube.size(); ++offset) {
    if ((session.narration.unseen >> offset & 1u) == 0) continue;
    if (!typeUnseenByAdvised(session, offset)) continue;
    untyped = static_cast<std::uint8_t>(untyped | (1u << offset));
  }
  std::cout << notation::board(session.state, untyped) << positionExtras(positionOf(session));
}

/// Apply an action whose chance outcome is already known, by taking the branch
/// that matches what actually happened.
bool applyWithOutcome(Session* session, const Action& action, bool haveShell, Shell shell) {
  const Snapshot point = snapshot(*session);
  if (!isLegal(*session, action)) {
    std::cout << "That move is not available here: the seat to move either does not hold the "
                 "item or cannot use it in this position.\n";
    return false;
  }
  if (haveShell) {
    if (!fitChamber(session, shell)) {
      restore(session, point);
      return false;
    }
    std::string why;
    if (!chamberCanFire(session->state.tube, shell, &why)) {
      restore(session, point);
      std::cout << why;
      return false;
    }
    // Force the chamber to what was observed. With an inversion pending, the
    // shell drawn from the pool is the opposite of the one that fired, and
    // resolveChamberDraw is what moves the public counts to match.
    if (session->state.tube.chamberInverted) {
      const Shell drawn = shell == Shell::Live ? Shell::Blank : Shell::Live;
      session->state.tube.resolveChamberDraw(drawn, allSeats(session->state));
    } else {
      session->state.tube.resolve(0, shell, allSeats(session->state));
    }
    if (!tubeStillFits(session->state.tube)) {
      restore(session, point);
      std::cout << contradictionMessage(shell);
      return false;
    }
  }
  const bool itemUse = action.kind == Action::Kind::UseItem;
  const Item effect = action.isSteal() ? action.stolen : action.item;
  const bool unpinned = itemUse && effect == Item::Inverter && unpinBeforeInversion(session);
  const GameState before = session->state;
  std::vector<Outcome> outcomes = rules::apply(before, action, session->config);
  if (outcomes.empty()) {
    restore(session, point);
    std::cout << "That move is not available here.\n";
    return false;
  }
  // With the chamber pinned there is only one branch with any weight left.
  const Outcome* chosen = &outcomes.front();
  for (const Outcome& outcome : outcomes) {
    if (outcome.probability > chosen->probability) chosen = &outcome;
  }
  session->history.push_back(point);
  session->state = chosen->state;
  if (unpinned) sayUnpinned();
  finishEvent(session, point.state, before, action, haveShell ? shell : Shell::Unknown);
  return true;
}

/// Expired medicine has two branches of equal weight, so the caller says which
/// one happened and the matching branch is taken. They are told apart by the
/// failure, which always takes a charge (MedicineManager.gd 25-27): a success
/// can leave the charges as they were, at the maximum or below the heal floor
/// (HealthCounter.gd 141-153).
bool applyMedicine(Session* session, const Action& action, bool healed) {
  if (!isLegal(*session, action)) {
    std::cout << "That move is not available here.\n";
    return false;
  }
  const int seat = session->state.current;
  const GameState before = session->state;
  for (const Outcome& outcome : rules::apply(before, action, session->config)) {
    const bool failed = outcome.state.players[seat].hp < before.players[seat].hp;
    if (failed == healed) continue;
    session->history.push_back(snapshot(*session));
    session->state = outcome.state;
    finishEvent(session, before, before, action, Shell::Unknown);
    return true;
  }
  std::cout << "That outcome is not possible here.\n";
  return false;
}

/// "The dealer's" or "p2's", for the seat to move.
std::string whoseItem(const Session& session) {
  const int seat = session.state.current;
  if (session.scriptedDealer && seat == kDealerSeat) return "The dealer's";
  return "p" + std::to_string(seat + 1) + "'s";
}

/// The command that records a seat's own use of `item`, for messages.
std::string ownForm(Item item) {
  switch (item) {
    case Item::MagnifyingGlass:
      return "mg";
    case Item::BurnerPhone:
      return "phone";
    case Item::Beer:
      return "eject";
    default:
      return std::string("use ") + itemToken(item);
  }
}

/// Point `action` at the copy of `item` that `holder` gives up: the
/// `ordinal`-th copy counted from the front of the hand. Copies in one run
/// leave the same hand behind, so a move names the first copy of the run.
/// When the hand has no such copy and none was named, the move is left as it
/// is for the legality check to refuse.
bool chooseCopy(const GameState& state, int holder, Item item, int ordinal, bool named,
                Action* action) {
  const Hand& hand = state.players[holder].hand;
  int index = hand.indexOfCopy(item, ordinal);
  if (index < 0) {
    if (!named) return true;
    std::cout << "p" << (holder + 1) << " holds no " << itemName(item) << " #" << (ordinal + 1)
              << ".\n";
    return false;
  }
  while (index > 0 && hand.at[static_cast<std::size_t>(index - 1)] == item) --index;
  action->copy = static_cast<std::uint8_t>(hand.ordinalAt(index));
  return true;
}

void sayNotAvailable() {
  std::cout << "That move is not available here: the seat to move either does not hold the "
               "item or cannot use it in this position.\n";
}

/// Whether p2 is to move and plays the game's dealer script.
bool scriptedDealerToMove(const Session& session) {
  return session.scriptedDealer && session.state.playerCount == 2 &&
         session.state.current == kDealerSeat;
}

/// A Magnifying Glass, the seat's own or stolen. `rest` is what it showed:
/// live, blank, or unseen when only the seat using it saw.
bool recordGlass(Session* session, const Action& action, const std::string& form,
                 const std::vector<std::string>& rest) {
  const int seat = session->state.current;
  const bool own = seat == session->options.seat;
  const std::string word = rest.empty() ? std::string() : rest[0];
  const bool unseen = word == "unseen";
  Shell shell = Shell::Unknown;
  if (!unseen && !parseShell(word, &shell)) {
    if (word.empty() && !own) {
      // Only the seat using it learns what it says, and the advised seat's
      // answer is the same whatever that was.
      std::cout << whoseItem(*session) << " glass is private: type " << form << " unseen (or "
                << form << " live or " << form << " blank if you know; the answer is the same).\n";
    } else {
      std::cout << "Say what it showed, as in " << form << " live, or " << form
                << " unseen when only the seat using it saw.\n";
    }
    return false;
  }
  if (session->state.tube.empty()) {
    std::cout << "The tube is empty.\n";
    return false;
  }
  if (unseen && own) {
    std::cout << "You saw what your own glass showed, so say which, as in " << form << " live.\n";
    return false;
  }
  if (!isLegal(*session, action)) {
    sayNotAvailable();
    return false;
  }
  const Snapshot point = snapshot(*session);
  const Tube& chamber = session->state.tube;
  if (!own && chamber.truth[0] == Shell::Unknown && chamber.chamberInverted) {
    // Pinning a shell with an inversion pending moves the public counts by
    // the type it is pinned to, which the advised seat did not see.
    const GameState before = session->state;
    session->history.push_back(point);
    pay(&session->state, action);
    finishEvent(session, point.state, before, action, Shell::Unknown);
    std::cout << "The chamber was inverted before anyone saw it, so the glass is recorded as "
                 "used and what it showed is left out: p"
              << (seat + 1) << " is treated as not knowing the chamber.\n";
    return true;
  }
  if (unseen) {
    // Any type the tube can supply will do: the advised seat's answer
    // averages over it either way.
    const Tube& tube = session->state.tube;
    shell = tube.truth[0] != Shell::Unknown ? tube.truth[0]
            : tube.canFire(Shell::Live)     ? Shell::Live
                                            : Shell::Blank;
  } else if (!fitChamber(session, shell)) {
    return false;
  }
  std::string why;
  if (!chamberCanFire(session->state.tube, shell, &why)) {
    restore(session, point);
    std::cout << why;
    return false;
  }
  GameState probe = session->state;
  if (probe.tube.chamberInverted && probe.tube.truth[0] == Shell::Unknown) {
    probe.tube.resolveChamberDraw(opposite(shell), static_cast<std::uint8_t>(1u << seat));
  } else {
    probe.tube.resolve(0, shell, static_cast<std::uint8_t>(1u << seat));
  }
  if (!tubeStillFits(probe.tube)) {
    restore(session, point);
    std::cout << contradictionMessage(shell);
    return false;
  }
  const GameState before = session->state;
  session->history.push_back(point);
  session->state = probe;
  pay(&session->state, action);
  if (unseen) session->narration.unseen = static_cast<std::uint8_t>(session->narration.unseen | 1u);
  finishEvent(session, point.state, before, action, Shell::Unknown);
  return true;
}

/// A Burner Phone, the seat's own or stolen. The advised seat heard its own
/// phone, so it says which shell and what it is; any other seat's read is
/// recorded as unseen, because the game never shows it (BurnerPhone.gd 6,
/// DealerIntelligence.gd 187-194). With one shell left a phone names nothing
/// (BurnerPhone.gd 13, 32), so any seat's use is recorded with no result. The
/// dealer's script uses a phone, its own or a stolen one, only with more than
/// two shells in the tube (DealerIntelligence.gd 187).
bool recordPhone(Session* session, const Action& action, const std::string& form,
                 const std::vector<std::string>& rest) {
  const int seat = session->state.current;
  const bool own = seat == session->options.seat;
  const int size = session->state.tube.size();
  // The shell an example names: shell 3, or shell 2 when only two are left.
  const std::string example = std::to_string(std::min(3, std::max(2, size)));
  if (scriptedDealerToMove(*session) && size <= 2) {
    std::cout << "The dealer uses a burner phone only with more than two shells in the tube.\n";
    return false;
  }
  const bool unseen = rest.size() == 1 && rest[0] == "unseen";
  if (unseen || (rest.empty() && size < 2)) {
    if (own && size >= 2) {
      std::cout << "You heard what your own phone said, so say which, as in " << form << " "
                << example << " live.\n";
      return false;
    }
    if (!isLegal(*session, action)) {
      sayNotAvailable();
      return false;
    }
    int reads = 0;
    for (const UnseenRead& read : session->narration.unseenReads) {
      if (read.seat == seat) ++reads;
    }
    if (size >= 2 && reads >= 8) {
      std::cout << "a seat makes at most 8 phone reads in one load\n";
      return false;
    }
    const Snapshot point = snapshot(*session);
    session->history.push_back(point);
    pay(&session->state, action);
    if (size >= 2) {
      UnseenRead read;
      read.seat = static_cast<std::uint8_t>(seat);
      read.sizeAtUse = static_cast<std::uint8_t>(size);
      session->narration.unseenReads.push_back(read);
    }
    finishEvent(session, point.state, point.state, action, Shell::Unknown);
    return true;
  }
  if (size < 2) {
    std::cout << "With one shell left a burner phone names nothing, so type " << form
              << " on its own.\n";
    return false;
  }
  if (rest.empty() && !own) {
    std::cout << whoseItem(*session) << " phone is private: type " << form << " unseen.\n";
    return false;
  }
  long parsed = 0;
  const bool number = rest.size() == 2 && cli::parseWholeNumber(rest[0], 0, 64, &parsed);
  if (number && !own) {
    std::cout << "the game never shows which shell another seat's phone named; type phone "
                 "unseen\n";
    return false;
  }
  if (number && rest[1] == "unseen") {
    std::cout << "You heard what your own phone said, so say which, as in " << form << " "
              << example << " live.\n";
    return false;
  }
  const int position = number ? static_cast<int>(parsed) : 0;
  Shell shell = Shell::Unknown;
  if (!number || position < 2 || position > size || !parseShell(rest[1], &shell)) {
    std::cout << "Say which shell your phone named and what it is, as in " << form << " " << example
              << " blank, or " << form
              << " unseen for another seat's phone. A burner phone never names the chamber, so "
                 "the number starts at 2.\n";
    return false;
  }
  // The player's phone moves a pick of the eighth shell to the seventh, so it
  // never names shell 8 of 8; the dealer's can (BurnerPhone.gd 13-15,
  // DealerIntelligence.gd 187-194).
  const std::array<double, kMaxShells> weights =
      rules::phoneOffsetWeights(seat, session->state.playerCount, size);
  if (weights[static_cast<std::size_t>(position - 1)] <= 0.0) {
    std::cout << "This phone never names shell " << position << " when " << size
              << " shells are loaded: it names shells 2 to " << (size - 1) << ".\n";
    return false;
  }
  if (!isLegal(*session, action)) {
    sayNotAvailable();
    return false;
  }
  const int offset = position - 1;
  const Snapshot point = snapshot(*session);
  const Shell already = session->state.tube.truth[offset];
  if (already == Shell::Unknown) {
    makeRoomFor(session, shell, offset);
  } else if (already != shell) {
    if (!typeUnseenByAdvised(*session, offset)) {
      std::cout << "Shell " << position << " is already recorded as "
                << (already == Shell::Live ? "live" : "blank") << ".\n";
      return false;
    }
    if (!repin(session, offset, shell)) {
      std::cout << contradictionMessage(shell);
      return false;
    }
  }
  GameState probe = session->state;
  probe.tube.resolve(offset, shell, static_cast<std::uint8_t>(1u << seat));
  if (!tubeStillFits(probe.tube)) {
    restore(session, point);
    std::cout << contradictionMessage(shell);
    return false;
  }
  const GameState before = session->state;
  session->history.push_back(point);
  session->state = probe;
  pay(&session->state, action);
  finishEvent(session, point.state, before, action, Shell::Unknown);
  return true;
}

/// Take the one branch an item use has once its outcome is settled, keeping
/// `point` as the place `undo` returns to.
bool settleItem(Session* session, const Snapshot& point, const Action& action) {
  const GameState before = session->state;
  std::vector<Outcome> outcomes = rules::apply(before, action, session->config);
  if (outcomes.empty()) {
    restore(session, point);
    sayNotAvailable();
    return false;
  }
  const Outcome* chosen = &outcomes.front();
  for (const Outcome& outcome : outcomes) {
    if (outcome.probability > chosen->probability) chosen = &outcome;
  }
  session->history.push_back(point);
  session->state = chosen->state;
  finishEvent(session, point.state, before, action, Shell::Unknown);
  return true;
}

/// The dealer's saw, its own or stolen, against its script. A dealer that
/// knows the chamber saws only a live one (DealerIntelligence.gd 181), and the
/// saw coin needs a chamber it does not take to be blank (203-215), so a saw
/// from a dealer that saw the chamber, or holds the last shell, shows every
/// seat a live chamber. The endless rules also count a chamber the dealer saw
/// before its turn (96-104, 282-283). A dealer that only believes a blank
/// never saws, and any other saw is the coin's, which says nothing about the
/// chamber.
bool recordDealerSaw(Session* session, const Action& action) {
  const Tube& tube = session->state.tube;
  const dealer::Memory& memory = session->narration.dealerMemory;
  dealer::Brain brain = dealer::Brain::Endless;
  dealer::brainFor(session->config, &brain);
  const bool sawChamber =
      tube.knows(kDealerSeat, 0) && (memory.knows || brain == dealer::Brain::Endless);
  const bool lastShell = tube.size() == 1;
  if (!memory.knows && !sawChamber && !lastShell) {
    return applyWithOutcome(session, action, false, Shell::Unknown);
  }
  if (!isLegal(*session, action)) {
    sayNotAvailable();
    return false;
  }
  if (memory.knows && !sawChamber && !lastShell) {
    std::cout << "The dealer saws only a chamber it takes to be live, and it takes this one to "
                 "be blank.\n";
    return false;
  }
  const Snapshot point = snapshot(*session);
  std::string why;
  if (!showChamber(session, Shell::Live, &why)) {
    std::cout << "The dealer saws only a chamber it knows is live. " << why;
    return false;
  }
  return settleItem(session, point, action);
}

/// The dealer's Inverter, against its script. The script uses it only on a
/// chamber it takes to be blank, and then writes the chamber live rather than
/// flipping it (DealerIntelligence.gd 195-201), so every seat sees a live
/// chamber afterwards. Where the dealer knows the chamber, it was blank and
/// the counts move by one blank to live: the endless rules never keep a stale
/// shell (96-104, 170-176), and the last shell is always known (106-112).
/// Where it only believes a blank, the counts move only if the chamber was
/// blank, which the record settles only when the advised seat has seen it.
bool recordDealerInversion(Session* session, const Action& action) {
  if (!isLegal(*session, action)) {
    sayNotAvailable();
    return false;
  }
  if (session->state.tube.empty()) {
    std::cout << "The tube is empty.\n";
    return false;
  }
  const Snapshot point = snapshot(*session);
  const Tube& tube = session->state.tube;
  dealer::Brain brain = dealer::Brain::Endless;
  dealer::brainFor(session->config, &brain);
  const bool knowsChamber = brain == dealer::Brain::Endless || tube.size() == 1 ||
                            (session->narration.dealerMemory.knows && tube.knows(kDealerSeat, 0));
  if (knowsChamber) {
    std::string why;
    if (!showChamber(session, Shell::Blank, &why)) {
      std::cout << "The dealer inverts only a chamber it knows is blank. " << why;
      return false;
    }
  } else if (!tube.knows(session->options.seat, 0)) {
    std::cout << "The dealer has not looked at this chamber in its turn, so whether its Inverter "
                 "turned a blank live, and so moved the counts, rests on a shell nobody saw. The "
                 "advisor cannot follow that.\n";
    return false;
  }
  const GameState before = session->state;
  Tube& written = session->state.tube;
  if (written.truth[0] == Shell::Blank) {
    ++written.live;
    --written.blank;
  }
  written.truth[0] = Shell::Live;
  written.knownBy[0] = allSeats(session->state);
  written.chamberInverted = false;
  written.pinnedFlip = false;
  session->narration.unseen = static_cast<std::uint8_t>(session->narration.unseen & ~1u);
  pay(&session->state, action);
  session->history.push_back(point);
  finishEvent(session, point.state, before, action, Shell::Unknown);
  return true;
}

/// One item use, the seat's own or stolen, once the words naming the item and
/// its copy are read. `form` is the command as typed up to the result, for
/// messages, and `rest` is what follows it: a target, what a glass showed,
/// what a phone named, what a Beer ejected or how medicine went.
bool runItem(Session* session, Action action, const std::string& form,
             const std::vector<std::string>& rest) {
  const Item effect = action.isSteal() ? action.stolen : action.item;
  if (itemNeedsTarget(effect)) {
    int target = 0;
    if (rest.empty() || !parseSeat(rest[0], session->state, &target)) {
      std::cout << "That item needs a target, as in " << form << " p2.\n";
      return false;
    }
    action.target = static_cast<std::uint8_t>(target);
    return applyWithOutcome(session, action, false, Shell::Unknown);
  }
  // Items whose result is a matter of chance are recorded with the result
  // named, so that the advisor tracks what actually happened rather than the
  // likeliest branch.
  switch (effect) {
    case Item::MagnifyingGlass:
      return recordGlass(session, action, form, rest);
    case Item::BurnerPhone:
      return recordPhone(session, action, form, rest);
    case Item::Beer: {
      Shell shell = Shell::Unknown;
      if (rest.empty() || !parseShell(rest[0], &shell)) {
        std::cout << "Say what was ejected, as in " << form << " blank.\n";
        return false;
      }
      return applyWithOutcome(session, action, true, shell);
    }
    case Item::ExpiredMedicine: {
      const std::string outcome = rest.empty() ? std::string() : rest[0];
      if (outcome != "ok" && outcome != "bad") {
        std::cout << "Expired medicine is a toss up, so say how it went: " << form << " ok, or "
                  << form << " bad.\n";
        return false;
      }
      return applyMedicine(session, action, outcome == "ok");
    }
    case Item::HandSaw:
      if (scriptedDealerToMove(*session)) return recordDealerSaw(session, action);
      break;
    case Item::Inverter:
      if (scriptedDealerToMove(*session)) return recordDealerInversion(session, action);
      break;
    default:
      break;
  }
  return applyWithOutcome(session, action, false, Shell::Unknown);
}

/// Who acts first after a reload under `config`.
const char* reloadTurnText(const RuleConfig& config) {
  switch (config.reloadTurn) {
    case ReloadTurn::PlayerFirst:
      return "p1 acts first";
    case ReloadTurn::DealerFirst:
      return "p2 acts first";
    case ReloadTurn::KeepCurrent:
      return "the seat to move keeps the turn";
  }
  return "";
}

/// Whether a position with the dealer to move carries something only its own
/// turn could have left there: a sawed barrel or a cuff it put on p1. Every
/// shot clears the saw, and p1 cannot cuff itself. An inverted chamber is not
/// a sign, because p1's own Inverter leaves one for the dealer's turn.
bool looksMidDealerTurn(const GameState& state, int seat) {
  return state.tube.sawed || state.cuffUsedThisTurn || state.players[seat].cuffed;
}

/// `narratedMidTurn` is set when the narration recorded the dealer using an
/// item in the turn it is still taking.
void printRanking(const SolveResult& result, const Position& position, const RuleConfig& config,
                  int seat, OpponentModel opponent, bool narratedMidTurn) {
  const GameState& state = position.state;
  if (result.refused) {
    std::cout << result.assumptions << "\n";
    return;
  }
  const auto printChance = [&result]() {
    std::cout << "  Your chance of being the last player standing: " << std::fixed
              << std::setprecision(4) << result.value << "\n";
  };
  const auto printFooter = [&result]() {
    if (result.budgetReached) {
      std::cout << "  Note: the search hit its reload budget in some lines, so those were "
                   "valued by charges in hand.\n";
    }
    if (result.nodeLimitHit) {
      std::cout << "  Note: the search stopped at the node limit; values may be wrong.\n";
    }
    std::cout << "  " << result.assumptions << "\n";
    std::cout << "  " << result.nodes << (result.nodes == 1 ? " state" : " states")
              << " examined.\n\n";
  };
  // Neither of these changes when a handcuffed seat is skipped, so the
  // position as given answers both, whatever the opponent model.
  if (state.roundOver()) {
    std::cout << "\nAdvising seat p" << (seat + 1) << ". The round is over";
    const int survivor = state.soleSurvivor();
    if (survivor >= 0) {
      std::cout << ": p" << (survivor + 1) << " is the last player standing.\n";
    } else {
      std::cout << ", with nobody left standing.\n";
    }
    printChance();
    std::cout << "\n";
    return;
  }
  if (state.needsReload()) {
    // A narrated round asks for the next load before it asks for advice, so
    // only a written position gets here.
    std::cout << "\nAdvising seat p" << (seat + 1)
              << ". The tube is empty and a reload is due (after a reload "
              << reloadTurnText(config)
              << "). To ask about the next load, give its shells in tube=.\n";
    printChance();
    printFooter();
    return;
  }
  if (result.ranked.empty() && opponent == OpponentModel::Dealer && result.mover != seat) {
    // The dealer does not choose between moves, so there is nothing to rank,
    // but the position still has a worth to the advised seat.
    std::cout << "\nAdvising seat p" << (seat + 1) << ". The dealer (p" << (result.mover + 1)
              << ") is to move and plays by its script";
    const bool skipped = result.mover != static_cast<int>(state.current);
    if (skipped) {
      std::cout << " (p" << (static_cast<int>(state.current) + 1) << " is handcuffed and skipped)";
    }
    std::cout << ".\n";
    printChance();
    const std::string token = memoryToken(state, position.dealerMemory);
    if (narratedMidTurn && token.empty()) {
      std::cout << "  Note: the dealer is part-way through its turn and has not yet seen the "
                   "chamber or chosen a target, so the value goes on from there.\n";
    } else if (narratedMidTurn) {
      std::cout << "  Note: the dealer is part-way through its turn, and the value goes on from "
                   "what it has settled so far ("
                << token << ").\n";
    } else if (!token.empty()) {
      std::cout << "  Its turn is taken to be under way, with " << token << ".\n";
    } else if (!skipped && looksMidDealerTurn(state, seat)) {
      std::cout << "  Note: this looks like the middle of a dealer turn, and the value assumes its "
                   "turn starts here. If it is, say what the dealer has settled with dealer=, as "
                   "in dealer=aim:p1.\n";
    } else {
      std::cout << "  Its turn is taken to start here.\n";
    }
    printFooter();
    return;
  }
  if (result.ranked.empty()) {
    std::cout << "No legal move from this position.\n";
    return;
  }
  std::cout << "\nAdvising seat p" << (seat + 1) << ", to move: p" << (result.mover + 1);
  if (result.mover != static_cast<int>(state.current)) {
    std::cout << " (p" << (static_cast<int>(state.current) + 1) << " is handcuffed and skipped)";
  }
  std::cout << "\n";
  const double top = result.ranked.front().value;
  for (const ActionValue& entry : result.ranked) {
    // An opponent's ranking is sorted the other way, so the test has to be
    // symmetric or every row looks best.
    const bool best = std::abs(top - entry.value) < 1e-9;
    // A stolen item makes for a long name, and a name that fills the column
    // used to run into the number after it.
    std::string name = entry.action.describe(result.mover);
    if (name.size() >= 34) name += " ";
    std::cout << (best ? "  * " : "    ") << std::left << std::setw(34) << name << std::right
              << std::fixed << std::setprecision(4) << entry.value;
    if (!best) {
      std::cout << "   (" << std::showpos << std::setprecision(4) << (entry.value - top)
                << std::noshowpos << ")";
    }
    std::cout << "\n";
  }
  // A tie among rows that are averages is an artefact of the averaging, not a
  // choice the seat holding the gun faces, so it is only worth saying when the
  // rows mean what they look like.
  const bool rowsAreAverages = result.opponentKnownShells > 0 && result.mover != seat;
  const std::vector<Action> best = result.bestActions();
  if (best.size() > 1 && !rowsAreAverages) {
    std::cout << "  " << best.size() << " moves tie at the top; any of them is optimal.\n";
  }
  if (rowsAreAverages) {
    // Every row above is averaged over a shell p<mover> can see and this seat
    // cannot, so no row says what that seat will actually do. The position is
    // worth what its choice makes it, which is lower than the rows look.
    std::cout << "  p" << (result.mover + 1) << " has looked at " << result.opponentKnownShells
              << " shell" << (result.opponentKnownShells == 1 ? "" : "s")
              << " you have not, so the rows above average over what it saw and you did not. "
                 "It does not have to average: this position is worth "
              << std::fixed << std::setprecision(4) << result.value << " to you.\n";
  }
  printFooter();
}

/// An item that the chosen rule set never deals is still parsed, still printed
/// on the board, and still used by whichever seat holds it, the dealer's script
/// included; only a reload never brings more. Holding one is usually a sign
/// that the mode is not the one meant, so the answer says so.
std::string itemsOutsideThePool(const GameState& state, const RuleConfig& config) {
  std::string names;
  for (int index = 0; index < kItemCount; ++index) {
    const Item item = itemAt(index);
    bool held = false;
    for (int seat = 0; seat < state.playerCount; ++seat) {
      if (state.players[seat].hand.holds(item)) held = true;
    }
    if (!held) continue;
    if (std::find(config.itemPool.begin(), config.itemPool.end(), item) != config.itemPool.end()) {
      continue;
    }
    if (!names.empty()) names += ", ";
    names += itemName(item);
  }
  if (names.empty()) return names;
  return "These rules never deal " + names +
         ", so no reload brings one, but a seat already holding one still uses it. Check the "
         "mode if that is not the table meant: " +
         config.describe();
}

void printHelp() {
  std::cout << R"(Commands

  Position
    set <notation>        replace the position, e.g.
                          set p1=3/4[saw,beer] p2=2/4[mg] tube=2L3B turn=p1
                          A written position may also carry listcigs,
                          phoned= and dealer=, described at the end.
    load <n>L<m>B         start a fresh load: clears knowledge, cuffs and
                          phone reads, keeps a sawed barrel where the rules
                          say so, and hands the turn to the seat the rules
                          name
    state                 print the position in one line
    board                 print the position as a board
    seat p<N>             advise this seat (default p1)
    mode don|story<N>|mp  double or nothing, story stage N, or multiplayer
    opponent solver|dealer
                          how p2 plays: to minimise your chance (default), or
                          by the game's dealer script, which needs two seats,
                          story or double or nothing, and seat p1 advised.
                          opponent alone says which is in force
    reloads <n>           how many reloads to look through (default 2)
    rule <name> <value>   change a rule setting, as in
                          rule reload-turn keep. rules lists them all
    rules                 the rule settings and what they are set to

  Edits
    hp p<N> <charges>     set charges
    give p<N> <item>      add an item at the end of the hand
    take p<N> <item>      remove an item, the first copy unless #k names one
    turn p<N>             hand the turn to a seat
    cuff p<N>             cuff a seat        uncuff p<N>
    saw                   mark the barrel sawed
    invert                mark the chamber inverted

  Events, as they happen
    shot self live        you shot yourself and it was live
    shot p<N> blank       the seat to move shot p<N> and it was blank
    eject live|blank      a beer ejected a shell of that type
    mg live|blank|unseen  a magnifying glass showed the seat to move that
                          shell; unseen when only that seat saw it
    phone <k> live|blank  your burner phone named shell k, counting from 2,
                          since a burner phone never names the chamber; with
                          8 shells loaded only the dealer's phone names
                          shell 8
    phone unseen          another seat used a burner phone; the game never
                          shows which shell it named
    phone                 any seat used a burner phone with one shell left,
                          when it names nothing
    use <item> [p<N>]     the seat to move used an item with no chance outcome
    use med ok|bad        expired medicine, and how it went
    use adr               adrenaline spent on its own, taking nothing
    use adr p<N> <item> ...
                          the seat to move stole an item from p<N> and used
                          it; what follows is what that item's own command
                          takes, as in use adr p2 beer blank, use adr p1 mg
                          unseen or use adr p2 cuff p2

  Copies
    #k                    add to take, use, mg, phone or eject to name the
                          k-th copy of the item, counting from the front of
                          the hand it leaves (the victim's, for a steal), as
                          in take p1 beer #2 or use adr p2 cig #2. The board
                          numbers copies of a type held in more than one
                          place. Without #k the first copy goes.

  Other
    advise (or a blank line)   rank the legal moves
    undo    help    quit

  Tokens only a written position carries
    listcigs              the dealer's item list from its last pass holds
                          Cigarettes that p1 owns
    phoned=p2@5,4         p2 used a burner phone this load at tube sizes 5
                          and 4, and you never heard where it looked
    dealer=seen           the dealer is part-way through its turn with this
                          memory: seen, believes:B, aim:self or aim:p1, then
                          med after it took expired medicine, or med alone.
                          A narrated dealer turn keeps it as it goes
)";
}

const char* opponentName(OpponentModel opponent) {
  switch (opponent) {
    case OpponentModel::Optimal:
      return "solver";
    case OpponentModel::Dealer:
      return "dealer";
    case OpponentModel::Paranoid:
      return "paranoid";
  }
  return "";
}

/// Print a result as JSON, so another implementation can be compared against
/// this one move by move. `mover` and `opponent` tell an empty list of actions
/// with the scripted dealer to move apart from a position with no move at all.
void printJson(const SolveResult& result, OpponentModel opponent) {
  std::cout << "{\"value\": " << std::fixed << std::setprecision(12) << result.value
            << ", \"mover\": \"p" << (result.mover + 1) << "\", \"opponent\": \""
            << opponentName(opponent) << "\", \"refused\": " << (result.refused ? "true" : "false")
            << ", \"actions\": [";
  for (std::size_t i = 0; i < result.ranked.size(); ++i) {
    if (i > 0) std::cout << ", ";
    std::cout << "{\"action\": \"" << result.ranked[i].action.describe(result.mover)
              << "\", \"value\": " << result.ranked[i].value << "}";
  }
  std::cout << "], \"nodes\": " << result.nodes
            << ", \"truncated\": " << (result.truncated ? "true" : "false")
            << ", \"budgetReached\": " << (result.budgetReached ? "true" : "false")
            << ", \"nodeLimitHit\": " << (result.nodeLimitHit ? "true" : "false") << "}\n";
}

int runOnce(const std::string& text, int seat, int reloads, long long nodeLimit,
            const std::string& mode, bool scriptedDealer, bool asJson,
            const std::vector<std::pair<std::string, std::string>>& ruleSettings) {
  Position position;
  std::string error;
  if (!notation::parsePosition(text, &position, &error)) {
    std::cerr << error << "\n";
    return 1;
  }
  const GameState& state = position.state;
  if (seat < 0 || seat >= state.playerCount) {
    std::cerr << "this position has " << static_cast<int>(state.playerCount)
              << " seats, so --seat must be between 1 and " << static_cast<int>(state.playerCount)
              << "\n";
    return 1;
  }
  RuleConfig config = RuleConfig::doubleOrNothing(state.players[0].maxHp);
  SolveOptions options;
  options.seat = seat;
  options.reloadBudget = reloads;
  options.nodeLimit = nodeLimit;
  if (mode == "mp" || mode == "multiplayer") {
    config = RuleConfig::multiplayer(state.playerCount, state.players[0].maxHp);
    options.opponent = OpponentModel::Paranoid;
  } else if (mode.rfind("story", 0) == 0) {
    long round = 2;
    if (mode.size() > 5 && !cli::parseWholeNumber(mode.substr(5), 1, 3, &round)) {
      std::cerr << "Modes: don, story1, story2, story3, mp.\n";
      return 1;
    }
    config = RuleConfig::storyRound(static_cast<int>(round));
  } else if (mode != "don") {
    std::cerr << "Modes: don, story1, story2, story3, mp.\n";
    return 1;
  }
  std::string settingError;
  if (!cli::applyRuleSettings(ruleSettings, &config, &settingError)) {
    std::cerr << settingError << "\n";
    return 1;
  }
  if (scriptedDealer) {
    std::string reason;
    if (!dealerSupported(state, config, options, &reason)) {
      std::cerr << "--opponent dealer: " << reason << "\n";
      return 1;
    }
    options.opponent = OpponentModel::Dealer;
  }
  const std::string outside = itemsOutsideThePool(state, config);
  if (!outside.empty() && !asJson) std::cerr << outside << "\n";
  const SolveResult result = solve(position, config, options);
  if (asJson) {
    printJson(result, options.opponent);
  } else {
    std::cout << notation::board(state) << positionExtras(position);
    printRanking(result, position, config, seat, options.opponent, false);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Session session;
  std::string error;
  if (!notation::parse("p1=4/4 p2=4/4 tube=2L2B turn=p1", &session.state, &error)) {
    std::cerr << "internal: " << error << "\n";
    return 1;
  }
  std::string startPosition;
  std::vector<std::pair<std::string, std::string>> ruleSettings;
  std::string startMode = "don";
  int startSeat = 0;
  int reloads = 2;
  long long nodeLimit = SolveOptions{}.nodeLimit;
  bool asJson = false;
  bool scriptedDealer = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    long number = 0;
    if (arg == "--help" || arg == "-h") {
      std::cout << "advisor [--position \"<notation>\"] [--seat N] [--reloads N]\n"
                   "        [--mode don|story1|story2|story3|mp] [--opponent solver|dealer]\n"
                   "        [--node-limit N] [--json] [rule settings, listed below]\n\n"
                   "--seat, --reloads, --mode and --opponent set the starting value of the "
                   "command\nof the same name below (seat p1, 2 reloads, don and solver by "
                   "default).\n--position answers that one position and exits; --json prints "
                   "that answer as\nJSON and needs --position.\n"
                   "--node-limit stops a search from looking further once it has met N new\n"
                   "positions (1 to 10000000000, 40000000 by default) and says so, since\n"
                   "the values it gives then may be wrong. Each new position the lines\n"
                   "still open reach is then scored by charges in hand and counted once,\n"
                   "so the count can end above N.\n\n";
      printHelp();
      std::cout << "\n" << cli::ruleSettingsHelp();
      return 0;
    }
    if (arg == "--position" || arg == "-p") {
      // A written position is the one value that may look like a flag.
      if (i + 1 >= argc) {
        std::cerr << arg << " needs a position\n";
        return 2;
      }
      startPosition = argv[++i];
    } else if (arg == "--seat") {
      if (!cli::nextNumber(argc, argv, &i, arg, 1, kMaxPlayers, &number)) return 2;
      startSeat = static_cast<int>(number) - 1;
    } else if (arg == "--reloads") {
      if (!cli::nextNumber(argc, argv, &i, arg, 0, 6, &number)) return 2;
      reloads = static_cast<int>(number);
    } else if (arg == "--node-limit") {
      long long limit = 0;
      if (!cli::nextNumber(argc, argv, &i, arg, 1LL, 10000000000LL, &limit)) return 2;
      nodeLimit = limit;
    } else if (arg == "--mode") {
      if (!cli::nextValue(argc, argv, &i, arg, &startMode)) return 2;
    } else if (arg == "--opponent") {
      std::string value;
      if (!cli::nextValue(argc, argv, &i, arg, &value)) return 2;
      if (!parseOpponent(value, &scriptedDealer)) {
        std::cerr << "--opponent takes solver or dealer, not " << value << "\n";
        return 2;
      }
    } else if (arg == "--json") {
      asJson = true;
    } else if (cli::isRuleSetting(arg)) {
      std::string value;
      if (!cli::nextValue(argc, argv, &i, arg, &value)) return 2;
      std::string settingError;
      RuleConfig probe;
      if (!cli::applyRuleSetting(arg, value, &probe, &settingError)) {
        std::cerr << settingError << "\n";
        return 2;
      }
      ruleSettings.emplace_back(arg, value);
    } else {
      std::cerr << "unrecognised option " << arg << "\n";
      return 2;
    }
  }
  if (!startPosition.empty()) {
    return runOnce(startPosition, startSeat, reloads, nodeLimit, startMode, scriptedDealer, asJson,
                   ruleSettings);
  }
  if (asJson) {
    std::cerr << "--json prints one answer, so it needs --position\n";
    return 2;
  }
  // --mode sets the starting mode as the mode command would.
  if (!applyMode(&session, startMode)) {
    std::cerr << "Modes: don, story1, story2, story3, mp.\n";
    return 2;
  }
  session.ruleSettings = ruleSettings;
  if (!reapplySettings(&session)) return 2;
  session.options.seat = startSeat;
  session.options.reloadBudget = reloads;
  session.options.nodeLimit = nodeLimit;
  session.scriptedDealer = scriptedDealer;

  if (BSR_ISATTY(BSR_FILENO(stdin)) != 0) {
    std::cout << "Buckshot Roulette advisor. Type help for commands, quit to leave.\n";
    std::cout << notation::board(session.state) << "\n";
  }

  // A prompt is for a person. When input is piped, leaving it out keeps a
  // captured session readable.
  const bool interactive = BSR_ISATTY(BSR_FILENO(stdin)) != 0;
  std::string line;
  while (true) {
    if (interactive) std::cout << "> " << std::flush;
    if (!std::getline(std::cin, line)) {
      std::cout << "\n";
      break;
    }
    std::vector<std::string> words = tokenize(line);
    if (words.empty()) words.emplace_back("advise");
    std::string command = words[0];
    std::transform(command.begin(), command.end(), command.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // One word may name which copy of an item a command means, as #2 does.
    // A written position never holds one, so `set` keeps its line as typed.
    int copyOrdinal = 0;
    bool copyNamed = false;
    bool badCopy = false;
    if (command != "set" && command != "pos") {
      for (auto word = words.begin() + 1; word != words.end();) {
        if (word->empty() || (*word)[0] != '#') {
          ++word;
          continue;
        }
        int ordinal = 0;
        if (copyNamed || !cli::parseCopySelector(*word, &ordinal)) {
          badCopy = true;
          break;
        }
        copyOrdinal = ordinal;
        copyNamed = true;
        word = words.erase(word);
      }
    }
    if (badCopy) {
      std::cout << "Name at most one copy, from #1 to #8, as in take p1 beer #2.\n";
      continue;
    }
    if (copyNamed && command == "give") {
      std::cout << "give adds the item at the end of the hand, so it takes no copy number.\n";
      continue;
    }
    if (copyNamed && command != "take" && command != "use" && command != "mg" &&
        command != "phone" && command != "eject" && command != "beer") {
      std::cout << "Only take, use, mg, phone and eject name a copy, as in eject blank #2.\n";
      continue;
    }

    if (command == "quit" || command == "exit") break;
    if (command == "help") {
      printHelp();
      continue;
    }
    if (command == "state") {
      std::cout << notation::printPosition(positionOf(session)) << "\n";
      continue;
    }
    if (command == "board") {
      printBoard(session);
      continue;
    }
    if (command == "undo") {
      if (session.history.empty()) {
        std::cout << "Nothing to undo.\n";
      } else {
        restore(&session, session.history.back());
        session.history.pop_back();
        printBoard(session);
      }
      continue;
    }
    if (command == "set" || command == "pos") {
      const std::size_t pos = line.find(words[0]) + words[0].size();
      Position parsed;
      if (!notation::parsePosition(line.substr(pos), &parsed, &error)) {
        std::cout << error << "\n";
        continue;
      }
      session.history.push_back(snapshot(session));
      session.state = parsed.state;
      session.narration = Narration{};
      session.narration.dealerMemory = parsed.dealerMemory;
      session.narration.unseenReads = parsed.unseenReads;
      printBoard(session);
      continue;
    }
    if (command == "seat" && words.size() >= 2) {
      int seat = 0;
      if (!parseSeat(words[1], session.state, &seat) || seat >= session.state.playerCount) {
        std::cout << "Name a seat at this table, as in seat p1.\n";
        continue;
      }
      session.options.seat = seat;
      std::cout << "Advising seat p" << (seat + 1) << ".\n";
      continue;
    }
    if (command == "reloads" && words.size() >= 2) {
      long budget = 0;
      if (!cli::parseWholeNumber(words[1], 0, 6, &budget)) {
        std::cout << "Give a number of reloads between 0 and 6.\n";
        continue;
      }
      session.options.reloadBudget = static_cast<int>(budget);
      std::cout << "Looking through " << session.options.reloadBudget << " reloads.\n";
      continue;
    }
    if (command == "mode" && words.size() >= 2) {
      if (!applyMode(&session, words[1])) {
        std::cout << "Modes: don, story1, story2, story3, mp.\n";
        continue;
      }
      reapplySettings(&session);
      std::cout << session.config.describe() << "\n";
      continue;
    }
    if (command == "opponent") {
      if (words.size() < 2) {
        if (session.scriptedDealer) {
          std::cout << "p2 plays by the game's dealer script (dealer). Type opponent solver to "
                       "switch.\n";
        } else if (session.config.mode == Mode::Multiplayer) {
          std::cout << "Every other seat plays to minimise your chance (solver), the only model "
                       "multiplayer has.\n";
        } else {
          std::cout << "p2 plays to minimise your chance (solver). Type opponent dealer to "
                       "switch.\n";
        }
        continue;
      }
      if (!parseOpponent(words[1], &session.scriptedDealer)) {
        std::cout << "opponent takes solver or dealer, not " << words[1]
                  << ", as in opponent dealer.\n";
        continue;
      }
      std::cout << (session.scriptedDealer ? "p2 plays by the game's dealer script.\n"
                                           : "p2 plays to minimise your chance.\n");
      continue;
    }
    if (command == "rule" && words.size() >= 3) {
      const std::string flag = words[1].rfind("--", 0) == 0 ? words[1] : "--" + words[1];
      std::string settingError;
      RuleConfig probe = session.config;
      if (!cli::applyRuleSetting(flag, words[2], &probe, &settingError)) {
        std::cout << settingError << "\n";
        continue;
      }
      session.ruleSettings.emplace_back(flag, words[2]);
      session.config = probe;
      std::cout << session.config.describe() << "\n";
      continue;
    }
    if (command == "rule" || command == "rules") {
      std::cout << cli::ruleSettingsHelp() << "\n"
                << session.config.describe() << "\n"
                << "opponent: "
                << (session.scriptedDealer ? "dealer script" : "solver, minimising your chance")
                << "\n";
      continue;
    }
    if (command == "load" && words.size() >= 2) {
      std::string spec = "p1=1/1 p2=1/1 tube=" + words[1];
      GameState probe;
      if (!notation::parse(spec, &probe, &error)) {
        std::cout << error << "\n";
        continue;
      }
      if (probe.tube.empty()) {
        std::cout << "A load holds at least one shell, as in load 2L3B.\n";
        continue;
      }
      // The same reload the search makes: the saw, the cuffs and the turn
      // follow the rules in force. What anyone knew about the old tube, its
      // phone reads and the dealer's turn memory all go with it.
      GameState next = session.state;
      rules::reloadInto(&next, probe.tube.live, probe.tube.blank, session.config);
      session.history.push_back(snapshot(session));
      session.state = next;
      session.narration = Narration{};
      printBoard(session);
      std::cout << "p" << (static_cast<int>(next.current) + 1) << " is to move.\n";
      continue;
    }
    if (command == "hp" && words.size() >= 3) {
      int seat = 0;
      if (!parseSeat(words[1], session.state, &seat)) {
        std::cout << "Name a seat, as in hp p1 3.\n";
        continue;
      }
      long parsed = 0;
      if (!cli::parseWholeNumber(words[2], 0, 64, &parsed)) {
        std::cout << "Give a number of charges.\n";
        continue;
      }
      const int charges = static_cast<int>(parsed);
      PlayerState& player = session.state.players[seat];
      if (charges > player.maxHp) {
        std::cout << "That seat holds at most " << static_cast<int>(player.maxHp) << " charges.\n";
        continue;
      }
      session.history.push_back(snapshot(session));
      player.hp = static_cast<std::uint8_t>(charges);
      printBoard(session);
      continue;
    }
    if ((command == "give" || command == "take") && words.size() >= 3) {
      int seat = 0;
      Item item;
      if (!parseSeat(words[1], session.state, &seat) || !itemFromToken(words[2], &item)) {
        std::cout << "Say which seat and which item, as in give p1 saw.\n";
        continue;
      }
      Hand& hand = session.state.players[seat].hand;
      if (command == "give" && hand.size() >= kMaxItemsPerSeat) {
        std::cout << "a seat holds at most 8 items\n";
        continue;
      }
      if (command == "take" && hand.indexOfCopy(item, copyOrdinal) < 0) {
        std::cout << "p" << (seat + 1);
        if (copyNamed) {
          std::cout << " holds no " << itemName(item) << " #" << (copyOrdinal + 1) << ".\n";
        } else {
          std::cout << " is not holding a " << itemName(item) << ".\n";
        }
        continue;
      }
      session.history.push_back(snapshot(session));
      if (command == "give") {
        hand.append(item);
      } else {
        hand.removeCopy(item, copyOrdinal);
      }
      printBoard(session);
      continue;
    }
    if (command == "turn" && words.size() >= 2) {
      int seat = 0;
      if (!parseSeat(words[1], session.state, &seat)) {
        std::cout << "Name a seat, as in turn p2.\n";
        continue;
      }
      session.history.push_back(snapshot(session));
      session.state.current = static_cast<std::uint8_t>(seat);
      // A seat that is handed the turn plays it, so it is no longer owed the
      // turn a restraint took.
      session.state.players[seat].skipConsumed = false;
      session.state.cuffUsedThisTurn = false;
      session.narration.dealerMidTurn = false;
      session.narration.dealerMemory = dealer::Memory{};
      printBoard(session);
      continue;
    }
    if ((command == "cuff" || command == "uncuff") && words.size() >= 2) {
      int seat = 0;
      if (!parseSeat(words[1], session.state, &seat)) {
        std::cout << "Name a seat, as in cuff p2.\n";
        continue;
      }
      session.history.push_back(snapshot(session));
      session.state.players[seat].cuffed = command == "cuff";
      // A seat waiting to lose a turn has not lost one yet.
      if (command == "cuff") session.state.players[seat].skipConsumed = false;
      printBoard(session);
      continue;
    }
    if (command == "saw" || command == "invert") {
      session.history.push_back(snapshot(session));
      if (command == "saw") {
        session.state.tube.sawed = true;
      } else {
        if (unpinBeforeInversion(&session)) sayUnpinned();
        session.state.tube.invertChamber();
      }
      printBoard(session);
      continue;
    }
    if (command == "shot" && words.size() >= 3) {
      int target = 0;
      Shell shell;
      if (!parseSeat(words[1], session.state, &target) || !parseShell(words[2], &shell)) {
        std::cout << "Say who was shot and what came out, as in shot self live.\n";
        continue;
      }
      if (applyWithOutcome(&session, Action::shoot(target), true, shell)) {
        printBoard(session);
      }
      continue;
    }
    if (command == "eject" || command == "beer" || command == "mg" || command == "phone") {
      const Item item = command == "mg"      ? Item::MagnifyingGlass
                        : command == "phone" ? Item::BurnerPhone
                                             : Item::Beer;
      Action action = Action::use(item);
      if (!chooseCopy(session.state, session.state.current, item, copyOrdinal, copyNamed,
                      &action)) {
        continue;
      }
      const std::vector<std::string> rest(words.begin() + 1, words.end());
      if (runItem(&session, action, command == "beer" ? "beer" : ownForm(item), rest)) {
        printBoard(session);
      }
      continue;
    }
    if (command == "use" && words.size() >= 2) {
      Item item;
      if (!itemFromToken(words[1], &item)) {
        std::cout << "No item is called " << words[1] << ".\n";
        continue;
      }
      const int mover = session.state.current;
      if (item != Item::Adrenaline) {
        Action action = Action::use(item);
        if (!chooseCopy(session.state, mover, item, copyOrdinal, copyNamed, &action)) continue;
        const std::vector<std::string> rest(words.begin() + 2, words.end());
        if (runItem(&session, action, ownForm(item), rest)) printBoard(session);
        continue;
      }
      if (words.size() == 2) {
        // Spent with nothing taken. Whichever Adrenaline goes, the hand left
        // behind is the same as far as any rule reads it.
        if (copyNamed) {
          std::cout << "An Adrenaline used on its own is the first in the hand, so it takes no "
                       "copy number.\n";
          continue;
        }
        if (applyWithOutcome(&session, Action::adrenalineAlone(mover), false, Shell::Unknown)) {
          printBoard(session);
        }
        continue;
      }
      // A steal is two decisions: whose item, and which one. Without both it
      // would silently take whatever the default is.
      int from = 0;
      Item stolen;
      if (words.size() < 4 || !parseSeat(words[2], session.state, &from) ||
          !itemFromToken(words[3], &stolen)) {
        std::cout << "Say whose item and which one, as in use adr p2 saw, or use adr alone to "
                     "spend it taking nothing.\n";
        continue;
      }
      if (stolen == Item::Adrenaline) {
        std::cout << "Adrenaline cannot take another adrenaline.\n";
        continue;
      }
      if (!session.state.players[from].hand.holds(stolen)) {
        std::cout << "p" << (from + 1) << " is not holding a " << itemName(stolen) << ".\n";
        continue;
      }
      Action action = Action::steal(from, stolen);
      if (!chooseCopy(session.state, from, stolen, copyOrdinal, copyNamed, &action)) continue;
      const std::vector<std::string> rest(words.begin() + 4, words.end());
      if (runItem(&session, action, "use adr " + words[2] + " " + words[3], rest)) {
        printBoard(session);
      }
      continue;
    }
    if (command == "advise" || command == "go") {
      if (session.state.needsReload()) {
        std::cout << "The tube is empty. Start the next load, as in load 2L3B.\n";
        continue;
      }
      const std::string outside = itemsOutsideThePool(session.state, session.config);
      if (!outside.empty()) std::cout << outside << "\n";
      SolveOptions options = session.options;
      const Position position = positionOf(session);
      if (session.scriptedDealer) {
        std::string reason;
        if (!dealerSupported(session.state, session.config, options, &reason)) {
          std::cout << "The dealer opponent cannot answer this: " << reason
                    << ". Type opponent solver to go back.\n";
          continue;
        }
        options.opponent = OpponentModel::Dealer;
      }
      const SolveResult result = solve(position, session.config, options);
      printRanking(result, position, session.config, options.seat, options.opponent,
                   session.narration.dealerMidTurn);
      continue;
    }
    std::cout << "I do not know the command " << words[0] << ". Type help.\n";
  }
  return 0;
}
