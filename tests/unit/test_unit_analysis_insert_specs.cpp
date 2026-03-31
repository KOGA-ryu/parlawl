#include <QFile>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "analysis_insert_specs.h"
#include "analysis_repository.h"
#include "database_manager.h"
#include "parlawl_config.h"
#include "worker_protocol.h"

namespace {

QJsonObject loadFixtureRequest()
{
    QFile requestFile(QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_request_fixture.json"));
    if (!requestFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QJsonDocument::fromJson(requestFile.readAll()).object();
}

WorkerAnalysisResponse loadFixtureResponse()
{
    QFile responseFile(QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_response_fixture.json"));
    if (!responseFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return worker_protocol::parseAnalysisResponse(responseFile.readAll());
}

PuzzleRound fixturePuzzleRound()
{
    const QJsonObject puzzleObject = loadFixtureRequest().value(QStringLiteral("puzzle_round")).toObject();
    PuzzleRound puzzleRound;
    puzzleRound.puzzleId = puzzleObject.value(QStringLiteral("puzzle_id")).toString();
    puzzleRound.puzzleRating = puzzleObject.value(QStringLiteral("puzzle_rating")).toInt();
    puzzleRound.timeControl = puzzleObject.value(QStringLiteral("time_control")).toString();
    puzzleRound.whitePlayer = puzzleObject.value(QStringLiteral("white_player")).toString();
    puzzleRound.whiteRating = puzzleObject.value(QStringLiteral("white_rating")).toInt();
    puzzleRound.blackPlayer = puzzleObject.value(QStringLiteral("black_player")).toString();
    puzzleRound.blackRating = puzzleObject.value(QStringLiteral("black_rating")).toInt();
    puzzleRound.sideToMove = puzzleObject.value(QStringLiteral("side_to_move")).toString();
    puzzleRound.sourceGameId = puzzleObject.value(QStringLiteral("source_game_id")).toString();
    puzzleRound.fetchedAtUtc = QDateTime::fromString(puzzleObject.value(QStringLiteral("fetched_at")).toString(), Qt::ISODate);
    puzzleRound.initialFen = puzzleObject.value(QStringLiteral("initial_fen")).toString();
    puzzleRound.lastMove = puzzleObject.value(QStringLiteral("last_move")).toString();
    puzzleRound.solved = puzzleObject.value(QStringLiteral("solved")).toBool();
    puzzleRound.rawPuzzleJson = puzzleObject.value(QStringLiteral("raw_puzzle_json")).toString();
    puzzleRound.rawActivityJson = puzzleObject.value(QStringLiteral("raw_activity_json")).toString();
    puzzleRound.solutionMovesJson = puzzleObject.value(QStringLiteral("solution_moves_json")).toString();
    puzzleRound.themesJson = puzzleObject.value(QStringLiteral("themes_json")).toString();
    return puzzleRound;
}

SourceGame fixtureSourceGame()
{
    const QJsonObject sourceObject = loadFixtureRequest().value(QStringLiteral("source_game")).toObject();
    SourceGame sourceGame;
    sourceGame.sourceGameId = sourceObject.value(QStringLiteral("source_game_id")).toString();
    sourceGame.pgnText = sourceObject.value(QStringLiteral("pgn_text")).toString();
    sourceGame.openingName = sourceObject.value(QStringLiteral("opening_name")).toString();
    sourceGame.fetchedAtUtc = QDateTime::fromString(sourceObject.value(QStringLiteral("fetched_at")).toString(), Qt::ISODate);
    return sourceGame;
}

int placeholderCount(const QString &sql)
{
    return sql.count(QLatin1Char('?'));
}

} // namespace

class TestUnitAnalysisInsertSpecs : public QObject
{
    Q_OBJECT

private slots:
    void tacticalEventInsertSpecMatchesBinder();
    void criticalMoveInsertSpecMatchesBinder();
    void insertAndReloadPreservesKeyFields();
};

void TestUnitAnalysisInsertSpecs::tacticalEventInsertSpecMatchesBinder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DatabaseManager manager;
    QVERIFY(manager.initialize(dir.path() + QStringLiteral("/test.sqlite3")).ok);

    WorkerAnalysisResponse response = loadFixtureResponse();
    QVERIFY(response.ok);

    QSqlQuery query(manager.database());
    const QString sql = storage::insert_specs::tacticalEventInsertSql();
    QVERIFY2(query.prepare(sql), qPrintable(query.lastError().text()));
    storage::insert_specs::bindTacticalEvent(query, response.tacticalEvent);

    QCOMPARE(storage::insert_specs::tacticalEventColumns().size(), placeholderCount(sql));
    QCOMPARE(query.boundValues().size(), storage::insert_specs::tacticalEventColumns().size());
}

void TestUnitAnalysisInsertSpecs::criticalMoveInsertSpecMatchesBinder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DatabaseManager manager;
    QVERIFY(manager.initialize(dir.path() + QStringLiteral("/test.sqlite3")).ok);

    WorkerAnalysisResponse response = loadFixtureResponse();
    QVERIFY(response.ok);
    QVERIFY(!response.criticalMoves.isEmpty());

    QSqlQuery query(manager.database());
    const QString sql = storage::insert_specs::criticalMoveInsertSql();
    QVERIFY2(query.prepare(sql), qPrintable(query.lastError().text()));
    storage::insert_specs::bindCriticalMove(query, response.criticalMoves.first());

    QCOMPARE(storage::insert_specs::criticalMoveColumns().size(), placeholderCount(sql));
    QCOMPARE(query.boundValues().size(), storage::insert_specs::criticalMoveColumns().size());
}

void TestUnitAnalysisInsertSpecs::insertAndReloadPreservesKeyFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DatabaseManager manager;
    QVERIFY(manager.initialize(dir.path() + QStringLiteral("/test.sqlite3")).ok);

    AnalysisRepository repository(manager.database());
    QString errorMessage;

    const PuzzleRound puzzleRound = fixturePuzzleRound();
    const SourceGame sourceGame = fixtureSourceGame();
    QVERIFY2(repository.upsertPuzzleRound(puzzleRound, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(repository.upsertSourceGame(sourceGame, &errorMessage), qPrintable(errorMessage));

    AnalysisRun run = repository.createAnalysisRun(
        puzzleRound.puzzleId,
        QStringLiteral("stockfish_window_v0_1"),
        QStringLiteral("stockfish"),
        10,
        &errorMessage
    );
    QVERIFY2(!run.runId.isEmpty(), qPrintable(errorMessage));

    WorkerAnalysisResponse response = loadFixtureResponse();
    QVERIFY(response.ok);
    response.tacticalEvent.runId = run.runId;
    response.tacticalEvent.eventId = QStringLiteral("insert-spec-event-001");
    for (CriticalMove &move : response.criticalMoves) {
        move.eventId = response.tacticalEvent.eventId;
    }

    QVERIFY2(repository.saveAnalysisResult(response.tacticalEvent, response.criticalMoves, &errorMessage), qPrintable(errorMessage));

    PersistedAnalysisReport report;
    QVERIFY2(repository.loadReport(run.runId, &report, &errorMessage), qPrintable(errorMessage));

    QCOMPARE(report.tacticalEvent.eventId, QStringLiteral("insert-spec-event-001"));
    QCOMPARE(report.tacticalEvent.retainedBreakSummary, response.tacticalEvent.retainedBreakSummary);
    QCOMPARE(report.tacticalEvent.localTargetSummary, response.tacticalEvent.localTargetSummary);
    QCOMPARE(report.tacticalEvent.structuralV2Summary, response.tacticalEvent.structuralV2Summary);
    QCOMPARE(report.tacticalEvent.structuralV2Confidence, response.tacticalEvent.structuralV2Confidence);
    QCOMPARE(report.tacticalEvent.engineLimitSummary, response.tacticalEvent.engineLimitSummary);
    QCOMPARE(report.tacticalEvent.assistantInferenceStatus, response.tacticalEvent.assistantInferenceStatus);

    QCOMPARE(report.criticalMoves.size(), response.criticalMoves.size());
    QCOMPARE(report.criticalMoves.first().criticalMoveId, response.criticalMoves.first().criticalMoveId);
    QCOMPARE(report.criticalMoves.first().criticalReasonType, response.criticalMoves.first().criticalReasonType);
    QCOMPARE(report.criticalMoves.first().candidateRankingType, response.criticalMoves.first().candidateRankingType);
    QCOMPARE(report.criticalMoves.first().structuralLinkSummary, response.criticalMoves.first().structuralLinkSummary);
    QCOMPARE(report.criticalMoves.first().linkedPinnedCriticalPiece, response.criticalMoves.first().linkedPinnedCriticalPiece);
    QCOMPARE(report.criticalMoves.first().linkedDefenderRemovalExposure, response.criticalMoves.first().linkedDefenderRemovalExposure);
    QCOMPARE(report.criticalMoves.first().linkedKingColorComplex, response.criticalMoves.first().linkedKingColorComplex);
    QCOMPARE(report.criticalMoves.first().linkedTargetZoneImbalance, response.criticalMoves.first().linkedTargetZoneImbalance);
    QCOMPARE(report.criticalMoves.first().candidateMovesJson, response.criticalMoves.first().candidateMovesJson);
}

QTEST_GUILESS_MAIN(TestUnitAnalysisInsertSpecs)

#include "test_unit_analysis_insert_specs.moc"
