#pragma once

#include "types.hpp"

#include <array>

namespace quasar::bb {

// Bitboard constants ---------------------------------------------------------

inline constexpr Bitboard FILE_A = 0x0101010101010101ULL;
inline constexpr Bitboard FILE_H = FILE_A << 7;
inline constexpr Bitboard RANK_1 = 0x00000000000000FFULL;
inline constexpr Bitboard RANK_8 = RANK_1 << 56;
inline constexpr Bitboard DARK_SQUARES = 0xAA55AA55AA55AA55ULL;
inline constexpr Bitboard LIGHT_SQUARES = ~DARK_SQUARES;

constexpr Bitboard bit(int sq) { return Bitboard{1} << sq; }
constexpr Bitboard fileBb(int file) { return FILE_A << file; }
constexpr Bitboard rankBb(int rank) { return RANK_1 << (rank * 8); }

constexpr Bitboard shiftNorth(Bitboard b) { return b << 8; }
constexpr Bitboard shiftSouth(Bitboard b) { return b >> 8; }
constexpr Bitboard shiftEast(Bitboard b) { return (b & ~FILE_H) << 1; }
constexpr Bitboard shiftWest(Bitboard b) { return (b & ~FILE_A) >> 1; }

// Attack tables (initialised once by init()) ---------------------------------
extern std::array<Bitboard, 64> KnightAttacks;
extern std::array<Bitboard, 64> KingAttacks;
extern std::array<std::array<Bitboard, 64>, COLOR_NB> PawnAttacks;
extern std::array<Bitboard, 64> RookMasks;
extern std::array<Bitboard, 64> BishopMasks;

void init();

Bitboard rookAttacks(int sq, Bitboard occupancy);
Bitboard bishopAttacks(int sq, Bitboard occupancy);
Bitboard queenAttacks(int sq, Bitboard occupancy);

// All attacks of a piece, ignoring the colour. Pawns are unsupported here;
// use PawnAttacks[colour][square] for them.
Bitboard attacks(PieceType pt, int sq, Bitboard occupancy);

}  // namespace quasar::bb
