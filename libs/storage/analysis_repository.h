#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include "analysis_run.h"
#include "critical_move.h"
#include "puzzle_round.h"
#include "source_game.h"
#include "tactical_event.h"

struct PersistedAnalysisReport
{
    AnalysisRun analysisRun;
    PuzzleRound puzzleRound;
    SourceGame sourceGame;
    TacticalEvent tacticalEvent;
    QList<CriticalMove> criticalMoves;
};

class AnalysisRepository
{
public:
    explicit AnalysisRepository(const QSqlDatabase &database);

    bool upsertPuzzleRound(const PuzzleRound &puzzleRound, QString *errorMessage = nullptr) const;
    bool upsertSourceGame(const SourceGame &sourceGame, QString *errorMessage = nullptr) const;
    AnalysisRun createAnalysisRun(
        const QString &puzzleId,
        const QString &engineMode,
        const QString &engineName,
        int engineDepth,
        QString *errorMessage = nullptr
    ) const;
    bool completeAnalysisRun(const QString &runId, const QString &status, const QString &errorMessageText, QString *errorMessage = nullptr) const;
    bool saveAnalysisResult(
        const TacticalEvent &tacticalEvent,
        const QList<CriticalMove> &criticalMoves,
        QString *errorMessage = nullptr
    ) const;
    bool saveAssistantInference(
        const QString &runId,
        const QString &status,
        const QString &labelsJson,
        const QString &summaryMarkdown,
        QString *errorMessage = nullptr
    ) const;
    QList<AnalysisRun> listRecentRuns(int limit, QString *errorMessage = nullptr) const;
    bool pruneAnalysisHistory(
        int keepRecentRuns,
        bool preserveCompletedRuns,
        int *deletedRunCount = nullptr,
        QString *errorMessage = nullptr
    ) const;
    bool loadReport(const QString &runId, PersistedAnalysisReport *report, QString *errorMessage = nullptr) const;

private:
    mutable QSqlDatabase m_database;
};
