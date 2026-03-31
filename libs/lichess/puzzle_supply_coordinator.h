#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QVector>

#include <functional>

#include "lichess_client.h"
#include "puzzle_supply_cache.h"

enum class PuzzleSupplySource
{
    Remote,
    Cache,
    CooldownBlocked,
    Error,
};

struct PuzzleSupplyBatchResponse
{
    bool ok = false;
    PuzzleSupplySource source = PuzzleSupplySource::Error;
    QString message;
    QVector<parlawl::puzzle_runner::PuzzleDefinition> puzzles;
    QDateTime cooldownUntilUtc;
    int statusCode = 0;
};

class PuzzleSupplyCoordinator : public QObject
{
    Q_OBJECT

public:
    using BatchFetcher = std::function<LichessBatchResult(const QString &, int, const QString &)>;

    explicit PuzzleSupplyCoordinator(QObject *parent = nullptr);
    explicit PuzzleSupplyCoordinator(BatchFetcher fetcher, QObject *parent = nullptr);
    PuzzleSupplyCoordinator(BatchFetcher fetcher, QString cacheDirectoryPath, QObject *parent = nullptr);

    PuzzleSupplyBatchResponse requestBatch(
        const QString &apiToken,
        int count,
        const QString &difficulty,
        const QDateTime &nowUtc = QDateTime::currentDateTimeUtc());
    PuzzleSupplyBatchResponse restoreCachedBatch(
        int count,
        const QString &difficulty,
        const QDateTime &nowUtc = QDateTime::currentDateTimeUtc()) const;

    bool isCooldownActive(const QDateTime &nowUtc = QDateTime::currentDateTimeUtc()) const;
    QDateTime cooldownUntilUtc() const;
    bool hasCachedBatch(const QString &difficulty) const;

private:
    QString normalizeDifficulty(QString difficulty) const;
    QVector<parlawl::puzzle_runner::PuzzleDefinition> cachedBatch(const QString &difficulty, int count) const;
    static QString cooldownMessage(const QDateTime &cooldownUntilUtc, const QDateTime &nowUtc, bool usingCache);

    BatchFetcher m_fetcher;
    PuzzleSupplyCache m_persistentCache;
    mutable QHash<QString, QVector<parlawl::puzzle_runner::PuzzleDefinition>> m_cacheByDifficulty;
    bool m_inFlight = false;
    QDateTime m_cooldownUntilUtc;
};
