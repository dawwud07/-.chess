#include "quasar/zobrist.hpp"

#include <array>

namespace quasar::zobrist {

std::array<std::array<std::uint64_t, 64>, PIECE_NB> psq{};
std::uint64_t sideToMove = 0;
std::array<std::uint64_t, 16> castling{};
std::array<std::uint64_t, 8> enPassantFile{};

namespace {

std::uint64_t state = 0x243F6A8885A308D3ULL;

std::uint64_t next() {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

}  // namespace

void init() {
    static bool initialised = false;
    if (initialised) {
        return;
    }
    initialised = true;

    for (auto& piece : psq) {
        for (auto& square : piece) {
            square = next();
        }
    }
    sideToMove = next();
    for (auto& value : enPassantFile) {
        value = next();
    }

    // Make castling keys consistent for every combination of rights.
    castling[0] = 0;
    for (int mask = 1; mask < 16; ++mask) {
        const int lowest = mask & -mask;
        castling[mask] = castling[mask ^ lowest] ^ next();
    }
}

}  // namespace quasar::zobrist
