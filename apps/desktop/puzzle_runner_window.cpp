#include "puzzle_runner_window.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPushButton>
#include <QScrollArea>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QSettings>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

#include "analysis_orchestrator.h"
#include "analysis_repository.h"
#include "board_widget.h"
#include "database_manager.h"
#include "evaluation_bar_widget.h"
#include "engine_validated_puzzle_pack.h"
#include "game_study_window.h"
#include "lichess_client.h"
#include "parlawl_config.h"
#include "player_statistics_panel.h"
#include "puzzle_supply_coordinator.h"
#include "puzzle_info_summary_builder.h"
#include "puzzle_attempt_repository.h"
#include "puzzle_panels.h"
#include "pgn_utils.h"
#include "report_formatter.h"
#include "source_game_pgn_cache.h"
#include "stockfish_review_controller.h"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace parlawl::puzzle_runner;

namespace {

QString timestamped(const QString &message)
{
    const auto nowUtc = QDateTime::currentDateTimeUtc();
    return QStringLiteral("%1  %2").arg(nowUtc.toString(Qt::ISODateWithMs), message);
}

QString newOpaqueUuid(const QString &prefix)
{
    return prefix + QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

bool isExactOpaqueUuid(const QString &value, const QString &prefix)
{
    if (!value.startsWith(prefix) || value.size() != prefix.size() + 36
        || value != value.toLower()) {
        return false;
    }
    const QString suffix = value.mid(prefix.size());
    const QUuid uuid = QUuid::fromString(suffix);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces).toLower() == suffix;
}

bool readDirectRegularFile(
    const QString &path,
    qsizetype maximumBytes,
    const QString &fileLabel,
    QByteArray *bytes,
    QString *errorMessage)
{
    if (bytes == nullptr || !QDir::isAbsolutePath(path)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 must use an absolute direct file path").arg(fileLabel);
        }
        return false;
    }

    const QByteArray encodedPath = QFile::encodeName(path);
    int descriptor = -1;
    do {
        descriptor = ::open(
            encodedPath.constData(),
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 could not be opened as a direct regular file").arg(fileLabel);
        }
        return false;
    }

    struct stat beforeStatus {};
    if (::fstat(descriptor, &beforeStatus) != 0 || !S_ISREG(beforeStatus.st_mode)
        || beforeStatus.st_size <= 0
        || beforeStatus.st_size > static_cast<off_t>(maximumBytes)) {
        ::close(descriptor);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 must be a non-empty direct regular file within its size limit").arg(fileLabel);
        }
        return false;
    }

    QFile file;
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 could not be read").arg(fileLabel);
        }
        return false;
    }
    const QByteArray payload = file.read(maximumBytes + 1);
    struct stat afterStatus {};
    const bool stable = ::fstat(file.handle(), &afterStatus) == 0
        && S_ISREG(afterStatus.st_mode)
        && afterStatus.st_dev == beforeStatus.st_dev
        && afterStatus.st_ino == beforeStatus.st_ino
        && afterStatus.st_size == beforeStatus.st_size
        && payload.size() == beforeStatus.st_size
        && file.atEnd();
    file.close();
    if (!stable) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 changed while it was being read").arg(fileLabel);
        }
        return false;
    }
    *bytes = payload;
    return true;
}

QString assistantStatusOrDefault(const TacticalEvent &event)
{
    return event.assistantInferenceStatus.isEmpty() ? QStringLiteral("pending") : event.assistantInferenceStatus;
}

QString readDotEnvValue(const QString &path, const QString &key)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const qsizetype separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }
        if (line.left(separator).trimmed() != key) {
            continue;
        }
        QString value = line.mid(separator + 1).trimmed();
        if ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')))) {
            value = value.mid(1, value.size() - 2);
        }
        return value;
    }

    return QString();
}

QString resolveLichessToken()
{
    const QString environmentToken = qEnvironmentVariable("LICHESS_API_TOKEN").trimmed();
    if (!environmentToken.isEmpty()) {
        return environmentToken;
    }

    return readDotEnvValue(QDir::current().absoluteFilePath(QStringLiteral(".env")), QStringLiteral("LICHESS_API_TOKEN")).trimmed();
}

QString discoverExecutable(const QString &name, const QString &fallback = QString())
{
    const QString discovered = QStandardPaths::findExecutable(name).trimmed();
    return discovered.isEmpty() ? fallback : discovered;
}

QString isoOrEmpty(const QDateTime &value)
{
    return value.isValid() ? value.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QString secondsRemainingText(const QDateTime &untilUtc)
{
    const qint64 secondsRemaining = std::max<qint64>(1, QDateTime::currentDateTimeUtc().secsTo(untilUtc));
    return QString::number(secondsRemaining);
}

QJsonValue jsonOrString(const QString &raw)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(raw.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError) {
        if (document.isObject()) {
            return document.object();
        }
        if (document.isArray()) {
            return document.array();
        }
    }
    return raw;
}

QJsonObject exportAnalysisRunJson(const AnalysisRun &run)
{
    return {
        {QStringLiteral("run_id"), run.runId},
        {QStringLiteral("puzzle_id"), run.puzzleId},
        {QStringLiteral("status"), run.status},
        {QStringLiteral("engine_mode"), run.engineMode},
        {QStringLiteral("engine_name"), run.engineName},
        {QStringLiteral("engine_depth"), run.engineDepth},
        {QStringLiteral("created_at_utc"), isoOrEmpty(run.createdAtUtc)},
        {QStringLiteral("completed_at_utc"), isoOrEmpty(run.completedAtUtc)},
        {QStringLiteral("error_message"), run.errorMessage},
    };
}

QJsonObject exportPuzzleRoundJson(const PuzzleRound &puzzleRound)
{
    return {
        {QStringLiteral("puzzle_id"), puzzleRound.puzzleId},
        {QStringLiteral("puzzle_rating"), puzzleRound.puzzleRating},
        {QStringLiteral("time_control"), puzzleRound.timeControl},
        {QStringLiteral("white_player"), puzzleRound.whitePlayer},
        {QStringLiteral("white_rating"), puzzleRound.whiteRating},
        {QStringLiteral("black_player"), puzzleRound.blackPlayer},
        {QStringLiteral("black_rating"), puzzleRound.blackRating},
        {QStringLiteral("side_to_move"), puzzleRound.sideToMove},
        {QStringLiteral("fetched_at_utc"), isoOrEmpty(puzzleRound.fetchedAtUtc)},
        {QStringLiteral("source_game_id"), puzzleRound.sourceGameId},
        {QStringLiteral("initial_fen"), puzzleRound.initialFen},
        {QStringLiteral("last_move"), puzzleRound.lastMove},
        {QStringLiteral("solved"), puzzleRound.solved},
        {QStringLiteral("themes"), jsonOrString(puzzleRound.themesJson)},
        {QStringLiteral("solution_moves"), jsonOrString(puzzleRound.solutionMovesJson)},
        {QStringLiteral("raw_puzzle"), jsonOrString(puzzleRound.rawPuzzleJson)},
        {QStringLiteral("raw_activity"), jsonOrString(puzzleRound.rawActivityJson)},
    };
}

QJsonObject exportSourceGameJson(const SourceGame &sourceGame)
{
    return {
        {QStringLiteral("source_game_id"), sourceGame.sourceGameId},
        {QStringLiteral("opening_name"), sourceGame.openingName},
        {QStringLiteral("fetched_at_utc"), isoOrEmpty(sourceGame.fetchedAtUtc)},
        {QStringLiteral("pgn_text"), sourceGame.pgnText},
    };
}

QJsonObject exportTacticalEventJson(const TacticalEvent &event)
{
    return {
        {QStringLiteral("event_id"), event.eventId},
        {QStringLiteral("run_id"), event.runId},
        {QStringLiteral("puzzle_id"), event.puzzleId},
        {QStringLiteral("source_game_id"), event.sourceGameId},
        {QStringLiteral("opening_family"), event.openingFamily},
        {QStringLiteral("game_phase"), event.gamePhase},
        {QStringLiteral("material_balance"), event.materialBalance},
        {QStringLiteral("king_safety_state"), event.kingSafetyState},
        {QStringLiteral("piece_activity"), event.pieceActivity},
        {QStringLiteral("key_weakness"), event.keyWeakness},
        {QStringLiteral("mapping_status"), event.mappingStatus},
        {QStringLiteral("mapping_method"), event.mappingMethod},
        {QStringLiteral("mapping_confidence"), event.mappingConfidence},
        {QStringLiteral("analysis_window_start_ply"), event.analysisWindowStartPly},
        {QStringLiteral("analysis_window_end_ply"), event.analysisWindowEndPly},
        {QStringLiteral("primary_break_ply"), event.primaryBreakPly},
        {QStringLiteral("primary_break_reason"), event.primaryBreakReason},
        {QStringLiteral("retained_break_summary"), event.retainedBreakSummary},
        {QStringLiteral("divergence_type"), event.divergenceType},
        {QStringLiteral("divergence_severity"), event.divergenceSeverity},
        {QStringLiteral("divergence_compact_summary"), event.divergenceCompactSummary},
        {QStringLiteral("collapse_sequence_summary"), event.collapseSequenceSummary},
        {QStringLiteral("structural_feature_summary"), event.structuralFeatureSummary},
        {QStringLiteral("structural_feature_confidence"), event.structuralFeatureConfidence},
        {QStringLiteral("local_target_summary"), event.localTargetSummary},
        {QStringLiteral("local_target_confidence"), event.localTargetConfidence},
        {QStringLiteral("structural_v2_summary"), event.structuralV2Summary},
        {QStringLiteral("structural_v2_confidence"), event.structuralV2Confidence},
        {QStringLiteral("structural_v3_summary"), event.structuralV3Summary},
        {QStringLiteral("structural_v3_confidence"), event.structuralV3Confidence},
        {QStringLiteral("structural_v4_summary"), event.structuralV4Summary},
        {QStringLiteral("structural_v4_confidence"), event.structuralV4Confidence},
        {QStringLiteral("tactical_candidates"), jsonOrString(event.tacticalCandidatesJson)},
        {QStringLiteral("structural_candidates"), jsonOrString(event.structuralCandidatesJson)},
        {QStringLiteral("evidence_payload"), jsonOrString(event.evidencePayloadJson)},
        {QStringLiteral("assistant_inference_status"), assistantStatusOrDefault(event)},
        {QStringLiteral("assistant_labels"), jsonOrString(event.assistantLabelsJson)},
        {QStringLiteral("assistant_summary_markdown"), event.assistantSummaryMarkdown},
    };
}

QJsonObject exportCriticalMoveJson(const CriticalMove &move)
{
    return {
        {QStringLiteral("critical_move_id"), move.criticalMoveId},
        {QStringLiteral("event_id"), move.eventId},
        {QStringLiteral("role"), move.role},
        {QStringLiteral("ply"), move.ply},
        {QStringLiteral("side"), move.side},
        {QStringLiteral("played_move"), move.playedMove},
        {QStringLiteral("best_move"), move.bestMove},
        {QStringLiteral("critical_reason_type"), move.criticalReasonType},
        {QStringLiteral("critical_reason_severity"), move.criticalReasonSeverity},
        {QStringLiteral("critical_reason_compact_summary"), move.criticalReasonCompactSummary},
        {QStringLiteral("candidate_ranking_type"), move.candidateRankingType},
        {QStringLiteral("candidate_ranking_severity"), move.candidateRankingSeverity},
        {QStringLiteral("candidate_ranking_compact_summary"), move.candidateRankingCompactSummary},
        {QStringLiteral("evidence_note_type"), move.evidenceNoteType},
        {QStringLiteral("evidence_note_severity"), move.evidenceNoteSeverity},
        {QStringLiteral("evidence_note_compact"), move.evidenceNoteCompact},
        {QStringLiteral("best_continuation_compact"), move.bestContinuationCompact},
        {QStringLiteral("played_continuation_compact"), move.playedContinuationCompact},
        {QStringLiteral("structural_link_summary"), move.structuralLinkSummary},
        {QStringLiteral("candidate_moves"), jsonOrString(move.candidateMovesJson)},
    };
}

QJsonDocument exportPersistedReportJson(const PersistedAnalysisReport &report, const QString &reportText)
{
    QJsonArray criticalMoves;
    for (const CriticalMove &move : report.criticalMoves) {
        criticalMoves.append(exportCriticalMoveJson(move));
    }

    return QJsonDocument(QJsonObject{
        {QStringLiteral("export_type"), QStringLiteral("parlawl_saved_report_v0_1")},
        {QStringLiteral("analysis_run"), exportAnalysisRunJson(report.analysisRun)},
        {QStringLiteral("puzzle_round"), exportPuzzleRoundJson(report.puzzleRound)},
        {QStringLiteral("source_game"), exportSourceGameJson(report.sourceGame)},
        {QStringLiteral("tactical_event"), exportTacticalEventJson(report.tacticalEvent)},
        {QStringLiteral("critical_moves"), criticalMoves},
        {QStringLiteral("report_markdown"), reportText},
    });
}

QString exportAssistantPacketMarkdown(const PersistedAnalysisReport &report, const QString &reportText)
{
    const QByteArray payloadJson = exportPersistedReportJson(report, reportText).toJson(QJsonDocument::Indented);
    return QStringLiteral(
               "# parlawl assistant packet\n\n"
               "run id: `%1`\n"
               "puzzle id: `%2`\n"
               "source game id: `%3`\n\n"
               "This packet is exported from Parlawl. Objective evidence remains authoritative. "
               "Assistant inference must stay external and should not rewrite the evidence fields.\n\n"
               "## copyable prompt\n\n"
               "Paste the prompt below into ChatGPT together with this file. Ask ChatGPT to return a downloadable JSON file only.\n\n"
               "```text\n"
               "Read the Parlawl assistant packet. Treat the extracted evidence as authoritative and keep assistant inference external. "
               "Do not rewrite or reinterpret objective evidence fields. Return one downloadable JSON file only, using the exact response "
               "artifact shape shown below. Keep `run_id` and `puzzle_id` unchanged. Put compact labels in `assistant_labels` and put the "
               "full explanation in `assistant_summary_markdown`.\n"
               "```\n\n"
               "## expected assistant response artifact\n\n"
               "Return a downloadable JSON file with this shape:\n\n"
               "```json\n"
               "{\n"
               "  \"artifact_type\": \"parlawl_assistant_inference_v0_1\",\n"
               "  \"run_id\": \"%1\",\n"
               "  \"puzzle_id\": \"%2\",\n"
               "  \"assistant_inference_status\": \"provided\",\n"
               "  \"assistant_labels\": [\"label_a\", \"label_b\"],\n"
               "  \"assistant_summary_markdown\": \"# external inference\\n...\"\n"
               "}\n"
               "```\n\n"
               "## rendered evidence report\n\n"
               "```text\n%4\n```\n\n"
               "## structured saved packet\n\n"
               "```json\n%5```\n")
        .arg(report.analysisRun.runId,
             report.puzzleRound.puzzleId,
             report.sourceGame.sourceGameId,
             reportText,
             QString::fromUtf8(payloadJson));
}

