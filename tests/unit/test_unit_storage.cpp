#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "analysis_repository.h"
#include "database_manager.h"

class TestUnitStorage : public QObject
{
    Q_OBJECT

private slots:
    void initializesAndMigrates();
    void reinitializesAtNewPath();
    void listsRecentRunsNewestFirst();
};

void TestUnitStorage::initializesAndMigrates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DatabaseManager manager;
    const QString dbPath = dir.path() + QStringLiteral("/test.sqlite3");
    const auto result = manager.initialize(dbPath);
    QVERIFY2(result.ok, qPrintable(result.message));

    QSqlQuery query(manager.database());
    QVERIFY(query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")));

    QStringList names;
    while (query.next()) {
        names.append(query.value(0).toString());
    }

    QVERIFY(names.contains(QStringLiteral("analysis_runs")));
    QVERIFY(names.contains(QStringLiteral("critical_moves")));
    QVERIFY(names.contains(QStringLiteral("puzzle_rounds")));
    QVERIFY(names.contains(QStringLiteral("source_games")));
    QVERIFY(names.contains(QStringLiteral("tactical_events")));

    QVERIFY(query.exec(QStringLiteral("PRAGMA table_info(tactical_events)")));
    QStringList tacticalColumns;
    while (query.next()) {
        tacticalColumns.append(query.value(1).toString());
    }

    QVERIFY(tacticalColumns.contains(QStringLiteral("king_exposure_type")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("loose_piece_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("overloaded_defender_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("back_rank_state")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("structural_feature_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("king_zone_target_type")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("vulnerable_piece_target_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("pressure_lane_target_type")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("decisive_imbalance_target")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("pinned_critical_piece_type")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("pinned_critical_piece_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("defender_removal_exposure_type")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("defender_removal_exposure_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("king_color_complex_state")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("king_color_complex_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("target_zone_imbalance_type")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("target_zone_imbalance_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("structural_v2_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("structural_v2_confidence")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("escape_geometry_state")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("escape_geometry_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("flight_control_type")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("flight_control_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("defensive_escape_fragility_type")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("defensive_escape_fragility_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("structural_v3_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("structural_v3_confidence")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("attacker_coordination_type")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("attacker_coordination_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("defensive_network_fragility_type")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("defensive_network_fragility_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("structural_v4_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("structural_v4_confidence")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("local_target_summary")));
    QVERIFY(tacticalColumns.contains(QStringLiteral("local_target_confidence")));

    QVERIFY(query.exec(QStringLiteral("PRAGMA table_info(critical_moves)")));
    QStringList criticalColumns;
    while (query.next()) {
        criticalColumns.append(query.value(1).toString());
    }

    QVERIFY(criticalColumns.contains(QStringLiteral("structural_link_format_version")));
    QVERIFY(criticalColumns.contains(QStringLiteral("linked_king_zone_target")));
    QVERIFY(criticalColumns.contains(QStringLiteral("linked_vulnerable_piece_target")));
    QVERIFY(criticalColumns.contains(QStringLiteral("linked_pressure_lane_target")));
    QVERIFY(criticalColumns.contains(QStringLiteral("linked_decisive_imbalance_target")));
    QVERIFY(criticalColumns.contains(QStringLiteral("linked_pinned_critical_piece")));
    QVERIFY(criticalColumns.contains(QStringLiteral("linked_defender_removal_exposure")));
    QVERIFY(criticalColumns.contains(QStringLiteral("linked_king_color_complex")));
    QVERIFY(criticalColumns.contains(QStringLiteral("linked_target_zone_imbalance")));
    QVERIFY(criticalColumns.contains(QStringLiteral("linked_attacker_coordination")));
    QVERIFY(criticalColumns.contains(QStringLiteral("linked_defensive_network_fragility")));
    QVERIFY(criticalColumns.contains(QStringLiteral("structural_link_summary")));
}

void TestUnitStorage::reinitializesAtNewPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DatabaseManager manager;
    const QString firstPath = dir.path() + QStringLiteral("/first.sqlite3");
    const QString secondPath = dir.path() + QStringLiteral("/second.sqlite3");

    auto firstResult = manager.initialize(firstPath);
    QVERIFY2(firstResult.ok, qPrintable(firstResult.message));
    QCOMPARE(QFileInfo(manager.databasePath()).absoluteFilePath(), QFileInfo(firstPath).absoluteFilePath());

    {
        QSqlQuery query(manager.database());
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS reinit_probe (value TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO reinit_probe(value) VALUES ('first')")));
    }

    auto secondResult = manager.initialize(secondPath);
    QVERIFY2(secondResult.ok, qPrintable(secondResult.message));
    QCOMPARE(QFileInfo(manager.databasePath()).absoluteFilePath(), QFileInfo(secondPath).absoluteFilePath());

    QSqlQuery query(manager.database());
    QVERIFY(query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='reinit_probe'")));
    QVERIFY(!query.next());
}

void TestUnitStorage::listsRecentRunsNewestFirst()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DatabaseManager manager;
    const QString dbPath = dir.path() + QStringLiteral("/runs.sqlite3");
    const auto result = manager.initialize(dbPath);
    QVERIFY2(result.ok, qPrintable(result.message));

    AnalysisRepository repository(manager.database());
    QString errorMessage;

    AnalysisRun first = repository.createAnalysisRun(
        QStringLiteral("puzzle-a"),
        QStringLiteral("stockfish_window_v0_1"),
        QStringLiteral("stockfish"),
        10,
        &errorMessage
    );
    QVERIFY2(!first.runId.isEmpty(), qPrintable(errorMessage));
    QVERIFY2(repository.completeAnalysisRun(first.runId, QStringLiteral("completed"), QString(), &errorMessage), qPrintable(errorMessage));
    QTest::qSleep(2);

    AnalysisRun second = repository.createAnalysisRun(
        QStringLiteral("puzzle-b"),
        QStringLiteral("stockfish_window_v0_1"),
        QStringLiteral("stockfish"),
        10,
        &errorMessage
    );
    QVERIFY2(!second.runId.isEmpty(), qPrintable(errorMessage));
    QVERIFY2(repository.completeAnalysisRun(second.runId, QStringLiteral("failed"), QStringLiteral("boom"), &errorMessage), qPrintable(errorMessage));

    const QList<AnalysisRun> runs = repository.listRecentRuns(10, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(runs.size(), 2);
    QCOMPARE(runs.at(0).runId, second.runId);
    QCOMPARE(runs.at(0).status, QStringLiteral("failed"));
    QCOMPARE(runs.at(1).runId, first.runId);
    QCOMPARE(runs.at(1).status, QStringLiteral("completed"));
}

QTEST_GUILESS_MAIN(TestUnitStorage)

#include "test_unit_storage.moc"
