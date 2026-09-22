#pragma once

#include <array>
#include <cstddef>

namespace bsr {

/// A fixed array indexed by a signed position.
///
/// Seats and shell offsets are counted with `int` throughout the engine, and
/// the alternative to this wrapper is a `static_cast<std::size_t>` at every
/// subscript, which buries the logic under casts and gives a warning-clean
/// build only as long as nobody forgets one. The bounds are the array's own.
template <typename T, std::size_t N>
struct IndexedArray : std::array<T, N> {
  using Base = std::array<T, N>;
  using Base::operator[];

  T& operator[](int index) { return Base::operator[](static_cast<std::size_t>(index)); }
  const T& operator[](int index) const {
    return Base::operator[](static_cast<std::size_t>(index));
  }
};

}  // namespace bsr
