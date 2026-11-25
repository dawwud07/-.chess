#include "quasar/position.hpp"

#include "quasar/bitboard.hpp"
#include "quasar/zobrist.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace quasar {

namespace {

constexpr std::uint8_t whiteCastling = WHITE_KINGSIDE | WHITE_QUEENSIDE;
constexpr std::uint8_t blackCastling = BLACK_KINGSIDE | BLACK_QUEENSIDE;

}  // namespace

Position::Position() {
    board_.fill(NO_PIECE);
    setStartpos();
}

Position::Position(std::string_view fen) {
    board_.fill(NO_PIECE);
    if (!setFen(fen)) {
        throw std::invalid_argument("Invalid FEN: " + std::string(fen));
    }
}

Position Position::fromFen(std::string_view fen) {
    return Position(fen);
}

void Position::clear() {
    colorBb_.fill(0);
    for (auto& perColor : typeBb_) {
        perColor.fill(0);
    }
    board_.fill(NO_PIECE);
    kingSq_ = {-1, -1};
    side_ = WHITE;
    castling_ = 0;
    ep_ = -1;
    halfmove_ = 0;
    fullmove_ = 1;
    key_ = 0;
    history_.clear();
    keyHistory_.clear();
}

void Position::addPiece(Piece piece, int sq) {
    assert(isPiece(piece));
    assert(isValidSquare(sq));
    assert(board_[static_cast<std::size_t>(sq)] == NO_PIECE);

    board_[static_cast<std::size_t>(sq)] = piece;
    const Bitboard b = bb::bit(sq);
    const Color c = pieceColor(piece);
    const PieceType pt = pieceType(piece);
    colorBb_[c] |= b;
    typeBb_[c][pt] |= b;
    key_ ^= zobrist::psq[piece][sq];
    if (pt == KING) {
        kingSq_[c] = sq;
    }
}

void Position::removePiece(int sq) {
    const Piece piece = board_[static_cast<std::size_t>(sq)];
    if (piece == NO_PIECE) {
        return;
    }

    const Bitboard b = bb::bit(sq);
    const Color c = pieceColor(piece);
    const PieceType pt = pieceType(piece);
    colorBb_[c] &= ~b;
    typeBb_[c][pt] &= ~b;
    key_ ^= zobrist::psq[piece][sq];
    if (pt == KING) {
        kingSq_[c] = -1;
    }
    board_[static_cast<std::size_t>(sq)] = NO_PIECE;
}

void Position::movePiece(int from, int to) {
    const Piece piece = board_[static_cast<std::size_t>(from)];
    removePiece(from);
    addPiece(piece, to);
}

void Position::setStartpos() {
    (void)setFen(START_FEN);
}

bool Position::setFen(std::string_view fen) {
    clear();

    std::istringstream input{std::string(fen)};
    std::string placement;
    std::string side;
    std::string castling;
    std::string ep;
    int halfmove = 0;
    int fullmove = 1;

    if (!(input >> placement >> side >> castling >> ep >> halfmove >> fullmove)) {
        return false;
    }
    if (halfmove < 0 || fullmove < 1) {
        return false;
    }

    int rank = 7;
    int file = 0;
    for (const char c : placement) {
        if (c == '/') {
            if (file != 8 || rank == 0) {
                return false;
            }
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) {
                return false;
            }
        } else {
            const Piece piece = charToPiece(c);
            if (piece == NO_PIECE || rank < 0 || file > 7) {
                return false;
            }
            addPiece(piece, makeSquare(file, rank));
            ++file;
        }
    }
    if (rank != 0 || file != 8) {
        return false;
    }

    if (side == "w") {
        side_ = WHITE;
    } else if (side == "b") {
        side_ = BLACK;
    } else {
        return false;
    }

    if (castling != "-") {
        for (const char c : castling) {
            switch (c) {
                case 'K': castling_ |= WHITE_KINGSIDE; break;
                case 'Q': castling_ |= WHITE_QUEENSIDE; break;
                case 'k': castling_ |= BLACK_KINGSIDE; break;
                case 'q': castling_ |= BLACK_QUEENSIDE; break;
                default: return false;
            }
        }
    }

    if (ep == "-") {
        ep_ = -1;
    } else {
        ep_ = parseSquare(ep);
        if (ep_ < 0) {
            return false;
        }
    }

    if (kingSq_[WHITE] < 0 || kingSq_[BLACK] < 0) {
        return false;
    }

    halfmove_ = halfmove;
    fullmove_ = fullmove;
    key_ = computeKey();
    keyHistory_.push_back(key_);
    return true;
}

