#include "quasar/bench.hpp"

#include "quasar/position.hpp"
#include "quasar/search.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

namespace quasar {

namespace {

constexpr std::string_view BENCH_POSITIONS[] = {
    START_FEN,
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r2q1rk1/ppp2ppp/2npbn2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 8",
};

}  // namespace

BenchResult runBench(int depth, std::size_t hashMb) {
    depth = std::clamp(depth, 1, 15);
    Searcher searcher(hashMb);
    searcher.newGame();

    const auto start = std::chrono::steady_clock::now();
    std::uint64_t totalNodes = 0;

    std::cout << "Quasar benchmark, depth " << depth << ", hash " << hashMb << " MB\n";
    std::cout << "------------------------------------------------------------\n";

    int index = 0;
    for (const auto fen : BENCH_POSITIONS) {
        ++index;
        Position pos(fen);
        SearchLimits limits;
        limits.depth = depth;
        const auto result = searcher.search(pos, limits);
        totalNodes += result.nodes;
        std::cout << "Position " << index << '/' << std::size(BENCH_POSITIONS) << "  nodes "
                  << std::setw(10) << result.nodes << "  best " << moveToUci(result.bestMove)
                  << "  depth " << result.depth << '\n';
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    const int timeMs = static_cast<int>(elapsed.count());
    const std::uint64_t nps =
        timeMs > 0 ? (totalNodes * 1000ULL) / static_cast<std::uint64_t>(timeMs) : 0;

    std::cout << "------------------------------------------------------------\n";
    std::cout << "Depth        : " << depth << '\n';
    std::cout << "Total time   : " << timeMs << " ms\n";
    std::cout << "Nodes        : " << totalNodes << '\n';
    std::cout << "NPS          : " << nps << '\n';

    return BenchResult{depth, totalNodes, timeMs};
}

}  // namespace quasar
