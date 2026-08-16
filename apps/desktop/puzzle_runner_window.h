#pragma once

#include <QCloseEvent>
#include <QDateTime>
#include <QMainWindow>
#include <QPair>

#include <optional>

#include "analysis_run.h"
#include "analysis_repository.h"
#include "puzzle_round.h"
#include "session_controller.h"
#include "source_game.h"
#include "annotated_replay_pack.h"
#include "replay_session.h"

class AnalysisOrchestrator;
class BoardWidget;
class DatabaseManager;
class EvaluationBarWidget;
class EnginePanel;
class PuzzleSupplyCoordinator;
class SourceGamePgnCache;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class MetadataCard;
class MoveListPanel;
class ReplayEvidencePanel;
class QPushButton;
class QTabWidget;
class QTextEdit;
class QThread;
class StockfishReviewController;
class TransportControls;
class SettingsCard;

class PuzzleRunnerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PuzzleRunnerWindow(QWidget *parent = nullptr);
    ~PuzzleRunnerWindow() override;
    bool buildAnalysisInput(PuzzleRound *puzzleRound, SourceGame *sourceGame, QString *errorMessage) const;

signals:
    void analyzeCurrentPuzzleRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshUi();
    void onBoardSquareClicked(int square);
    void onAnalyzeCurrentPuzzleRequested();
    void onCancelAnalysisRequested();
    void onHintRequested();
    void onSolutionRequested();
    void onExportJsonRequested();
    void onExportAssistantPacketRequested();
    void onImportAssistantInferenceRequested();
    void onControllerHint(const QString &hint);
    void onControllerError(const QString &message);
    void appendLogMessage(const QString &message);
    void onAnalysisStarted();
    void onAnalysisProgress(const QString &phase, const QString &message);
    void onAnalysisCompleted(const QString &runId, const QString &summaryText);
    void onAnalysisFailed(const QString &stage, const QString &message);
    void onRecentRunSelected(QListWidgetItem *current, QListWidgetItem *previous);
    void persistSettings();
    void onEngineReviewUpdated();
    void onEngineRefreshRequested();
    void onEngineAutoRefreshChanged(bool enabled);
    void onCleanupRequested();
    void onReloadPuzzlesRequested();
    void onOpenValidatedPuzzlePackRequested();
    void onOpenAnnotatedReplayRequested();
    void onBackToPuzzlesRequested();
    void onShowReplayVariationRequested();
    void onReturnFromReplayVariationRequested();
    void onReplayPlyRequested(int ply);

private:
    void buildUi();
    void loadSettings();
    void loadDatabase();
    QString defaultDatabasePath() const;
    bool ensureDatabaseReady();
    bool validateAnalyzeSettings(bool requireLichessToken, QString *message) const;
    void setAnalysisInProgress(bool inProgress);
    void refreshRecentRuns(const QString &preferredRunId = QString());
    void showRunSummary(const AnalysisRun &run);
    void updateBoard();
    void updatePanels();
    void updateReplayBoard();
    void updateReplayPanels();
    void refreshReplayUi();
    void setAnnotatedReplayWorkspaceUi(bool enabled);
    bool loadAnnotatedReplayFile(const QString &path, QString *errorMessage = nullptr);
    bool loadValidatedPuzzlePackFile(const QString &path, QString *errorMessage = nullptr);
    void clearSelectionIfInvalid();
    void maybeRefreshEngineReview(bool forceRefresh = false);
    void resetPuzzleScopedUiState(const QString &puzzleId);
    bool ensureCurrentPuzzleSourceHistory(QString *errorMessage = nullptr);
    bool reloadPuzzleSupply(bool append, QString *errorMessage = nullptr);
    bool restoreCachedPuzzleSupply(QString *errorMessage = nullptr);
    void maybeTopUpPuzzleSupply();
    bool usingLivePuzzleSupply() const;
    QString currentSupplyStatusText() const;
    void refreshSupplyStatus();

    DatabaseManager *m_databaseManager;
    AnalysisOrchestrator *m_orchestrator;
    QThread *m_orchestratorThread;
    parlawl::puzzle_runner::SessionController m_sessionController;
    StockfishReviewController *m_stockfishReviewController;
    PuzzleSupplyCoordinator *m_puzzleSupplyCoordinator;
    SourceGamePgnCache *m_sourceGamePgnCache;

    enum class WorkspaceMode {
        Puzzle,
        AnnotatedReplay,
    };
    WorkspaceMode m_workspaceMode = WorkspaceMode::Puzzle;
    std::optional<parlawl::puzzle_runner::AnnotatedReplayPack> m_annotatedReplayPack;
    parlawl::puzzle_runner::ReplaySession m_replaySession;
    int m_replayVariationAnchorPly = 0;
    int m_preReplayInfoTabIndex = 0;
    bool m_replayWorkspaceUiActive = false;

    QLineEdit *m_lichessTokenEdit;
    QLineEdit *m_stockfishPathEdit;
    QLineEdit *m_pythonWorkerPathEdit;
    QLineEdit *m_databasePathEdit;
    EvaluationBarWidget *m_evaluationBarWidget;
    BoardWidget *m_boardWidget;
    MoveListPanel *m_moveListPanel;
    ReplayEvidencePanel *m_replayEvidencePanel;
    QTabWidget *m_rightTabs;
    QTabWidget *m_infoTabs;
    QWidget *m_settingsPage;
    MetadataCard *m_metadataCard;
    SettingsCard *m_settingsCard;
    TransportControls *m_transportControls;
    EnginePanel *m_enginePanel;
    QTextEdit *m_reportView;
    QTextEdit *m_logView;
    QListWidget *m_recentRunsList;
    QLabel *m_statusStateLabel;
    QLabel *m_statusDetailLabel;
    QPushButton *m_cancelButton;
    QPushButton *m_hintButton;
    QPushButton *m_solutionButton;
    QPushButton *m_analyzeButton;
    QPushButton *m_exportButton;
    QPushButton *m_exportAssistantPacketButton;
    QPushButton *m_importAssistantInferenceButton;
    bool m_analysisInProgress;
    bool m_closeRequested;
    QString m_currentPhase;
    QString m_selectedRunId;
    QString m_activePuzzleId;
    QString m_lastReviewedFen;
    QPair<int, int> m_lastMoveSquares;
    bool m_hasLoadedReport;
    PersistedAnalysisReport m_loadedReport;
    QString m_queueSizeSetting;
    bool m_refillWhenLowSetting;
    QString m_refillThresholdSetting;
    QString m_keepRecentRunsSetting;
    bool m_preserveAnalyzedSetting;
    bool m_liveSupplyReloadInProgress = false;
    bool m_liveSupplyActive = false;
    bool m_validatedPuzzlePackActive = false;
    int m_validatedPuzzlePackCount = 0;
    int m_lastSupplyCheckSlot = -1;
    bool m_sourceHistoryHydrationInProgress = false;
};
