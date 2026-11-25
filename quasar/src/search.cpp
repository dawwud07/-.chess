#include "quasar/search.hpp"

#include "quasar/eval.hpp"
#include "quasar/movegen.hpp"

#include <algorithm>
#include <limits>

namespace quasar {

namespace {

constexpr int HISTORY_MAX = 1'000'000;

int captureScore(const Position& pos, Move move) {
    const PieceType victim = pos.capturedType(move);
    const PieceType attacker = pieceType(pos.pieceAt(moveFrom(move)));
    return 500'000 + 100 * pieceValue(victim) - pieceValue(attacker);
}

}  // namespace

Searcher::Searcher(std::size_t hashMb) : tt_(hashMb) {
    for (auto& perColor : history_) {
        for (auto& perFrom : perColor) {
            perFrom.fill(0);
        }
    }
}

void Searcher::setHash(std::size_t megabytes) {
    tt_.resize(megabytes);
}

void Searcher::clear() {
    tt_.clear();
    killers_ = {};
    pvLength_.fill(0);
    for (auto& perColor : history_) {
        for (auto& perFrom : perColor) {
            perFrom.fill(0);
        }
    }
}

void Searcher::newGame() {
    clear();
}

void Searcher::stop() {
    stop_.store(true, std::memory_order_relaxed);
}

int Searcher::elapsedMs() const {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - startTime_);
    return static_cast<int>(elapsed.count());
}

void Searcher::configureTime(const SearchLimits& limits, Color side) {
    maxDepth_ = limits.depth > 0 ? std::clamp(limits.depth, 1, MAX_PLY - 2) : MAX_PLY - 2;
    maxNodes_ = limits.nodes;
    useTime_ = false;
    softTimeMs_ = 0;
    hardTimeMs_ = 0;

    if (limits.movetime > 0) {
        hardTimeMs_ = std::max(1, limits.movetime - 15);
        softTimeMs_ = hardTimeMs_;
        useTime_ = true;
        return;
    }

    if (limits.infinite) {
        return;
    }

    const int time = side == WHITE ? limits.wtime : limits.btime;
    const int increment = side == WHITE ? limits.winc : limits.binc;
    if (time <= 0) {
        return;
    }

    const int movesToGo = limits.movestogo > 0 ? std::clamp(limits.movestogo, 1, 50) : 30;
    const int base = time / movesToGo + (increment * 3) / 4;
    softTimeMs_ = std::max(1, (base * 3) / 4);
    hardTimeMs_ = std::max(5, std::min(time / 2, base * 3));
    useTime_ = true;
}

