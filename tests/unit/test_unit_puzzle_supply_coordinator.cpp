#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "puzzle_supply_coordinator.h"

using namespace parlawl::puzzle_runner;

namespace {

PuzzleDefinition makePuzzle(const QString &id, const QString &difficulty)
{
    PuzzleDefinition puzzle;
    puzzle.id = id;
    puzzle.fenStart = QStringLiteral("8/8/8/8/8/8/8/8 w - - 0 1");
    puzzle.solutionMoves = {QStringLiteral("a1a1")};
    puzzle.metadata.difficulty = difficulty;
    return puzzle;
}

} // namespace

class PuzzleSupplyCoordinatorTest : public QObject
{
    Q_OBJECT

private slots:
    void reentrantRequestDoesNotFetchTwice();
    void rateLimitStartsCooldownAndUsesCache();
    void cooldownBlocksRemoteFetches();
    void restoreCachedBatchUsesPersistedBatchWithoutNetwork();
    void persistedCooldownAndCacheSurviveNewCoordinator();
};

void PuzzleSupplyCoordinatorTest::reentrantRequestDoesNotFetchTwice()
{
    int fetchCount = 0;
    bool nestedRequestWasRejected = false;
    PuzzleSupplyCoordinator *coordinatorPtr = nullptr;
    PuzzleSupplyCoordinator coordinator(
        PuzzleSupplyCoordinator::BatchFetcher([&](const QString &, int, const QString &) -> LichessBatchResult {
            ++fetchCount;
            if (fetchCount == 1) {
                const PuzzleSupplyBatchResponse nested = coordinatorPtr->requestBatch(
                    QStringLiteral("token"),
                    10,
                    QStringLiteral("hard"),
                    QDateTime::fromString(QStringLiteral("2026-03-30T16:30:00Z"), Qt::ISODate));
                nestedRequestWasRejected =
                    !nested.ok
                    && nested.source == PuzzleSupplySource::Error
                    && nested.message == QStringLiteral("live puzzle request already in progress");
            }
            LichessBatchResult result;
            result.ok = true;
            result.statusCode = 200;
            result.puzzles = {makePuzzle(QStringLiteral("p1"), QStringLiteral("hard"))};
            return result;
        }));
    coordinatorPtr = &coordinator;

    const PuzzleSupplyBatchResponse response = coordinator.requestBatch(
        QStringLiteral("token"),
        10,
        QStringLiteral("hard"),
        QDateTime::fromString(QStringLiteral("2026-03-30T16:30:00Z"), Qt::ISODate));

    QVERIFY(response.ok);
    QCOMPARE(response.source, PuzzleSupplySource::Remote);
    QCOMPARE(fetchCount, 1);
    QVERIFY(nestedRequestWasRejected);
}

void PuzzleSupplyCoordinatorTest::rateLimitStartsCooldownAndUsesCache()
{
    int fetchCount = 0;
    bool shouldRateLimit = false;
    PuzzleSupplyCoordinator coordinator(
        [&](const QString &, int, const QString &difficulty) {
            ++fetchCount;
            LichessBatchResult result;
            if (shouldRateLimit) {
                result.ok = false;
                result.statusCode = 429;
                result.errorMessage = QStringLiteral("GET /api/puzzle/batch failed (429)");
                return result;
            }
            result.ok = true;
            result.statusCode = 200;
            result.puzzles = {
                makePuzzle(QStringLiteral("p1"), difficulty),
                makePuzzle(QStringLiteral("p2"), difficulty),
            };
            return result;
        });

    const QDateTime firstRequestTime = QDateTime::fromString(QStringLiteral("2026-03-30T16:30:00Z"), Qt::ISODate);
    const PuzzleSupplyBatchResponse firstResponse = coordinator.requestBatch(
        QStringLiteral("token"), 10, QStringLiteral("hard"), firstRequestTime);
    QVERIFY(firstResponse.ok);
    QCOMPARE(firstResponse.source, PuzzleSupplySource::Remote);

    shouldRateLimit = true;
    const QDateTime rateLimitTime = QDateTime::fromString(QStringLiteral("2026-03-30T16:30:10Z"), Qt::ISODate);
    const PuzzleSupplyBatchResponse secondResponse = coordinator.requestBatch(
        QStringLiteral("token"), 10, QStringLiteral("hard"), rateLimitTime);
    QVERIFY(secondResponse.ok);
    QCOMPARE(secondResponse.source, PuzzleSupplySource::Cache);
    QVERIFY(secondResponse.message.contains(QStringLiteral("Lichess rate-limited")));
    QVERIFY(coordinator.isCooldownActive(rateLimitTime));
    QCOMPARE(fetchCount, 2);
}

void PuzzleSupplyCoordinatorTest::cooldownBlocksRemoteFetches()
{
    int fetchCount = 0;
    PuzzleSupplyCoordinator coordinator(
        [&](const QString &, int, const QString &) {
            ++fetchCount;
            LichessBatchResult result;
            result.ok = false;
            result.statusCode = 429;
            result.errorMessage = QStringLiteral("GET /api/puzzle/batch failed (429)");
            return result;
        });

    const QDateTime firstAttempt = QDateTime::fromString(QStringLiteral("2026-03-30T16:30:00Z"), Qt::ISODate);
    const PuzzleSupplyBatchResponse firstResponse = coordinator.requestBatch(
        QStringLiteral("token"), 10, QStringLiteral("hard"), firstAttempt);
    QVERIFY(!firstResponse.ok);
    QCOMPARE(firstResponse.source, PuzzleSupplySource::CooldownBlocked);
    QCOMPARE(fetchCount, 1);

    const QDateTime blockedAttempt = QDateTime::fromString(QStringLiteral("2026-03-30T16:30:20Z"), Qt::ISODate);
    const PuzzleSupplyBatchResponse secondResponse = coordinator.requestBatch(
        QStringLiteral("token"), 10, QStringLiteral("hard"), blockedAttempt);
    QVERIFY(!secondResponse.ok);
    QCOMPARE(secondResponse.source, PuzzleSupplySource::CooldownBlocked);
    QCOMPARE(fetchCount, 1);
    QVERIFY(secondResponse.message.contains(QStringLiteral("Retry after")));
}