std::uint64_t Position::computeKey() const {
    std::uint64_t key = 0;
    for (int sq = 0; sq < 64; ++sq) {
        const Piece piece = board_[static_cast<std::size_t>(sq)];
        if (piece != NO_PIECE) {
            key ^= zobrist::psq[piece][sq];
        }
    }
    if (side_ == BLACK) {
        key ^= zobrist::sideToMove;
    }
    key ^= zobrist::castling[castling_];
    if (ep_ != -1) {
        key ^= zobrist::enPassantFile[fileOf(ep_)];
    }
    return key;
}

bool Position::isAttacked(int sq, Color by) const {
    const Bitboard occupied = occupancy();
    const Color them = opposite(by);

    if (bb::PawnAttacks[them][sq] & pieces(by, PAWN)) {
        return true;
    }
    if (bb::KnightAttacks[sq] & pieces(by, KNIGHT)) {
        return true;
    }
    if (bb::KingAttacks[sq] & pieces(by, KING)) {
        return true;
    }

    const Bitboard rookSliders = pieces(by, ROOK) | pieces(by, QUEEN);
    if (rookSliders != 0 && (bb::rookAttacks(sq, occupied) & rookSliders) != 0) {
        return true;
    }
    const Bitboard bishopSliders = pieces(by, BISHOP) | pieces(by, QUEEN);
    if (bishopSliders != 0 && (bb::bishopAttacks(sq, occupied) & bishopSliders) != 0) {
        return true;
    }
    return false;
}

bool Position::inCheck(Color c) const {
    const int king = kingSq_[c];
    return king >= 0 && isAttacked(king, opposite(c));
}

bool Position::hasNonPawnMaterial(Color c) const {
    return (pieces(c, KNIGHT) | pieces(c, BISHOP) | pieces(c, ROOK) | pieces(c, QUEEN)) != 0;
}

bool Position::isInsufficientMaterial() const {
    if ((pieces(PAWN) | pieces(ROOK) | pieces(QUEEN)) != 0) {
        return false;
    }

    const int whiteKnights = std::popcount(pieces(WHITE, KNIGHT));
    const int whiteBishops = std::popcount(pieces(WHITE, BISHOP));
    const int blackKnights = std::popcount(pieces(BLACK, KNIGHT));
    const int blackBishops = std::popcount(pieces(BLACK, BISHOP));
    const int whiteMinors = whiteKnights + whiteBishops;
    const int blackMinors = blackKnights + blackBishops;

    if (whiteMinors == 0 && blackMinors == 0) {
        return true;
    }
    if (whiteMinors == 1 && blackMinors == 0) {
        return true;
    }
    if (blackMinors == 1 && whiteMinors == 0) {
        return true;
    }
    if (whiteKnights == 0 && blackKnights == 0 && whiteBishops == 1 && blackBishops == 1) {
        const int whiteBishop = std::countr_zero(pieces(WHITE, BISHOP));
        const int blackBishop = std::countr_zero(pieces(BLACK, BISHOP));
        const bool whiteDark = ((bb::bit(whiteBishop) & bb::DARK_SQUARES) != 0);
        const bool blackDark = ((bb::bit(blackBishop) & bb::DARK_SQUARES) != 0);
        return whiteDark == blackDark;
    }
    return false;
}

bool Position::isRepetition() const {
    const int n = static_cast<int>(keyHistory_.size());
    if (n < 3) {
        return false;
    }

    const int limit = std::max(0, n - 1 - halfmove_);
    for (int i = n - 3; i >= limit; i -= 2) {
        if (keyHistory_[static_cast<std::size_t>(i)] == key_) {
            return true;
        }
    }
    return false;
}