bool parseAssistantInferenceArtifact(
    const QByteArray &raw,
    QString *runId,
    QString *puzzleId,
    QString *status,
    QString *labelsJson,
    QString *summaryMarkdown,
    QString *errorMessage
)
{
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        const QString text = QString::fromUtf8(raw);
        const int fenceStart = text.indexOf(QStringLiteral("```json"));
        if (fenceStart >= 0) {
            const int jsonStart = text.indexOf(QLatin1Char('{'), fenceStart);
            const int fenceEnd = text.indexOf(QStringLiteral("```"), jsonStart);
            if (jsonStart >= 0 && fenceEnd > jsonStart) {
                document = QJsonDocument::fromJson(text.mid(jsonStart, fenceEnd - jsonStart).toUtf8(), &parseError);
            }
        }
    }

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("assistant inference artifact must be a JSON object or markdown containing a fenced JSON object");
        }
        return false;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("artifact_type")).toString().trimmed() != QStringLiteral("parlawl_assistant_inference_v0_1")) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("assistant inference artifact_type must be parlawl_assistant_inference_v0_1");
        }
        return false;
    }

    const QString parsedRunId = object.value(QStringLiteral("run_id")).toString().trimmed();
    const QString parsedPuzzleId = object.value(QStringLiteral("puzzle_id")).toString().trimmed();
    if (parsedRunId.isEmpty() || parsedPuzzleId.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("assistant inference artifact must include non-empty run_id and puzzle_id");
        }
        return false;
    }

    const QJsonValue labelsValue = object.value(QStringLiteral("assistant_labels"));
    if (!labelsValue.isArray()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("assistant inference artifact assistant_labels must be an array");
        }
        return false;
    }

    const QString parsedLabels = QString::fromUtf8(QJsonDocument(labelsValue.toArray()).toJson(QJsonDocument::Compact));
    const QString parsedSummary = object.value(QStringLiteral("assistant_summary_markdown")).toString();
    if (parsedSummary.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("assistant inference artifact assistant_summary_markdown must be non-empty");
        }
        return false;
    }

    if (runId != nullptr) {
        *runId = parsedRunId;
    }
    if (puzzleId != nullptr) {
        *puzzleId = parsedPuzzleId;
    }
    if (status != nullptr) {
        const QString parsedStatus = object.value(QStringLiteral("assistant_inference_status")).toString().trimmed();
        *status = parsedStatus.isEmpty() ? QStringLiteral("provided") : parsedStatus;
    }
    if (labelsJson != nullptr) {
        *labelsJson = parsedLabels;
    }
    if (summaryMarkdown != nullptr) {
        *summaryMarkdown = parsedSummary;
    }
    return true;
}

} // namespace

PuzzleRunnerWindow::PuzzleRunnerWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_databaseManager(new DatabaseManager(this))
    , m_puzzleAttemptRepository()
    , m_attemptRepositoryDatabasePath()
    , m_attemptSolverId()
    , m_attemptSessionId(newOpaqueUuid(QStringLiteral("parlawl-session-v1:")))
    , m_orchestrator(new AnalysisOrchestrator())
    , m_orchestratorThread(new QThread(this))
    , m_replayPlaybackTimer(new QTimer(this))
    , m_sessionController(this)
    , m_stockfishReviewController(new StockfishReviewController(this))
    , m_puzzleSupplyCoordinator(new PuzzleSupplyCoordinator(this))
    , m_sourceGamePgnCache(new SourceGamePgnCache())
    , m_lichessTokenEdit(nullptr)
    , m_stockfishPathEdit(nullptr)
    , m_pythonWorkerPathEdit(nullptr)
    , m_databasePathEdit(nullptr)
    , m_evaluationBarWidget(nullptr)
    , m_boardWidget(nullptr)
    , m_moveListPanel(nullptr)
    , m_playerStatisticsPanel(nullptr)
    , m_replayEvidencePanel(nullptr)
    , m_gameReviewPanel(nullptr)
    , m_gameReviewHubWindow(nullptr)
    , m_rightTabs(nullptr)
    , m_infoTabs(nullptr)
    , m_settingsPage(nullptr)
    , m_metadataCard(nullptr)
    , m_settingsCard(nullptr)
    , m_transportControls(nullptr)
    , m_enginePanel(nullptr)
    , m_reportView(nullptr)
    , m_logView(nullptr)
    , m_recentRunsList(nullptr)
    , m_statusStateLabel(nullptr)
    , m_statusDetailLabel(nullptr)
    , m_cancelButton(nullptr)
    , m_hintButton(nullptr)
    , m_solutionButton(nullptr)
    , m_analyzeButton(nullptr)
    , m_exportButton(nullptr)
    , m_exportAssistantPacketButton(nullptr)
    , m_importAssistantInferenceButton(nullptr)
    , m_analysisInProgress(false)
    , m_closeRequested(false)
    , m_currentPhase()
    , m_selectedRunId()
    , m_activePuzzleId()
    , m_lastReviewedFen()
    , m_lastMoveSquares{-1, -1}
    , m_hasLoadedReport(false)
    , m_loadedReport()
    , m_queueSizeSetting(QStringLiteral("10"))
    , m_refillWhenLowSetting(true)
    , m_refillThresholdSetting(QStringLiteral("2"))
    , m_keepRecentRunsSetting(QStringLiteral("50"))
    , m_preserveAnalyzedSetting(true)
{
    buildUi();
    m_gameReviewHubWindow = new GameReviewHubWindow(this);
    connect(
        m_gameReviewHubWindow,
        &GameReviewHubWindow::playerExplorerRequested,
        this,
        [this]() {
            m_gameReviewHubWindow->hideWorkspace();
            showNormal();
            raise();
            activateWindow();
            if (m_rightTabs != nullptr && m_playerStatisticsPanel != nullptr) {
                m_rightTabs->setCurrentWidget(m_playerStatisticsPanel);
            }
        });
    loadSettings();
    loadDatabase();
    m_sessionController.setQueueSize(m_queueSizeSetting.toInt());
    m_sessionController.setRefillWhenLow(m_refillWhenLowSetting);
    m_sessionController.setRefillThreshold(m_refillThresholdSetting.toInt());

    m_orchestrator->moveToThread(m_orchestratorThread);
    connect(m_orchestratorThread, &QThread::finished, m_orchestrator, &QObject::deleteLater);
    m_orchestratorThread->start();

    connect(&m_sessionController, &SessionController::sessionChanged, this, &PuzzleRunnerWindow::refreshUi);
    connect(&m_sessionController, &SessionController::hintAvailable, this, &PuzzleRunnerWindow::onControllerHint);
    connect(&m_sessionController, &SessionController::errorRaised, this, &PuzzleRunnerWindow::onControllerError);
    connect(m_stockfishReviewController, &StockfishReviewController::reviewUpdated, this, &PuzzleRunnerWindow::onEngineReviewUpdated);
    connect(m_orchestrator, &AnalysisOrchestrator::logMessage, this, &PuzzleRunnerWindow::appendLogMessage);
    connect(m_orchestrator, &AnalysisOrchestrator::analysisStarted, this, &PuzzleRunnerWindow::onAnalysisStarted);
    connect(m_orchestrator, &AnalysisOrchestrator::analysisProgress, this, &PuzzleRunnerWindow::onAnalysisProgress);
    connect(m_orchestrator, &AnalysisOrchestrator::analysisCompleted, this, &PuzzleRunnerWindow::onAnalysisCompleted);
    connect(m_orchestrator, &AnalysisOrchestrator::analysisFailed, this, &PuzzleRunnerWindow::onAnalysisFailed);

    QString errorMessage;
    if (!m_sessionController.initialize(&errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("puzzle runner"), errorMessage);
    }
    QString restoreError;
    if (!restoreCachedPuzzleSupply(&restoreError) && !restoreError.isEmpty()) {
        appendLogMessage(timestamped(QStringLiteral("live puzzle cache restore skipped: %1").arg(restoreError)));
    }
    refreshSupplyStatus();
    refreshUi();
}

PuzzleRunnerWindow::~PuzzleRunnerWindow()
{
    if (m_sessionController.hasTrackedPuzzleAttempt()) {
        QString attemptError;
        if (!m_sessionController.finalizePuzzleAttemptForAppExit(&attemptError)) {
            qWarning().noquote() << "could not finalize solve history during destruction:" << attemptError;
        }
    }
    if (m_analysisInProgress) {
        m_orchestrator->requestCancel();
    }
    m_orchestratorThread->quit();
    m_orchestratorThread->wait(5000);
    persistSettings();
    delete m_sourceGamePgnCache;
}

void PuzzleRunnerWindow::closeEvent(QCloseEvent *event)
{
    if (!m_analysisInProgress) {
        QString attemptError;
        if (!m_sessionController.finalizePuzzleAttemptForAppExit(&attemptError)) {
            QMessageBox::warning(
                this,
                QStringLiteral("solve history"),
                QStringLiteral("ParlAWL could not safely finish the current solve attempt:\n%1\n\nThe window will remain open.")
                    .arg(attemptError));
            event->ignore();
            return;
        }
        QMainWindow::closeEvent(event);
        return;
    }

    m_closeRequested = true;
    m_orchestrator->requestCancel();
    m_reportView->setPlainText(QStringLiteral("cancelling analysis..."));
    appendLogMessage(timestamped(QStringLiteral("window close requested; cancelling active analysis")));
    event->ignore();
}

bool PuzzleRunnerWindow::buildAnalysisInput(PuzzleRound *puzzleRound, SourceGame *sourceGame, QString *errorMessage) const
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("annotated replay mode is read-only and cannot start a new analysis");
        }
        return false;
    }
    return m_sessionController.buildAnalysisInput(puzzleRound, sourceGame, errorMessage);
}

bool PuzzleRunnerWindow::loadAnnotatedReplayFile(const QString &path, QString *errorMessage)
{
    if (m_analysisInProgress) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("finish or cancel the active analysis before opening a replay");
        }
        return false;
    }
    QByteArray bytes;
    if (!readDirectRegularFile(
            path,
            kMaximumAnnotatedReplayBytes,
            QStringLiteral("analysis replay"),
            &bytes,
            errorMessage)) {
        return false;
    }

    QString parseError;
    const auto parsed = AnnotatedReplayPack::fromJson(bytes, &parseError);
    if (!parsed.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = parseError.isEmpty() ? QStringLiteral("analysis replay is invalid") : parseError;
        }
        return false;
    }

    QString attemptError;
    if (!m_sessionController.finalizePuzzleAttemptForAnnotatedReplay(&attemptError)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "analysis replay was not opened because the active solve attempt could not be preserved: %1")
                                .arg(attemptError);
        }
        return false;
    }

    m_annotatedReplayPack = *parsed;
    m_replayPlaybackTimer->stop();
    m_transportControls->setPlaying(false);
    m_replaySession.load(*m_annotatedReplayPack);
    m_replayVariationAnchorPly = 0;
    m_workspaceMode = WorkspaceMode::AnnotatedReplay;
    m_lastReviewedFen.clear();
    if (m_stockfishReviewController != nullptr) {
        m_stockfishReviewController->resetCurrentReview(
            QStringLiteral("fresh engine review is disabled in annotated replay"));
    }
    setAnnotatedReplayWorkspaceUi(true);
    appendLogMessage(timestamped(
        QStringLiteral("opened read-only annotated replay %1").arg(m_annotatedReplayPack->replayId())));
    refreshReplayUi();
    return true;
}

bool PuzzleRunnerWindow::loadValidatedPuzzlePackFile(const QString &path, QString *errorMessage)
{
    if (m_analysisInProgress) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("finish or cancel the active analysis before opening a puzzle pack");
        }
        return false;
    }
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("return to puzzles before importing an engine-line pack");
        }
        return false;
    }

    QByteArray bytes;
    if (!readDirectRegularFile(
            path,
            kMaximumValidatedPuzzlePackBytes,
            QStringLiteral("engine-line puzzle pack"),
            &bytes,
            errorMessage)) {
        return false;
    }

    QString parseError;
    const auto pack = EngineValidatedPuzzlePack::fromJsonLines(bytes, &parseError);
    if (!pack.has_value() || pack->puzzles().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = parseError.isEmpty()
                ? QStringLiteral("engine-line pack does not contain any accepted records declaring engine_validated")
                : parseError;
        }
        return false;
    }

    const bool previousLiveSupplyActive = m_liveSupplyActive;
    const bool previousPackActive = m_validatedPuzzlePackActive;
    const int previousPackCount = m_validatedPuzzlePackCount;
    const int previousSupplyCheckSlot = m_lastSupplyCheckSlot;
    const QString previousActivePuzzleId = m_activePuzzleId;
    const QString previousReviewedFen = m_lastReviewedFen;

    // These flags change before replacement emits session signals, so a UI
    // refresh cannot start a Lichess top-up against the imported queue.
    m_liveSupplyActive = false;
    m_validatedPuzzlePackActive = true;
    m_validatedPuzzlePackCount = pack->puzzles().size();
    m_lastSupplyCheckSlot = -1;
    m_activePuzzleId.clear();
    m_lastReviewedFen.clear();

    QString replaceError;
    if (!m_sessionController.replacePuzzles(pack->puzzles(), &replaceError)) {
        m_liveSupplyActive = previousLiveSupplyActive;
        m_validatedPuzzlePackActive = previousPackActive;
        m_validatedPuzzlePackCount = previousPackCount;
        m_lastSupplyCheckSlot = previousSupplyCheckSlot;
        m_activePuzzleId = previousActivePuzzleId;
        m_lastReviewedFen = previousReviewedFen;
        refreshSupplyStatus();
        if (errorMessage != nullptr) {
            *errorMessage = replaceError.isEmpty()
                ? QStringLiteral("engine-line pack could not replace the puzzle queue")
                : replaceError;
        }
        return false;
    }

    refreshSupplyStatus();
    appendLogMessage(timestamped(
        QStringLiteral(
            "imported %1 engine-line record(s) declaring engine_validated; producer not authenticated, engine not rerun, remote top-up disabled")
            .arg(pack->puzzles().size())));
    return true;
}

void PuzzleRunnerWindow::onOpenAnnotatedReplayRequested()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Analysis Replay"),
        QDir::homePath(),
        QStringLiteral("Annotated replay (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString errorMessage;
    if (!loadAnnotatedReplayFile(path, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("analysis replay"), errorMessage);
    }
}

void PuzzleRunnerWindow::onOpenPlayerStatisticsRequested()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Player Statistics"),
        QDir::homePath(),
        QStringLiteral("Player analysis catalog or explorer (*.sqlite3);;Legacy player snapshot (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString errorMessage;
    const bool explorer = QFileInfo(path).suffix().compare(
        QStringLiteral("sqlite3"), Qt::CaseInsensitive) == 0;
    if (explorer) {
        if (!openPlayerGameExplorer(
                path, QString(), QString(), QString(), QString(), &errorMessage)) {
            QMessageBox::warning(this, QStringLiteral("player statistics"), errorMessage);
        }
        return;
    }

    QByteArray raw;
    const bool loaded = readDirectRegularFile(
            path,
            64 * 1024 * 1024,
            QStringLiteral("player-statistics snapshot"),
            &raw,
            &errorMessage)
        && m_playerStatisticsPanel->loadSnapshot(raw, &errorMessage);
    if (!loaded) {
        QMessageBox::warning(this, QStringLiteral("player statistics"), errorMessage);
        return;
    }
    m_rightTabs->setCurrentWidget(m_playerStatisticsPanel);
    appendLogMessage(timestamped(
        QStringLiteral(
            "opened a local display-only player-statistics snapshot; ParlAWL did not authenticate or rebuild its source evidence")));
}