void PuzzleSupplyCoordinatorTest::restoreCachedBatchUsesPersistedBatchWithoutNetwork()
{
    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    int fetchCount = 0;
    PuzzleSupplyCoordinator firstCoordinator(
        [&](const QString &, int, const QString &difficulty) {
            ++fetchCount;
            LichessBatchResult result;
            result.ok = true;
            result.statusCode = 200;
            result.puzzles = {
                makePuzzle(QStringLiteral("cached-1"), difficulty),
                makePuzzle(QStringLiteral("cached-2"), difficulty),
            };
            return result;
        },
        cacheDir.path());

    const QDateTime fetchTime = QDateTime::fromString(QStringLiteral("2026-03-30T20:15:00Z"), Qt::ISODate);
    const PuzzleSupplyBatchResponse firstResponse = firstCoordinator.requestBatch(
        QStringLiteral("token"), 10, QStringLiteral("hard"), fetchTime);
    QVERIFY(firstResponse.ok);
    QCOMPARE(firstResponse.source, PuzzleSupplySource::Remote);
    QCOMPARE(fetchCount, 1);

    int secondCoordinatorFetchCount = 0;
    PuzzleSupplyCoordinator secondCoordinator(
        [&](const QString &, int, const QString &) {
            ++secondCoordinatorFetchCount;
            LichessBatchResult result;
            result.ok = false;
            result.statusCode = 500;
            result.errorMessage = QStringLiteral("network should not have been called");
            return result;
        },
        cacheDir.path());

    const PuzzleSupplyBatchResponse restoredResponse = secondCoordinator.restoreCachedBatch(
        10,
        QStringLiteral("hard"),
        fetchTime.addSecs(5));
    QVERIFY(restoredResponse.ok);
    QCOMPARE(restoredResponse.source, PuzzleSupplySource::Cache);
    QCOMPARE(restoredResponse.puzzles.size(), 2);
    QCOMPARE(restoredResponse.message, QStringLiteral("restored cached live puzzle batch"));
    QCOMPARE(secondCoordinatorFetchCount, 0);
}

void PuzzleSupplyCoordinatorTest::persistedCooldownAndCacheSurviveNewCoordinator()
{
    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    int firstCoordinatorFetchCount = 0;
    bool shouldRateLimit = false;
    PuzzleSupplyCoordinator firstCoordinator(
        [&](const QString &, int, const QString &difficulty) {
            ++firstCoordinatorFetchCount;
            LichessBatchResult result;
            if (shouldRateLimit) {
                result.ok = false;
                result.statusCode = 429;
                result.errorMessage = QStringLiteral("GET /api/puzzle/batch failed (429)");
                return result;
            }
            result.ok = true;
            result.statusCode = 200;
            result.puzzles = {
                makePuzzle(QStringLiteral("persisted-1"), difficulty),
                makePuzzle(QStringLiteral("persisted-2"), difficulty),
            };
            return result;
        },
        cacheDir.path());

    const QDateTime fetchTime = QDateTime::fromString(QStringLiteral("2026-03-30T20:00:00Z"), Qt::ISODate);
    const PuzzleSupplyBatchResponse firstResponse = firstCoordinator.requestBatch(
        QStringLiteral("token"), 10, QStringLiteral("hard"), fetchTime);
    QVERIFY(firstResponse.ok);
    QCOMPARE(firstResponse.source, PuzzleSupplySource::Remote);

    shouldRateLimit = true;
    const QDateTime rateLimitTime = QDateTime::fromString(QStringLiteral("2026-03-30T20:00:10Z"), Qt::ISODate);
    const PuzzleSupplyBatchResponse secondResponse = firstCoordinator.requestBatch(
        QStringLiteral("token"), 10, QStringLiteral("hard"), rateLimitTime);
    QVERIFY(secondResponse.ok);
    QCOMPARE(secondResponse.source, PuzzleSupplySource::Cache);
    QCOMPARE(firstCoordinatorFetchCount, 2);

    int secondCoordinatorFetchCount = 0;
    PuzzleSupplyCoordinator secondCoordinator(
        [&](const QString &, int, const QString &) {
            ++secondCoordinatorFetchCount;
            LichessBatchResult result;
            result.ok = false;
            result.statusCode = 500;
            result.errorMessage = QStringLiteral("network should not have been called");
            return result;
        },
        cacheDir.path());

    QVERIFY(secondCoordinator.isCooldownActive(rateLimitTime));
    const PuzzleSupplyBatchResponse persistedResponse = secondCoordinator.requestBatch(
        QStringLiteral("token"), 10, QStringLiteral("hard"), rateLimitTime);
    QVERIFY(persistedResponse.ok);
    QCOMPARE(persistedResponse.source, PuzzleSupplySource::Cache);
    QCOMPARE(persistedResponse.puzzles.size(), 2);
    QCOMPARE(persistedResponse.puzzles.front().id, QStringLiteral("persisted-1"));
    QCOMPARE(secondCoordinatorFetchCount, 0);
}

QTEST_MAIN(PuzzleSupplyCoordinatorTest)

#include "test_unit_puzzle_supply_coordinator.moc"
