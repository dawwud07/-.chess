#include "quasar/eval.hpp"

#include "quasar/bitboard.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>

namespace quasar {

namespace {

using Pst = std::array<int, 64>;

constexpr int MAX_PHASE = 24;

int materialMg(PieceType pt) {
    switch (pt) {
        case PAWN: return 100;
        case KNIGHT: return 320;
        case BISHOP: return 330;
        case ROOK: return 500;
        case QUEEN: return 900;
        default: return 0;
    }
}

int materialEg(PieceType pt) {
    switch (pt) {
        case PAWN: return 120;
        case KNIGHT: return 330;
        case BISHOP: return 350;
        case ROOK: return 550;
        case QUEEN: return 950;
        default: return 0;
    }
}

int phaseWeight(PieceType pt) {
    switch (pt) {
        case KNIGHT:
        case BISHOP: return 1;
        case ROOK: return 2;
        case QUEEN: return 4;
        default: return 0;
    }
}

Pst pawnMg{};
Pst pawnEg{};
Pst knightMg{};
Pst knightEg{};
Pst bishopMg{};
Pst bishopEg{};
Pst rookMg{};
Pst rookEg{};
Pst queenMg{};
Pst queenEg{};
Pst kingMg{};
Pst kingEg{};

std::array<std::array<Bitboard, 64>, COLOR_NB> passedMask{};

int centerDistance(int file, int rank) {
    return std::max(std::abs(2 * file - 7), std::abs(2 * rank - 7));
}

int centrality(int file, int rank) {
    return 7 - centerDistance(file, rank);
}

void buildTables() {
    for (int sq = 0; sq < 64; ++sq) {
        const int file = fileOf(sq);
        const int rank = rankOf(sq);
        const int c = centrality(file, rank);

        pawnMg[sq] = rank == 0 ? 0 : (rank - 1) * 8 + c * 2;
        pawnEg[sq] = rank == 0 ? 0 : (rank - 1) * 10 + c * 3;

        knightMg[sq] = c * 5 - 10;
        knightEg[sq] = c * 4 - 10;

        bishopMg[sq] = c * 3;
        bishopEg[sq] = c * 3;

        rookMg[sq] = c + (rank == 6 ? 20 : 0);
        rookEg[sq] = (rank == 6 ? 12 : 0);

        queenMg[sq] = c;
        queenEg[sq] = c * 2;

        if (rank == 0) {
            if (file == 6 || file == 2) {
                kingMg[sq] = 25;
            } else if (file == 0 || file == 7) {
                kingMg[sq] = 15;
            } else if (file == 1 || file == 5) {
                kingMg[sq] = 10;
            } else {
                kingMg[sq] = -12;
            }
        } else if (rank == 1) {
            kingMg[sq] = -15;
        } else {
            kingMg[sq] = -5 * rank - 3 * c;
        }
        kingEg[sq] = c * 8 - 20;
    }

    for (int sq = 0; sq < 64; ++sq) {
        const int file = fileOf(sq);
        const int rank = rankOf(sq);
        Bitboard white = 0;
        Bitboard black = 0;
        for (int df = -1; df <= 1; ++df) {
            const int f = file + df;
            if (f < 0 || f > 7) {
                continue;
            }
            for (int r = rank + 1; r < 8; ++r) {
                white |= bb::bit(makeSquare(f, r));
            }
            for (int r = rank - 1; r >= 0; --r) {
                black |= bb::bit(makeSquare(f, r));
            }
        }
        passedMask[WHITE][sq] = white;
        passedMask[BLACK][sq] = black;
    }
}

int pstMgValue(PieceType pt, int sq) {
    switch (pt) {
        case PAWN: return pawnMg[static_cast<std::size_t>(sq)];
        case KNIGHT: return knightMg[static_cast<std::size_t>(sq)];
        case BISHOP: return bishopMg[static_cast<std::size_t>(sq)];
        case ROOK: return rookMg[static_cast<std::size_t>(sq)];
        case QUEEN: return queenMg[static_cast<std::size_t>(sq)];
        case KING: return kingMg[static_cast<std::size_t>(sq)];
        default: return 0;
    }
}

int pstEgValue(PieceType pt, int sq) {
    switch (pt) {
        case PAWN: return pawnEg[static_cast<std::size_t>(sq)];
        case KNIGHT: return knightEg[static_cast<std::size_t>(sq)];
        case BISHOP: return bishopEg[static_cast<std::size_t>(sq)];
        case ROOK: return rookEg[static_cast<std::size_t>(sq)];
        case QUEEN: return queenEg[static_cast<std::size_t>(sq)];
        case KING: return kingEg[static_cast<std::size_t>(sq)];
        default: return 0;
    }
}

int mobilityWeight(PieceType pt, bool endgame) {
    switch (pt) {
        case KNIGHT: return 3;
        case BISHOP: return endgame ? 3 : 2;
        case ROOK: return endgame ? 3 : 2;
        case QUEEN: return endgame ? 2 : 1;
        default: return 0;
    }
}

int passedBonusMg(int relativeRank) {
    constexpr int bonus[8] = {0, 0, 5, 10, 15, 25, 45, 80};
    return bonus[std::clamp(relativeRank, 0, 7)];
}

int passedBonusEg(int relativeRank) {
    constexpr int bonus[8] = {0, 5, 10, 20, 35, 60, 100, 150};
    return bonus[std::clamp(relativeRank, 0, 7)];
}

struct EvalScore {
    int mg = 0;
    int eg = 0;
};

struct EvalTerms {
    int mobilityMg = 0;
    int mobilityEg = 0;
    int pawnMg = 0;
    int pawnEg = 0;
    int kingSafety = 0;
};

void addForColor(const Position& pos, Color color, EvalScore& score, int& phase,
                 EvalTerms& terms) {
    const int sign = color == WHITE ? 1 : -1;
    const Bitboard own = pos.pieces(color);
    const Bitboard occupied = pos.occupancy();
    const Bitboard ownPawns = pos.pieces(color, PAWN);
    const Bitboard enemyPawns = pos.pieces(opposite(color), PAWN);

    for (const PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) {
        Bitboard pieces = pos.pieces(color, pt);
        while (pieces != 0) {
            const int sq = std::countr_zero(pieces);
            pieces &= pieces - 1;
            const int mirrored = color == WHITE ? sq : sq ^ 56;

            score.mg += sign * (materialMg(pt) + pstMgValue(pt, mirrored));
            score.eg += sign * (materialEg(pt) + pstEgValue(pt, mirrored));
            phase += phaseWeight(pt);

            const int mobility = std::popcount(bb::attacks(pt, sq, occupied) & ~own);
            terms.mobilityMg += sign * mobility * mobilityWeight(pt, false);
            terms.mobilityEg += sign * mobility * mobilityWeight(pt, true);
        }
    }

    std::array<int, 8> fileCounts{};
    Bitboard pawns = ownPawns;
    while (pawns != 0) {
        const int sq = std::countr_zero(pawns);
        pawns &= pawns - 1;
        ++fileCounts[static_cast<std::size_t>(fileOf(sq))];
    }

    pawns = ownPawns;
    int pawnStructureMg = 0;
    int pawnStructureEg = 0;
    while (pawns != 0) {
        const int sq = std::countr_zero(pawns);
        pawns &= pawns - 1;
        const int file = fileOf(sq);
        const int rank = rankOf(sq);
        const int mirrored = color == WHITE ? sq : sq ^ 56;

        score.mg += sign * (materialMg(PAWN) + pstMgValue(PAWN, mirrored));
        score.eg += sign * (materialEg(PAWN) + pstEgValue(PAWN, mirrored));

        const bool isolated =
            (file == 0 || fileCounts[static_cast<std::size_t>(file - 1)] == 0) &&
            (file == 7 || fileCounts[static_cast<std::size_t>(file + 1)] == 0);
        if (isolated) {
            pawnStructureMg -= 12;
            pawnStructureEg -= 15;
        }

        if (fileCounts[static_cast<std::size_t>(file)] > 1) {
            pawnStructureMg -= 8;
            pawnStructureEg -= 12;
        }

        if ((enemyPawns & passedMask[color][static_cast<std::size_t>(sq)]) == 0) {
            const int relativeRank = color == WHITE ? rank : 7 - rank;
            pawnStructureMg += passedBonusMg(relativeRank);
            pawnStructureEg += passedBonusEg(relativeRank);
        }

        const int frontRank = color == WHITE ? rank + 1 : rank - 1;
        for (int df = -1; df <= 1; ++df) {
            const int f = file + df;
            if (f < 0 || f > 7 || frontRank < 0 || frontRank > 7) {
                continue;
            }
            if ((ownPawns & bb::bit(makeSquare(f, frontRank))) != 0) {
                pawnStructureMg += 2;
                pawnStructureEg += 2;
                break;
            }
        }
    }
    terms.pawnMg += sign * pawnStructureMg;
    terms.pawnEg += sign * pawnStructureEg;

    if (std::popcount(pos.pieces(color, BISHOP)) >= 2) {
        score.mg += sign * 25;
        score.eg += sign * 40;
    }

    Bitboard rooks = pos.pieces(color, ROOK);
    while (rooks != 0) {
        const int sq = std::countr_zero(rooks);
        rooks &= rooks - 1;
        const int file = fileOf(sq);
        const Bitboard fileMask = bb::fileBb(file);
        if ((ownPawns & fileMask) == 0) {
            if ((pos.pieces(PAWN) & fileMask) == 0) {
                score.mg += sign * 18;
                score.eg += sign * 10;
            } else {
                score.mg += sign * 9;
                score.eg += sign * 5;
            }
        }
        const int relativeRank = color == WHITE ? rankOf(sq) : 7 - rankOf(sq);
        if (relativeRank == 6) {
            score.mg += sign * 12;
            score.eg += sign * 8;
        }
    }

    const int kingSq = pos.kingSquare(color);
    if (kingSq >= 0) {
        const int kingFile = fileOf(kingSq);
        const int kingRank = rankOf(kingSq);
        const int shieldRank = color == WHITE ? kingRank + 1 : kingRank - 1;
        int shield = 0;
        for (int df = -1; df <= 1; ++df) {
            const int f = kingFile + df;
            if (f < 0 || f > 7 || shieldRank < 0 || shieldRank > 7) {
                continue;
            }
            if ((ownPawns & bb::bit(makeSquare(f, shieldRank))) != 0) {
                ++shield;
            }
            const int secondRank = color == WHITE ? shieldRank + 1 : shieldRank - 1;
            if (secondRank >= 0 && secondRank < 8 &&
                (ownPawns & bb::bit(makeSquare(f, secondRank))) != 0) {
                ++shield;
            }
        }
        terms.kingSafety += sign * (shield * 6 - 24);
        if (kingRank == (color == WHITE ? 0 : 7) && (kingFile == 2 || kingFile == 6)) {
            terms.kingSafety += sign * 12;
        }
    }
}

}  // namespace

void initEval() {
    static bool initialised = false;
    if (initialised) {
        return;
    }
    initialised = true;
    buildTables();
}

int evaluate(const Position& pos) {
    initEval();

    EvalScore score{};
    EvalTerms terms;
    int phase = 0;

    addForColor(pos, WHITE, score, phase, terms);
    addForColor(pos, BLACK, score, phase, terms);

    phase = std::min(phase, MAX_PHASE);
    score.mg += terms.mobilityMg + terms.pawnMg + terms.kingSafety;
    score.eg += terms.mobilityEg + terms.pawnEg;

    const int tapered = (score.mg * phase + score.eg * (MAX_PHASE - phase)) / MAX_PHASE;
    const int perspective = pos.sideToMove() == WHITE ? tapered : -tapered;
    return std::clamp(perspective + 8, -29000, 29000);
}

}  // namespace quasar
