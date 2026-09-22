#pragma once

#include <string>

#include "engine/Config.h"
#include "engine/State.h"

namespace bsr {

/// A position as one line of text, so that a position can be typed, printed,
/// pinned in a test, pasted into an issue and diffed.
///
/// Grammar, tokens separated by spaces and accepted in any order:
///
///   p1=3/4[saw,beer]   seat 1 has 3 charges of 4 and holds a saw and a beer
///   tube=2L3B          two live and three blank shells remain
///   turn=p1            seat 1 is to move
///   cuffed=p2          seat 2 is handcuffed and will be skipped
///   sawed              the barrel is sawed for the next shot
///   inverted           an inverter flipped a chamber nobody has seen
///   known=p1:0L,2B     seat 1 has seen the chamber (live) and offset 2 (blank)
///   dir=ccw            turn order runs counter clockwise (a remote was used)
///
/// Item tokens are mg, beer, cig, cuff, saw, phone, adr, inv, med, jam, rem.
namespace notation {

/// Parse a position. Returns false and fills `error` when the text does not
/// describe a consistent position, which includes a known shell count that
/// exceeds the tube.
bool parse(const std::string& text, GameState* state, std::string* error);

/// Print a position in the same grammar `parse` accepts.
std::string print(const GameState& state);

/// A multi-line board for humans, with each seat's charges, items and knowledge.
std::string board(const GameState& state);

}  // namespace notation
}  // namespace bsr
