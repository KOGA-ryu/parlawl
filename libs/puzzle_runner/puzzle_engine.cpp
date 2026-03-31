#include "puzzle_engine.h"

namespace parlawl::puzzle_runner {

void PuzzleEngine::loadPuzzle(const PuzzleDefinition &puzzle)
{
    m_puzzle = puzzle;
    reset();
}

void PuzzleEngine::reset()
{
    QString errorMessage;
    const auto position = ChessPosition::fromFen(m_puzzle.fenStart, &errorMessage);
    m_userSide = position.has_value() ? position->sideToMove() : PieceColor::White;
    m_solutionIndex = 0;
    m_status = SessionStatus::Active;
}

QString PuzzleEngine::prompt() const
{
    return promptForStatus();
}

QString PuzzleEngine::nextExpectedMove() const
{
    if (m_solutionIndex < 0 || m_solutionIndex >= m_puzzle.solutionMoves.size()) {
        return QString();
    }
    return m_puzzle.solutionMoves.at(m_solutionIndex);
}

SubmissionResult PuzzleEngine::submitUserMove(const ChessPosition &position, const QString &moveUci)
{
    SubmissionResult result;
    result.status = m_status;
    if (m_status != SessionStatus::Active) {
        result.prompt = promptForStatus();
        return result;
    }

    const QString normalizedMove = moveUci.trimmed().toLower();
    const auto move = Move::fromUci(normalizedMove);
    if (!move.has_value() || !position.isLegalMove(*move)) {
        result.prompt = QStringLiteral("select a legal move");
        return result;
    }

    result.accepted = true;
    result.appliedMoves.append({normalizedMove, true});

    const QString expectedMove = nextExpectedMove();
    if (normalizedMove != expectedMove) {
        m_status = SessionStatus::Failed;
        result.status = m_status;
        result.prompt = QStringLiteral("incorrect move; review the failed position or retry");
        return result;
    }

    ChessPosition simulation = position;
    simulation.applyMove(*move);
    ++m_solutionIndex;

    while (m_solutionIndex < m_puzzle.solutionMoves.size() && simulation.sideToMove() != m_userSide) {
        const QString replyMoveUci = m_puzzle.solutionMoves.at(m_solutionIndex);
        const auto replyMove = Move::fromUci(replyMoveUci);
        if (!replyMove.has_value() || !simulation.applyMove(*replyMove)) {
            m_status = SessionStatus::Failed;
            result.status = m_status;
            result.prompt = QStringLiteral("solution continuation could not be applied");
            return result;
        }
        result.appliedMoves.append({replyMoveUci, false});
        ++m_solutionIndex;
    }

    m_status = m_solutionIndex >= m_puzzle.solutionMoves.size() ? SessionStatus::Solved : SessionStatus::Active;
    result.status = m_status;
    result.prompt = promptForStatus();
    return result;
}

SubmissionResult PuzzleEngine::revealSolution(const ChessPosition &position)
{
    SubmissionResult result;
    result.accepted = true;
    result.status = SessionStatus::Solved;

    ChessPosition simulation = position;
    while (m_solutionIndex < m_puzzle.solutionMoves.size()) {
        const QString moveUci = m_puzzle.solutionMoves.at(m_solutionIndex);
        const auto move = Move::fromUci(moveUci);
        if (!move.has_value() || !simulation.applyMove(*move)) {
            m_status = SessionStatus::Failed;
            result.status = m_status;
            result.prompt = QStringLiteral("solution replay failed");
            return result;
        }
        result.appliedMoves.append({moveUci, simulation.sideToMove() != m_userSide});
        ++m_solutionIndex;
    }

    m_status = SessionStatus::Solved;
    result.prompt = QStringLiteral("solution replay complete");
    return result;
}

QString PuzzleEngine::promptForStatus() const
{
    switch (m_status) {
    case SessionStatus::Ready:
        return QStringLiteral("ready");
    case SessionStatus::Active:
        return QStringLiteral("your turn");
    case SessionStatus::Solved:
        return QStringLiteral("puzzle solved");
    case SessionStatus::Failed:
        return QStringLiteral("puzzle failed");
    }
    return QStringLiteral("ready");
}

} // namespace parlawl::puzzle_runner
