#include "quasar/transposition.hpp"

#include <algorithm>
#include <bit>

namespace quasar {

namespace {

std::size_t nextPowerOfTwo(std::size_t value) {
    if (value == 0) {
        return 1;
    }
    if ((value & (value - 1)) == 0) {
        return value;
    }
    return std::bit_ceil(value);
}

int adjustMateForStore(int score, int ply) {
    if (score >= MATE_IN_MAX_PLY) {
        return score + ply;
    }
    if (score <= -MATE_IN_MAX_PLY) {
        return score - ply;
    }
    return score;
}

int adjustMateForProbe(int score, int ply) {
    if (score >= MATE_IN_MAX_PLY) {
        return score - ply;
    }
    if (score <= -MATE_IN_MAX_PLY) {
        return score + ply;
    }
    return score;
}

}  // namespace

TranspositionTable::TranspositionTable(std::size_t megabytes) {
    resize(megabytes);
}

void TranspositionTable::resize(std::size_t megabytes) {
    const std::size_t bytes = std::max<std::size_t>(megabytes, 1) * 1024 * 1024;
    const std::size_t entries = std::max<std::size_t>(bytes / sizeof(TTEntry), 1024);
    const std::size_t size = nextPowerOfTwo(entries);
    table_.assign(size, TTEntry{});
    mask_ = size - 1;
    generation_ = 0;
}

void TranspositionTable::clear() {
    std::fill(table_.begin(), table_.end(), TTEntry{});
    generation_ = 0;
}

void TranspositionTable::newSearch() {
    ++generation_;
    if (generation_ == 0) {
        clear();
        generation_ = 1;
    }
}

bool TranspositionTable::probe(std::uint64_t key, int ply, int depth, int alpha, int beta,
                               Move& ttMove, int& ttScore) const {
    const TTEntry& entry = table_[key & mask_];
    if (entry.key != key || entry.bound == static_cast<std::uint8_t>(Bound::None)) {
        return false;
    }

    ttMove = entry.move;
    if (entry.depth < depth) {
        return false;
    }

    const int score = adjustMateForProbe(entry.score, ply);
    const Bound bound = static_cast<Bound>(entry.bound);
    const bool cutoff =
        bound == Bound::Exact || (bound == Bound::Lower && score >= beta) ||
        (bound == Bound::Upper && score <= alpha);
    if (!cutoff) {
        return false;
    }

    ttScore = score;
    return true;
}

void TranspositionTable::store(std::uint64_t key, int ply, int depth, int score, Move move,
                               Bound bound) {
    TTEntry& entry = table_[key & mask_];
    const bool samePosition = entry.key == key;
    const bool newGeneration = entry.generation != generation_;
    if (!samePosition && !newGeneration && depth + 3 < entry.depth) {
        return;
    }

    entry.key = key;
    entry.move = move;
    entry.score = static_cast<std::int16_t>(adjustMateForStore(score, ply));
    entry.depth = static_cast<std::uint8_t>(std::clamp(depth, 0, 255));
    entry.bound = static_cast<std::uint8_t>(bound);
    entry.generation = generation_;
}

int TranspositionTable::hashfull() const {
    const std::size_t sample = std::min<std::size_t>(table_.size(), 1000);
    int used = 0;
    for (std::size_t i = 0; i < sample; ++i) {
        if (table_[i].bound != 0 && table_[i].generation == generation_) {
            ++used;
        }
    }
    return sample == 0 ? 0 : static_cast<int>(used * 1000 / sample);
}

}  // namespace quasar
