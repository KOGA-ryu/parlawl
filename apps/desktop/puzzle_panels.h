#pragma once

#include <QGroupBox>
#include <QWidget>

#include "puzzle_info_summary_builder.h"
#include "puzzle_types.h"
#include "annotated_replay_pack.h"
#include "replay_session.h"

class QLabel;
class QTableWidget;
class QPushButton;
class QCheckBox;
class QComboBox;
class QTextEdit;

namespace parlawl::puzzle_runner {
class ReviewEngineAdapter;
}

class MoveListPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit MoveListPanel(QWidget *parent = nullptr);
    void setMoves(
        const parlawl::puzzle_runner::PuzzleDefinition &puzzle,
        const QVector<parlawl::puzzle_runner::AppliedMove> &moves,
        int currentViewIndex);
    void setAnnotatedReplay(
        const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
        int currentMainlinePly,
        bool variationActive,
        int variationAnchorPly);
    [[nodiscard]] QString truthStatusText() const;

signals:
    void replayPlyRequested(int ply);

private:
    QLabel *m_truthStatusLabel;
    QTableWidget *m_table;
    bool m_showingAnnotatedReplay = false;
    int m_replayMoveCount = 0;
};

class ReplayEvidencePanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit ReplayEvidencePanel(QWidget *parent = nullptr);
    void setEmptyState();
    void setReplayState(
        const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
        const parlawl::puzzle_runner::ReplaySession &session,
        int variationAnchorPly);
    [[nodiscard]] QString summaryText() const;
    [[nodiscard]] bool canShowEngineLine() const;
    [[nodiscard]] bool canReturnToGame() const;

signals:
    void openReplayRequested();
    void backToPuzzlesRequested();
    void showEngineLineRequested();
    void returnToGameRequested();

private:
    QLabel *m_gameLabel;
    QLabel *m_openingLabel;
    QLabel *m_engineLabel;
    QTextEdit *m_summaryView;
    QPushButton *m_openButton;
    QPushButton *m_backButton;
    QPushButton *m_showEngineLineButton;
    QPushButton *m_returnToGameButton;
};

class GameReviewPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GameReviewPanel(QWidget *parent = nullptr);
    void setReplayState(
        const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
        const parlawl::puzzle_runner::ReplaySession &session,
        int variationAnchorPly);
    void setEmptyState();

signals:
    void replayPlyRequested(int ply);
    void backToPlayerStatisticsRequested();

private:
    MoveListPanel *m_moveListPanel;
    ReplayEvidencePanel *m_evidencePanel;
};

class MetadataCard : public QGroupBox
{
    Q_OBJECT

public:
    explicit MetadataCard(QWidget *parent = nullptr);
    void setPuzzle(const parlawl::puzzle_runner::PuzzleDefinition &puzzle, int currentIndex, int puzzleCount, const QString &status);
    void setAnalysisSummary(const PuzzleInfoSummary &summary);
    void setAwaitingAnalysis(const parlawl::puzzle_runner::PuzzleDefinition &puzzle, int currentIndex, int puzzleCount);

private:
    QLabel *m_titleLabel;
    QLabel *m_availabilityLabel;
    QLabel *m_warningLabel;
    QLabel *m_openingLabel;
    QLabel *m_strategicErrorLabel;
    QLabel *m_planLabel;
    QLabel *m_criticalMistakeLabel;
    QLabel *m_lastPracticalMistakeLabel;
    QLabel *m_tacticalThemeLabel;
    QPushButton *m_rawEvidenceToggle;
    QLabel *m_rawEvidenceLabel;
};

class SettingsCard : public QGroupBox
{
    Q_OBJECT

public:
    explicit SettingsCard(QWidget *parent = nullptr);
    void setSettings(bool autoAdvance, const QString &difficulty);
    void setSupplySettings(const QString &queueSize, bool refillWhenLow, const QString &refillThreshold);
    void setRetentionSettings(const QString &keepRecentRuns, bool preserveAnalyzed);
    void setAvailablePuzzleCount(int availableCount);
    void setSupplyStatusText(const QString &statusText);
    [[nodiscard]] QString supplyStatusText() const;

signals:
    void autoAdvanceChanged(bool autoAdvance);
    void difficultyChanged(const QString &difficulty);
    void queueSizeChanged(const QString &queueSize);
    void refillWhenLowChanged(bool enabled);
    void refillThresholdChanged(const QString &threshold);
    void keepRecentRunsChanged(const QString &keepRecentRuns);
    void preserveAnalyzedChanged(bool enabled);
    void cleanupRequested();
    void reloadPuzzlesRequested();
    void openValidatedPuzzlePackRequested();
    void exportSolveHistoryRequested();

private:
    QCheckBox *m_autoAdvanceCheck;
    QComboBox *m_difficultyCombo;
    QComboBox *m_queueSizeCombo;
    QCheckBox *m_refillWhenLowCheck;
    QComboBox *m_refillThresholdCombo;
    QLabel *m_availableToSolveLabel;
    QLabel *m_supplyStatusLabel;
    QPushButton *m_reloadPuzzlesButton;
    QPushButton *m_openValidatedPuzzlePackButton;
    QPushButton *m_exportSolveHistoryButton;
    QComboBox *m_keepRecentRunsCombo;
    QCheckBox *m_preserveAnalyzedCheck;
    QPushButton *m_cleanupButton;
};

class TransportControls : public QWidget
{
    Q_OBJECT

public:
    explicit TransportControls(QWidget *parent = nullptr);
    void setEnabledState(
        bool reviewMode,
        bool canStepBackward,
        bool canStepForward,
        bool canGoToPreviousPuzzle,
        bool canGoToNextPuzzle);
    void setReplayMode(bool enabled);

signals:
    void previousRequested();
    void nextRequested();
    void retryRequested();

private:
    QPushButton *m_previousButton;
    QPushButton *m_nextButton;
    QPushButton *m_retryButton;
};

class EnginePanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit EnginePanel(QWidget *parent = nullptr);
    void setReviewState(
        const QString &statusText,
        const QString &evaluationText,
        const QString &bestMoveText,
        const QString &pvText,
        bool canRefresh,
        bool autoRefresh,
        bool reviewInProgress);

signals:
    void refreshRequested();
    void autoRefreshChanged(bool enabled);

private:
    QLabel *m_statusLabel;
    QLabel *m_evaluationLabel;
    QLabel *m_bestMoveLabel;
    QLabel *m_pvLabel;
    QPushButton *m_refreshButton;
    QCheckBox *m_autoRefreshCheck;
};