bool Position::isCapture(Move m) const {
    return capturedPiece(m) != NO_PIECE;
}

Piece Position::capturedPiece(Move m) const {
    const int from = moveFrom(m);
    const int to = moveTo(m);
    const Piece target = board_[static_cast<std::size_t>(to)];
    if (target != NO_PIECE) {
        return target;
    }
    if (pieceType(board_[static_cast<std::size_t>(from)]) == PAWN && to == ep_ &&
        fileOf(from) != fileOf(to)) {
        return makePiece(opposite(side_), PAWN);
    }
    return NO_PIECE;
}

PieceType Position::capturedType(Move m) const {
    const Piece captured = capturedPiece(m);
    return captured == NO_PIECE ? NO_PIECE_TYPE : pieceType(captured);
}

bool Position::makeMove(Move m) {
    if (m == MOVE_NONE) {
        return false;
    }

    const Color us = side_;
    const Color them = opposite(us);
    const int from = moveFrom(m);
    const int to = moveTo(m);
    if (from == to) {
        return false;
    }

    const Piece moving = board_[static_cast<std::size_t>(from)];
    if (!isPiece(moving) || pieceColor(moving) != us) {
        return false;
    }

    const PieceType movingType = pieceType(moving);
    const int promotion = movePromotion(m);
    const bool promotionRank = rankOf(to) == (us == WHITE ? 7 : 0);
    if (promotion != 0) {
        if (movingType != PAWN || promotion > QUEEN || !promotionRank) {
            return false;
        }
    } else if (movingType == PAWN && promotionRank) {
        return false;
    }

    if (pieceColor(board_[static_cast<std::size_t>(to)]) == us) {
        return false;
    }

    Piece captured = board_[static_cast<std::size_t>(to)];
    int capturedSq = to;
    bool enPassant = false;
    if (movingType == PAWN && to == ep_ && fileOf(from) != fileOf(to)) {
        if (std::abs(fileOf(to) - fileOf(from)) != 1) {
            return false;
        }
        capturedSq = to + (us == WHITE ? -8 : 8);
        captured = board_[static_cast<std::size_t>(capturedSq)];
        if (captured != makePiece(them, PAWN)) {
            return false;
        }
        enPassant = true;
    }
    if (isPiece(captured) && pieceType(captured) == KING) {
        return false;
    }

    const bool castle = movingType == KING && std::abs(to - from) == 2;
    int rookFrom = -1;
    int rookTo = -1;
    if (castle) {
        if (us == WHITE) {
            if (from != E1 || (to != G1 && to != C1)) {
                return false;
            }
            if (to == G1) {
                if ((castling_ & WHITE_KINGSIDE) == 0 || board_[F1] != NO_PIECE ||
                    board_[G1] != NO_PIECE || board_[H1] != W_ROOK) {
                    return false;
                }
                rookFrom = H1;
                rookTo = F1;
            } else {
                if ((castling_ & WHITE_QUEENSIDE) == 0 || board_[B1] != NO_PIECE ||
                    board_[C1] != NO_PIECE || board_[D1] != NO_PIECE || board_[A1] != W_ROOK) {
                    return false;
                }
                rookFrom = A1;
                rookTo = D1;
            }
        } else {
            if (from != E8 || (to != G8 && to != C8)) {
                return false;
            }
            if (to == G8) {
                if ((castling_ & BLACK_KINGSIDE) == 0 || board_[F8] != NO_PIECE ||
                    board_[G8] != NO_PIECE || board_[H8] != B_ROOK) {
                    return false;
                }
                rookFrom = H8;
                rookTo = F8;
            } else {
                if ((castling_ & BLACK_QUEENSIDE) == 0 || board_[B8] != NO_PIECE ||
                    board_[C8] != NO_PIECE || board_[D8] != NO_PIECE || board_[A8] != B_ROOK) {
                    return false;
                }
                rookFrom = A8;
                rookTo = D8;
            }
        }
    }

    if (castle && (isAttacked(from, them) || isAttacked((from + to) / 2, them))) {
        return false;
    }

    Undo undo;
    undo.key = key_;
    undo.epSquare = ep_;
    undo.halfmove = halfmove_;
    undo.castling = castling_;
    undo.captured = captured;
    undo.enPassant = enPassant;

    if (ep_ != -1) {
        key_ ^= zobrist::enPassantFile[fileOf(ep_)];
    }
    key_ ^= zobrist::castling[castling_];

    if (enPassant) {
        removePiece(capturedSq);
    } else if (captured != NO_PIECE) {
        removePiece(to);
    }

    removePiece(from);
    if (promotion != 0) {
        addPiece(makePiece(us, static_cast<PieceType>(promotion)), to);
    } else {
        addPiece(moving, to);
    }

    if (castle) {
        movePiece(rookFrom, rookTo);
    }

    if (movingType == KING) {
        castling_ &= static_cast<std::uint8_t>(us == WHITE ? ~whiteCastling : ~blackCastling);
    } else if (movingType == ROOK) {
        if (from == H1) {
            castling_ &= static_cast<std::uint8_t>(~WHITE_KINGSIDE);
        } else if (from == A1) {
            castling_ &= static_cast<std::uint8_t>(~WHITE_QUEENSIDE);
        } else if (from == H8) {
            castling_ &= static_cast<std::uint8_t>(~BLACK_KINGSIDE);
        } else if (from == A8) {
            castling_ &= static_cast<std::uint8_t>(~BLACK_QUEENSIDE);
        }
    }

    if (capturedSq == H1) {
        castling_ &= static_cast<std::uint8_t>(~WHITE_KINGSIDE);
    } else if (capturedSq == A1) {
        castling_ &= static_cast<std::uint8_t>(~WHITE_QUEENSIDE);
    } else if (capturedSq == H8) {
        castling_ &= static_cast<std::uint8_t>(~BLACK_KINGSIDE);
    } else if (capturedSq == A8) {
        castling_ &= static_cast<std::uint8_t>(~BLACK_QUEENSIDE);
    }

    ep_ = -1;
    if (movingType == PAWN && std::abs(to - from) == 16) {
        const int candidate = (from + to) / 2;
        if ((bb::PawnAttacks[us][candidate] & pieces(them, PAWN)) != 0) {
            ep_ = candidate;
        }
    }

    key_ ^= zobrist::castling[castling_];
    if (ep_ != -1) {
        key_ ^= zobrist::enPassantFile[fileOf(ep_)];
    }
    key_ ^= zobrist::sideToMove;

    side_ = them;
    if (movingType == PAWN || captured != NO_PIECE) {
        halfmove_ = 0;
    } else {
        ++halfmove_;
    }
    if (us == BLACK) {
        ++fullmove_;
    }

    history_.push_back(undo);
    keyHistory_.push_back(key_);

    if (isAttacked(kingSq_[us], them)) {
        unmakeMove(m);
        return false;
    }
    return true;
}

