#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace quasar {

using Bitboard = std::uint64_t;

// ---------------------------------------------------------------------------
// Colours and pieces
// ---------------------------------------------------------------------------

enum Color : std::uint8_t { WHITE = 0, BLACK = 1, COLOR_NB = 2 };

constexpr Color opposite(Color c) { return static_cast<Color>(c ^ 1); }

enum PieceType : std::uint8_t {
    PAWN = 0,
    KNIGHT = 1,
    BISHOP = 2,
    ROOK = 3,
    QUEEN = 4,
    KING = 5,
    PIECE_TYPE_NB = 6,
    NO_PIECE_TYPE = 6
};

// Piece encoding is colour * 6 + type.
enum Piece : std::uint8_t {
    W_PAWN = 0,
    W_KNIGHT = 1,
    W_BISHOP = 2,
    W_ROOK = 3,
    W_QUEEN = 4,
    W_KING = 5,
    B_PAWN = 6,
    B_KNIGHT = 7,
    B_BISHOP = 8,
    B_ROOK = 9,
    B_QUEEN = 10,
    B_KING = 11,
    PIECE_NB = 12,
    NO_PIECE = 12
};

constexpr Piece makePiece(Color c, PieceType pt) {
    return static_cast<Piece>(static_cast<int>(c) * 6 + static_cast<int>(pt));
}

constexpr PieceType pieceType(Piece p) {
    return static_cast<PieceType>(static_cast<int>(p) % 6);
}

constexpr Color pieceColor(Piece p) {
    return static_cast<Color>(static_cast<int>(p) / 6);
}

constexpr bool isPiece(Piece p) { return p < PIECE_NB; }

// ---------------------------------------------------------------------------
// Squares
// ---------------------------------------------------------------------------

// a1 = 0, b1 = 1, ..., h8 = 63.
constexpr int fileOf(int sq) { return sq & 7; }
constexpr int rankOf(int sq) { return sq >> 3; }
constexpr int makeSquare(int file, int rank) { return rank * 8 + file; }
constexpr bool isValidSquare(int sq) { return sq >= 0 && sq < 64; }

inline std::string squareName(int sq) {
    if (!isValidSquare(sq)) {
        return "??";
    }
    std::string s(2, ' ');
    s[0] = static_cast<char>('a' + fileOf(sq));
    s[1] = static_cast<char>('1' + rankOf(sq));
    return s;
}

inline int parseSquare(std::string_view s) {
    if (s.size() != 2 || s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') {
        return -1;
    }
    return makeSquare(s[0] - 'a', s[1] - '1');
}

// ---------------------------------------------------------------------------
// Castling rights
// ---------------------------------------------------------------------------

enum CastlingRight : std::uint8_t {
    WHITE_KINGSIDE = 1,
    WHITE_QUEENSIDE = 2,
    BLACK_KINGSIDE = 4,
    BLACK_QUEENSIDE = 8
};

constexpr int A1 = 0, B1 = 1, C1 = 2, D1 = 3, E1 = 4, F1 = 5, G1 = 6, H1 = 7;
constexpr int A8 = 56, B8 = 57, C8 = 58, D8 = 59, E8 = 60, F8 = 61, G8 = 62, H8 = 63;

// ---------------------------------------------------------------------------
// Moves
// ---------------------------------------------------------------------------
//
// A move fits in 16 bits:
//   bits 0-5   from square
//   bits 6-11  to square
//   bits 12-14 promotion piece type (0 = not a promotion, else KNIGHT..QUEEN)
//   bit  15    unused
//
// The nature of a move (capture, en passant, castle, double push) is derived
// from the position, which keeps the encoding small enough for the TT.

using Move = std::uint16_t;
inline constexpr Move MOVE_NONE = 0;

constexpr Move makeMove(int from, int to, int promotion = 0) {
    return static_cast<Move>(from | (to << 6) | ((promotion & 7) << 12));
}

constexpr int moveFrom(Move m) { return m & 0x3f; }
constexpr int moveTo(Move m) { return (m >> 6) & 0x3f; }
constexpr int movePromotion(Move m) { return (m >> 12) & 7; }
constexpr bool isPromotion(Move m) { return movePromotion(m) != 0; }

inline std::string moveToUci(Move m) {
    if (m == MOVE_NONE) {
        return "0000";
    }
    std::string s = squareName(moveFrom(m)) + squareName(moveTo(m));
    const int promo = movePromotion(m);
    if (promo != 0) {
        constexpr char promoChars[] = {' ', 'n', 'b', 'r', 'q'};
        s.push_back(promoChars[promo]);
    }
    return s;
}

inline Move parseUciMove(std::string_view s) {
    if (s.size() != 4 && s.size() != 5) {
        return MOVE_NONE;
    }
    const int from = parseSquare(s.substr(0, 2));
    const int to = parseSquare(s.substr(2, 2));
    if (from < 0 || to < 0) {
        return MOVE_NONE;
    }
    int promotion = 0;
    if (s.size() == 5) {
        switch (s[4]) {
            case 'n': promotion = KNIGHT; break;
            case 'b': promotion = BISHOP; break;
            case 'r': promotion = ROOK; break;
            case 'q': promotion = QUEEN; break;
            default: return MOVE_NONE;
        }
    }
    return makeMove(from, to, promotion);
}

// ---------------------------------------------------------------------------
// Limits and scores
// ---------------------------------------------------------------------------

inline constexpr int MAX_PLY = 128;
inline constexpr int MAX_MOVES = 256;
inline constexpr int MATE = 32000;
inline constexpr int INFINITE = 32000;
inline constexpr int MATE_IN_MAX_PLY = MATE - MAX_PLY;
inline constexpr int VALUE_NONE = 32001;

constexpr bool isMateScore(int score) {
    return score >= MATE_IN_MAX_PLY || score <= -MATE_IN_MAX_PLY;
}

constexpr int mateDistance(int score) {
    return (score > 0 ? MATE - score : -MATE - score);
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

inline constexpr char pieceChar(Piece p) {
    constexpr char chars[] = "PNBRQKpnbrqk";
    return isPiece(p) ? chars[p] : '.';
}

inline Piece charToPiece(char c) {
    switch (c) {
        case 'P': return W_PAWN;
        case 'N': return W_KNIGHT;
        case 'B': return W_BISHOP;
        case 'R': return W_ROOK;
        case 'Q': return W_QUEEN;
        case 'K': return W_KING;
        case 'p': return B_PAWN;
        case 'n': return B_KNIGHT;
        case 'b': return B_BISHOP;
        case 'r': return B_ROOK;
        case 'q': return B_QUEEN;
        case 'k': return B_KING;
        default: return NO_PIECE;
    }
}

inline constexpr int pieceValue(PieceType pt) {
    constexpr int values[PIECE_TYPE_NB] = {100, 320, 330, 500, 900, 0};
    return values[pt];
}

inline constexpr std::string_view START_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

}  // namespace quasar