void PuzzleRunnerWindow::onOpenBoardStructureStatisticsRequested()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Board Structure Statistics"),
        QDir::homePath(),
        QStringLiteral("BoardStructure snapshot (*.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString errorMessage;
    QByteArray raw;
    const bool loaded = readDirectRegularFile(
            path,
            64 * 1024 * 1024,
            QStringLiteral("BoardStructure player-statistics snapshot"),
            &raw,
            &errorMessage)
        && m_playerStatisticsPanel->loadBoardStructureSnapshot(raw, &errorMessage);
    if (!loaded) {
        QMessageBox::warning(this, QStringLiteral("Board Structure statistics"), errorMessage);
        return;
    }
    m_rightTabs->setCurrentWidget(m_playerStatisticsPanel);
    appendLogMessage(timestamped(
        QStringLiteral(
            "opened local display-only BoardStructure statistics; ParlAWL did not authenticate source replay or run an engine")));
}

bool PuzzleRunnerWindow::openPlayerGameExplorer(
    const QString &absoluteSqlitePath,
    const QString &playerId,
    const QString &sourceGameId,
    const QString &selectiveReportDirectory,
    const QString &gameReviewDirectory,
    QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    const auto fail = [errorMessage](const QString &message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (!sourceGameId.isEmpty() && playerId.isEmpty()) {
        return fail(QStringLiteral("an exact player ID is required to open an exact game"));
    }
    std::optional<SelectiveDeepReportCatalog> deepReports;
    if (!selectiveReportDirectory.isEmpty()) {
        deepReports = SelectiveDeepReportCatalog::fromDirectory(
            selectiveReportDirectory,
            errorMessage);
        if (!deepReports.has_value()) {
            return false;
        }
    }
    std::optional<GameReviewDisplayCatalog> gameReviewDisplays;
    if (!gameReviewDirectory.isEmpty()) {
        gameReviewDisplays = GameReviewDisplayCatalog::fromDirectory(
            gameReviewDirectory,
            errorMessage);
        if (!gameReviewDisplays.has_value()) {
            return false;
        }
    }
    if (!m_playerStatisticsPanel->loadExplorerDatabase(
            absoluteSqlitePath,
            errorMessage)) {
        return false;
    }
    if (!playerId.isEmpty() && !m_playerStatisticsPanel->selectPlayer(playerId)) {
        m_playerStatisticsPanel->clearSnapshot();
        return fail(QStringLiteral("player explorer does not contain the exact requested player ID"));
    }
    m_selectiveDeepReports = std::move(deepReports);
    m_gameReviewDisplays = std::move(gameReviewDisplays);

    if (m_gameReviewDisplays.has_value()) {
        QSet<QString> reportGames;
        if (m_selectiveDeepReports.has_value()) {
            const QStringList ids = m_selectiveDeepReports->sourceGameIds();
            reportGames = QSet<QString>(ids.cbegin(), ids.cend());
        }
        int orphanCount = 0;
        for (const QString &gameId : m_gameReviewDisplays->sourceGameIds()) {
            if (!reportGames.contains(gameId)) {
                ++orphanCount;
            }
        }
        if (orphanCount > 0) {
            appendLogMessage(timestamped(QStringLiteral(
                "ignored %1 orphan Coach Review sidecar%2 with no joined Report-v2 game")
                    .arg(orphanCount)
                    .arg(orphanCount == 1 ? QString() : QStringLiteral("s"))));
        }
    }

    m_rightTabs->setCurrentWidget(m_playerStatisticsPanel);
    appendLogMessage(timestamped(
        m_selectiveDeepReports.has_value()
            ? QStringLiteral(
                  "opened local read-only player analysis data with %1 exact-join selective deep reports and %2 Coach Review projection%3; ParlAWL ran no engine, network, or source replay")
                  .arg(m_selectiveDeepReports->reportCount())
                  .arg(m_gameReviewDisplays.has_value()
                           ? m_gameReviewDisplays->reviewCount() : 0)
                  .arg(m_gameReviewDisplays.has_value()
                           && m_gameReviewDisplays->reviewCount() == 1
                       ? QString() : QStringLiteral("s"))
            : QStringLiteral(
                  "opened local read-only player analysis data; ParlAWL ran no engine, network, or source replay")));
    if (!sourceGameId.isEmpty()
        && !openPlayerGameBreakdown(sourceGameId, errorMessage)) {
        return false;
    }
    return true;
}

void PuzzleRunnerWindow::surfaceGameStudyWorkspace()
{
    if (m_gameReviewHubWindow != nullptr
        && m_gameReviewHubWindow->openGameCount() > 0) {
        hide();
        m_gameReviewHubWindow->surfaceActiveGame();
    }
}

bool PuzzleRunnerWindow::openPlayerGameBreakdown(
    const QString &sourceGameId,
    QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    const auto fail = [errorMessage](const QString &message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (m_analysisInProgress) {
        return fail(QStringLiteral(
            "Finish or cancel the active analysis before opening a game."));
    }

    QString details;
    const auto breakdown = m_playerStatisticsPanel->gameBreakdown(sourceGameId, &details);
    if (!breakdown.has_value()) {
        return fail(details);
    }

    MechanicalReplayGame mechanical;
    mechanical.sourceGameId = breakdown->sourceGameId;
    mechanical.canonicalGameUrl = breakdown->canonicalGameUrl;
    mechanical.eventStartUtc = breakdown->eventStartUtc;
    mechanical.whiteUsername = breakdown->whitePlayerId;
    mechanical.blackUsername = breakdown->blackPlayerId;
    mechanical.whiteRating = breakdown->whiteRating;
    mechanical.blackRating = breakdown->blackRating;
    mechanical.result = breakdown->result;
    mechanical.openingStatus = breakdown->openingStatus;
    mechanical.openingEco = breakdown->openingEco;
    mechanical.openingName = breakdown->openingName;
    mechanical.openingLastBookPly = breakdown->openingLastBookPly;
    mechanical.viewedPlayerColor = breakdown->viewedPlayerColor;
    if (m_selectiveDeepReports.has_value()) {
        if (const SelectiveDeepGameReview *review =
                m_selectiveDeepReports->reviewForGame(sourceGameId);
            review != nullptr) {
            mechanical.selectiveDeepReview = *review;
        }
    }
    const GameReviewDisplay *gameReviewDisplay = nullptr;
    if (m_gameReviewDisplays.has_value()) {
        gameReviewDisplay = m_gameReviewDisplays->reviewForGame(sourceGameId);
        if (gameReviewDisplay != nullptr) {
            if (!mechanical.selectiveDeepReview.has_value()) {
                gameReviewDisplay = nullptr;
            } else if (gameReviewDisplay->sourceReportId
                       != mechanical.selectiveDeepReview->reportId) {
                return fail(QStringLiteral(
                    "Coach Review sidecar source_report_id does not match the joined Report-v2 game."));
            }
        }
    }
    if (breakdown->engineEvidence.has_value()) {
        const PlayerStatisticsEngineGameEvidence &source = *breakdown->engineEvidence;
        PersistedEngineGameEvidence engine;
        engine.evidenceId = source.evidenceId;
        engine.representativeRunId = source.representativeRunId;
        engine.analysisRecordedAtUtc = source.analysisRecordedAtUtc;
        engine.lineageCount = source.lineageCount;
        engine.engineConfigId = source.engineConfigId;
        engine.engineName = source.engineName;
        engine.engineAuthor = source.engineAuthor;
        engine.engineBinarySha256 = source.engineBinarySha256;
        engine.engineAdapterVersion = source.engineAdapterVersion;
        engine.nodeLimit = source.nodeLimit;
        engine.hashMebibytes = source.hashMebibytes;
        engine.threads = source.threads;
        engine.wdlLossThresholds = source.wdlLossThresholds;
        engine.winningExpectationMillionths = source.winningExpectationMillionths;
        mechanical.engineEvidence = engine;
    }
    mechanical.moves.reserve(breakdown->moves.size());
    for (const PlayerStatisticsGameMove &source : breakdown->moves) {
        MechanicalReplayMove move;
        move.ply = source.ply;
        move.san = source.san;
        move.uci = source.uci;
        move.positionPhase = source.phase;
        move.forcednessStatus = source.forcedness;
        move.legalMoveCount = source.legalMoveCount;
        move.decisionStartClockMs = source.decisionStartClockMs;
        move.clockRemainingAfterMoveMs = source.clockAfterMs;
        move.elapsedMoveMs = source.elapsedMs;
        move.elapsedStatus = source.elapsedStatus;
        if (source.engineEvidence.has_value()) {
            const PlayerStatisticsEngineMoveEvidence &sourceEngine = *source.engineEvidence;
            PersistedEngineMoveEvidence engine;
            engine.expectedBeforeMillionths = sourceEngine.expectedBeforeMillionths;
            engine.expectedAfterMillionths = sourceEngine.expectedAfterMillionths;
            engine.wdlLossMillionths = sourceEngine.wdlLossMillionths;
            engine.centipawnLoss = sourceEngine.centipawnLoss;
            engine.missedWinningAdvantage = sourceEngine.missedWinningAdvantage;
            engine.missedForcedMate = sourceEngine.missedForcedMate;
            engine.severity = sourceEngine.severity;
            engine.beforeScoreKind = sourceEngine.beforeScoreKind;
            engine.beforeCentipawnsWhite = sourceEngine.beforeCentipawnsWhite;
            engine.beforeMateForWhite = sourceEngine.beforeMateForWhite;
            engine.beforeWdlWhite = sourceEngine.beforeWdlWhite;
            engine.beforeBestMoveUci = sourceEngine.beforeBestMoveUci;
            engine.beforeDepth = sourceEngine.beforeDepth;
            engine.beforeSelectiveDepth = sourceEngine.beforeSelectiveDepth;
            engine.beforeNodes = sourceEngine.beforeNodes;
            engine.beforePvUci = sourceEngine.beforePvUci;
            engine.afterScoreKind = sourceEngine.afterScoreKind;
            engine.afterCentipawnsWhite = sourceEngine.afterCentipawnsWhite;
            engine.afterMateForWhite = sourceEngine.afterMateForWhite;
            engine.afterWdlWhite = sourceEngine.afterWdlWhite;
            move.engineEvidence = engine;
        }
        mechanical.moves.append(move);
    }

    const auto replay = AnnotatedReplayPack::fromMechanicalGame(mechanical, &details);
    if (!replay.has_value()) {
        return fail(details);
    }
    QString attemptError;
    if (!m_sessionController.finalizePuzzleAttemptForAnnotatedReplay(&attemptError)) {
        return fail(QStringLiteral(
            "The game was not opened because the active solve attempt could not be preserved: %1")
                .arg(attemptError));
    }

    if (m_gameReviewHubWindow == nullptr
        || !m_gameReviewHubWindow->openGame(*replay, &details, gameReviewDisplay)) {
        return fail(details.isEmpty()
                ? QStringLiteral("The game could not be opened in Review Hub.")
                : details);
    }
    appendLogMessage(timestamped(
        QStringLiteral("opened local read-only game breakdown %1 in Review Hub; no engine or network process was started")
            .arg(sourceGameId)));
    return true;
}

void PuzzleRunnerWindow::onPlayerGameBreakdownRequested(const QString &sourceGameId)
{
    QString errorMessage;
    if (!openPlayerGameBreakdown(sourceGameId, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("game breakdown"), errorMessage);
        return;
    }
    surfaceGameStudyWorkspace();
}

void PuzzleRunnerWindow::onOpenValidatedPuzzlePackRequested()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Engine-Line Pack"),
        QDir::homePath(),
        QStringLiteral("Engine-line puzzle pack (*.jsonl);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString errorMessage;
    if (!loadValidatedPuzzlePackFile(path, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("engine-line pack"), errorMessage);
    }
}

void PuzzleRunnerWindow::onExportSolveHistoryRequested()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        QMessageBox::warning(
            this,
            QStringLiteral("solve history"),
            QStringLiteral("Return to puzzles before exporting solve history."));
        return;
    }
    if (!ensureDatabaseReady() || !m_puzzleAttemptRepository) {
        QMessageBox::warning(
            this,
            QStringLiteral("solve history"),
            QStringLiteral("The local solve-history ledger is not available."));
        return;
    }

    QString exportDirectory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (exportDirectory.isEmpty()) {
        exportDirectory = QDir::homePath();
    }
    const QString suggestedPath = exportDirectory + QStringLiteral("/parlawl-solve-history-%1.jsonl")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Solve History"),
        suggestedPath,
        QStringLiteral("Solve history (*.jsonl);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo::exists(path)) {
        QMessageBox::warning(
            this,
            QStringLiteral("solve history"),
            QStringLiteral("Choose a new file name. Solve-history export never overwrites an existing file."));
        return;
    }

    int exportedCount = 0;
    QString errorMessage;
    if (!m_puzzleAttemptRepository->exportTerminalAttempts(path, &exportedCount, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("solve history"), errorMessage);
        return;
    }
    appendLogMessage(timestamped(
        QStringLiteral("exported %1 completed solve attempt(s) to %2; nothing was uploaded or used for training")
            .arg(exportedCount)
            .arg(path)));
    QMessageBox::information(
        this,
        QStringLiteral("solve history"),
        QStringLiteral("Exported %1 completed attempt(s).\n\nThe file was created locally; nothing was uploaded or used for training.")
            .arg(exportedCount));
}

void PuzzleRunnerWindow::onBackToPuzzlesRequested()
{
    if (m_workspaceMode != WorkspaceMode::AnnotatedReplay) {
        return;
    }
    const bool returnToPlayerStatistics = m_annotatedReplayPack.has_value()
        && m_annotatedReplayPack->isMechanicalGameBreakdown();
    m_replayPlaybackTimer->stop();
    m_transportControls->setPlaying(false);
    if (m_stockfishReviewController != nullptr) {
        m_stockfishReviewController->resetCurrentReview(
            QStringLiteral("review the current puzzle position"));
    }
    m_lastReviewedFen.clear();
    m_workspaceMode = WorkspaceMode::Puzzle;
    m_annotatedReplayPack.reset();
    m_replaySession = ReplaySession {};
    m_replayVariationAnchorPly = 0;
    // Replay transitions do not pause solve attempts. Reset the puzzle and its
    // exposure baseline; persistence remains lazy until the next interaction.
    m_sessionController.retryPuzzle();
    setAnnotatedReplayWorkspaceUi(false);
    m_replayEvidencePanel->setEmptyState();
    m_gameReviewPanel->setEmptyState();
    refreshUi();
    if (returnToPlayerStatistics) {
        m_rightTabs->setCurrentWidget(m_playerStatisticsPanel);
    }
}

void PuzzleRunnerWindow::onShowReplayVariationRequested()
{
    if (!m_annotatedReplayPack.has_value() || m_replaySession.inVariation()) {
        return;
    }
    const int anchorPly = m_replaySession.currentMainlinePly();
    QString errorMessage;
    if (!m_replaySession.enterPreferredVariation(anchorPly, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("engine line"), errorMessage);
        return;
    }
    m_replayVariationAnchorPly = anchorPly;
    if (!m_replaySession.stepForward()) {
        m_replaySession.exitVariation();
        m_replayVariationAnchorPly = 0;
        QMessageBox::warning(this, QStringLiteral("engine line"), QStringLiteral("supplied engine line is empty"));
        return;
    }
    refreshReplayUi();
}

