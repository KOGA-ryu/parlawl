#include "puzzle_supply_coordinator.h"

#include <algorithm>

namespace {

QString normalizedDifficultyKey(QString difficulty)
{
    difficulty = difficulty.trimmed().toLower();
    if (difficulty != QStringLiteral("medium") && difficulty != QStringLiteral("hard")) {
        return QStringLiteral("hard");
    }
    return difficulty;
}

} // namespace

PuzzleSupplyCoordinator::PuzzleSupplyCoordinator(QObject *parent)
    : PuzzleSupplyCoordinator(
          [](const QString &apiToken, int count, const QString &difficulty) {
              LichessClient client;
              return client.fetchTrainingBatch(apiToken, count, difficulty);
          },
          QString(),
          parent)
{
}

PuzzleSupplyCoordinator::PuzzleSupplyCoordinator(BatchFetcher fetcher, QObject *parent)
    : PuzzleSupplyCoordinator(std::move(fetcher), QString(), parent)
{
}

PuzzleSupplyCoordinator::PuzzleSupplyCoordinator(BatchFetcher fetcher, QString cacheDirectoryPath, QObject *parent)
    : QObject(parent)
    , m_fetcher(std::move(fetcher))
    , m_persistentCache(std::move(cacheDirectoryPath))
{
    m_cooldownUntilUtc = m_persistentCache.loadCooldownUntilUtc();
}

PuzzleSupplyBatchResponse PuzzleSupplyCoordinator::requestBatch(
    const QString &apiToken,
    int count,
    const QString &difficulty,
    const QDateTime &nowUtc)
{
    const QString normalizedDifficulty = normalizeDifficulty(difficulty);
    const int normalizedCount = std::max(count, 1);

    if (isCooldownActive(nowUtc)) {
        const QVector<parlawl::puzzle_runner::PuzzleDefinition> cached = cachedBatch(normalizedDifficulty, normalizedCount);
        if (!cached.isEmpty()) {
            return {
                true,
                PuzzleSupplySource::Cache,
                cooldownMessage(m_cooldownUntilUtc, nowUtc, true),
                cached,
                m_cooldownUntilUtc,
                429,
            };
        }
        return {
            false,
            PuzzleSupplySource::CooldownBlocked,
            cooldownMessage(m_cooldownUntilUtc, nowUtc, false),
            {},
            m_cooldownUntilUtc,
            429,
        };
    }

    if (m_inFlight) {
        const QVector<parlawl::puzzle_runner::PuzzleDefinition> cached = cachedBatch(normalizedDifficulty, normalizedCount);
        if (!cached.isEmpty()) {
            return {
                true,
                PuzzleSupplySource::Cache,
                QStringLiteral("live puzzle request already in progress; using cached puzzles"),
                cached,
                m_cooldownUntilUtc,
                0,
            };
        }
        return {
            false,
            PuzzleSupplySource::Error,
            QStringLiteral("live puzzle request already in progress"),
            {},
            m_cooldownUntilUtc,
            0,
        };
    }

    m_inFlight = true;
    const LichessBatchResult result = m_fetcher(apiToken, normalizedCount, normalizedDifficulty);
    m_inFlight = false;

    if (result.ok) {
        m_cooldownUntilUtc = {};
        m_cacheByDifficulty.insert(normalizedDifficulty, result.puzzles);
        m_persistentCache.storeBatch(normalizedDifficulty, result.puzzles);
        m_persistentCache.storeCooldownUntilUtc(m_cooldownUntilUtc);
        return {
            true,
            PuzzleSupplySource::Remote,
            QStringLiteral("live puzzle batch loaded"),
            result.puzzles,
            m_cooldownUntilUtc,
            result.statusCode,
        };
    }

    if (result.statusCode == 429) {
        m_cooldownUntilUtc = nowUtc.addSecs(60);
        m_persistentCache.storeCooldownUntilUtc(m_cooldownUntilUtc);
        const QVector<parlawl::puzzle_runner::PuzzleDefinition> cached = cachedBatch(normalizedDifficulty, normalizedCount);
        if (!cached.isEmpty()) {
            return {
                true,
                PuzzleSupplySource::Cache,
                cooldownMessage(m_cooldownUntilUtc, nowUtc, true),
                cached,
                m_cooldownUntilUtc,
                result.statusCode,
            };
        }
        return {
            false,
            PuzzleSupplySource::CooldownBlocked,
            cooldownMessage(m_cooldownUntilUtc, nowUtc, false),
            {},
            m_cooldownUntilUtc,
            result.statusCode,
        };
    }

    return {
        false,
        PuzzleSupplySource::Error,
        result.errorMessage,
        {},
        m_cooldownUntilUtc,
        result.statusCode,
    };
}

