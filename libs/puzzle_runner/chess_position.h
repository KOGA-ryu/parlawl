#pragma once

#include <array>
#include <optional>

#include <QString>
#include <QVector>

namespace parlawl::puzzle_runner {

enum class PieceColor {
    None,
    White,
    Black,
};

enum class PieceType {
    None,
    Pawn,
    Knight,
    Bishop,
    Rook,
    Queen,
    King,
};

struct Piece {
    PieceType type = PieceType::None;
    PieceColor color = PieceColor::None;

    [[nodiscard]] bool isEmpty() const { return type == PieceType::None || color == PieceColor::None; }
};

struct Move {
    int from = -1;
    int to = -1;
    PieceType promotion = PieceType::None;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QString uci() const;

    static std::optional<Move> fromUci(const QString &uci);

    bool operator==(const Move &other) const = default;
};

class ChessPosition
{
public:
    static std::optional<ChessPosition> fromFen(const QString &fen, QString *errorMessage = nullptr);

    [[nodiscard]] QString toFen() const;
    [[nodiscard]] PieceColor sideToMove() const { return m_sideToMove; }
    [[nodiscard]] Piece pieceAt(int square) const;
    [[nodiscard]] QVector<Move> legalMoves() const;
    [[nodiscard]] bool isLegalMove(const Move &move) const;
    bool applyMove(const Move &move);

    [[nodiscard]] int selectedKingSquare(PieceColor color) const;
    [[nodiscard]] bool isInCheck(PieceColor color) const;

    static int fileOf(int square);
    static int rankOf(int square);
    static int squareIndex(int file, int rank);
    static QString squareName(int square);
    static int squareFromName(const QString &name);
    static PieceColor opposite(PieceColor color);
    static QChar pieceGlyph(const Piece &piece);

private:
    QVector<Move> pseudoLegalMoves() const;
    bool isSquareAttacked(int square, PieceColor attacker) const;
    void addPawnMoves(QVector<Move> *moves, int square, const Piece &piece) const;
    void addKnightMoves(QVector<Move> *moves, int square, const Piece &piece) const;
    void addSlidingMoves(QVector<Move> *moves, int square, const Piece &piece, const QVector<int> &directions) const;
    void addKingMoves(QVector<Move> *moves, int square, const Piece &piece) const;
    void addPromotionMoves(QVector<Move> *moves, int from, int to) const;
    bool applyUnchecked(const Move &move);
    bool canCastleKingSide(PieceColor color) const;
    bool canCastleQueenSide(PieceColor color) const;

    std::array<Piece, 64> m_board {};
    PieceColor m_sideToMove = PieceColor::White;
    bool m_whiteCastleKingSide = false;
    bool m_whiteCastleQueenSide = false;
    bool m_blackCastleKingSide = false;
    bool m_blackCastleQueenSide = false;
    int m_enPassantSquare = -1;
    int m_halfmoveClock = 0;
    int m_fullmoveNumber = 1;
};

} // namespace parlawl::puzzle_runner