void PuzzleRunnerWindow::onReturnFromReplayVariationRequested()
{
    if (!m_replaySession.inVariation()) {
        return;
    }
    QString errorMessage;
    if (!m_replaySession.exitVariation(&errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("engine line"), errorMessage);
        return;
    }
    m_replayVariationAnchorPly = 0;
    refreshReplayUi();
}

void PuzzleRunnerWindow::onReplayPlyRequested(int ply)
{
    if (!m_annotatedReplayPack.has_value()) {
        return;
    }
    m_replayPlaybackTimer->stop();
    m_transportControls->setPlaying(false);
    if (m_replaySession.inVariation()) {
        QString errorMessage;
        if (!m_replaySession.exitVariation(&errorMessage)) {
            QMessageBox::warning(this, QStringLiteral("analysis replay"), errorMessage);
            return;
        }
        m_replayVariationAnchorPly = 0;
    }
    if (m_replaySession.seekMainlinePly(ply)) {
        refreshReplayUi();
    }
}

void PuzzleRunnerWindow::refreshUi()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        refreshReplayUi();
        return;
    }
    const GameStateStore *store = m_sessionController.gameStateStore();
    if (store == nullptr || !store->hasPosition()) {
        return;
    }
    const QString puzzleId = store->currentPuzzle().id;
    if (puzzleId != m_activePuzzleId) {
        resetPuzzleScopedUiState(puzzleId);
        QString sourceHistoryError;
        if (ensureCurrentPuzzleSourceHistory(&sourceHistoryError)) {
            return;
        }
        if (!sourceHistoryError.isEmpty()) {
            appendLogMessage(timestamped(QStringLiteral("source history unavailable: %1").arg(sourceHistoryError)));
        }
    }
    updateBoard();
    updatePanels();
    maybeRefreshEngineReview(false);
    const int currentSlot = m_sessionController.currentPuzzleSlot();
    if (currentSlot != m_lastSupplyCheckSlot) {
        m_lastSupplyCheckSlot = currentSlot;
        maybeTopUpPuzzleSupply();
    }
}

bool PuzzleRunnerWindow::ensureCurrentPuzzleSourceHistory(QString *errorMessage)
{
    if (m_sourceHistoryHydrationInProgress) {
        return false;
    }

    const GameStateStore *store = m_sessionController.gameStateStore();
    if (store == nullptr || !store->hasPosition()) {
        return false;
    }

    const PuzzleDefinition &puzzle = store->currentPuzzle();
    if (!allowsLichessPgnHydration(puzzle.analysisSeed)) {
        return false;
    }
    if (!puzzle.analysisSeed.sourceGamePgn.trimmed().isEmpty() || puzzle.analysisSeed.sourceGameId.trimmed().isEmpty()) {
        return false;
    }

    const QString cachedPgn = m_sourceGamePgnCache->load(puzzle.analysisSeed.sourceGameId);
    if (!cachedPgn.trimmed().isEmpty()) {
        m_sourceHistoryHydrationInProgress = true;
        QString hydrateError;
        const bool hydrated = m_sessionController.setCurrentPuzzleSourceGamePgn(
            cachedPgn,
            parlawl::puzzle_runner::pgnOpeningName(cachedPgn),
            &hydrateError);
        m_sourceHistoryHydrationInProgress = false;
        if (!hydrated && errorMessage != nullptr) {
            *errorMessage = hydrateError;
        }
        return hydrated;
    }

    const QString apiToken = resolveLichessToken().trimmed();
    LichessClient client;
    const SourceGamePgnResult result = client.fetchSourceGamePgnText(puzzle.analysisSeed.sourceGameId, apiToken);
    if (!result.ok) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorMessage;
        }
        return false;
    }

    QString cacheError;
    m_sourceGamePgnCache->store(puzzle.analysisSeed.sourceGameId, result.pgnText, &cacheError);
    if (!cacheError.isEmpty()) {
        appendLogMessage(timestamped(QStringLiteral("source history cache warning: %1").arg(cacheError)));
    }

    m_sourceHistoryHydrationInProgress = true;
    QString hydrateError;
    const bool hydrated = m_sessionController.setCurrentPuzzleSourceGamePgn(result.pgnText, result.openingName, &hydrateError);
    m_sourceHistoryHydrationInProgress = false;
    if (hydrated) {
        appendLogMessage(timestamped(QStringLiteral("loaded full source game history for %1").arg(puzzle.id)));
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = hydrateError;
    }
    return false;
}

void PuzzleRunnerWindow::onBoardSquareClicked(int square)
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    GameStateStore *store = m_sessionController.gameStateStore();
    if (!m_sessionController.canSubmitMoves()) {
        return;
    }

    if (store->selectedSquare() == square) {
        store->clearSelectedSquare();
        return;
    }

    const auto piece = store->currentPosition().pieceAt(square);
    if (!piece.isEmpty() && piece.color == store->currentPosition().sideToMove()) {
        const QVector<Move> candidates = store->legalMovesFromSquare(square);
        if (!candidates.isEmpty()) {
            store->setSelectedSquare(square);
            return;
        }
    }

    if (store->selectedSquare() < 0) {
        return;
    }

    const QVector<Move> matches = store->legalMovesBetween(store->selectedSquare(), square);
    if (matches.isEmpty()) {
        store->clearSelectedSquare();
        return;
    }

    Move chosenMove = matches.first();
    if (matches.size() > 1) {
        for (const Move &candidate : matches) {
            if (candidate.promotion == PieceType::Queen) {
                chosenMove = candidate;
                break;
            }
        }
    }

    m_lastMoveSquares = {chosenMove.from, chosenMove.to};
    store->clearSelectedSquare();
    m_sessionController.submitUserMove(chosenMove.uci());
}

void PuzzleRunnerWindow::onHintRequested()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    m_sessionController.requestHint();
}

void PuzzleRunnerWindow::onSolutionRequested()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    m_lastMoveSquares = {-1, -1};
    m_sessionController.revealSolution();
}

void PuzzleRunnerWindow::onControllerHint(const QString &hint)
{
    appendLogMessage(timestamped(QStringLiteral("trainer hint: %1").arg(hint)));
}

void PuzzleRunnerWindow::onControllerError(const QString &message)
{
    appendLogMessage(timestamped(QStringLiteral("trainer: %1").arg(message)));
}

void PuzzleRunnerWindow::onEngineReviewUpdated()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    const StockfishReviewSnapshot &snapshot = m_stockfishReviewController->snapshot();
    m_enginePanel->setReviewState(
        snapshot.statusText,
        snapshot.evaluationText,
        snapshot.bestMove,
        snapshot.pvLine,
        !m_stockfishPathEdit->text().trimmed().isEmpty() && !snapshot.fen.isEmpty(),
        m_stockfishReviewController->autoRefreshEnabled(),
        snapshot.inProgress);
    m_evaluationBarWidget->setExpectation(snapshot.whiteExpectation, snapshot.available);
}

void PuzzleRunnerWindow::onEngineRefreshRequested()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    maybeRefreshEngineReview(true);
}

void PuzzleRunnerWindow::onEngineAutoRefreshChanged(bool enabled)
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    m_stockfishReviewController->setAutoRefreshEnabled(enabled);
    if (enabled) {
        maybeRefreshEngineReview(false);
    }
}

void PuzzleRunnerWindow::onCleanupRequested()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        appendLogMessage(timestamped(QStringLiteral("cleanup is unavailable in read-only annotated replay mode")));
        return;
    }
    persistSettings();
    m_stockfishReviewController->clearCache();

    if (!ensureDatabaseReady()) {
        onAnalysisFailed(QStringLiteral("database"), QStringLiteral("database initialization failed"));
        return;
    }

    AnalysisRepository repository(m_databaseManager->database());
    QString errorMessage;
    int deletedRuns = 0;
    const int keepRecentRuns = m_keepRecentRunsSetting.toInt();
    if (!repository.pruneAnalysisHistory(
            std::max(keepRecentRuns, 0),
            m_preserveAnalyzedSetting,
            &deletedRuns,
            &errorMessage)) {
        onAnalysisFailed(QStringLiteral("cleanup"), errorMessage);
        return;
    }

    m_hasLoadedReport = false;
    m_selectedRunId.clear();
    refreshRecentRuns();
    refreshUi();
    appendLogMessage(timestamped(QStringLiteral("cleanup completed: deleted %1 runs and cleared review cache").arg(deletedRuns)));
    m_statusStateLabel->setText(QStringLiteral("cleanup"));
    m_statusDetailLabel->setText(QStringLiteral("deleted %1 runs and cleared review cache").arg(deletedRuns));
}

void PuzzleRunnerWindow::onReloadPuzzlesRequested()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        appendLogMessage(timestamped(QStringLiteral("puzzle reload is unavailable in read-only annotated replay mode")));
        return;
    }
    QString errorMessage;
    if (!reloadPuzzleSupply(false, &errorMessage)) {
        onAnalysisFailed(QStringLiteral("puzzle_supply"), errorMessage.isEmpty() ? QStringLiteral("failed to reload puzzles") : errorMessage);
        return;
    }
    appendLogMessage(timestamped(QStringLiteral("puzzle supply reloaded")));
}

void PuzzleRunnerWindow::onAnalyzeCurrentPuzzleRequested()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        appendLogMessage(timestamped(QStringLiteral("analysis is unavailable in read-only annotated replay mode")));
        return;
    }
    persistSettings();
    if (m_analysisInProgress) {
        appendLogMessage(timestamped(QStringLiteral("puzzle analysis handoff ignored because a run is already in progress")));
        return;
    }
    if (!ensureDatabaseReady()) {
        onAnalysisFailed(QStringLiteral("database"), QStringLiteral("database initialization failed"));
        return;
    }

    QString validationMessage;
    if (!validateAnalyzeSettings(false, &validationMessage)) {
        onAnalysisFailed(QStringLiteral("config"), validationMessage);
        return;
    }

    PuzzleRound puzzleRound;
    SourceGame sourceGame;
    QString handoffError;
    if (!buildAnalysisInput(&puzzleRound, &sourceGame, &handoffError)) {
        onAnalysisFailed(QStringLiteral("puzzle_runner"), handoffError);
        return;
    }

    const QString workerPath = m_pythonWorkerPathEdit->text().trimmed();
    const QString stockfishPath = m_stockfishPathEdit->text().trimmed();
    const QString databasePath = m_databasePathEdit->text().trimmed();
    appendLogMessage(timestamped(QStringLiteral("analysis handoff requested for puzzle %1 from trainer shell").arg(puzzleRound.puzzleId)));
    QMetaObject::invokeMethod(
        m_orchestrator,
        [this, puzzleRound, sourceGame, workerPath, stockfishPath, databasePath]() {
            m_orchestrator->analyzeProvidedPuzzle(puzzleRound, sourceGame, workerPath, stockfishPath, databasePath);
        },
        Qt::QueuedConnection);
}

void PuzzleRunnerWindow::onCancelAnalysisRequested()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    if (!m_analysisInProgress) {
        appendLogMessage(timestamped(QStringLiteral("cancel request ignored because no analysis is in progress")));
        return;
    }

    m_cancelButton->setEnabled(false);
    m_reportView->setPlainText(QStringLiteral("cancelling analysis..."));
    appendLogMessage(timestamped(QStringLiteral("cancel requested from unified shell")));
    QMetaObject::invokeMethod(
        m_orchestrator,
        [this]() {
            m_orchestrator->requestCancel();
        },
        Qt::QueuedConnection);
}

void PuzzleRunnerWindow::onExportJsonRequested()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    persistSettings();
    if (m_selectedRunId.isEmpty()) {
        appendLogMessage(timestamped(QStringLiteral("export ignored because no completed run is selected")));
        return;
    }
    if (!ensureDatabaseReady()) {
        onAnalysisFailed(QStringLiteral("database"), QStringLiteral("database initialization failed"));
        return;
    }

    AnalysisRepository repository(m_databaseManager->database());
    PersistedAnalysisReport report;
    QString errorMessage;
    if (!repository.loadReport(m_selectedRunId, &report, &errorMessage)) {
        onAnalysisFailed(QStringLiteral("export_json"), errorMessage);
        return;
    }

    const QString defaultPath = QDir::homePath() + QStringLiteral("/parlawl-report-%1.json").arg(m_selectedRunId);
    const QString exportPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("export report json"),
        defaultPath,
        QStringLiteral("json files (*.json)"));
    if (exportPath.isEmpty()) {
        appendLogMessage(timestamped(QStringLiteral("export cancelled")));
        return;
    }

    QSaveFile file(exportPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        onAnalysisFailed(QStringLiteral("export_json"), QStringLiteral("failed to open %1 for writing").arg(exportPath));
        return;
    }

    const QString reportText = ReportFormatter::formatAnalysisReport(
        report.puzzleRound,
        report.sourceGame,
        report.tacticalEvent,
        report.criticalMoves);
    const QByteArray payload = exportPersistedReportJson(report, reportText).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit()) {
        onAnalysisFailed(QStringLiteral("export_json"), QStringLiteral("failed to write %1").arg(exportPath));
        return;
    }

    appendLogMessage(timestamped(QStringLiteral("exported report json to %1").arg(exportPath)));
}

void PuzzleRunnerWindow::onExportAssistantPacketRequested()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    persistSettings();
    if (m_selectedRunId.isEmpty()) {
        appendLogMessage(timestamped(QStringLiteral("assistant packet export ignored because no completed run is selected")));
        return;
    }
    if (!ensureDatabaseReady()) {
        onAnalysisFailed(QStringLiteral("database"), QStringLiteral("database initialization failed"));
        return;
    }

    AnalysisRepository repository(m_databaseManager->database());
    PersistedAnalysisReport report;
    QString errorMessage;
    if (!repository.loadReport(m_selectedRunId, &report, &errorMessage)) {
        onAnalysisFailed(QStringLiteral("export_assistant_packet"), errorMessage);
        return;
    }

    const QString defaultPath = QDir::homePath() + QStringLiteral("/parlawl-assistant-packet-%1.md").arg(m_selectedRunId);
    const QString exportPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("export assistant packet"),
        defaultPath,
        QStringLiteral("markdown files (*.md)"));
    if (exportPath.isEmpty()) {
        appendLogMessage(timestamped(QStringLiteral("assistant packet export cancelled")));
        return;
    }

    const QString reportText = ReportFormatter::formatAnalysisReport(
        report.puzzleRound,
        report.sourceGame,
        report.tacticalEvent,
        report.criticalMoves);

    QSaveFile file(exportPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        onAnalysisFailed(QStringLiteral("export_assistant_packet"), QStringLiteral("failed to open %1 for writing").arg(exportPath));
        return;
    }

    const QByteArray payload = exportAssistantPacketMarkdown(report, reportText).toUtf8();
    if (file.write(payload) != payload.size() || !file.commit()) {
        onAnalysisFailed(QStringLiteral("export_assistant_packet"), QStringLiteral("failed to write %1").arg(exportPath));
        return;
    }

    appendLogMessage(timestamped(QStringLiteral("exported assistant packet to %1").arg(exportPath)));
}

