#include "analysis_orchestrator.h"

#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <QSqlDatabase>
#include <QUuid>

#include "analysis_repository.h"
#include "database_manager.h"
#include "lichess_client.h"
#include "parlawl_config.h"
#include "puzzle_round.h"
#include "report_formatter.h"
#include "source_game.h"
#include "worker_protocol.h"

namespace {

QString logLine(const QString &message)
{
    return QStringLiteral("%1  %2").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), message);
}

QString fallbackWorkerPython()
{
    QFileInfo configured(QString::fromUtf8(PARLAWL_DEFAULT_WORKER_PYTHON));
    return configured.exists() ? configured.absoluteFilePath() : QStringLiteral("python3");
}

} // namespace

AnalysisOrchestrator::AnalysisOrchestrator(QObject *parent)
    : QObject(parent)
    , m_cancelRequested(false)
{
}

void AnalysisOrchestrator::analyzeLatestSolvedPuzzle(
    const QString &lichessApiToken,
    const QString &workerPythonPath,
    const QString &stockfishPath,
    const QString &databasePath
)
{
    m_cancelRequested.store(false);
    emit logMessage(logLine(QStringLiteral("analyze latest solved puzzle requested")));
    emit analysisStarted();
    emit analysisProgress(QStringLiteral("preparing"), QStringLiteral("preparing analysis request"));
    runAnalyzeLatest(lichessApiToken, workerPythonPath, stockfishPath, databasePath);
}

void AnalysisOrchestrator::analyzeProvidedPuzzle(
    const PuzzleRound &puzzleRound,
    const SourceGame &sourceGame,
    const QString &workerPythonPath,
    const QString &stockfishPath,
    const QString &databasePath
)
{
    m_cancelRequested.store(false);
    emit logMessage(logLine(QStringLiteral("analyze provided puzzle requested for %1").arg(puzzleRound.puzzleId)));
    emit analysisStarted();
    emit analysisProgress(QStringLiteral("preparing"), QStringLiteral("preparing analysis request for puzzle %1").arg(puzzleRound.puzzleId));
    runAnalyzePrepared(puzzleRound, sourceGame, workerPythonPath, stockfishPath, databasePath);
}

void AnalysisOrchestrator::requestCancel()
{
    m_cancelRequested.store(true);
    emit logMessage(logLine(QStringLiteral("analysis cancellation requested")));
    emit analysisProgress(QStringLiteral("cancelling"), QStringLiteral("cancellation requested"));
}

void AnalysisOrchestrator::batchSyncOrRerun(const QString &workerPythonPath, const QString &databasePath)
{
    Q_UNUSED(workerPythonPath)
    Q_UNUSED(databasePath)
    emit analysisFailed(QStringLiteral("batch_sync"), QStringLiteral("batch sync / re-run remains out of scope for this slice"));
}

void AnalysisOrchestrator::exportJson(const QString &databasePath)
{
    Q_UNUSED(databasePath)
    emit analysisFailed(QStringLiteral("export_json"), QStringLiteral("export json remains out of scope for this slice"));
}

void AnalysisOrchestrator::runAnalyzeLatest(
    const QString &lichessApiToken,
    const QString &workerPythonPath,
    const QString &stockfishPath,
    const QString &databasePath
)
{
    auto failCancelled = [this](const QString &stage) {
        emit analysisFailed(stage, QStringLiteral("analysis cancelled"));
    };

    LichessClient lichessClient;

    emit logMessage(logLine(QStringLiteral("fetching latest solved puzzle from lichess")));
    emit analysisProgress(QStringLiteral("fetching"), QStringLiteral("fetching latest solved puzzle from lichess"));
    const LichessFetchResult fetchResult = lichessClient.fetchLatestSolvedPuzzle(lichessApiToken);
    if (!fetchResult.ok) {
        if (isCancelRequested()) {
            failCancelled(QStringLiteral("cancelled"));
            return;
        }
        emit analysisFailed(
            fetchResult.failureStage.isEmpty() ? QStringLiteral("fetch_failure") : fetchResult.failureStage,
            fetchResult.errorMessage
        );
        return;
    }

    if (isCancelRequested()) {
        failCancelled(QStringLiteral("cancelled"));
        return;
    }

    runAnalyzePrepared(fetchResult.puzzleRound, fetchResult.sourceGame, workerPythonPath, stockfishPath, databasePath);
}

