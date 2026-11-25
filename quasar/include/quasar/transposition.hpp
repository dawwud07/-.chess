#pragma once

#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace quasar {

enum class Bound : std::uint8_t {
    None = 0,
    Upper = 1,
    Lower = 2,
    Exact = 3,
};

struct TTEntry {
    std::uint64_t key = 0;
    Move move = MOVE_NONE;
    std::int16_t score = 0;
    std::uint8_t depth = 0;
    std::uint8_t bound = 0;
    std::uint8_t generation = 0;
    std::uint8_t padding = 0;
};

static_assert(sizeof(TTEntry) == 16, "TTEntry should stay cache friendly");

class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t megabytes = 64);

    void resize(std::size_t megabytes);
    void clear();
    void newSearch();

    // Returns true if the entry is usable as a cutoff. ttMove is filled
    // whenever the entry matches, even when the score itself is not usable.
    bool probe(std::uint64_t key, int ply, int depth, int alpha, int beta, Move& ttMove,
               int& ttScore) const;

    void store(std::uint64_t key, int ply, int depth, int score, Move move, Bound bound);

    int hashfull() const;
    std::size_t sizeMb() const { return (table_.size() * sizeof(TTEntry)) / (1024 * 1024); }
    std::size_t entryCount() const { return table_.size(); }

private:
    std::vector<TTEntry> table_;
    std::size_t mask_ = 0;
    std::uint8_t generation_ = 0;
};

}  // namespace quasar
