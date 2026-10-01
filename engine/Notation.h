#pragma once

#include <cstdint>
#include <string>

#include "engine/Config.h"
#include "engine/Position.h"
#include "engine/State.h"

namespace bsr {

/// A position as one line of text, so that a position can be typed, printed,
/// pinned in a test, pasted into an issue and diffed.
///
/// Grammar, tokens separated by spaces and accepted in any order:
///
///   p1=3/4[saw,beer]   seat 1 has 3 charges of 4 and holds a saw and a beer,
///                      in the order it received them; at most 8 items
///   tube=2L3B          two live and three blank shells remain
///   turn=p1            seat 1 is to move
///   cuffed=p2          seat 2 is handcuffed and will be skipped
///   skipped=p2         seat 2 lost a turn to handcuffs and is owed one back,
///                      so it cannot be restrained again until it has played
///   restraintused      the seat to move has already spent a restraint
///   sawed              the barrel is sawed for the next shot
///   inverted           an inverter flipped a chamber nobody has seen
///   known=p1:0L,2B     seat 1 has seen the chamber (live) and offset 2 (blank)
///   dir=ccw            turn order runs counter clockwise (a remote was used)
///   listcigs           the dealer's item list from its last pass still holds
///                      Cigarettes that p1 owns (two seats only)
///
/// A whole position, read by `parsePosition`, may also carry:
///
///   phoned=p2@5,4      seat 2 used a Burner Phone this load at tube sizes 5
///                      and 4, and the reader never saw where it looked
///   dealer=seen,med    the dealer is in the middle of its turn with this
///                      memory: one of seen, believes:B, aim:self and aim:p1,
///                      then med when it has taken Expired Medicine, or med
///                      alone
///
/// Item tokens are mg, beer, cig, cuff, saw, phone, adr, inv, med, jam, rem.
namespace notation {

/// Parse a position. Returns false and fills `error` when the text does not
/// describe a consistent position, which includes a known shell count that
/// exceeds the tube. `phoned=` and `dealer=` are refused here, because a
/// `GameState` has nowhere to keep them.
bool parse(const std::string& text, GameState* state, std::string* error);

/// Parse a whole position, including `phoned=` and `dealer=`.
bool parsePosition(const std::string& text, Position* position, std::string* error);

/// Print a position in the same grammar `parse` accepts.
std::string print(const GameState& state);

/// Print a whole position in the grammar `parsePosition` accepts.
std::string printPosition(const Position& position);

/// A multi-line board for humans, with each seat's charges, items and knowledge.
/// Offsets set in `untyped`, one bit each, are shells the reader has not seen
/// whose type is pinned only as a stand-in: a seat that knows one is shown
/// knowing it, without the type.
std::string board(const GameState& state, std::uint8_t untyped = 0);

}  // namespace notation
}  // namespace bsr