bool Searcher::shouldStop() {
    if (stop_.load(std::memory_order_relaxed)) {
        return true;
    }
    if (maxNodes_ != 0 && nodes_ >= maxNodes_) {
        stop_.store(true, std::memory_order_relaxed);
        return true;
    }
    if (useTime_ && (nodes_ & 2047) == 0 && elapsedMs() >= hardTimeMs_) {
        stop_.store(true, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void Searcher::scoreMoves(const Position& pos, MoveList& list, Move ttMove, int ply) const {
    std::array<int, MAX_MOVES> scores{};
    const Color us = pos.sideToMove();

    for (int i = 0; i < list.size(); ++i) {
        const Move move = list[i];
        int score = 0;
        if (move == ttMove) {
            score = 1'000'000;
        } else if (pos.isCapture(move)) {
            score = captureScore(pos, move);
            if (isPromotion(move)) {
                score += 200'000;
            }
        } else if (isPromotion(move)) {
            score = 450'000 +
                    pieceValue(static_cast<PieceType>(movePromotion(move)));
        } else if (move == killers_[static_cast<std::size_t>(ply)][0]) {
            score = 300'000;
        } else if (move == killers_[static_cast<std::size_t>(ply)][1]) {
            score = 290'000;
        } else {
            score = history_[us][moveFrom(move)][moveTo(move)];
        }
        scores[static_cast<std::size_t>(i)] = score;
    }

    // A small selection sort keeps the hot path allocation-free.
    for (int i = 0; i < list.size(); ++i) {
        int best = i;
        for (int j = i + 1; j < list.size(); ++j) {
            if (scores[static_cast<std::size_t>(j)] > scores[static_cast<std::size_t>(best)]) {
                best = j;
            }
        }
        if (best != i) {
            std::swap(list[i], list[best]);
            std::swap(scores[static_cast<std::size_t>(i)],
                      scores[static_cast<std::size_t>(best)]);
        }
    }
}

void Searcher::scoreCaptures(const Position& pos, MoveList& list) const {
    std::array<int, MAX_MOVES> scores{};
    for (int i = 0; i < list.size(); ++i) {
        const Move move = list[i];
        int score = 0;
        if (pos.isCapture(move)) {
            score = captureScore(pos, move);
        }
        if (isPromotion(move)) {
            score += 400'000 +
                     pieceValue(static_cast<PieceType>(movePromotion(move)));
        }
        scores[static_cast<std::size_t>(i)] = score;
    }

    for (int i = 0; i < list.size(); ++i) {
        int best = i;
        for (int j = i + 1; j < list.size(); ++j) {
            if (scores[static_cast<std::size_t>(j)] > scores[static_cast<std::size_t>(best)]) {
                best = j;
            }
        }
        if (best != i) {
            std::swap(list[i], list[best]);
            std::swap(scores[static_cast<std::size_t>(i)],
                      scores[static_cast<std::size_t>(best)]);
        }
    }
}

void Searcher::updatePv(int ply, Move move) {
    pvTable_[static_cast<std::size_t>(ply)][static_cast<std::size_t>(ply)] = move;
    const int childLength = pvLength_[static_cast<std::size_t>(ply + 1)];
    for (int i = ply + 1; i < childLength; ++i) {
        pvTable_[static_cast<std::size_t>(ply)][static_cast<std::size_t>(i)] =
            pvTable_[static_cast<std::size_t>(ply + 1)][static_cast<std::size_t>(i)];
    }
    pvLength_[static_cast<std::size_t>(ply)] = std::max(childLength, ply + 1);
}

std::vector<Move> Searcher::extractPv() const {
    std::vector<Move> pv;
    const int length = pvLength_[0];
    for (int i = 0; i < length; ++i) {
        const Move move = pvTable_[0][static_cast<std::size_t>(i)];
        if (move == MOVE_NONE) {
            break;
        }
        pv.push_back(move);
    }
    return pv;
}

bool Searcher::searchRoot(Position& pos, int depth, Move& bestMove, int& bestScore) {
    MoveList list;
    generateMoves(pos, list, GenType::All);

    Move ttMove = MOVE_NONE;
    int ignored = 0;
    (void)tt_.probe(pos.key(), 0, depth, -INFINITE, INFINITE, ttMove, ignored);
    scoreMoves(pos, list, ttMove, 0);

    int alpha = -INFINITE;
    const int beta = INFINITE;
    int localBestScore = -INFINITE;
    Move localBestMove = MOVE_NONE;
    int legal = 0;

    for (const Move move : list) {
        if (!pos.makeMove(move)) {
            continue;
        }
        ++legal;

        int score;
        if (legal == 1) {
            score = -negamax(pos, depth - 1, -beta, -alpha, 1, true);
        } else {
            score = -negamax(pos, depth - 1, -alpha - 1, -alpha, 1, false);
            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1, -beta, -alpha, 1, true);
            }
        }
        pos.unmakeMove(move);

        if (stop_.load(std::memory_order_relaxed)) {
            return false;
        }

        if (score > localBestScore) {
            localBestScore = score;
            localBestMove = move;
            if (score > alpha) {
                alpha = score;
                updatePv(0, move);
            }
        }
    }

    if (legal == 0) {
        localBestScore = pos.inCheck() ? -MATE : 0;
        localBestMove = MOVE_NONE;
    }

    bestMove = localBestMove;
    bestScore = localBestScore;
    if (localBestMove != MOVE_NONE) {
        tt_.store(pos.key(), 0, depth, localBestScore, localBestMove, Bound::Exact);
    }
    return true;
}

int Searcher::negamax(Position& pos, int depth, int alpha, int beta, int ply, bool pvNode) {
    if (depth <= 0) {
        return qsearch(pos, alpha, beta, ply);
    }

    pvLength_[static_cast<std::size_t>(ply)] = ply;

    ++nodes_;
    if (shouldStop()) {
        return 0;
    }
    if (ply > seldepth_) {
        seldepth_ = ply;
    }
    if (ply >= MAX_PLY - 1) {
        return evaluate(pos);
    }

    if (pos.isRepetition() || pos.halfmoveClock() >= 100 || pos.isInsufficientMaterial()) {
        return 0;
    }

    const bool inCheck = pos.inCheck();
    if (inCheck && depth < MAX_PLY - 2) {
        ++depth;
    }

    alpha = std::max(alpha, -MATE + ply);
    beta = std::min(beta, MATE - ply - 1);
    if (alpha >= beta) {
        return alpha;
    }

    Move ttMove = MOVE_NONE;
    int ttScore = 0;
    if (tt_.probe(pos.key(), ply, depth, alpha, beta, ttMove, ttScore)) {
        return ttScore;
    }

    const int staticEval = inCheck ? VALUE_NONE : evaluate(pos);

    if (!pvNode && !inCheck && depth <= 6 && staticEval - 75 * depth >= beta) {
        return staticEval;
    }

    if (!pvNode && !inCheck && depth >= 3 && staticEval >= beta &&
        beta < MATE_IN_MAX_PLY && pos.hasNonPawnMaterial(pos.sideToMove())) {
        const int reduction = 2 + depth / 6;
        pos.makeNullMove();
        const int score = -negamax(pos, depth - 1 - reduction, -beta, -beta + 1, ply + 1, false);
        pos.unmakeNullMove();
        if (stop_.load(std::memory_order_relaxed)) {
            return 0;
        }
        if (score >= beta) {
            return score >= MATE_IN_MAX_PLY ? beta : score;
        }
    }

    MoveList list;
    generateMoves(pos, list, GenType::All);
    scoreMoves(pos, list, ttMove, ply);

    const Color us = pos.sideToMove();
    const int originalAlpha = alpha;
    int bestScore = -INFINITE;
    Move bestMove = MOVE_NONE;
    int legal = 0;

    for (const Move move : list) {
        const bool capture = pos.isCapture(move);
        const bool promotion = isPromotion(move);
        const bool quiet = !capture && !promotion;

        if (!pvNode && !inCheck && depth <= 3 && legal >= 1 && quiet &&
            staticEval + 100 * depth <= alpha) {
            continue;
        }

        if (!pos.makeMove(move)) {
            continue;
        }
        ++legal;

        const bool givesCheck = pos.inCheck();
        const int newDepth = depth - 1;
        int reduction = 0;
        if (depth >= 3 && legal > 3 && quiet && !inCheck && !givesCheck) {
            reduction = 1 + (legal > 6 ? 1 : 0) + (depth >= 6 ? 1 : 0) + (pvNode ? 0 : 1);
            reduction = std::min(reduction, std::max(0, newDepth - 1));
        }

        int score;
        if (legal == 1) {
            score = -negamax(pos, newDepth, -beta, -alpha, ply + 1, pvNode);
        } else {
            score = -negamax(pos, newDepth - reduction, -alpha - 1, -alpha, ply + 1, false);
            if (reduction > 0 && score > alpha) {
                score = -negamax(pos, newDepth, -alpha - 1, -alpha, ply + 1, false);
            }
            if (score > alpha && score < beta) {
                score = -negamax(pos, newDepth, -beta, -alpha, ply + 1, pvNode);
            }
        }
        pos.unmakeMove(move);

        if (stop_.load(std::memory_order_relaxed)) {
            return 0;
        }

        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
            if (score > alpha) {
                alpha = score;
                updatePv(ply, move);

                if (alpha >= beta) {
                    if (quiet) {
                        if (killers_[static_cast<std::size_t>(ply)][0] != move) {
                            killers_[static_cast<std::size_t>(ply)][1] =
                                killers_[static_cast<std::size_t>(ply)][0];
                            killers_[static_cast<std::size_t>(ply)][0] = move;
                        }
                        const int from = moveFrom(move);
                        const int to = moveTo(move);
                        int& entry = history_[us][from][to];
                        entry += depth * depth;
                        if (entry > HISTORY_MAX) {
                            entry = HISTORY_MAX;
                        }
                    }
                    break;
                }
            }
        }
    }

    if (legal == 0) {
        const int score = inCheck ? -MATE + ply : 0;
        tt_.store(pos.key(), ply, depth, score, MOVE_NONE, Bound::Exact);
        return score;
    }

    const Bound bound = bestScore <= originalAlpha
                            ? Bound::Upper
                            : (bestScore >= beta ? Bound::Lower : Bound::Exact);
    tt_.store(pos.key(), ply, depth, bestScore, bestMove, bound);
    return bestScore;
}