void PuzzleRunnerWindow::onImportAssistantInferenceRequested()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    persistSettings();
    if (m_selectedRunId.isEmpty()) {
        appendLogMessage(timestamped(QStringLiteral("assistant inference import ignored because no completed run is selected")));
        return;
    }
    if (!ensureDatabaseReady()) {
        onAnalysisFailed(QStringLiteral("database"), QStringLiteral("database initialization failed"));
        return;
    }

    const QString importPath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("import assistant inference"),
        QDir::homePath(),
        QStringLiteral("assistant artifacts (*.json *.md)"));
    if (importPath.isEmpty()) {
        appendLogMessage(timestamped(QStringLiteral("assistant inference import cancelled")));
        return;
    }

    QFile file(importPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        onAnalysisFailed(QStringLiteral("assistant_inference_import"), QStringLiteral("failed to open %1").arg(importPath));
        return;
    }

    QString artifactRunId;
    QString artifactPuzzleId;
    QString artifactStatus;
    QString artifactLabelsJson;
    QString artifactSummaryMarkdown;
    QString parseError;
    if (!parseAssistantInferenceArtifact(
            file.readAll(),
            &artifactRunId,
            &artifactPuzzleId,
            &artifactStatus,
            &artifactLabelsJson,
            &artifactSummaryMarkdown,
            &parseError)) {
        onAnalysisFailed(QStringLiteral("assistant_inference_import"), parseError);
        return;
    }

    if (artifactRunId != m_selectedRunId) {
        onAnalysisFailed(
            QStringLiteral("assistant_inference_import"),
            QStringLiteral("assistant artifact run_id %1 does not match selected run %2").arg(artifactRunId, m_selectedRunId));
        return;
    }

    AnalysisRepository repository(m_databaseManager->database());
    PersistedAnalysisReport report;
    QString errorMessage;
    if (!repository.loadReport(m_selectedRunId, &report, &errorMessage)) {
        onAnalysisFailed(QStringLiteral("assistant_inference_import"), errorMessage);
        return;
    }
    if (!artifactPuzzleId.isEmpty() && artifactPuzzleId != report.puzzleRound.puzzleId) {
        onAnalysisFailed(
            QStringLiteral("assistant_inference_import"),
            QStringLiteral("assistant artifact puzzle_id %1 does not match selected run puzzle %2").arg(artifactPuzzleId, report.puzzleRound.puzzleId));
        return;
    }
    if (!repository.saveAssistantInference(m_selectedRunId, artifactStatus, artifactLabelsJson, artifactSummaryMarkdown, &errorMessage)) {
        onAnalysisFailed(QStringLiteral("assistant_inference_import"), errorMessage);
        return;
    }

    appendLogMessage(timestamped(QStringLiteral("imported assistant inference from %1").arg(importPath)));
    refreshRecentRuns(m_selectedRunId);
}

void PuzzleRunnerWindow::appendLogMessage(const QString &message)
{
    m_logView->append(message);
}

void PuzzleRunnerWindow::onAnalysisStarted()
{
    setAnalysisInProgress(true);
    m_currentPhase = QStringLiteral("starting");
    m_statusStateLabel->setText(QStringLiteral("running"));
    m_statusDetailLabel->setText(QStringLiteral("starting analysis"));
    m_reportView->setPlainText(QStringLiteral("analysis in progress..."));
    appendLogMessage(timestamped(QStringLiteral("analysis started")));
}

void PuzzleRunnerWindow::onAnalysisProgress(const QString &phase, const QString &message)
{
    m_currentPhase = phase;
    m_statusStateLabel->setText(QStringLiteral("running"));
    m_statusDetailLabel->setText(message);
}

void PuzzleRunnerWindow::onAnalysisCompleted(const QString &runId, const QString &summaryText)
{
    setAnalysisInProgress(false);
    m_currentPhase.clear();
    m_statusStateLabel->setText(QStringLiteral("completed"));
    m_statusDetailLabel->setText(QStringLiteral("analysis run %1 completed").arg(runId));
    m_reportView->setPlainText(summaryText);
    m_selectedRunId = runId;
    refreshRecentRuns(runId);
    if (ensureDatabaseReady()) {
        AnalysisRepository repository(m_databaseManager->database());
        QString errorMessage;
        m_hasLoadedReport = repository.loadReport(runId, &m_loadedReport, &errorMessage);
        if (!m_hasLoadedReport) {
            appendLogMessage(timestamped(QStringLiteral("load_report failed after completion: %1").arg(errorMessage)));
        }
    }
    refreshUi();
    appendLogMessage(timestamped(QStringLiteral("analysis completed for run %1").arg(runId)));
    if (m_closeRequested) {
        m_closeRequested = false;
        close();
    }
}

void PuzzleRunnerWindow::onAnalysisFailed(const QString &stage, const QString &message)
{
    setAnalysisInProgress(false);
    m_currentPhase.clear();
    const QString header = stage == QStringLiteral("cancelled")
                               ? QStringLiteral("analysis cancelled")
                               : QStringLiteral("analysis failed");
    const QString state = stage == QStringLiteral("cancelled")
                              ? QStringLiteral("cancelled")
                              : QStringLiteral("failed");
    m_statusStateLabel->setText(state);
    m_statusDetailLabel->setText(QStringLiteral("%1: %2").arg(stage, message));
    m_reportView->setPlainText(QStringLiteral("%1\nstage: %2\nmessage: %3").arg(header, stage, message));
    m_hasLoadedReport = false;
    refreshRecentRuns(m_selectedRunId);
    refreshUi();
    appendLogMessage(timestamped(QStringLiteral("%1 failed: %2").arg(stage, message)));
    if (m_closeRequested) {
        m_closeRequested = false;
        close();
    }
}

void PuzzleRunnerWindow::onRecentRunSelected(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous)
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    if (current == nullptr) {
        m_selectedRunId.clear();
        m_hasLoadedReport = false;
        m_exportButton->setEnabled(false);
        m_exportAssistantPacketButton->setEnabled(false);
        m_importAssistantInferenceButton->setEnabled(false);
        return;
    }

    const QString runId = current->data(Qt::UserRole).toString();
    const QString status = current->data(Qt::UserRole + 1).toString();
    const QString errorMessage = current->data(Qt::UserRole + 2).toString();
    m_selectedRunId = runId;
    m_exportButton->setEnabled(!m_analysisInProgress && status == QStringLiteral("completed"));
    m_exportAssistantPacketButton->setEnabled(!m_analysisInProgress && status == QStringLiteral("completed"));
    m_importAssistantInferenceButton->setEnabled(!m_analysisInProgress && status == QStringLiteral("completed"));

    if (runId.isEmpty()) {
        return;
    }

    if (status != QStringLiteral("completed")) {
        m_hasLoadedReport = false;
        AnalysisRun run;
        run.runId = runId;
        run.status = status;
        run.errorMessage = errorMessage;
        showRunSummary(run);
        refreshUi();
        return;
    }

    if (!ensureDatabaseReady()) {
        onAnalysisFailed(QStringLiteral("database"), QStringLiteral("database initialization failed"));
        return;
    }

    AnalysisRepository repository(m_databaseManager->database());
    PersistedAnalysisReport report;
    QString errorMessageText;
    if (!repository.loadReport(runId, &report, &errorMessageText)) {
        m_hasLoadedReport = false;
        onAnalysisFailed(QStringLiteral("load_report"), errorMessageText);
        return;
    }

    m_loadedReport = report;
    m_hasLoadedReport = true;

    m_statusStateLabel->setText(report.analysisRun.status);
    m_statusDetailLabel->setText(QStringLiteral("loaded run %1").arg(runId));
    m_reportView->setPlainText(
        ReportFormatter::formatAnalysisReport(
            report.puzzleRound,
            report.sourceGame,
            report.tacticalEvent,
            report.criticalMoves));
    refreshUi();
}

void PuzzleRunnerWindow::persistSettings()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("settings/lichess_api_token"), m_lichessTokenEdit->text().trimmed());
    settings.setValue(QStringLiteral("settings/stockfish_path"), m_stockfishPathEdit->text().trimmed());
    settings.setValue(QStringLiteral("settings/python_worker_path"), m_pythonWorkerPathEdit->text().trimmed());
    settings.setValue(QStringLiteral("settings/database_path"), m_databasePathEdit->text().trimmed());
    settings.setValue(QStringLiteral("settings/queue_size"), m_queueSizeSetting);
    settings.setValue(QStringLiteral("settings/refill_when_low"), m_refillWhenLowSetting);
    settings.setValue(QStringLiteral("settings/refill_threshold"), m_refillThresholdSetting);
    settings.setValue(QStringLiteral("settings/keep_recent_runs"), m_keepRecentRunsSetting);
    settings.setValue(QStringLiteral("settings/preserve_analyzed"), m_preserveAnalyzedSetting);
    settings.sync();
}

void PuzzleRunnerWindow::loadSettings()
{
    QSettings settings;
    const QString solverPrefix = QStringLiteral("parlawl-solver-v1:");
    m_attemptSolverId = settings.value(QStringLiteral("privacy/solve_history_solver_id")).toString();
    if (!isExactOpaqueUuid(m_attemptSolverId, solverPrefix)) {
        m_attemptSolverId = newOpaqueUuid(solverPrefix);
        settings.setValue(QStringLiteral("privacy/solve_history_solver_id"), m_attemptSolverId);
        settings.sync();
    }
    const QString resolvedToken = resolveLichessToken().trimmed();
    const QString persistedToken = settings.value(QStringLiteral("settings/lichess_api_token")).toString().trimmed();
    m_lichessTokenEdit->setText(resolvedToken.isEmpty() ? persistedToken : resolvedToken);
    m_stockfishPathEdit->setText(settings.value(QStringLiteral("settings/stockfish_path")).toString());
    m_pythonWorkerPathEdit->setText(
        settings.value(
            QStringLiteral("settings/python_worker_path"),
            QString::fromUtf8(PARLAWL_DEFAULT_WORKER_PYTHON)).toString());
    if (m_stockfishPathEdit->text().trimmed().isEmpty()) {
        m_stockfishPathEdit->setText(discoverExecutable(QStringLiteral("stockfish"), QString::fromUtf8(PARLAWL_DEFAULT_STOCKFISH_PATH)));
    }
    m_databasePathEdit->setText(settings.value(QStringLiteral("settings/database_path"), defaultDatabasePath()).toString());
    m_queueSizeSetting = settings.value(QStringLiteral("settings/queue_size"), QStringLiteral("10")).toString();
    m_refillWhenLowSetting = settings.value(QStringLiteral("settings/refill_when_low"), true).toBool();
    m_refillThresholdSetting = settings.value(QStringLiteral("settings/refill_threshold"), QStringLiteral("2")).toString();
    m_keepRecentRunsSetting = settings.value(QStringLiteral("settings/keep_recent_runs"), QStringLiteral("50")).toString();
    m_preserveAnalyzedSetting = settings.value(QStringLiteral("settings/preserve_analyzed"), true).toBool();
    if (m_queueSizeSetting != QStringLiteral("10") && m_queueSizeSetting != QStringLiteral("20")) {
        m_queueSizeSetting = QStringLiteral("10");
    }
    if (m_refillThresholdSetting != QStringLiteral("2")
        && m_refillThresholdSetting != QStringLiteral("5")
        && m_refillThresholdSetting != QStringLiteral("10")) {
        m_refillThresholdSetting = QStringLiteral("2");
    }
    m_stockfishReviewController->setEnginePath(m_stockfishPathEdit->text().trimmed());
    appendLogMessage(timestamped(QStringLiteral("settings loaded")));
}

void PuzzleRunnerWindow::loadDatabase()
{
    if (ensureDatabaseReady()) {
        refreshRecentRuns();
    }
}

QString PuzzleRunnerWindow::defaultDatabasePath() const
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return baseDir + QStringLiteral("/parlawl.sqlite3");
}

bool PuzzleRunnerWindow::ensureDatabaseReady()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return false;
    }
    const QString requestedPath = m_databasePathEdit->text().trimmed();
    if (requestedPath.isEmpty()) {
        appendLogMessage(timestamped(QStringLiteral("database path is empty")));
        return false;
    }

    const QString absoluteRequestedPath = QFileInfo(requestedPath).absoluteFilePath();
    const bool databaseMatches = m_databaseManager->database().isOpen()
        && QFileInfo(m_databaseManager->databasePath()).absoluteFilePath() == absoluteRequestedPath;
    if (m_sessionController.hasTrackedPuzzleAttempt() && !databaseMatches) {
        appendLogMessage(timestamped(QStringLiteral(
            "database change blocked until the active solve attempt is finished or abandoned")));
        return false;
    }
    if (databaseMatches) {
        QString ledgerError;
        if (!installPuzzleAttemptRepository(&ledgerError)) {
            appendLogMessage(timestamped(QStringLiteral("solve-history ledger unavailable: %1").arg(ledgerError)));
            return false;
        }
        return true;
    }

    auto result = m_databaseManager->initialize(requestedPath);
    if (!result.ok) {
        const QString fallbackPath = QDir::tempPath() + QStringLiteral("/parlawl/parlawl.sqlite3");
        appendLogMessage(timestamped(QStringLiteral("primary database init failed; falling back to %1").arg(fallbackPath)));
        m_databasePathEdit->setText(fallbackPath);
        persistSettings();
        result = m_databaseManager->initialize(fallbackPath);
    }

    appendLogMessage(timestamped(result.message));
    if (!result.ok) {
        return false;
    }
    QString ledgerError;
    if (!installPuzzleAttemptRepository(&ledgerError)) {
        appendLogMessage(timestamped(QStringLiteral("solve-history ledger unavailable: %1").arg(ledgerError)));
        return false;
    }
    return true;
}

bool PuzzleRunnerWindow::installPuzzleAttemptRepository(QString *errorMessage)
{
    if (!m_databaseManager->database().isOpen()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("database is not open");
        }
        return false;
    }
    const QString databasePath = QFileInfo(m_databaseManager->databasePath()).absoluteFilePath();
    if (m_puzzleAttemptRepository && m_attemptRepositoryDatabasePath == databasePath) {
        return true;
    }
    if (m_sessionController.hasTrackedPuzzleAttempt()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("an active solve attempt still belongs to the previous ledger");
        }
        return false;
    }
    auto replacement = std::make_unique<PuzzleAttemptRepository>(m_databaseManager->database());
    PuzzleAttemptRepository *replacementPointer = replacement.get();
    m_sessionController.configurePuzzleAttemptLedger(
        replacementPointer,
        m_attemptSolverId,
        m_attemptSessionId);
    m_puzzleAttemptRepository = std::move(replacement);
    m_attemptRepositoryDatabasePath = databasePath;
    return true;
}

bool PuzzleRunnerWindow::validateAnalyzeSettings(bool requireLichessToken, QString *message) const
{
    const QString token = m_lichessTokenEdit->text().trimmed();
    if (requireLichessToken && token.isEmpty()) {
        *message = QStringLiteral("lichess api token is required before analysis can run");
        return false;
    }

    const QString stockfishPath = m_stockfishPathEdit->text().trimmed();
    QFileInfo stockfishInfo(stockfishPath);
    if (stockfishPath.isEmpty() || !stockfishInfo.exists() || !stockfishInfo.isExecutable()) {
        *message = QStringLiteral("valid stockfish path is required before engine-backed analysis can run");
        return false;
    }

    const QString workerPath = m_pythonWorkerPathEdit->text().trimmed();
    if (!workerPath.isEmpty()) {
        QFileInfo workerInfo(workerPath);
        if (!workerInfo.exists() || !workerInfo.isExecutable()) {
            *message = QStringLiteral("python worker path must point to an executable interpreter or be left empty for fallback lookup");
            return false;
        }
    }

    return true;
}

