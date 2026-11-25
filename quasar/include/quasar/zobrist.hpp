#pragma once

#include "types.hpp"

#include <array>
#include <cstdint>

namespace quasar::zobrist {

void init();

extern std::array<std::array<std::uint64_t, 64>, PIECE_NB> psq;
extern std::uint64_t sideToMove;
extern std::array<std::uint64_t, 16> castling;
extern std::array<std::uint64_t, 8> enPassantFile;

}  // namespace quasar::zobrist
