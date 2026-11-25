#pragma once

#include "types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace quasar {

class Position {
public:
    Position();
    explicit Position(std::string_view fen);

    static Position fromFen(std::string_view fen);

    // Returns false if the FEN is malformed.
    bool setFen(std::string_view fen);
    void setStartpos();

    // State ---------------------------------------------------------------
    Color sideToMove() const { return side_; }
    std::uint8_t castlingRights() const { return castling_; }
    int epSquare() const { return ep_; }
    int halfmoveClock() const { return halfmove_; }
    int fullmoveNumber() const { return fullmove_; }
    std::uint64_t key() const { return key_; }
    int historySize() const { return static_cast<int>(history_.size()); }

    Piece pieceAt(int sq) const { return board_[static_cast<std::size_t>(sq)]; }
    Bitboard pieces(Color c) const { return colorBb_[c]; }
    Bitboard pieces(Color c, PieceType pt) const { return typeBb_[c][pt]; }
    Bitboard pieces(PieceType pt) const { return typeBb_[WHITE][pt] | typeBb_[BLACK][pt]; }
    Bitboard occupancy() const { return colorBb_[WHITE] | colorBb_[BLACK]; }
    int kingSquare(Color c) const { return kingSq_[c]; }

    bool inCheck() const { return inCheck(side_); }
    bool inCheck(Color c) const;
    bool isAttacked(int sq, Color by) const;
    bool hasNonPawnMaterial(Color c) const;
    bool isInsufficientMaterial() const;

    // True if the current position has occurred before in the game history.
    bool isRepetition() const;

    // Move making ---------------------------------------------------------
    // Applies a pseudo-legal move, verifies that the mover's king is safe and
    // rolls the move back if it is illegal. Returns true on success.
    bool makeMove(Move m);
    void unmakeMove(Move m);

    void makeNullMove();
    void unmakeNullMove();

    bool isCapture(Move m) const;
    bool isQuiet(Move m) const { return !isCapture(m); }
    Piece capturedPiece(Move m) const;
    PieceType capturedType(Move m) const;
    bool isLegal(Move m);

    // Debug/testing -------------------------------------------------------
    std::uint64_t computeKey() const;
    std::string fen() const;
    std::string toAscii(bool flipped = false) const;

private:
    struct Undo {
        std::uint64_t key = 0;
        int epSquare = -1;
        int halfmove = 0;
        std::uint8_t castling = 0;
        Piece captured = NO_PIECE;
        bool enPassant = false;
    };

    void addPiece(Piece piece, int sq);
    void removePiece(int sq);
    void movePiece(int from, int to);
    void clear();
    bool validatePseudoLegal(Move m) const;

    std::array<Bitboard, COLOR_NB> colorBb_{};
    std::array<std::array<Bitboard, PIECE_TYPE_NB>, COLOR_NB> typeBb_{};
    std::array<Piece, 64> board_{};
    std::array<int, COLOR_NB> kingSq_{-1, -1};

    Color side_ = WHITE;
    std::uint8_t castling_ = 0;
    int ep_ = -1;
    int halfmove_ = 0;
    int fullmove_ = 1;
    std::uint64_t key_ = 0;

    std::vector<Undo> history_;
    std::vector<std::uint64_t> keyHistory_;
};

}  // namespace quasar