bool PuzzleRunnerWindow::usingLivePuzzleSupply() const
{
    return !m_lichessTokenEdit->text().trimmed().isEmpty();
}

QString PuzzleRunnerWindow::currentSupplyStatusText() const
{
    if (m_validatedPuzzlePackActive) {
        return QStringLiteral(
                   "%1 imported engine line(s) active. Their records declare engine_validated; ParlAWL did not authenticate the producer or rerun the engine. Remote top-up is off; Reload puzzles replaces this queue with Lichess.")
            .arg(m_validatedPuzzlePackCount);
    }
    const QString difficulty = m_sessionController.settings().difficulty;
    const bool hasToken = usingLivePuzzleSupply();
    const bool hasCachedBatch = m_puzzleSupplyCoordinator->hasCachedBatch(difficulty);
    const bool cooldownActive = m_puzzleSupplyCoordinator->isCooldownActive();

    if (m_liveSupplyActive) {
        if (cooldownActive) {
            return QStringLiteral("Lichess rate-limited; using cached live puzzles for about %1 more seconds.")
                .arg(secondsRemainingText(m_puzzleSupplyCoordinator->cooldownUntilUtc()));
        }
        if (!hasToken && hasCachedBatch) {
            return QStringLiteral("Using restored cached live puzzles from a previous session. Add a token to reload from Lichess.");
        }
        return QStringLiteral("Live Lichess supply ready. Reload replaces the queue; refill uses remote top-up.");
    }

    if (cooldownActive) {
        if (hasCachedBatch) {
            return QStringLiteral("Lichess rate-limited. Cached live puzzles are available for about %1 more seconds, but no live queue is active.")
                .arg(secondsRemainingText(m_puzzleSupplyCoordinator->cooldownUntilUtc()));
        }
        return QStringLiteral("Lichess rate-limited and no cached live queue is available. Retry in about %1 seconds.")
            .arg(secondsRemainingText(m_puzzleSupplyCoordinator->cooldownUntilUtc()));
    }

    if (hasToken) {
        if (hasCachedBatch) {
            return QStringLiteral("Cached live puzzles are available. Click Reload puzzles to restore or refresh the live queue.");
        }
        return QStringLiteral("Live supply available. Click Reload puzzles to load a Lichess batch.");
    }

    if (hasCachedBatch) {
        return QStringLiteral("Cached live puzzles are available from a previous session. Add a token to refresh from Lichess.");
    }

    return QStringLiteral("Live supply inactive. Add a Lichess token and click Reload puzzles.");
}

void PuzzleRunnerWindow::refreshSupplyStatus()
{
    if (m_settingsCard == nullptr) {
        return;
    }
    m_settingsCard->setSupplyStatusText(currentSupplyStatusText());
}

bool PuzzleRunnerWindow::reloadPuzzleSupply(bool append, QString *errorMessage)
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("live puzzle supply is disabled in annotated replay");
        }
        return false;
    }
    if (!usingLivePuzzleSupply()) {
        m_liveSupplyActive = false;
        refreshSupplyStatus();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("lichess api token is required before puzzles can be reloaded");
        }
        return false;
    }

    appendLogMessage(timestamped(QStringLiteral("live puzzle fetch requested: nb=%1 difficulty=%2")
                                     .arg(std::max(m_queueSizeSetting.toInt(), 1))
                                     .arg(m_sessionController.settings().difficulty)));
    const PuzzleSupplyBatchResponse response = m_puzzleSupplyCoordinator->requestBatch(
        m_lichessTokenEdit->text().trimmed(),
        std::max(m_queueSizeSetting.toInt(), 1),
        m_sessionController.settings().difficulty);
    if (!response.ok) {
        m_liveSupplyActive = false;
        refreshSupplyStatus();
        if (response.statusCode == 429) {
            appendLogMessage(timestamped(QStringLiteral("lichess rate-limited; cooldown active until %1")
                                             .arg(response.cooldownUntilUtc.toString(Qt::ISODateWithMs))));
        }
        if (errorMessage != nullptr) {
            *errorMessage = response.message;
        }
        return false;
    }

    if (append) {
        if (response.source != PuzzleSupplySource::Remote) {
            if (errorMessage != nullptr) {
                *errorMessage = response.message;
            }
            return false;
        }
        const bool ok = m_sessionController.appendPuzzles(response.puzzles, errorMessage);
        if (ok) {
            m_liveSupplyActive = true;
            m_validatedPuzzlePackActive = false;
            m_validatedPuzzlePackCount = 0;
            refreshSupplyStatus();
            appendLogMessage(timestamped(QStringLiteral("remote live fetch success: added %1 live puzzles to the queue")
                                             .arg(response.puzzles.size())));
        }
        return ok;
    }

    const bool ok = m_sessionController.replacePuzzles(response.puzzles, errorMessage);
    if (ok) {
        m_liveSupplyActive = true;
        m_validatedPuzzlePackActive = false;
        m_validatedPuzzlePackCount = 0;
        m_lastSupplyCheckSlot = -1;
        refreshSupplyStatus();
        if (response.source == PuzzleSupplySource::Cache) {
            appendLogMessage(timestamped(response.message));
        } else {
            appendLogMessage(timestamped(QStringLiteral("remote live fetch success: loaded %1 live puzzles from lichess")
                                             .arg(response.puzzles.size())));
        }
    }
    return ok;
}

bool PuzzleRunnerWindow::restoreCachedPuzzleSupply(QString *errorMessage)
{
    const PuzzleSupplyBatchResponse response = m_puzzleSupplyCoordinator->restoreCachedBatch(
        std::max(m_queueSizeSetting.toInt(), 1),
        m_sessionController.settings().difficulty);
    if (!response.ok) {
        if (errorMessage != nullptr) {
            *errorMessage = response.message;
        }
        return false;
    }

    const bool ok = m_sessionController.replacePuzzles(response.puzzles, errorMessage);
    if (!ok) {
        m_liveSupplyActive = false;
        refreshSupplyStatus();
        return false;
    }

    m_liveSupplyActive = true;
    m_validatedPuzzlePackActive = false;
    m_validatedPuzzlePackCount = 0;
    m_lastSupplyCheckSlot = -1;
    refreshSupplyStatus();
    appendLogMessage(timestamped(response.message));
    appendLogMessage(timestamped(QStringLiteral("restored %1 cached live puzzles from the previous session")
                                     .arg(response.puzzles.size())));
    return true;
}

void PuzzleRunnerWindow::maybeTopUpPuzzleSupply()
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    if (m_liveSupplyReloadInProgress || !m_sessionController.shouldFetchMorePuzzles()) {
        return;
    }
    if (!m_liveSupplyActive || !usingLivePuzzleSupply()) {
        return;
    }
    if (m_puzzleSupplyCoordinator->isCooldownActive()) {
        return;
    }

    m_liveSupplyReloadInProgress = true;
    QString errorMessage;
    const bool ok = reloadPuzzleSupply(true, &errorMessage);
    m_liveSupplyReloadInProgress = false;
    if (!ok && !errorMessage.isEmpty()) {
        appendLogMessage(timestamped(QStringLiteral("live puzzle top-up skipped: %1").arg(errorMessage)));
    }
}

void PuzzleRunnerWindow::setAnalysisInProgress(bool inProgress)
{
    m_analysisInProgress = inProgress;
    const bool puzzleWorkspace = m_workspaceMode == WorkspaceMode::Puzzle;
    m_analyzeButton->setEnabled(puzzleWorkspace && !inProgress && m_sessionController.canAnalyzeCurrentPuzzle());
    m_cancelButton->setEnabled(puzzleWorkspace && inProgress);
    m_lichessTokenEdit->setEnabled(puzzleWorkspace && !inProgress);
    m_stockfishPathEdit->setEnabled(puzzleWorkspace && !inProgress);
    m_pythonWorkerPathEdit->setEnabled(puzzleWorkspace && !inProgress);
    m_databasePathEdit->setEnabled(puzzleWorkspace && !inProgress);
    if (inProgress || !puzzleWorkspace) {
        m_exportButton->setEnabled(false);
        m_exportAssistantPacketButton->setEnabled(false);
        m_importAssistantInferenceButton->setEnabled(false);
    } else if (m_recentRunsList->currentItem() != nullptr) {
        const QString status = m_recentRunsList->currentItem()->data(Qt::UserRole + 1).toString();
        m_exportButton->setEnabled(status == QStringLiteral("completed"));
        m_exportAssistantPacketButton->setEnabled(status == QStringLiteral("completed"));
        m_importAssistantInferenceButton->setEnabled(status == QStringLiteral("completed"));
    }
}

void PuzzleRunnerWindow::refreshRecentRuns(const QString &preferredRunId)
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    if (!m_databaseManager->database().isOpen()) {
        return;
    }

    AnalysisRepository repository(m_databaseManager->database());
    QString errorMessage;
    const QList<AnalysisRun> runs = repository.listRecentRuns(20, &errorMessage);
    if (!errorMessage.isEmpty()) {
        appendLogMessage(timestamped(errorMessage));
        return;
    }

    m_recentRunsList->clear();
    if (runs.isEmpty()) {
        m_recentRunsList->addItem(QStringLiteral("no runs yet"));
        m_exportButton->setEnabled(false);
        m_exportAssistantPacketButton->setEnabled(false);
        m_importAssistantInferenceButton->setEnabled(false);
        return;
    }

    int preferredIndex = -1;
    for (int i = 0; i < runs.size(); ++i) {
        const AnalysisRun &run = runs.at(i);
        const QString detail = run.status == QStringLiteral("completed")
                                   ? QStringLiteral("run=%1").arg(run.runId)
                                   : run.errorMessage.isEmpty()
                                         ? QStringLiteral("run=%1").arg(run.runId)
                                         : run.errorMessage;
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  %2  %3")
                .arg(run.createdAtUtc.toUTC().toString(Qt::ISODateWithMs), run.status, detail));
        item->setData(Qt::UserRole, run.runId);
        item->setData(Qt::UserRole + 1, run.status);
        item->setData(Qt::UserRole + 2, run.errorMessage);
        m_recentRunsList->addItem(item);
        if (!preferredRunId.isEmpty() && run.runId == preferredRunId) {
            preferredIndex = i;
        }
    }

    if (preferredIndex >= 0) {
        m_recentRunsList->setCurrentRow(preferredIndex);
    } else if (m_recentRunsList->count() > 0) {
        m_recentRunsList->setCurrentRow(0);
    }
}

void PuzzleRunnerWindow::showRunSummary(const AnalysisRun &run)
{
    m_statusStateLabel->setText(run.status);
    const QString detail = run.errorMessage.isEmpty()
                               ? QStringLiteral("run %1").arg(run.runId)
                               : QStringLiteral("run %1 | %2").arg(run.runId, run.errorMessage);
    m_statusDetailLabel->setText(detail);
    m_reportView->setPlainText(
        QStringLiteral("analysis %1\nrun: %2\npuzzle: %3\ncreated: %4\ncompleted: %5\nerror: %6")
            .arg(run.status,
                 run.runId,
                 run.puzzleId,
                 isoOrEmpty(run.createdAtUtc),
                 isoOrEmpty(run.completedAtUtc),
                 run.errorMessage.isEmpty() ? QStringLiteral("none") : run.errorMessage));
}

void PuzzleRunnerWindow::resetPuzzleScopedUiState(const QString &puzzleId)
{
    m_activePuzzleId = puzzleId;
    m_hasLoadedReport = false;
    m_loadedReport = {};
    m_selectedRunId.clear();
    m_lastReviewedFen.clear();

    if (m_recentRunsList != nullptr) {
        const QSignalBlocker blocker(m_recentRunsList);
        m_recentRunsList->clearSelection();
        m_recentRunsList->setCurrentItem(nullptr);
    }

    if (m_reportView != nullptr) {
        m_reportView->setPlainText(QStringLiteral("Run Analyze to generate a report for this puzzle."));
    }
    if (m_statusStateLabel != nullptr) {
        m_statusStateLabel->setText(QStringLiteral("ready"));
    }
    if (m_statusDetailLabel != nullptr) {
        m_statusDetailLabel->setText(QStringLiteral("no analysis selected for the current puzzle"));
    }
    if (m_exportButton != nullptr) {
        m_exportButton->setEnabled(false);
    }
    if (m_exportAssistantPacketButton != nullptr) {
        m_exportAssistantPacketButton->setEnabled(false);
    }
    if (m_importAssistantInferenceButton != nullptr) {
        m_importAssistantInferenceButton->setEnabled(false);
    }
    if (m_stockfishReviewController != nullptr) {
        m_stockfishReviewController->resetCurrentReview(QStringLiteral("review the current puzzle position"));
    }
}

void PuzzleRunnerWindow::maybeRefreshEngineReview(bool forceRefresh)
{
    if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
        return;
    }
    const GameStateStore *store = m_sessionController.gameStateStore();
    if (store == nullptr || !store->hasPosition()) {
        return;
    }

    const QString fen = store->currentPosition().toFen();
    if (!forceRefresh && !m_stockfishReviewController->autoRefreshEnabled()) {
        if (m_lastReviewedFen.isEmpty()) {
            m_lastReviewedFen = fen;
        }
        return;
    }

    if (!forceRefresh && fen == m_lastReviewedFen) {
        return;
    }

    m_lastReviewedFen = fen;
    m_stockfishReviewController->requestReview(fen, forceRefresh);
}

