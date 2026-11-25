#include "quasar/bitboard.hpp"
#include "quasar/eval.hpp"
#include "quasar/movegen.hpp"
#include "quasar/position.hpp"
#include "quasar/search.hpp"
#include "quasar/zobrist.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

template <typename A, typename B>
void checkEqual(const A& actual, const B& expected, std::string_view what) {
    ++checks;
    if (!(actual == expected)) {
        ++failures;
        std::cerr << "FAIL: " << what << " (expected " << expected << ", got " << actual
                  << ")\n";
    }
}

struct PerftCase {
    std::string_view name;
    std::string_view fen;
    std::vector<std::uint64_t> expected;
};

void testPerft() {
    const std::vector<PerftCase> cases = {
        {"startpos", quasar::START_FEN, {20, 400, 8902, 197281, 4865609}},
        {"kiwipete",
         "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         {48, 2039, 97862, 4085603}},
        {"position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
         {14, 191, 2812, 43238, 674624}},
        {"position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
         {6, 264, 9467, 422333}},
        {"position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
         {44, 1486, 62379, 2103487}},
        {"position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
         {46, 2079, 89890, 3894594}},
    };

    for (const auto& test : cases) {
        quasar::Position pos(test.fen);
        for (int depth = 1; depth <= static_cast<int>(test.expected.size()); ++depth) {
            const std::uint64_t nodes = quasar::perft(pos, depth);
            const std::string what = std::string(test.name) + " perft(" +
                                     std::to_string(depth) + ")";
            checkEqual(nodes, test.expected[static_cast<std::size_t>(depth - 1)], what);
        }
    }
}

quasar::Bitboard slowSlider(int sq, quasar::Bitboard occupancy, int dr, int df) {
    quasar::Bitboard attacks = 0;
    int rank = quasar::rankOf(sq) + dr;
    int file = quasar::fileOf(sq) + df;
    while (rank >= 0 && rank < 8 && file >= 0 && file < 8) {
        const quasar::Bitboard target =
            quasar::bb::bit(quasar::makeSquare(file, rank));
        attacks |= target;
        if ((occupancy & target) != 0) {
            break;
        }
        rank += dr;
        file += df;
    }
    return attacks;
}

quasar::Bitboard slowRookAttacks(int sq, quasar::Bitboard occupancy) {
    return slowSlider(sq, occupancy, 1, 0) | slowSlider(sq, occupancy, -1, 0) |
           slowSlider(sq, occupancy, 0, 1) | slowSlider(sq, occupancy, 0, -1);
}

quasar::Bitboard slowBishopAttacks(int sq, quasar::Bitboard occupancy) {
    return slowSlider(sq, occupancy, 1, 1) | slowSlider(sq, occupancy, 1, -1) |
           slowSlider(sq, occupancy, -1, 1) | slowSlider(sq, occupancy, -1, -1);
}

void testBitboards() {
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    const auto next = [&state]() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };

    for (int sq = 0; sq < 64; ++sq) {
        for (int sample = 0; sample < 128; ++sample) {
            const quasar::Bitboard occupancy = sample == 0 ? 0 : next() & next();
            checkEqual(quasar::bb::rookAttacks(sq, occupancy),
                       slowRookAttacks(sq, occupancy), "magic rook attacks");
            checkEqual(quasar::bb::bishopAttacks(sq, occupancy),
                       slowBishopAttacks(sq, occupancy), "magic bishop attacks");
        }
        checkEqual(quasar::bb::rookAttacks(sq, ~quasar::Bitboard{0}),
                   slowRookAttacks(sq, ~quasar::Bitboard{0}),
                   "rook attacks on a full board");
        checkEqual(quasar::bb::bishopAttacks(sq, ~quasar::Bitboard{0}),
                   slowBishopAttacks(sq, ~quasar::Bitboard{0}),
                   "bishop attacks on a full board");
    }
}

void testFenRoundtrip() {
    const std::vector<std::string_view> positions = {
        quasar::START_FEN,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    };
    for (const auto fen : positions) {
        quasar::Position pos(fen);
        checkEqual(pos.fen(), std::string(fen), "FEN roundtrip");
    }
}

void testZobrist() {
    quasar::Position pos(quasar::START_FEN);
    const std::string_view moves =
        "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f6e4 d2d4 b7b5";
    std::vector<quasar::Move> madeMoves;
    std::vector<std::uint64_t> keys{pos.key()};
    std::size_t index = 0;
    while (index < moves.size()) {
        const auto space = moves.find(' ', index);
        const auto token = moves.substr(index, space - index);
        const quasar::Move move = quasar::parseUciMove(token);
        checkEqual(pos.makeMove(move), true, "make move " + std::string(token));
        checkEqual(pos.key(), pos.computeKey(), "incremental key after " + std::string(token));
        madeMoves.push_back(move);
        keys.push_back(pos.key());
        index = space == std::string_view::npos ? moves.size() : space + 1;
    }

    for (int i = static_cast<int>(madeMoves.size()) - 1; i >= 0; --i) {
        pos.unmakeMove(madeMoves[static_cast<std::size_t>(i)]);
        checkEqual(pos.key(), keys[static_cast<std::size_t>(i)],
                   "incremental key after unmake " + std::to_string(i));
        checkEqual(pos.key(), pos.computeKey(),
                   "computed key after unmake " + std::to_string(i));
    }
    checkEqual(pos.fen(), std::string(quasar::START_FEN),
               "unwinding all moves restores the start position");
}

