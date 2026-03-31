#include <QFile>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "analysis_repository.h"
#include "analysis_row_mappers.h"
#include "database_manager.h"
#include "parlawl_config.h"
#include "worker_protocol.h"

namespace {

QString reversedSelect(const QSqlDatabase &database, const QString &tableName)
{
    QSqlQuery pragmaQuery(database);
    const QString pragma = QStringLiteral("PRAGMA table_info(%1)").arg(tableName);
    if (!pragmaQuery.exec(pragma)) {
        return {};
    }

    QStringList columns;
    while (pragmaQuery.next()) {
        columns.prepend(pragmaQuery.value(1).toString());
    }
    columns.prepend(QStringLiteral("42 AS schema_growth_sentinel"));
    return QStringLiteral("SELECT %1 FROM %2").arg(columns.join(QStringLiteral(", ")), tableName);
}

WorkerAnalysisResponse loadFixtureResponse()
{
    QFile responseFile(QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_response_fixture.json"));
    if (!responseFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return worker_protocol::parseAnalysisResponse(responseFile.readAll());
}

void seedFixtureData(DatabaseManager &manager, QString *runIdOut, QString *eventIdOut)
{
    QFile requestFile(QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_request_fixture.json"));
    QVERIFY(requestFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QJsonObject request = QJsonDocument::fromJson(requestFile.readAll()).object();

    PuzzleRound puzzleRound;
    const QJsonObject puzzleObject = request.value(QStringLiteral("puzzle_round")).toObject();
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

    SourceGame sourceGame;
    const QJsonObject sourceObject = request.value(QStringLiteral("source_game")).toObject();
    sourceGame.sourceGameId = sourceObject.value(QStringLiteral("source_game_id")).toString();
    sourceGame.pgnText = sourceObject.value(QStringLiteral("pgn_text")).toString();
    sourceGame.openingName = sourceObject.value(QStringLiteral("opening_name")).toString();
    sourceGame.fetchedAtUtc = QDateTime::fromString(sourceObject.value(QStringLiteral("fetched_at")).toString(), Qt::ISODate);

    AnalysisRepository repository(manager.database());
    QString errorMessage;
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
    response.tacticalEvent.eventId = QStringLiteral("mapper-event-001");
    for (CriticalMove &move : response.criticalMoves) {
        move.eventId = response.tacticalEvent.eventId;
    }

    QVERIFY2(repository.saveAnalysisResult(response.tacticalEvent, response.criticalMoves, &errorMessage), qPrintable(errorMessage));

    *runIdOut = run.runId;
    *eventIdOut = response.tacticalEvent.eventId;
}

} // namespace

class TestUnitAnalysisRowMappers : public QObject
{
    Q_OBJECT

private slots:
    void tacticalEventMappingIgnoresColumnOrder();
    void criticalMoveMappingIgnoresColumnOrder();
};

void TestUnitAnalysisRowMappers::tacticalEventMappingIgnoresColumnOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DatabaseManager manager;
    QVERIFY(manager.initialize(dir.path() + QStringLiteral("/test.sqlite3")).ok);

    QString runId;
    QString eventId;
    seedFixtureData(manager, &runId, &eventId);

    QSqlQuery query(manager.database());
    QVERIFY(query.prepare(reversedSelect(manager.database(), QStringLiteral("tactical_events")) + QStringLiteral(" WHERE run_id = ?")));
    query.addBindValue(runId);
    QVERIFY(query.exec());
    QVERIFY(query.next());

    const TacticalEvent event = storage::mapTacticalEventRow(query);
    QCOMPARE(event.eventId, QStringLiteral("mapper-event-001"));
    QCOMPARE(event.mappingMethod, QStringLiteral("exact_initial_ply_match"));
    QCOMPARE(event.primaryBreakPly, 18);
    QCOMPARE(event.kingZoneTargetType, QStringLiteral("no_clear_king_zone_target"));
    QCOMPARE(event.pressureLaneTargetSummary, QStringLiteral("diag b3-f7"));
    QCOMPARE(event.structuralV2Summary, QStringLiteral("no_clear_structural_v2"));
    QCOMPARE(event.structuralV2Confidence, QStringLiteral("low"));
    QCOMPARE(event.escapeGeometryState, QStringLiteral("no_clear_escape_geometry"));
    QCOMPARE(event.flightControlType, QStringLiteral("no_clear_flight_control"));
    QCOMPARE(event.defensiveEscapeFragilityType, QStringLiteral("no_clear_escape_fragility"));
    QCOMPARE(event.structuralV3Summary, QStringLiteral("no_clear_structural_v3"));
    QCOMPARE(event.structuralV3Confidence, QStringLiteral("low"));
    QCOMPARE(event.attackerCoordinationType, QStringLiteral("no_clear_attacker_coordination"));
    QCOMPARE(event.attackerCoordinationSummary, QStringLiteral("none"));
    QCOMPARE(event.defensiveNetworkFragilityType, QStringLiteral("no_clear_defensive_network_fragility"));
    QCOMPARE(event.defensiveNetworkFragilitySummary, QStringLiteral("none"));
    QCOMPARE(event.structuralV4Summary, QStringLiteral("no_clear_structural_v4"));
    QCOMPARE(event.structuralV4Confidence, QStringLiteral("low"));
    QCOMPARE(event.localTargetSummary, QStringLiteral("diag b3-f7"));
    QCOMPARE(event.assistantInferenceStatus, QStringLiteral("pending"));
}

void TestUnitAnalysisRowMappers::criticalMoveMappingIgnoresColumnOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DatabaseManager manager;
    QVERIFY(manager.initialize(dir.path() + QStringLiteral("/test.sqlite3")).ok);

    QString runId;
    QString eventId;
    seedFixtureData(manager, &runId, &eventId);

    QSqlQuery query(manager.database());
    QVERIFY(query.exec(reversedSelect(manager.database(), QStringLiteral("critical_moves")) + QStringLiteral(" WHERE event_id = '") + eventId + QStringLiteral("'")));
    QVERIFY(query.next());

    const CriticalMove move = storage::mapCriticalMoveRow(query);
    QCOMPARE(move.eventId, QStringLiteral("mapper-event-001"));
    QCOMPARE(move.role, QStringLiteral("last_holding_defense"));
    QCOMPARE(move.bestMove, QStringLiteral("h7h6"));
    QVERIFY(move.hasEvalDeltaCp);
    QCOMPARE(move.evalDeltaCp, 114);
    QCOMPARE(move.criticalReasonType, QStringLiteral("stronger_alternative_missed"));
    QCOMPARE(move.structuralLinkFormatVersion, QStringLiteral("mtlv3"));
    QCOMPARE(move.linkedPressureLaneTarget, QStringLiteral("diag b3-f7"));
    QCOMPARE(move.linkedPinnedCriticalPiece, QStringLiteral("none"));
    QCOMPARE(move.linkedDefenderRemovalExposure, QStringLiteral("none"));
    QCOMPARE(move.linkedKingColorComplex, QStringLiteral("none"));
    QCOMPARE(move.linkedTargetZoneImbalance, QStringLiteral("none"));
    QCOMPARE(move.linkedAttackerCoordination, QStringLiteral("none"));
    QCOMPARE(move.linkedDefensiveNetworkFragility, QStringLiteral("none"));
    QCOMPARE(move.structuralLinkSummary, QStringLiteral("lane diag b3-f7"));
}

QTEST_GUILESS_MAIN(TestUnitAnalysisRowMappers)

#include "test_unit_analysis_row_mappers.moc"