void Position::unmakeMove(Move m) {
    assert(!history_.empty());
    assert(!keyHistory_.empty());

    const Color us = opposite(side_);
    const Color them = side_;
    (void)them;
    const int from = moveFrom(m);
    const int to = moveTo(m);
    const int promotion = movePromotion(m);

    Undo undo = history_.back();
    history_.pop_back();
    keyHistory_.pop_back();

    const Piece moved = board_[static_cast<std::size_t>(to)];
    const bool castle = pieceType(moved) == KING && std::abs(to - from) == 2;

    if (castle) {
        const int rookTo = (to > from) ? from + 1 : from - 1;
        const int rookFrom = (to > from) ? from + 3 : from - 4;
        movePiece(rookTo, rookFrom);
    }

    removePiece(to);
    if (promotion != 0) {
        addPiece(makePiece(us, PAWN), from);
    } else {
        addPiece(moved, from);
    }

    if (undo.captured != NO_PIECE) {
        if (undo.enPassant) {
            const int capturedSq = to + (us == WHITE ? -8 : 8);
            addPiece(undo.captured, capturedSq);
        } else {
            addPiece(undo.captured, to);
        }
    }

    side_ = us;
    castling_ = undo.castling;
    ep_ = undo.epSquare;
    halfmove_ = undo.halfmove;
    key_ = undo.key;
    if (us == BLACK) {
        --fullmove_;
    }
}

