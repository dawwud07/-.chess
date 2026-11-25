#include "quasar/movegen.hpp"

#include "quasar/bitboard.hpp"

#include <bit>
#include <cstdlib>

namespace quasar {

namespace {

void addPawnMoves(const Position& pos, MoveList& list, int from, int to, int rank,
                  GenType type) {
    if (rank == (pos.sideToMove() == WHITE ? 7 : 0)) {
        list.add(makeMove(from, to, QUEEN));
        list.add(makeMove(from, to, ROOK));
        list.add(makeMove(from, to, BISHOP));
        list.add(makeMove(from, to, KNIGHT));
    } else if (type == GenType::All) {
        list.add(makeMove(from, to));
    }
}

void addTargets(MoveList& list, int from, Bitboard targets) {
    while (targets != 0) {
        const int to = std::countr_zero(targets);
        targets &= targets - 1;
        list.add(makeMove(from, to));
    }
}

}  // namespace

void generateMoves(const Position& pos, MoveList& list, GenType type) {
    const Color us = pos.sideToMove();
    const Color them = opposite(us);
    const Bitboard own = pos.pieces(us);
    const Bitboard enemy = pos.pieces(them);
    const Bitboard occupancy = own | enemy;
    const bool capturesOnly = type == GenType::Captures;

    // Pawns ---------------------------------------------------------------
    Bitboard pawns = pos.pieces(us, PAWN);
    while (pawns != 0) {
        const int from = std::countr_zero(pawns);
        pawns &= pawns - 1;

        const int rank = rankOf(from);
        const int file = fileOf(from);
        const int forward = us == WHITE ? 8 : -8;
        const int startRank = us == WHITE ? 1 : 6;
        const int promotionRank = us == WHITE ? 7 : 0;

        const int one = from + forward;
        if (rank != promotionRank && one >= 0 && one < 64 &&
            pos.pieceAt(one) == NO_PIECE) {
            if (rankOf(one) == promotionRank) {
                addPawnMoves(pos, list, from, one, rankOf(one), type);
            } else if (!capturesOnly) {
                list.add(makeMove(from, one));
                if (rank == startRank) {
                    const int two = from + 2 * forward;
                    if (pos.pieceAt(two) == NO_PIECE) {
                        list.add(makeMove(from, two));
                    }
                }
            }
        }

        for (int df = -1; df <= 1; df += 2) {
            const int targetFile = file + df;
            if (targetFile < 0 || targetFile > 7) {
                continue;
            }
            const int targetRank = rank + (us == WHITE ? 1 : -1);
            if (targetRank < 0 || targetRank > 7) {
                continue;
            }
            const int to = makeSquare(targetFile, targetRank);
            if (to == pos.epSquare()) {
                list.add(makeMove(from, to));
            } else if ((enemy & bb::bit(to)) != 0) {
                if (targetRank == promotionRank) {
                    addPawnMoves(pos, list, from, to, targetRank, type);
                } else {
                    list.add(makeMove(from, to));
                }
            }
        }
    }

    // Knights -------------------------------------------------------------
    Bitboard knights = pos.pieces(us, KNIGHT);
    while (knights != 0) {
        const int from = std::countr_zero(knights);
        knights &= knights - 1;
        Bitboard targets = bb::KnightAttacks[from] & ~own;
        if (capturesOnly) {
            targets &= enemy;
        }
        addTargets(list, from, targets);
    }

    // Bishops, rooks and queens ------------------------------------------
    constexpr PieceType sliders[] = {BISHOP, ROOK, QUEEN};
    for (const PieceType pt : sliders) {
        Bitboard pieces = pos.pieces(us, pt);
        while (pieces != 0) {
            const int from = std::countr_zero(pieces);
            pieces &= pieces - 1;
            Bitboard targets = bb::attacks(pt, from, occupancy) & ~own;
            if (capturesOnly) {
                targets &= enemy;
            }
            addTargets(list, from, targets);
        }
    }

    // King ----------------------------------------------------------------
    const int king = pos.kingSquare(us);
    if (king >= 0) {
        Bitboard targets = bb::KingAttacks[king] & ~own;
        if (capturesOnly) {
            targets &= enemy;
        }
        addTargets(list, king, targets);

        if (!capturesOnly) {
            const std::uint8_t rights = pos.castlingRights();
            if (us == WHITE) {
                if ((rights & WHITE_KINGSIDE) != 0 && pos.pieceAt(F1) == NO_PIECE &&
                    pos.pieceAt(G1) == NO_PIECE && !pos.isAttacked(E1, BLACK) &&
                    !pos.isAttacked(F1, BLACK)) {
                    list.add(makeMove(E1, G1));
                }
                if ((rights & WHITE_QUEENSIDE) != 0 && pos.pieceAt(B1) == NO_PIECE &&
                    pos.pieceAt(C1) == NO_PIECE && pos.pieceAt(D1) == NO_PIECE &&
                    !pos.isAttacked(E1, BLACK) && !pos.isAttacked(D1, BLACK)) {
                    list.add(makeMove(E1, C1));
                }
            } else {
                if ((rights & BLACK_KINGSIDE) != 0 && pos.pieceAt(F8) == NO_PIECE &&
                    pos.pieceAt(G8) == NO_PIECE && !pos.isAttacked(E8, WHITE) &&
                    !pos.isAttacked(F8, WHITE)) {
                    list.add(makeMove(E8, G8));
                }
                if ((rights & BLACK_QUEENSIDE) != 0 && pos.pieceAt(B8) == NO_PIECE &&
                    pos.pieceAt(C8) == NO_PIECE && pos.pieceAt(D8) == NO_PIECE &&
                    !pos.isAttacked(E8, WHITE) && !pos.isAttacked(D8, WHITE)) {
                    list.add(makeMove(E8, C8));
                }
            }
        }
    }
}

void generateLegalMoves(Position& pos, MoveList& list) {
    MoveList pseudo;
    generateMoves(pos, pseudo, GenType::All);
    list.clear();
    for (const Move m : pseudo) {
        if (pos.makeMove(m)) {
            pos.unmakeMove(m);
            list.add(m);
        }
    }
}

std::uint64_t perft(Position& pos, int depth) {
    if (depth <= 0) {
        return 1;
    }

    MoveList list;
    generateMoves(pos, list, GenType::All);

    std::uint64_t nodes = 0;
    for (const Move m : list) {
        if (!pos.makeMove(m)) {
            continue;
        }
        if (depth == 1) {
            ++nodes;
        } else {
            nodes += perft(pos, depth - 1);
        }
        pos.unmakeMove(m);
    }
    return nodes;
}

int countLegalMoves(Position& pos) {
    MoveList list;
    generateMoves(pos, list, GenType::All);
    int count = 0;
    for (const Move m : list) {
        if (pos.makeMove(m)) {
            ++count;
            pos.unmakeMove(m);
        }
    }
    return count;
}

}  // namespace quasar
