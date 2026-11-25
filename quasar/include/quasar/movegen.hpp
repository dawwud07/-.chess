#pragma once

#include "position.hpp"
#include "types.hpp"

#include <array>
#include <cstdint>

namespace quasar {

class MoveList {
public:
    void add(Move m) {
        if (size_ < MAX_MOVES) {
            moves_[static_cast<std::size_t>(size_++)] = m;
        }
    }

    void clear() { size_ = 0; }
    int size() const { return size_; }
    bool empty() const { return size_ == 0; }
    Move operator[](int index) const { return moves_[static_cast<std::size_t>(index)]; }
    Move& operator[](int index) { return moves_[static_cast<std::size_t>(index)]; }

    Move* begin() { return moves_.data(); }
    Move* end() { return moves_.data() + size_; }
    const Move* begin() const { return moves_.data(); }
    const Move* end() const { return moves_.data() + size_; }

private:
    std::array<Move, MAX_MOVES> moves_{};
    int size_ = 0;
};

enum class GenType { All, Captures };

void generateMoves(const Position& pos, MoveList& list, GenType type = GenType::All);
void generateLegalMoves(Position& pos, MoveList& list);

std::uint64_t perft(Position& pos, int depth);
int countLegalMoves(Position& pos);

}  // namespace quasar