PuzzleSupplyBatchResponse PuzzleSupplyCoordinator::restoreCachedBatch(
    int count,
    const QString &difficulty,
    const QDateTime &nowUtc) const
{
    const QString normalizedDifficulty = normalizeDifficulty(difficulty);
    const QVector<parlawl::puzzle_runner::PuzzleDefinition> cached = cachedBatch(normalizedDifficulty, std::max(count, 1));
    if (cached.isEmpty()) {
        return {
            false,
            PuzzleSupplySource::Error,
            QStringLiteral("no persisted live puzzle batch is available"),
            {},
            m_cooldownUntilUtc,
            0,
        };
    }

    const bool cooldownActive = isCooldownActive(nowUtc);
    return {
        true,
        PuzzleSupplySource::Cache,
        cooldownActive
            ? cooldownMessage(m_cooldownUntilUtc, nowUtc, true)
            : QStringLiteral("restored cached live puzzle batch"),
        cached,
        m_cooldownUntilUtc,
        cooldownActive ? 429 : 0,
    };
}

bool PuzzleSupplyCoordinator::isCooldownActive(const QDateTime &nowUtc) const
{
    return m_cooldownUntilUtc.isValid() && nowUtc < m_cooldownUntilUtc;
}

QDateTime PuzzleSupplyCoordinator::cooldownUntilUtc() const
{
    return m_cooldownUntilUtc;
}

bool PuzzleSupplyCoordinator::hasCachedBatch(const QString &difficulty) const
{
    return !cachedBatch(difficulty, 1).isEmpty();
}

QString PuzzleSupplyCoordinator::normalizeDifficulty(QString difficulty) const
{
    return normalizedDifficultyKey(std::move(difficulty));
}

QVector<parlawl::puzzle_runner::PuzzleDefinition> PuzzleSupplyCoordinator::cachedBatch(const QString &difficulty, int count) const
{
    const QString key = normalizeDifficulty(difficulty);
    QVector<parlawl::puzzle_runner::PuzzleDefinition> cached = m_cacheByDifficulty.value(key);
    if (cached.isEmpty()) {
        cached = m_persistentCache.loadBatch(key);
        if (!cached.isEmpty()) {
            m_cacheByDifficulty.insert(key, cached);
        }
    }
    if (cached.isEmpty()) {
        return {};
    }
    const int limitedCount = std::min(std::max(count, 1), static_cast<int>(cached.size()));
    return QVector<parlawl::puzzle_runner::PuzzleDefinition>(cached.begin(), cached.begin() + limitedCount);
}

QString PuzzleSupplyCoordinator::cooldownMessage(const QDateTime &cooldownUntilUtc, const QDateTime &nowUtc, bool usingCache)
{
    const qint64 secondsRemaining = std::max<qint64>(1, nowUtc.secsTo(cooldownUntilUtc));
    if (usingCache) {
        return QStringLiteral("Lichess rate-limited. Retrying after %1 seconds. Using cached puzzles if available.")
            .arg(secondsRemaining);
    }
    return QStringLiteral("Lichess rate-limited. Retry after %1 seconds.")
        .arg(secondsRemaining);
}
