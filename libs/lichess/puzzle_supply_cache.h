#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

#include "puzzle_types.h"

class PuzzleSupplyCache
{
public:
    explicit PuzzleSupplyCache(QString cacheDirectoryPath = QString());

    QVector<parlawl::puzzle_runner::PuzzleDefinition> loadBatch(const QString &difficulty, QString *errorMessage = nullptr) const;
    bool storeBatch(
        const QString &difficulty,
        const QVector<parlawl::puzzle_runner::PuzzleDefinition> &puzzles,
        QString *errorMessage = nullptr) const;

    QDateTime loadCooldownUntilUtc(QString *errorMessage = nullptr) const;
    bool storeCooldownUntilUtc(const QDateTime &cooldownUntilUtc, QString *errorMessage = nullptr) const;

private:
    QString cacheDirectoryPath() const;
    QString batchCacheFilePath(const QString &difficulty) const;
    QString stateCacheFilePath() const;

    QString m_cacheDirectoryPath;
};
