#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "engine/Config.h"
#include "engine/Dealer.h"
#include "engine/Position.h"
#include "engine/State.h"

namespace bsr {

/// How a table is set up.
struct TableOptions {
  RuleConfig config;
  std::uint32_t seed = 0;
  /// Charges each seat starts with. In double or nothing, 0 draws them from 2
  /// to 4 for the round, as the game does (RoundManager.gd 145). A story stage
  /// always takes the charges of its rule set. In multiplayer, 0 means 4.
  int charges = 0;
  /// Seats at the table. Only multiplayer seats more than two.
  int players = 2;
  /// Whether seat 2 is the game's scripted dealer. It applies only at a
  /// two-seat table in story mode or double or nothing.
  bool scriptedDealer = true;
};

/// What the table is waiting for.
enum class Step : std::uint8_t { Load, Choose, Dealer, Over };

/// One thing that happened at the table, in the order it happened. `text` is
/// the narration, in the third person, with lines separated by '\n'.
struct Event {
  enum class Kind : std::uint8_t { Load, Shot, Item, Learned, Rule, Skip, Over };
  Kind kind = Kind::Load;
  int seat = -1;    ///< who acted, learned, was skipped or won
  int target = -1;  ///< who was shot, restrained or robbed
  Item item = Item::MagnifyingGlass;
  Item stolen = Item::MagnifyingGlass;
  bool steal = false;
  int slot = -1;  ///< the tray slot an item left
  Shell shell = Shell::Unknown;
  /// On a shot, the charges the shot actually removed, after the stage 3
  /// clamp (`rules::shotDamage`).
  int damage = 0;
  /// On a learned shell, its offset in the current tube, 0 for the chamber.
  int offset = -1;
  int live = 0;
  int blank = 0;
  /// On a load, how many items each seat was offered.
  int dealt = 0;
  /// On a load, the items each seat received, in the order dealt.
  std::array<std::vector<Item>, 4> dealtItems;
  int first = 0;  ///< on a load, the seat to move
  /// The only seat that may see a learned shell, or -1 when every seat may.
  int privateTo = -1;
  std::string text;
};

/// A round being played: the true state, a seeded generator that draws every
/// load, deal, shell and coin, and what each seat has seen. Every seat that
/// chooses a move should choose it from `view(seat)`, which holds only what
/// that seat can know.
class Table {
 public:
  explicit Table(const TableOptions& options);

  /// A table that starts from a written position, as if its first load had
  /// already been dealt. Returns false and fills `error` when the position
  /// cannot be played from.
  static bool fromPosition(const Position& position, const TableOptions& options, Table* out,
                           std::string* error);

  /// What the table waits for. A seat that has to lose its turn has already
  /// lost it.
  Step next() const;

  /// Load the gun and deal. Only when `next()` is `Step::Load`.
  void load();

  /// One pass of the scripted dealer's turn. Only when `next()` is
  /// `Step::Dealer`.
  void dealerPass();

  /// Every move the seat to move may make. Only when `next()` is
  /// `Step::Choose`.
  std::vector<Action> legal() const;

  /// Make a move from `legal()`. `traySlot`, when given, names the tray slot
  /// the item is taken from, which must hold a copy the move may use.
  void play(const Action& action, int traySlot = -1);

  /// The position as `seat` can know it: shells it has not seen go back to
  /// the pool, the other seats' phone reads become reads it never saw, and
  /// the dealer's memory is the memory a turn starts with.
  Position view(int seat) const;

  const GameState& state() const { return state_; }
  const RuleConfig& config() const { return config_; }
  const dealer::Memory& dealerMemory() const { return memory_; }
  /// True between passes of the dealer's turn that did not shoot.
  bool dealerMidTurn() const { return midTurn_; }

  /// Each seat's eight places on the table, holding item indices, -1 when
  /// empty. A deal fills the lowest free place and a use empties its place.
  std::array<int, kMaxItemsPerSeat> tray(int seat) const;
  /// The tray slot of `seat`'s `ordinal`-th copy of `item`, or -1.
  int slotOf(int seat, Item item, int ordinal) const;
  /// The items a load draws from. The game takes the Hand Saw out of the
  /// first load of a round at 2 charges (ItemManager.gd 235-236, 357-358).
  std::vector<Item> poolFor(int loadIndex, int charges) const;
  /// Live and blank shells left, counted by the type each was loaded as. A
  /// chamber an Inverter flipped counts as what it was loaded as.
  std::pair<int, int> loadedCounts() const;

  int loadNumber() const { return loadNumber_; }
  int charges() const { return charges_; }
  const std::vector<Event>& log() const { return log_; }

 private:
  /// A Burner Phone use at a tube of two shells or more: who used it, the tube
  /// size then, and the offset it named in the current tube, or -1 once that
  /// shell has left.
  struct Read {
    int seat = 0;
    int sizeAtUse = 0;
    int offset = -1;
  };

  Table() = default;

  std::uint32_t draw();
  int uniformInt(int lo, int hi);
  double unitReal();
  std::size_t sample(const std::vector<double>& weights);

  std::string name(int seat) const;
  std::string sentenceName(int seat) const;
  void applySkips();
  void noteSkips(const GameState& before, const GameState& after);
  void noteOver();
  void afterAction(const GameState& before, const GameState& after);
  void spend(int seat, int handIndex, int slot);
  void markPublic(const GameState& after, int seat, Item item);
  int phoneOffset(const GameState& before, const GameState& after, int seat, bool anyOffset,
                  int rank) const;
  void recordRead(int seat, const GameState& before, int offset);
  /// The events an item use makes: the item events every seat sees, and the
  /// shells the user learned, which only it sees.
  struct ItemText {
    std::vector<Event> items;
    std::vector<Event> learned;
  };
  ItemText describeItem(const GameState& before, const GameState& after, const Action& action,
                        Shell racked, int thiefSlot, int victimSlot, int phoneAt,
                        bool knewPhone) const;
  Event describeShot(const GameState& before, const GameState& after, const Action& action,
                     Shell fired) const;

  TableOptions options_;
  RuleConfig config_;
  std::mt19937 rng_;
  GameState state_;
  int charges_ = 0;
  int loadNumber_ = 0;
  bool scripted_ = false;
  dealer::Brain brain_ = dealer::Brain::Endless;
  dealer::Memory memory_;
  bool midTurn_ = false;
  IndexedArray<std::vector<int>, kMaxPlayers> slots_{};
  std::vector<Event> log_;

  std::vector<Read> reads_;
  /// The dealer's bit on the chamber came from working it out, not from
  /// anything the other seats saw it do.
  bool deducedChamber_ = false;
  /// Seats whose bit on the chamber came from an act every seat saw.
  std::uint8_t publicChamber_ = 0;
  /// Live shells the Inverter added to the counts on the current chamber,
  /// minus the ones it took away.
  int netFlip_ = 0;
  /// Seats whose lost turn the next load voids, one bit each.
  unsigned unsaidSkips_ = 0;

  /// What set the target the dealer carries this turn, for narration.
  std::string aimWhen_;
  bool aimFromChamber_ = false;
  bool chamberRacked_ = false;
};

}  // namespace bsr
