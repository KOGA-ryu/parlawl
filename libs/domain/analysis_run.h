#pragma once

#include <QDateTime>
#include <QString>

struct AnalysisRun
{
    QString runId;
    QString puzzleId;
    QString status;
    QString engineMode;
    QString engineName;
    int engineDepth = 0;
    QDateTime createdAtUtc;
    QDateTime completedAtUtc;
    QString errorMessage;
};