void Position::makeNullMove() {
    Undo undo;
    undo.key = key_;
    undo.epSquare = ep_;
    undo.halfmove = halfmove_;
    undo.castling = castling_;
    undo.captured = NO_PIECE;
    undo.enPassant = false;

    if (ep_ != -1) {
        key_ ^= zobrist::enPassantFile[fileOf(ep_)];
        ep_ = -1;
    }
    key_ ^= zobrist::sideToMove;
    side_ = opposite(side_);
    ++halfmove_;

    history_.push_back(undo);
    keyHistory_.push_back(key_);
}

void Position::unmakeNullMove() {
    assert(!history_.empty());
    assert(!keyHistory_.empty());

    const Undo undo = history_.back();
    history_.pop_back();
    keyHistory_.pop_back();

    side_ = opposite(side_);
    key_ = undo.key;
    ep_ = undo.epSquare;
    halfmove_ = undo.halfmove;
    castling_ = undo.castling;
}

bool Position::isLegal(Move m) {
    if (!makeMove(m)) {
        return false;
    }
    unmakeMove(m);
    return true;
}

std::string Position::fen() const {
    std::string result;

    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece piece = board_[static_cast<std::size_t>(makeSquare(file, rank))];
            if (piece == NO_PIECE) {
                ++empty;
            } else {
                if (empty > 0) {
                    result += static_cast<char>('0' + empty);
                    empty = 0;
                }
                result += pieceChar(piece);
            }
        }
        if (empty > 0) {
            result += static_cast<char>('0' + empty);
        }
        if (rank > 0) {
            result += '/';
        }
    }

    result += side_ == WHITE ? " w " : " b ";

    if (castling_ == 0) {
        result += '-';
    } else {
        if (castling_ & WHITE_KINGSIDE) result += 'K';
        if (castling_ & WHITE_QUEENSIDE) result += 'Q';
        if (castling_ & BLACK_KINGSIDE) result += 'k';
        if (castling_ & BLACK_QUEENSIDE) result += 'q';
    }

    result += ' ';
    result += ep_ == -1 ? "-" : squareName(ep_);
    result += ' ';
    result += std::to_string(halfmove_);
    result += ' ';
    result += std::to_string(fullmove_);
    return result;
}

std::string Position::toAscii(bool flipped) const {
    std::string result;
    result.reserve(800);

    for (int row = 0; row < 8; ++row) {
        const int rank = flipped ? row : 7 - row;
        result += "  +---+---+---+---+---+---+---+---+\n";
        result += static_cast<char>('1' + rank);
        result += ' ';
        for (int col = 0; col < 8; ++col) {
            const int file = flipped ? 7 - col : col;
            const Piece piece = board_[static_cast<std::size_t>(makeSquare(file, rank))];
            result += "| ";
            result += pieceChar(piece);
            result += ' ';
        }
        result += "|\n";
    }
    result += "  +---+---+---+---+---+---+---+---+\n";
    result += "    a   b   c   d   e   f   g   h\n\n";
    result += "FEN:       " + fen() + "\n";
    result += "Key:       0x";
    constexpr char hex[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; --i) {
        result += hex[(key_ >> (i * 4)) & 0xF];
    }
    result += '\n';
    result += "Side:      ";
    result += side_ == WHITE ? "white" : "black";
    result += "\n";
    result += "Castling:  ";
    if (castling_ == 0) {
        result += '-';
    } else {
        if (castling_ & WHITE_KINGSIDE) result += 'K';
        if (castling_ & WHITE_QUEENSIDE) result += 'Q';
        if (castling_ & BLACK_KINGSIDE) result += 'k';
        if (castling_ & BLACK_QUEENSIDE) result += 'q';
    }
    result += "\n";
    result += "En passant: " + (ep_ == -1 ? std::string("-") : squareName(ep_)) + "\n";
    result += "Halfmove:   " + std::to_string(halfmove_) + "\n";
    result += "Fullmove:   " + std::to_string(fullmove_) + "\n";
    return result;
}

}  // namespace quasar