void testSpecialMoves() {
    quasar::Position pos("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    checkEqual(quasar::countLegalMoves(pos), 26, "castling legal move count");
    checkEqual(pos.isLegal(quasar::makeMove(quasar::E1, quasar::G1)), true,
               "white kingside castle is legal");
    checkEqual(pos.isLegal(quasar::makeMove(quasar::E1, quasar::C1)), true,
               "white queenside castle is legal");

    quasar::Position ep("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
    const quasar::Move epMove =
        quasar::makeMove(quasar::makeSquare(4, 4), quasar::makeSquare(5, 5));
    checkEqual(ep.isLegal(epMove), true, "en passant capture is legal");
    checkEqual(ep.makeMove(epMove), true, "en passant capture makes");
    checkEqual(ep.pieceAt(quasar::makeSquare(5, 4)), quasar::NO_PIECE,
               "en passant removes pawn");
    checkEqual(ep.pieceAt(quasar::makeSquare(5, 5)) == quasar::W_PAWN, true,
               "en passant lands on f6");
    ep.unmakeMove(epMove);
    checkEqual(ep.fen(),
               std::string("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3"),
               "en passant unmake restores FEN");
}

bool containsMove(const quasar::MoveList& list, quasar::Move move) {
    for (const quasar::Move candidate : list) {
        if (candidate == move) {
            return true;
        }
    }
    return false;
}

void testCaptureGeneration() {
    {
        quasar::Position pos(quasar::START_FEN);
        quasar::MoveList captures;
        quasar::generateMoves(pos, captures, quasar::GenType::Captures);
        checkEqual(captures.size(), 0, "start position has no captures");
    }

    {
        quasar::Position pos("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
        quasar::MoveList captures;
        quasar::generateMoves(pos, captures, quasar::GenType::Captures);
        checkEqual(containsMove(captures, quasar::parseUciMove("e4d5")), true,
                   "capture generation includes pawn captures");
        checkEqual(containsMove(captures, quasar::parseUciMove("e4e5")), false,
                   "capture generation excludes quiet pawn pushes");

        quasar::Position ep("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
        quasar::MoveList epCaptures;
        quasar::generateMoves(ep, epCaptures, quasar::GenType::Captures);
        checkEqual(containsMove(epCaptures, quasar::parseUciMove("e5f6")), true,
                   "capture generation includes en passant");
    }

    {
        quasar::Position pos("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        quasar::MoveList captures;
        quasar::generateMoves(pos, captures, quasar::GenType::Captures);
        checkEqual(containsMove(captures, quasar::parseUciMove("a7a8q")), true,
                   "capture generation includes promotion");
    }
}

void testEvaluation() {
    quasar::Position start(quasar::START_FEN);
    const int startEval = quasar::evaluate(start);
    checkEqual(std::abs(startEval) <= 30, true, "start position is roughly balanced");

    quasar::Position queenUp("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
    checkEqual(quasar::evaluate(queenUp) > 500, true, "extra queen is a large advantage");

    quasar::Position blackQueenUp("4k3/8/8/8/8/8/8/3QK3 b - - 0 1");
    checkEqual(quasar::evaluate(blackQueenUp) < -500, true,
               "evaluation is relative to the side to move");
}

void testSearch() {
    quasar::Searcher searcher(16);

    {
        quasar::Position pos("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1");
        quasar::SearchLimits limits;
        limits.depth = 4;
        const auto result = searcher.search(pos, limits);
        checkEqual(quasar::moveToUci(result.bestMove), std::string("d1d8"),
                   "finds mate in one");
        checkEqual(result.score >= quasar::MATE_IN_MAX_PLY, true,
                   "mate in one is scored as mate");
        checkEqual(pos.fen(), std::string("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1"),
                   "search restores the position");
    }

    {
        quasar::Position pos("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        quasar::SearchLimits limits;
        limits.depth = 3;
        const auto result = searcher.search(pos, limits);
        checkEqual(result.bestMove, quasar::MOVE_NONE, "detects stalemate");
        checkEqual(result.score, 0, "stalemate is scored as draw");
    }

    {
        quasar::Position pos("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
        quasar::SearchLimits limits;
        limits.depth = 3;
        const auto result = searcher.search(pos, limits);
        checkEqual(result.bestMove, quasar::MOVE_NONE, "detects checkmate");
        checkEqual(result.score, -quasar::MATE, "checkmate is scored as mate");
    }

    {
        quasar::Position pos("4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1");
        quasar::SearchLimits limits;
        limits.depth = 4;
        const auto result = searcher.search(pos, limits);
        checkEqual(quasar::moveToUci(result.bestMove), std::string("e4d5"),
                   "finds a free queen");
    }
}

}  // namespace

int main() {
    quasar::bb::init();
    quasar::zobrist::init();

    testPerft();
    testBitboards();
    testFenRoundtrip();
    testZobrist();
    testSpecialMoves();
    testCaptureGeneration();
    testEvaluation();
    testSearch();

    if (failures == 0) {
        std::cout << "All " << checks << " checks passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << failures << " of " << checks << " checks failed.\n";
    return EXIT_FAILURE;
}
