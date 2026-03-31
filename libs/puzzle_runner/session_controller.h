#pragma once

#include <algorithm>
#include <memory>

#include <QObject>

#include "puzzle_round.h"
#include "fixture_puzzle_source.h"
#include "game_state_store.h"
#include "puzzle_engine.h"
#include "review_engine_adapter.h"
#include "source_game.h"

namespace parlawl::puzzle_runner {

class SessionController : public QObject
{
    Q_OBJECT

public:
    explicit SessionController(QObject *parent = nullptr);

    bool initialize(QString *errorMessage);
    bool reloadPuzzles(QString *errorMessage = nullptr) { return initialize(errorMessage); }
    bool replacePuzzles(const QVector<PuzzleDefinition> &puzzles, QString *errorMessage = nullptr);
    bool appendPuzzles(const QVector<PuzzleDefinition> &puzzles, QString *errorMessage = nullptr);
    GameStateStore *gameStateStore() { return &m_gameStateStore; }
    const GameStateStore *gameStateStore() const { return &m_gameStateStore; }
    const PuzzleEngine &puzzleEngine() const { return m_puzzleEngine; }
    const SessionSettings &settings() const { return m_settings; }
    const ReviewEngineAdapter &reviewEngineAdapter() const { return m_reviewEngineAdapter; }
    int currentPuzzleIndex() const { return m_currentPuzzleIndex; }
    int currentPuzzleSlot() const { return m_currentPuzzleSlot; }
    int puzzleCount() const { return std::min(m_visiblePuzzleCount, static_cast<int>(m_filteredIndices.size())); }
    int remainingVisiblePuzzleCount() const { return std::max(0, puzzleCount() - (m_currentPuzzleSlot + 1)); }
    bool shouldFetchMorePuzzles() const;
    bool canGoToPreviousPuzzle() const { return m_currentPuzzleSlot > 0; }
    bool canGoToNextPuzzle() const { return m_currentPuzzleSlot >= 0 && m_currentPuzzleSlot < m_filteredIndices.size() - 1; }
    bool canSubmitMoves() const;
    bool canAnalyzeCurrentPuzzle() const;
    bool buildAnalysisInput(PuzzleRound *puzzleRound, SourceGame *sourceGame, QString *errorMessage) const;
    bool setCurrentPuzzleSourceGamePgn(const QString &pgnText, const QString &openingName, QString *errorMessage = nullptr);
    QString promptText() const { return currentPrompt(); }

public slots:
    void setAutoAdvance(bool autoAdvance);
    void setDifficulty(const QString &difficulty);
    void setQueueSize(int queueSize);
    void setRefillWhenLow(bool enabled);
    void setRefillThreshold(int threshold);
    void submitUserMove(const QString &moveUci);
    void requestHint();
    void revealSolution();
    void retryPuzzle();
    void stepBackward();
    void stepForward();
    void previousPuzzle();
    void nextPuzzle();

signals:
    void sessionChanged();
    void promptChanged(const QString &prompt);
    void hintAvailable(const QString &hint);
    void errorRaised(const QString &message);

private:
    void applyCurrentPuzzle(QString *errorMessage = nullptr);
    void rebuildFilteredPuzzleList();
    void resetVisiblePuzzleWindow();
    void maybeRefillVisiblePuzzleWindow(bool forceAtBoundary = false);
    QString currentPrompt() const;

    FixturePuzzleSource m_puzzleSource;
    GameStateStore m_gameStateStore;
    PuzzleEngine m_puzzleEngine;
    NullReviewEngineAdapter m_reviewEngineAdapter;
    QVector<PuzzleDefinition> m_puzzles;
    QVector<int> m_filteredIndices;
    SessionSettings m_settings;
    int m_currentPuzzleSlot = -1;
    int m_currentPuzzleIndex = -1;
    int m_visiblePuzzleCount = 0;
};

} // namespace parlawl::puzzle_runner