int Searcher::qsearch(Position& pos, int alpha, int beta, int ply) {
    pvLength_[static_cast<std::size_t>(ply)] = ply;
    ++nodes_;
    if (shouldStop()) {
        return 0;
    }
    if (ply > seldepth_) {
        seldepth_ = ply;
    }
    if (ply >= MAX_PLY - 1) {
        return evaluate(pos);
    }
    if (pos.isRepetition() || pos.halfmoveClock() >= 100 || pos.isInsufficientMaterial()) {
        return 0;
    }

    const bool inCheck = pos.inCheck();
    int standPat = 0;
    int best = -INFINITE;

    if (!inCheck) {
        standPat = evaluate(pos);
        if (standPat >= beta) {
            return standPat;
        }
        if (standPat > alpha) {
            alpha = standPat;
        }
        best = standPat;
    }

    MoveList list;
    generateMoves(pos, list, inCheck ? GenType::All : GenType::Captures);
    if (inCheck) {
        scoreMoves(pos, list, MOVE_NONE, ply);
    } else {
        scoreCaptures(pos, list);
    }

    int legal = 0;
    for (const Move move : list) {
        const bool capture = pos.isCapture(move);
        const bool promotion = isPromotion(move);

        if (!inCheck) {
            int delta = capture ? pieceValue(pos.capturedType(move)) : 0;
            if (promotion) {
                delta += pieceValue(QUEEN) - pieceValue(PAWN);
            }
            if (standPat + delta + 150 <= alpha) {
                continue;
            }
        }

        if (!pos.makeMove(move)) {
            continue;
        }
        ++legal;

        const int score = -qsearch(pos, -beta, -alpha, ply + 1);
        pos.unmakeMove(move);

        if (stop_.load(std::memory_order_relaxed)) {
            return 0;
        }

        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) {
                    break;
                }
            }
        }
    }

    if (inCheck && legal == 0) {
        return -MATE + ply;
    }
    return best;
}