void PuzzleRunnerWindow::setAnnotatedReplayWorkspaceUi(bool enabled)
{
    if (m_rightTabs == nullptr || m_infoTabs == nullptr || m_settingsPage == nullptr) {
        return;
    }

    if (enabled && !m_replayWorkspaceUiActive) {
        m_preReplayInfoTabIndex = m_infoTabs->currentIndex();
    }
    m_replayWorkspaceUiActive = enabled;
    const bool gameBreakdown = enabled && m_annotatedReplayPack.has_value()
        && m_annotatedReplayPack->isMechanicalGameBreakdown();

    if (gameBreakdown) {
        const auto &pack = *m_annotatedReplayPack;
        const QString date = pack.eventStartUtc().size() >= 10
            ? pack.eventStartUtc().left(10) : pack.eventStartUtc();
        setWindowTitle(
            QStringLiteral("%1 %2 · %3 · %4 %5 · %6")
                .arg(pack.whiteUsername())
                .arg(pack.whiteRating())
                .arg(pack.result())
                .arg(pack.blackUsername())
                .arg(pack.blackRating())
                .arg(date));
    } else {
        setWindowTitle(QStringLiteral("parlawl"));
    }

    for (int index = 0; index < m_rightTabs->count(); ++index) {
        QWidget *page = m_rightTabs->widget(index);
        const bool replayRelevant = gameBreakdown
            ? page == m_gameReviewPanel
            : page == m_moveListPanel || page == m_replayEvidencePanel;
        const bool available = !enabled || replayRelevant;
        m_rightTabs->setTabVisible(index, available);
        m_rightTabs->setTabEnabled(index, available);
    }
    if (enabled) {
        m_rightTabs->setCurrentWidget(
            gameBreakdown ? static_cast<QWidget *>(m_gameReviewPanel)
                          : static_cast<QWidget *>(m_replayEvidencePanel));
    } else {
        m_rightTabs->setCurrentWidget(m_moveListPanel);
    }
    m_rightTabs->tabBar()->setVisible(!gameBreakdown);
    m_rightTabs->setStyleSheet(
        gameBreakdown ? QStringLiteral("QTabWidget::pane { border: 0; }") : QString());

    for (int index = 0; index < m_infoTabs->count(); ++index) {
        const bool replayControls = m_infoTabs->widget(index) == m_settingsPage;
        const bool available = !enabled || replayControls;
        m_infoTabs->setTabVisible(index, available);
        m_infoTabs->setTabEnabled(index, available);
        if (replayControls) {
            m_infoTabs->setTabText(
                index,
                enabled ? QStringLiteral("Replay Controls") : QStringLiteral("Settings"));
        }
    }
    if (enabled) {
        m_infoTabs->setCurrentWidget(m_settingsPage);
    } else if (m_preReplayInfoTabIndex >= 0 && m_preReplayInfoTabIndex < m_infoTabs->count()) {
        m_infoTabs->setCurrentIndex(m_preReplayInfoTabIndex);
    }
    m_infoTabs->tabBar()->setVisible(!enabled);
    m_infoTabs->setStyleSheet(
        gameBreakdown ? QStringLiteral("QTabWidget::pane { border: 0; }") : QString());
    m_infoTabs->setMaximumHeight(enabled ? 112 : QWIDGETSIZE_MAX);
    m_evaluationBarWidget->setVisible(!gameBreakdown);
    m_boardWidget->setMinimumSize(
        gameBreakdown ? QSize(560, 560) : QSize(360, 360));
    m_rightTabs->setMaximumWidth(gameBreakdown ? 500 : QWIDGETSIZE_MAX);

    m_settingsCard->setVisible(!enabled);
    m_settingsCard->setEnabled(!enabled);
    for (QPushButton *button : {m_hintButton, m_solutionButton, m_analyzeButton, m_cancelButton}) {
        button->setVisible(!enabled);
        if (enabled) {
            button->setEnabled(false);
        }
    }
    m_transportControls->setReplayMode(enabled);
    m_enginePanel->setEnabled(!enabled);
    m_recentRunsList->setEnabled(!enabled);

    const bool configEnabled = !enabled && !m_analysisInProgress;
    for (QLineEdit *edit : {m_lichessTokenEdit, m_stockfishPathEdit, m_pythonWorkerPathEdit, m_databasePathEdit}) {
        edit->setEnabled(configEnabled);
    }
}

void PuzzleRunnerWindow::buildUi()
{
    setWindowTitle(QStringLiteral("parlawl"));
    resize(1280, 860);

    auto *central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(12);

    auto *leftColumn = new QWidget(central);
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    auto *boardRow = new QWidget(leftColumn);
    auto *boardRowLayout = new QHBoxLayout(boardRow);
    boardRowLayout->setContentsMargins(0, 0, 0, 0);
    boardRowLayout->setSpacing(8);
    m_evaluationBarWidget = new EvaluationBarWidget(boardRow);
    m_boardWidget = new BoardWidget(leftColumn);
    m_boardWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    boardRowLayout->addWidget(m_evaluationBarWidget);
    boardRowLayout->addWidget(m_boardWidget, 1);

    auto *rightColumn = new QWidget(central);
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    m_moveListPanel = new MoveListPanel(rightColumn);
    m_playerStatisticsPanel = new PlayerStatisticsPanel(rightColumn);
    m_rightTabs = new QTabWidget(rightColumn);
    m_rightTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *reportPage = new QWidget(m_rightTabs);
    auto *reportLayout = new QVBoxLayout(reportPage);
    reportLayout->setContentsMargins(0, 0, 0, 0);
    auto *reportActionsRow = new QHBoxLayout();
    reportActionsRow->setContentsMargins(0, 0, 0, 0);
    reportActionsRow->setSpacing(6);
    m_exportButton = new QPushButton(QStringLiteral("Export JSON"), reportPage);
    m_exportAssistantPacketButton = new QPushButton(QStringLiteral("Export Assistant Packet"), reportPage);
    auto applyCompactButtonStyle = [](QPushButton *button) {
        button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        button->setMaximumHeight(20);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { padding: 0px 6px; border-radius: 8px; min-height: 16px; }"
        ));
    };
    applyCompactButtonStyle(m_exportButton);
    applyCompactButtonStyle(m_exportAssistantPacketButton);
    reportActionsRow->addWidget(m_exportButton);
    reportActionsRow->addWidget(m_exportAssistantPacketButton);
    reportActionsRow->addStretch(1);
    m_reportView = new QTextEdit(reportPage);
    m_reportView->setReadOnly(true);
    m_reportView->setPlainText(QStringLiteral("report output will appear here after a completed analysis run."));
    reportLayout->addLayout(reportActionsRow);
    reportLayout->addWidget(m_reportView);

    auto *logPage = new QWidget(m_rightTabs);
    auto *logLayout = new QVBoxLayout(logPage);
    logLayout->setContentsMargins(0, 0, 0, 0);
    auto *statusBox = new QGroupBox(QStringLiteral("analysis status"), logPage);
    statusBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *statusLayout = new QVBoxLayout(statusBox);
    m_statusStateLabel = new QLabel(QStringLiteral("idle"), statusBox);
    m_statusDetailLabel = new QLabel(QStringLiteral("ready"), statusBox);
    m_statusDetailLabel->setWordWrap(true);
    statusLayout->addWidget(m_statusStateLabel);
    statusLayout->addWidget(m_statusDetailLabel);
    m_logView = new QTextEdit(logPage);
    m_logView->setReadOnly(true);
    logLayout->addWidget(statusBox);
    logLayout->addWidget(m_logView);

    auto *recentRunsPage = new QWidget(m_rightTabs);
    auto *recentRunsLayout = new QVBoxLayout(recentRunsPage);
    recentRunsLayout->setContentsMargins(0, 0, 0, 0);
    auto *recentRunActionsRow = new QHBoxLayout();
    recentRunActionsRow->setContentsMargins(0, 0, 0, 0);
    recentRunActionsRow->setSpacing(6);
    m_importAssistantInferenceButton = new QPushButton(QStringLiteral("Import Assistant Inference"), recentRunsPage);
    applyCompactButtonStyle(m_importAssistantInferenceButton);
    recentRunActionsRow->addWidget(m_importAssistantInferenceButton);
    recentRunActionsRow->addStretch(1);
    m_recentRunsList = new QListWidget(recentRunsPage);
    m_recentRunsList->addItem(QStringLiteral("no runs yet"));
    recentRunsLayout->addLayout(recentRunActionsRow);
    recentRunsLayout->addWidget(m_recentRunsList);

    m_replayEvidencePanel = new ReplayEvidencePanel(m_rightTabs);
    m_gameReviewPanel = new GameReviewPanel(m_rightTabs);
    m_rightTabs->addTab(m_moveListPanel, QStringLiteral("Move List"));
    m_rightTabs->addTab(m_replayEvidencePanel, QStringLiteral("Analysis Replay"));
    m_rightTabs->addTab(m_gameReviewPanel, QStringLiteral("Game Review"));
    m_rightTabs->addTab(m_playerStatisticsPanel, QStringLiteral("Player Stats"));
    m_rightTabs->addTab(reportPage, QStringLiteral("Report View"));
    m_rightTabs->addTab(logPage, QStringLiteral("Status / Log"));
    m_rightTabs->addTab(recentRunsPage, QStringLiteral("Recent Runs"));

    auto *runnerActionsBox = new QGroupBox(leftColumn);
    runnerActionsBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *runnerActionsLayout = new QVBoxLayout(runnerActionsBox);
    runnerActionsLayout->setContentsMargins(8, 8, 8, 8);
    runnerActionsLayout->setSpacing(6);
    m_hintButton = new QPushButton(QStringLiteral("Hint"), runnerActionsBox);
    m_solutionButton = new QPushButton(QStringLiteral("Solution"), runnerActionsBox);
    m_analyzeButton = new QPushButton(QStringLiteral("Analyze"), runnerActionsBox);
    applyCompactButtonStyle(m_hintButton);
    applyCompactButtonStyle(m_solutionButton);
    applyCompactButtonStyle(m_analyzeButton);
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), runnerActionsBox);
    applyCompactButtonStyle(m_cancelButton);
    auto *runnerButtonRow = new QHBoxLayout();
    runnerButtonRow->setContentsMargins(0, 0, 0, 0);
    runnerButtonRow->setSpacing(6);
    runnerButtonRow->addWidget(m_hintButton);
    runnerButtonRow->addWidget(m_solutionButton);
    m_transportControls = new TransportControls(runnerActionsBox);
    for (QPushButton *button : m_transportControls->findChildren<QPushButton *>()) {
        applyCompactButtonStyle(button);
    }
    runnerButtonRow->addWidget(m_transportControls);
    runnerButtonRow->addStretch(1);
    runnerActionsLayout->addLayout(runnerButtonRow);
    auto *analyzeRow = new QHBoxLayout();
    analyzeRow->setContentsMargins(0, 0, 0, 0);
    analyzeRow->setSpacing(6);
    analyzeRow->addWidget(m_analyzeButton);
    analyzeRow->addWidget(m_cancelButton);
    analyzeRow->addStretch(1);
    runnerActionsLayout->addLayout(analyzeRow);

    m_infoTabs = new QTabWidget(leftColumn);
    m_infoTabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *puzzleInfoPage = new QScrollArea(m_infoTabs);
    puzzleInfoPage->setWidgetResizable(true);
    puzzleInfoPage->setFrameShape(QFrame::NoFrame);
    m_metadataCard = new MetadataCard(puzzleInfoPage);
    puzzleInfoPage->setWidget(m_metadataCard);

    auto *settingsPage = new QScrollArea(m_infoTabs);
    m_settingsPage = settingsPage;
    settingsPage->setWidgetResizable(true);
    settingsPage->setFrameShape(QFrame::NoFrame);
    auto *settingsPageContent = new QWidget(settingsPage);
    auto *settingsPageLayout = new QVBoxLayout(settingsPageContent);
    settingsPageLayout->setContentsMargins(0, 0, 0, 0);
    settingsPageLayout->setSpacing(8);
    m_settingsCard = new SettingsCard(settingsPageContent);
    runnerActionsBox->setParent(settingsPageContent);
    settingsPageLayout->addWidget(runnerActionsBox);
    settingsPageLayout->addWidget(m_settingsCard);
    settingsPageLayout->addStretch(1);
    settingsPage->setWidget(settingsPageContent);

    m_enginePanel = new EnginePanel(m_infoTabs);
    m_infoTabs->addTab(puzzleInfoPage, QStringLiteral("Puzzle Info"));
    m_infoTabs->addTab(settingsPage, QStringLiteral("Settings"));
    m_infoTabs->addTab(m_enginePanel, QStringLiteral("Engine Review"));

    auto *appConfigBox = new QGroupBox(leftColumn);
    appConfigBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *appConfigLayout = new QFormLayout(appConfigBox);
    m_lichessTokenEdit = new QLineEdit(appConfigBox);
    m_lichessTokenEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_stockfishPathEdit = new QLineEdit(appConfigBox);
    m_pythonWorkerPathEdit = new QLineEdit(appConfigBox);
    m_databasePathEdit = new QLineEdit(appConfigBox);
    appConfigLayout->addRow(QStringLiteral("lichess api token"), m_lichessTokenEdit);
    appConfigLayout->addRow(QStringLiteral("stockfish path"), m_stockfishPathEdit);
    appConfigLayout->addRow(QStringLiteral("python worker path"), m_pythonWorkerPathEdit);
    appConfigLayout->addRow(QStringLiteral("database path"), m_databasePathEdit);

    m_infoTabs->addTab(appConfigBox, QStringLiteral("Analysis Config"));

    leftLayout->addWidget(boardRow, 4);
    leftLayout->addWidget(m_infoTabs, 2);

    leftColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    leftColumn->setMinimumWidth(360);
    rightColumn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    rightColumn->setMinimumWidth(400);
    rightLayout->addWidget(m_rightTabs, 1);
    rootLayout->addWidget(leftColumn, 5);
    rootLayout->addWidget(rightColumn, 3);

    setCentralWidget(central);

    connect(m_boardWidget, &BoardWidget::squareClicked, this, &PuzzleRunnerWindow::onBoardSquareClicked);
    connect(m_boardWidget, &BoardWidget::scrubRequested, this, [this](int stepDelta) {
        if (stepDelta == 0) {
            return;
        }
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            m_replayPlaybackTimer->stop();
            m_transportControls->setPlaying(false);
            bool changed = false;
            if (stepDelta < 0) {
                for (int i = 0; i < -stepDelta; ++i) {
                    changed = m_replaySession.stepBackward() || changed;
                }
            } else {
                for (int i = 0; i < stepDelta; ++i) {
                    changed = m_replaySession.stepForward() || changed;
                }
            }
            if (changed) {
                refreshReplayUi();
            }
            return;
        }
        if (stepDelta < 0) {
            for (int i = 0; i < -stepDelta; ++i) {
                m_sessionController.stepBackward();
            }
            return;
        }
        for (int i = 0; i < stepDelta; ++i) {
            m_sessionController.stepForward();
        }
    });
    connect(m_cancelButton, &QPushButton::clicked, this, &PuzzleRunnerWindow::onCancelAnalysisRequested);
    connect(m_hintButton, &QPushButton::clicked, this, &PuzzleRunnerWindow::onHintRequested);
    connect(m_solutionButton, &QPushButton::clicked, this, &PuzzleRunnerWindow::onSolutionRequested);
    connect(m_analyzeButton, &QPushButton::clicked, this, &PuzzleRunnerWindow::onAnalyzeCurrentPuzzleRequested);
    connect(m_exportButton, &QPushButton::clicked, this, &PuzzleRunnerWindow::onExportJsonRequested);
    connect(m_exportAssistantPacketButton, &QPushButton::clicked, this, &PuzzleRunnerWindow::onExportAssistantPacketRequested);
    connect(m_importAssistantInferenceButton, &QPushButton::clicked, this, &PuzzleRunnerWindow::onImportAssistantInferenceRequested);
    connect(m_replayEvidencePanel, &ReplayEvidencePanel::openReplayRequested, this, &PuzzleRunnerWindow::onOpenAnnotatedReplayRequested);
    connect(m_playerStatisticsPanel, &PlayerStatisticsPanel::openSnapshotRequested, this, &PuzzleRunnerWindow::onOpenPlayerStatisticsRequested);
    connect(m_playerStatisticsPanel, &PlayerStatisticsPanel::openBoardStructureSnapshotRequested, this, &PuzzleRunnerWindow::onOpenBoardStructureStatisticsRequested);
    connect(m_playerStatisticsPanel, &PlayerStatisticsPanel::gameBreakdownRequested, this, &PuzzleRunnerWindow::onPlayerGameBreakdownRequested);
    connect(m_replayEvidencePanel, &ReplayEvidencePanel::backToPuzzlesRequested, this, &PuzzleRunnerWindow::onBackToPuzzlesRequested);
    connect(m_replayEvidencePanel, &ReplayEvidencePanel::showEngineLineRequested, this, &PuzzleRunnerWindow::onShowReplayVariationRequested);
    connect(m_replayEvidencePanel, &ReplayEvidencePanel::returnToGameRequested, this, &PuzzleRunnerWindow::onReturnFromReplayVariationRequested);
    connect(m_moveListPanel, &MoveListPanel::replayPlyRequested, this, &PuzzleRunnerWindow::onReplayPlyRequested);
    connect(m_gameReviewPanel, &GameReviewPanel::replayPlyRequested, this, &PuzzleRunnerWindow::onReplayPlyRequested);
    connect(m_gameReviewPanel, &GameReviewPanel::backToPlayerStatisticsRequested, this, &PuzzleRunnerWindow::onBackToPuzzlesRequested);
    connect(m_transportControls, &TransportControls::previousRequested, this, [this]() {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            m_replayPlaybackTimer->stop();
            m_transportControls->setPlaying(false);
            if (m_replaySession.stepBackward()) {
                refreshReplayUi();
            }
            return;
        }
        const GameStateStore *store = m_sessionController.gameStateStore();
        if (store != nullptr && !store->isViewingLatest()) {
            m_sessionController.stepBackward();
            return;
        }
        m_sessionController.previousPuzzle();
    });
    connect(m_transportControls, &TransportControls::nextRequested, this, [this]() {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            m_replayPlaybackTimer->stop();
            m_transportControls->setPlaying(false);
            if (m_replaySession.stepForward()) {
                refreshReplayUi();
            }
            return;
        }
        const GameStateStore *store = m_sessionController.gameStateStore();
        if (store != nullptr && !store->isViewingLatest()) {
            m_sessionController.stepForward();
            return;
        }
        m_sessionController.nextPuzzle();
    });
    connect(m_transportControls, &TransportControls::retryRequested, this, [this]() {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            if (m_replayPlaybackTimer->isActive()) {
                m_replayPlaybackTimer->stop();
                m_transportControls->setPlaying(false);
                return;
            }
            if (m_replaySession.inVariation()) {
                m_replaySession.exitVariation();
                m_replayVariationAnchorPly = 0;
            }
            if (m_annotatedReplayPack.has_value()
                && m_replaySession.currentMainlinePly()
                    >= m_annotatedReplayPack->moves().size()) {
                m_replaySession.seekMainlinePly(0);
            }
            m_replayPlaybackTimer->start();
            m_transportControls->setPlaying(true);
            refreshReplayUi();
            return;
        }
        m_sessionController.retryPuzzle();
    });
    m_replayPlaybackTimer->setInterval(650);
    connect(m_replayPlaybackTimer, &QTimer::timeout, this, [this]() {
        if (m_workspaceMode != WorkspaceMode::AnnotatedReplay
            || !m_annotatedReplayPack.has_value()
            || !m_replaySession.stepForward()) {
            m_replayPlaybackTimer->stop();
            m_transportControls->setPlaying(false);
            return;
        }
        refreshReplayUi();
        if (m_replaySession.currentMainlinePly()
            >= m_annotatedReplayPack->moves().size()) {
            m_replayPlaybackTimer->stop();
            m_transportControls->setPlaying(false);
        }
    });
    connect(m_settingsCard, &SettingsCard::autoAdvanceChanged, this, [this](bool enabled) {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            return;
        }
        m_sessionController.setAutoAdvance(enabled);
    });
    connect(m_settingsCard, &SettingsCard::difficultyChanged, this, [this](const QString &value) {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            return;
        }
        m_sessionController.setDifficulty(value);
        m_liveSupplyActive = false;
        appendLogMessage(timestamped(QStringLiteral("difficulty updated to %1; click Reload puzzles to apply it to the next batch").arg(value)));
        refreshUi();
        persistSettings();
    });
    connect(m_settingsCard, &SettingsCard::queueSizeChanged, this, [this](const QString &value) {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            return;
        }
        m_queueSizeSetting = value;
        m_sessionController.setQueueSize(value.toInt());
        m_liveSupplyActive = false;
        appendLogMessage(timestamped(QStringLiteral("queue size updated to %1; click Reload puzzles to apply it to the next batch").arg(value)));
        refreshUi();
        persistSettings();
    });
    connect(m_settingsCard, &SettingsCard::refillWhenLowChanged, this, [this](bool enabled) {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            return;
        }
        m_refillWhenLowSetting = enabled;
        m_sessionController.setRefillWhenLow(enabled);
        persistSettings();
    });
    connect(m_settingsCard, &SettingsCard::refillThresholdChanged, this, [this](const QString &value) {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            return;
        }
        m_refillThresholdSetting = value;
        m_sessionController.setRefillThreshold(value.toInt());
        persistSettings();
    });
    connect(m_settingsCard, &SettingsCard::keepRecentRunsChanged, this, [this](const QString &value) {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            return;
        }
        m_keepRecentRunsSetting = value;
        persistSettings();
    });
    connect(m_settingsCard, &SettingsCard::preserveAnalyzedChanged, this, [this](bool enabled) {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            return;
        }
        m_preserveAnalyzedSetting = enabled;
        persistSettings();
    });
    connect(m_settingsCard, &SettingsCard::cleanupRequested, this, &PuzzleRunnerWindow::onCleanupRequested);
    connect(m_settingsCard, &SettingsCard::reloadPuzzlesRequested, this, &PuzzleRunnerWindow::onReloadPuzzlesRequested);
    connect(m_settingsCard, &SettingsCard::openValidatedPuzzlePackRequested, this, &PuzzleRunnerWindow::onOpenValidatedPuzzlePackRequested);
    connect(m_settingsCard, &SettingsCard::exportSolveHistoryRequested, this, &PuzzleRunnerWindow::onExportSolveHistoryRequested);
    connect(m_enginePanel, &EnginePanel::refreshRequested, this, &PuzzleRunnerWindow::onEngineRefreshRequested);
    connect(m_enginePanel, &EnginePanel::autoRefreshChanged, this, &PuzzleRunnerWindow::onEngineAutoRefreshChanged);
    connect(m_recentRunsList, &QListWidget::currentItemChanged, this, &PuzzleRunnerWindow::onRecentRunSelected);

    m_cancelButton->setEnabled(false);
    m_cancelButton->setToolTip(QStringLiteral("cancel the active analysis run"));
    m_exportButton->setEnabled(false);
    m_exportButton->setToolTip(QStringLiteral("export the selected completed run as persisted json"));
    m_exportAssistantPacketButton->setEnabled(false);
    m_exportAssistantPacketButton->setToolTip(QStringLiteral("export a ChatGPT-ready markdown packet for the selected completed run"));
    m_importAssistantInferenceButton->setEnabled(false);
    m_importAssistantInferenceButton->setToolTip(QStringLiteral("import an external assistant inference artifact for the selected completed run"));

    for (QLineEdit *edit : {m_lichessTokenEdit, m_stockfishPathEdit, m_pythonWorkerPathEdit, m_databasePathEdit}) {
        connect(edit, &QLineEdit::editingFinished, this, &PuzzleRunnerWindow::persistSettings);
    }
    connect(m_lichessTokenEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            return;
        }
        refreshSupplyStatus();
    });
    connect(m_stockfishPathEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_workspaceMode == WorkspaceMode::AnnotatedReplay) {
            return;
        }
        m_stockfishReviewController->setEnginePath(text);
        maybeRefreshEngineReview(false);
    });
}

