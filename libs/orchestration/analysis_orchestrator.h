#pragma once

#include <QObject>
#include <atomic>

struct PuzzleRound;
struct SourceGame;

class AnalysisOrchestrator : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisOrchestrator(QObject *parent = nullptr);

    void analyzeLatestSolvedPuzzle(
        const QString &lichessApiToken,
        const QString &workerPythonPath,
        const QString &stockfishPath,
        const QString &databasePath
    );
    void analyzeProvidedPuzzle(
        const PuzzleRound &puzzleRound,
        const SourceGame &sourceGame,
        const QString &workerPythonPath,
        const QString &stockfishPath,
        const QString &databasePath
    );
    void requestCancel();
    void batchSyncOrRerun(const QString &workerPythonPath, const QString &databasePath);
    void exportJson(const QString &databasePath);

signals:
    void logMessage(const QString &message);
    void analysisProgress(const QString &phase, const QString &message);
    void analysisCompleted(const QString &runId, const QString &summaryText);
    void analysisFailed(const QString &stage, const QString &message);
    void analysisStarted();

private:
    bool isCancelRequested() const;
    void runAnalyzeLatest(
        const QString &lichessApiToken,
        const QString &workerPythonPath,
        const QString &stockfishPath,
        const QString &databasePath
    );
    void runAnalyzePrepared(
        const PuzzleRound &puzzleRound,
        const SourceGame &sourceGame,
        const QString &workerPythonPath,
        const QString &stockfishPath,
        const QString &databasePath
    );

    std::atomic<bool> m_cancelRequested;
};
