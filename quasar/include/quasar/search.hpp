#pragma once

#include "movegen.hpp"
#include "position.hpp"
#include "transposition.hpp"
#include "types.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace quasar {

struct SearchLimits {
    int depth = 0;  // 0 = no explicit depth limit
    std::uint64_t nodes = 0;
    int movetime = 0;
    int wtime = 0;
    int btime = 0;
    int winc = 0;
    int binc = 0;
    int movestogo = 0;
    bool infinite = false;
};

struct SearchInfo {
    int depth = 0;
    int seldepth = 0;
    int score = 0;
    std::uint64_t nodes = 0;
    int timeMs = 0;
    int hashfull = 0;
    std::vector<Move> pv;
};

struct SearchResult {
    Move bestMove = MOVE_NONE;
    Move ponderMove = MOVE_NONE;
    int score = 0;
    int depth = 0;
    std::uint64_t nodes = 0;
    int timeMs = 0;
};

class Searcher {
public:
    explicit Searcher(std::size_t hashMb = 64);

    void setHash(std::size_t megabytes);
    void clear();
    void newGame();

    void stop();
    void clearStop() { stop_.store(false, std::memory_order_relaxed); }
    bool isStopped() const { return stop_.load(std::memory_order_relaxed); }
    std::uint64_t nodes() const { return nodes_; }

    SearchResult search(Position& pos, const SearchLimits& limits,
                        const std::function<void(const SearchInfo&)>& onInfo = {});

private:
    using Clock = std::chrono::steady_clock;

    int negamax(Position& pos, int depth, int alpha, int beta, int ply, bool pvNode);
    int qsearch(Position& pos, int alpha, int beta, int ply);
    bool searchRoot(Position& pos, int depth, Move& bestMove, int& bestScore);

    void scoreMoves(const Position& pos, MoveList& list, Move ttMove, int ply) const;
    void scoreCaptures(const Position& pos, MoveList& list) const;

    int elapsedMs() const;
    void configureTime(const SearchLimits& limits, Color side);
    bool shouldStop();

    void updatePv(int ply, Move move);
    std::vector<Move> extractPv() const;

    TranspositionTable tt_;
    std::atomic<bool> stop_{false};
    std::uint64_t nodes_ = 0;
    std::uint64_t maxNodes_ = 0;
    int maxDepth_ = MAX_PLY - 2;

    bool useTime_ = false;
    int softTimeMs_ = 0;
    int hardTimeMs_ = 0;
    Clock::time_point startTime_{};

    std::array<std::array<Move, MAX_PLY>, MAX_PLY> pvTable_{};
    std::array<int, MAX_PLY> pvLength_{};
    std::array<std::array<Move, 2>, MAX_PLY> killers_{};
    std::array<std::array<std::array<int, 64>, 64>, COLOR_NB> history_{};
    int seldepth_ = 0;
};

}  // namespace quasar