void PuzzleRunnerWindow::updateBoard()
{
    const GameStateStore *store = m_sessionController.gameStateStore();
    const QString sideLabel = store->currentPuzzle().analysisSeed.sideToMove.trimmed().toLower();
    const PieceColor viewColor = sideLabel == QStringLiteral("black")
        ? PieceColor::Black
        : PieceColor::White;
    QSet<int> legalTargets;
    for (const Move &move : store->legalMovesFromSquare(store->selectedSquare())) {
        legalTargets.insert(move.to);
    }

    const bool inputEnabled = m_sessionController.canSubmitMoves();
    const bool reviewMode = !store->isViewingLatest();
    m_boardWidget->setPosition(
        store->currentPosition(),
        viewColor,
        store->selectedSquare(),
        legalTargets,
        m_lastMoveSquares,
        m_sessionController.puzzleEngine().status(),
        reviewMode,
        inputEnabled);
}

void PuzzleRunnerWindow::refreshReplayUi()
{
    if (!m_annotatedReplayPack.has_value() || !m_replaySession.hasReplay()) {
        return;
    }
    updateReplayBoard();
    updateReplayPanels();
}

void PuzzleRunnerWindow::updateReplayBoard()
{
    if (!m_annotatedReplayPack.has_value() || !m_replaySession.hasReplay()) {
        return;
    }

    QString lastMoveUci;
    if (m_replaySession.inVariation()) {
        const auto *variation = m_annotatedReplayPack->preferredVariation(m_replayVariationAnchorPly);
        const int localPly = m_replaySession.currentVariationPly();
        if (variation != nullptr && localPly > 0 && localPly <= variation->displayedSteps.size()) {
            lastMoveUci = variation->displayedSteps.at(localPly - 1).uci;
        } else if (m_replayVariationAnchorPly > 1) {
            lastMoveUci = m_annotatedReplayPack->moves().at(m_replayVariationAnchorPly - 2).notation.uci;
        }
    } else if (m_replaySession.currentMainlinePly() > 0) {
        lastMoveUci = m_annotatedReplayPack->moves().at(m_replaySession.currentMainlinePly() - 1).notation.uci;
    }

    QPair<int, int> lastMoveSquares {-1, -1};
    const auto parsedMove = Move::fromUci(lastMoveUci);
    if (parsedMove.has_value()) {
        lastMoveSquares = {parsedMove->from, parsedMove->to};
    }
    const PieceColor viewColor = m_annotatedReplayPack->isMechanicalGameBreakdown()
            && m_annotatedReplayPack->viewedPlayerColor() == QStringLiteral("black")
        ? PieceColor::Black : PieceColor::White;
    m_boardWidget->setPosition(
        m_replaySession.currentPosition(),
        viewColor,
        -1,
        {},
        lastMoveSquares,
        SessionStatus::Ready,
        true,
        false);
    m_evaluationBarWidget->setExpectation(0.5, false);
}

void PuzzleRunnerWindow::updateReplayPanels()
{
    if (!m_annotatedReplayPack.has_value() || !m_replaySession.hasReplay()) {
        return;
    }
    const bool gameBreakdown = m_annotatedReplayPack->isMechanicalGameBreakdown();
    if (gameBreakdown) {
        m_gameReviewPanel->setReplayState(
            *m_annotatedReplayPack,
            m_replaySession,
            m_replayVariationAnchorPly);
    } else {
        m_moveListPanel->setAnnotatedReplay(
            *m_annotatedReplayPack,
            m_replaySession.currentMainlinePly(),
            m_replaySession.inVariation(),
            m_replayVariationAnchorPly);
        m_replayEvidencePanel->setReplayState(
            *m_annotatedReplayPack,
            m_replaySession,
            m_replayVariationAnchorPly);
    }

    bool canStepBackward = false;
    bool canStepForward = false;
    if (m_replaySession.inVariation()) {
        const auto *variation = m_annotatedReplayPack->preferredVariation(m_replayVariationAnchorPly);
        canStepBackward = m_replaySession.currentVariationPly() > 0;
        canStepForward = variation != nullptr
            && m_replaySession.currentVariationPly() < variation->displayedSteps.size();
    } else {
        canStepBackward = m_replaySession.currentMainlinePly() > 0;
        canStepForward = m_replaySession.currentMainlinePly() < m_annotatedReplayPack->moves().size();
    }
    m_transportControls->setReplayMode(true);
    m_transportControls->setEnabledState(true, canStepBackward, canStepForward, false, false);
    m_hintButton->setEnabled(false);
    m_solutionButton->setEnabled(false);
    m_analyzeButton->setEnabled(false);
    m_cancelButton->setEnabled(false);
    m_exportButton->setEnabled(false);
    m_exportAssistantPacketButton->setEnabled(false);
    m_importAssistantInferenceButton->setEnabled(false);
    m_enginePanel->setReviewState(
        gameBreakdown ? QStringLiteral("not included in this game explorer")
                      : QStringLiteral("disabled in annotated replay"),
        gameBreakdown
            ? QStringLiteral("Recorded moves and clocks are available. No engine process starts while browsing.")
            : QStringLiteral("Use the supplied-annotations panel; no fresh engine process is started."),
        QString(),
        QString(),
        false,
        false,
        false);
    m_statusStateLabel->setText(
        gameBreakdown ? QStringLiteral("read-only game breakdown")
                      : QStringLiteral("read-only replay"));
    m_statusDetailLabel->setText(m_annotatedReplayPack->replayId());
}

void PuzzleRunnerWindow::updatePanels()
{
    const GameStateStore *store = m_sessionController.gameStateStore();
    const PuzzleDefinition &puzzle = store->currentPuzzle();
    const SessionStatus status = m_sessionController.puzzleEngine().status();

    m_moveListPanel->setMoves(puzzle, store->moves(), store->currentViewIndex());
    m_metadataCard->setAwaitingAnalysis(
        puzzle,
        m_sessionController.currentPuzzleIndex(),
        m_sessionController.puzzleCount());
    if (m_hasLoadedReport
        && m_loadedReport.analysisRun.status == QStringLiteral("completed")
        && m_loadedReport.puzzleRound.puzzleId == puzzle.id) {
        m_metadataCard->setAnalysisSummary(
            PuzzleInfoSummaryBuilder::build(
                m_loadedReport.puzzleRound,
                m_loadedReport.sourceGame,
                m_loadedReport.tacticalEvent,
                m_loadedReport.criticalMoves));
    }
    m_settingsCard->setSettings(
        m_sessionController.settings().autoAdvance,
        m_sessionController.settings().difficulty);
    m_settingsCard->setSupplySettings(
        m_queueSizeSetting,
        m_refillWhenLowSetting,
        m_refillThresholdSetting);
    m_settingsCard->setAvailablePuzzleCount(m_sessionController.puzzleCount());
    refreshSupplyStatus();
    m_settingsCard->setRetentionSettings(
        m_keepRecentRunsSetting,
        m_preserveAnalyzedSetting);
    m_transportControls->setEnabledState(
        !store->isViewingLatest(),
        store->currentViewIndex() > 0,
        !store->isViewingLatest(),
        m_sessionController.canGoToPreviousPuzzle(),
        m_sessionController.canGoToNextPuzzle());
    const StockfishReviewSnapshot &snapshot = m_stockfishReviewController->snapshot();
    m_enginePanel->setReviewState(
        snapshot.statusText,
        snapshot.evaluationText,
        snapshot.bestMove,
        snapshot.pvLine,
        !m_stockfishPathEdit->text().trimmed().isEmpty(),
        m_stockfishReviewController->autoRefreshEnabled(),
        snapshot.inProgress);
    m_evaluationBarWidget->setExpectation(snapshot.whiteExpectation, snapshot.available);

    const bool canRequestHint = status == SessionStatus::Active && store->isViewingLatest();
    m_hintButton->setEnabled(canRequestHint);
    m_solutionButton->setEnabled(status == SessionStatus::Active || status == SessionStatus::Failed);
    m_analyzeButton->setEnabled(m_sessionController.canAnalyzeCurrentPuzzle());

}