SearchResult Searcher::search(Position& pos, const SearchLimits& limits,
                              const std::function<void(const SearchInfo&)>& onInfo) {
    nodes_ = 0;
    seldepth_ = 0;
    pvLength_.fill(0);
    killers_ = {};
    startTime_ = Clock::now();
    configureTime(limits, pos.sideToMove());
    tt_.newSearch();

    SearchResult result;

    MoveList legalRootMoves;
    generateLegalMoves(pos, legalRootMoves);
    if (legalRootMoves.empty()) {
        result.score = pos.inCheck() ? -MATE : 0;
        result.timeMs = elapsedMs();
        return result;
    }

    Move bestMove = legalRootMoves[0];
    int bestScore = 0;
    int completedDepth = 0;
    std::vector<Move> bestPv;

    for (int depth = 1; depth <= maxDepth_; ++depth) {
        Move iterationBest = MOVE_NONE;
        int iterationScore = 0;
        if (!searchRoot(pos, depth, iterationBest, iterationScore)) {
            break;
        }

        if (iterationBest == MOVE_NONE) {
            bestMove = MOVE_NONE;
            bestScore = iterationScore;
            completedDepth = depth;
            bestPv.clear();
            break;
        }

        bestMove = iterationBest;
        bestScore = iterationScore;
        completedDepth = depth;

        bestPv = extractPv();
        if (bestPv.empty() || bestPv[0] != iterationBest) {
            bestPv.insert(bestPv.begin(), iterationBest);
        }

        if (onInfo) {
            SearchInfo info;
            info.depth = depth;
            info.seldepth = seldepth_;
            info.score = iterationScore;
            info.nodes = nodes_;
            info.timeMs = elapsedMs();
            info.hashfull = tt_.hashfull();
            info.pv = bestPv;
            onInfo(info);
        }

        if (useTime_ && elapsedMs() >= softTimeMs_) {
            break;
        }
        if (maxNodes_ != 0 && nodes_ >= maxNodes_) {
            break;
        }
        if (stop_.load(std::memory_order_relaxed)) {
            break;
        }
    }

    result.bestMove = bestMove;
    if (bestPv.size() > 1) {
        result.ponderMove = bestPv[1];
    }
    result.score = bestScore;
    result.depth = completedDepth;
    result.nodes = nodes_;
    result.timeMs = elapsedMs();
    return result;
}

}  // namespace quasar
