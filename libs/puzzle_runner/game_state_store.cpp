#include "game_state_store.h"

namespace parlawl::puzzle_runner {

GameStateStore::GameStateStore(QObject *parent)
    : QObject(parent)
{
}

void GameStateStore::loadPuzzle(const PuzzleDefinition &puzzle, const QVector<ChessPosition> &sourceHistory)
{
    m_puzzle = puzzle;
    m_positions = sourceHistory;
    if (m_positions.isEmpty()) {
        m_positions = {};
    }
    m_moves.clear();
    m_puzzleStartViewIndex = std::max(0, static_cast<int>(m_positions.size()) - 1);
    m_viewIndex = m_puzzleStartViewIndex;
    m_selectedSquare = -1;
    emitChanged();
}

void GameStateStore::applyMoveSequence(const QVector<AppliedMove> &moves)
{
    if (m_positions.isEmpty()) {
        return;
    }

    ChessPosition current = latestPosition();
    for (const AppliedMove &appliedMove : moves) {
        const auto move = Move::fromUci(appliedMove.uci);
        if (!move.has_value() || !current.applyMove(*move)) {
            break;
        }
        m_moves.append(appliedMove);
        m_positions.append(current);
    }
    m_viewIndex = m_positions.size() - 1;
    m_selectedSquare = -1;
    emitChanged();
}

void GameStateStore::resetToInitial()
{
    if (m_positions.isEmpty()) {
        return;
    }
    m_positions.resize(m_puzzleStartViewIndex + 1);
    m_moves.clear();
    m_viewIndex = m_puzzleStartViewIndex;
    m_selectedSquare = -1;
    emitChanged();
}

bool GameStateStore::stepBackward()
{
    if (m_viewIndex <= 0) {
        return false;
    }
    --m_viewIndex;
    m_selectedSquare = -1;
    emitChanged();
    return true;
}

bool GameStateStore::stepForward()
{
    if (m_viewIndex >= m_positions.size() - 1) {
        return false;
    }
    ++m_viewIndex;
    m_selectedSquare = -1;
    emitChanged();
    return true;
}

void GameStateStore::setSelectedSquare(int square)
{
    m_selectedSquare = square;
    emitChanged();
}

void GameStateStore::clearSelectedSquare()
{
    if (m_selectedSquare < 0) {
        return;
    }
    m_selectedSquare = -1;
    emitChanged();
}

const ChessPosition &GameStateStore::currentPosition() const
{
    const int maxIndex = static_cast<int>(m_positions.size()) - 1;
    return m_positions[std::clamp(m_viewIndex, 0, maxIndex)];
}

const ChessPosition &GameStateStore::latestPosition() const
{
    return m_positions.last();
}

bool GameStateStore::isViewingLatest() const
{
    return m_viewIndex == m_positions.size() - 1;
}

QVector<Move> GameStateStore::legalMoves() const
{
    if (m_positions.isEmpty()) {
        return {};
    }
    return currentPosition().legalMoves();
}

QVector<Move> GameStateStore::legalMovesFromSquare(int square) const
{
    QVector<Move> result;
    const QVector<Move> candidates = legalMoves();
    for (const Move &move : candidates) {
        if (move.from == square) {
            result.append(move);
        }
    }
    return result;
}

QVector<Move> GameStateStore::legalMovesBetween(int fromSquare, int toSquare) const
{
    QVector<Move> result;
    const QVector<Move> candidates = legalMovesFromSquare(fromSquare);
    for (const Move &move : candidates) {
        if (move.to == toSquare) {
            result.append(move);
        }
    }
    return result;
}

void GameStateStore::emitChanged()
{
    emit stateChanged();
}

} // namespace parlawl::puzzle_runner