void AnalysisOrchestrator::runAnalyzePrepared(
    const PuzzleRound &puzzleRound,
    const SourceGame &sourceGame,
    const QString &workerPythonPath,
    const QString &stockfishPath,
    const QString &databasePath
)
{
    auto failCancelled = [this](const QString &stage) {
        emit analysisFailed(stage, QStringLiteral("analysis cancelled"));
    };

    const QString connectionName = QStringLiteral("parlawl-orchestrator-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    emit analysisProgress(QStringLiteral("database"), QStringLiteral("opening analysis database"));
    DatabaseManager databaseManager(nullptr, connectionName);
    const auto initResult = databaseManager.initialize(databasePath);
    if (!initResult.ok) {
        emit analysisFailed(QStringLiteral("database"), initResult.message);
        return;
    }
    QSqlDatabase database = databaseManager.database();

    if (isCancelRequested()) {
        failCancelled(QStringLiteral("cancelled"));
        return;
    }

    QFileInfo stockfishInfo(stockfishPath);
    if (stockfishPath.trimmed().isEmpty() || !stockfishInfo.exists() || !stockfishInfo.isExecutable()) {
        emit analysisFailed(QStringLiteral("engine_config"), QStringLiteral("valid stockfish path is required before engine-backed analysis can run"));
        return;
    }

    AnalysisRepository repository(database);
    QString errorMessage;
    emit analysisProgress(QStringLiteral("persisting_source"), QStringLiteral("saving puzzle and source game"));
    if (!repository.upsertPuzzleRound(puzzleRound, &errorMessage)) {
        emit analysisFailed(QStringLiteral("persistence_failure"), errorMessage);
        return;
    }
    if (!repository.upsertSourceGame(sourceGame, &errorMessage)) {
        emit analysisFailed(QStringLiteral("persistence_failure"), errorMessage);
        return;
    }

    const QString engineName = stockfishInfo.baseName();
    emit analysisProgress(QStringLiteral("creating_run"), QStringLiteral("creating analysis run"));
    AnalysisRun run = repository.createAnalysisRun(
        puzzleRound.puzzleId,
        QStringLiteral("stockfish_window_v0_1"),
        engineName,
        worker_protocol::kDefaultEngineDepth,
        &errorMessage
    );
    if (run.runId.isEmpty()) {
        emit analysisFailed(QStringLiteral("persistence_failure"), errorMessage);
        return;
    }

    emit logMessage(logLine(QStringLiteral("analysis run %1 created").arg(run.runId)));
    emit analysisProgress(QStringLiteral("launching_worker"), QStringLiteral("launching engine worker for run %1").arg(run.runId));

    if (isCancelRequested()) {
        repository.completeAnalysisRun(run.runId, QStringLiteral("cancelled"), QStringLiteral("analysis cancelled"));
        failCancelled(QStringLiteral("cancelled"));
        return;
    }

    const QString resolvedWorkerPython = workerPythonPath.isEmpty() ? fallbackWorkerPython() : workerPythonPath;
    const QString workerScript = QString::fromUtf8(PARLAWL_DEFAULT_WORKER_SCRIPT);
    QProcess process;
    process.start(resolvedWorkerPython, {workerScript});
    while (!process.waitForStarted(100)) {
        if (isCancelRequested()) {
            process.kill();
            process.waitForFinished(1000);
            repository.completeAnalysisRun(run.runId, QStringLiteral("cancelled"), QStringLiteral("analysis cancelled"));
            failCancelled(QStringLiteral("cancelled"));
            return;
        }
        if (process.state() == QProcess::NotRunning) {
            break;
        }
    }
    if (process.state() == QProcess::NotRunning) {
        repository.completeAnalysisRun(run.runId, QStringLiteral("failed"), QStringLiteral("worker failed to start"));
        emit analysisFailed(QStringLiteral("engine_launch_failure"), QStringLiteral("failed to start worker interpreter %1").arg(resolvedWorkerPython));
        return;
    }

    const QJsonDocument request = worker_protocol::makeAnalyzeLatestRequest(run, puzzleRound, sourceGame, stockfishInfo.absoluteFilePath());
    process.write(request.toJson(QJsonDocument::Compact));
    process.closeWriteChannel();

    emit analysisProgress(QStringLiteral("waiting_for_worker"), QStringLiteral("waiting for engine worker response"));
    while (!process.waitForFinished(100)) {
        if (isCancelRequested()) {
            process.terminate();
            if (!process.waitForFinished(1000)) {
                process.kill();
                process.waitForFinished(1000);
            }
            repository.completeAnalysisRun(run.runId, QStringLiteral("cancelled"), QStringLiteral("analysis cancelled"));
            failCancelled(QStringLiteral("cancelled"));
            return;
        }
    }

    const QByteArray stderrOutput = process.readAllStandardError();
    if (!stderrOutput.trimmed().isEmpty()) {
        emit logMessage(logLine(QStringLiteral("worker stderr: %1").arg(QString::fromUtf8(stderrOutput))));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        repository.completeAnalysisRun(
            run.runId,
            QStringLiteral("failed"),
            QStringLiteral("worker exit code %1").arg(process.exitCode())
        );
        const QString stderrText = QString::fromUtf8(stderrOutput).trimmed();
        QString stage = QStringLiteral("engine_analysis_failure");
        if (stderrText.contains(QStringLiteral("ambiguous mapping"), Qt::CaseInsensitive)) {
            stage = QStringLiteral("ambiguous_mapping");
        } else if (stderrText.contains(QStringLiteral("mapping failure"), Qt::CaseInsensitive)
                   || stderrText.contains(QStringLiteral("puzzle window mapping failure"), Qt::CaseInsensitive)) {
            stage = QStringLiteral("mapping_failure");
        } else if (stderrText.contains(QStringLiteral("pgn"), Qt::CaseInsensitive)) {
            stage = QStringLiteral("pgn_parse_failure");
        } else if (stderrText.contains(QStringLiteral("engine launch failure"), Qt::CaseInsensitive)) {
            stage = QStringLiteral("engine_launch_failure");
        } else if (stderrText.contains(QStringLiteral("evidence extraction failure"), Qt::CaseInsensitive)) {
            stage = QStringLiteral("evidence_extraction_failure");
        }
        emit analysisFailed(stage, stderrText.isEmpty() ? QStringLiteral("worker exited with code %1").arg(process.exitCode()) : stderrText);
        return;
    }

    emit logMessage(logLine(QStringLiteral("worker response received for run %1").arg(run.runId)));
    emit analysisProgress(QStringLiteral("parsing_worker_response"), QStringLiteral("parsing worker response"));
    WorkerAnalysisResponse workerResponse = worker_protocol::parseAnalysisResponse(process.readAllStandardOutput());
    if (!workerResponse.ok) {
        repository.completeAnalysisRun(run.runId, QStringLiteral("failed"), workerResponse.errorMessage);
        emit analysisFailed(QStringLiteral("engine_analysis_failure"), workerResponse.errorMessage);
        return;
    }

    emit analysisProgress(QStringLiteral("saving_result"), QStringLiteral("saving analysis result"));
    if (!repository.saveAnalysisResult(workerResponse.tacticalEvent, workerResponse.criticalMoves, &errorMessage)) {
        repository.completeAnalysisRun(run.runId, QStringLiteral("failed"), errorMessage);
        emit analysisFailed(QStringLiteral("persistence_failure"), errorMessage);
        return;
    }

    if (!repository.completeAnalysisRun(run.runId, QStringLiteral("completed"), QString(), &errorMessage)) {
        emit analysisFailed(QStringLiteral("persistence_failure"), errorMessage);
        return;
    }

    emit analysisProgress(QStringLiteral("loading_report"), QStringLiteral("loading rendered report"));
    PersistedAnalysisReport report;
    if (!repository.loadReport(run.runId, &report, &errorMessage)) {
        emit analysisFailed(QStringLiteral("persistence_failure"), errorMessage);
        return;
    }

    emit analysisCompleted(
        run.runId,
        ReportFormatter::formatAnalysisReport(
            report.puzzleRound,
            report.sourceGame,
            report.tacticalEvent,
            report.criticalMoves
        )
    );
}

bool AnalysisOrchestrator::isCancelRequested() const
{
    return m_cancelRequested.load();
}
