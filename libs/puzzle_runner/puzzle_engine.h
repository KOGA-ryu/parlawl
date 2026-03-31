#pragma once

#include <QVector>

#include "chess_position.h"
#include "puzzle_types.h"

namespace parlawl::puzzle_runner {

struct SubmissionResult {
    bool accepted = false;
    QVector<AppliedMove> appliedMoves;
    SessionStatus status = SessionStatus::Ready;
    QString prompt;
};

class PuzzleEngine
{
public:
    void loadPuzzle(const PuzzleDefinition &puzzle);
    void reset();

    [[nodiscard]] const PuzzleDefinition &currentPuzzle() const { return m_puzzle; }
    [[nodiscard]] SessionStatus status() const { return m_status; }
    [[nodiscard]] QString prompt() const;
    [[nodiscard]] QString nextExpectedMove() const;
    [[nodiscard]] int solutionIndex() const { return m_solutionIndex; }
    [[nodiscard]] PieceColor userSide() const { return m_userSide; }

    SubmissionResult submitUserMove(const ChessPosition &position, const QString &moveUci);
    SubmissionResult revealSolution(const ChessPosition &position);

private:
    QString promptForStatus() const;

    PuzzleDefinition m_puzzle;
    SessionStatus m_status = SessionStatus::Ready;
    PieceColor m_userSide = PieceColor::White;
    int m_solutionIndex = 0;
};

} // namespace parlawl::puzzle_runner
