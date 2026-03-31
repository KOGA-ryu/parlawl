#pragma once

#include <QObject>
#include <QVector>

#include "chess_position.h"
#include "puzzle_types.h"

namespace parlawl::puzzle_runner {

class GameStateStore : public QObject
{
    Q_OBJECT

public:
    explicit GameStateStore(QObject *parent = nullptr);

    void loadPuzzle(const PuzzleDefinition &puzzle, const QVector<ChessPosition> &sourceHistory);
    void applyMoveSequence(const QVector<AppliedMove> &moves);
    void resetToInitial();
    bool stepBackward();
    bool stepForward();
    void setSelectedSquare(int square);
    void clearSelectedSquare();

    [[nodiscard]] const PuzzleDefinition &currentPuzzle() const { return m_puzzle; }
    [[nodiscard]] bool hasPosition() const { return !m_positions.isEmpty(); }
    [[nodiscard]] const ChessPosition &currentPosition() const;
    [[nodiscard]] const ChessPosition &latestPosition() const;
    [[nodiscard]] const QVector<AppliedMove> &moves() const { return m_moves; }
    [[nodiscard]] int selectedSquare() const { return m_selectedSquare; }
    [[nodiscard]] bool isViewingLatest() const;
    [[nodiscard]] int currentViewIndex() const { return m_viewIndex; }
    [[nodiscard]] int puzzleStartViewIndex() const { return m_puzzleStartViewIndex; }
    [[nodiscard]] QVector<Move> legalMoves() const;
    [[nodiscard]] QVector<Move> legalMovesFromSquare(int square) const;
    [[nodiscard]] QVector<Move> legalMovesBetween(int fromSquare, int toSquare) const;

signals:
    void stateChanged();

private:
    void emitChanged();

    PuzzleDefinition m_puzzle;
    QVector<ChessPosition> m_positions;
    QVector<AppliedMove> m_moves;
    int m_puzzleStartViewIndex = 0;
    int m_viewIndex = 0;
    int m_selectedSquare = -1;
};

} // namespace parlawl::puzzle_runner
