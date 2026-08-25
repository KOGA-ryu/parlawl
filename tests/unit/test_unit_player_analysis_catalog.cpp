#include <QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "player_analysis_catalog.h"


namespace {

bool createCatalog(const QString &path, bool validSchema)
{
    const QString connectionName = QStringLiteral("catalog-fixture-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = true;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        ok = database.open();
        QSqlQuery query(database);
        const QStringList statements {
            QStringLiteral("CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE games(source_game_id TEXT PRIMARY KEY, event_start_utc TEXT, utc_day TEXT, opening_status TEXT, opening_eco TEXT, opening_name TEXT)"),
            QStringLiteral("CREATE TABLE player_games(source_game_id TEXT, player_id TEXT, opponent_id TEXT, player_color TEXT, outcome TEXT, PRIMARY KEY(source_game_id, player_id))"),
            QStringLiteral("CREATE TABLE moves(source_game_id TEXT, ply INTEGER)"),
            QStringLiteral("CREATE TABLE board_structure_metric_definitions(metric_code TEXT PRIMARY KEY, ordinal INTEGER, definition_id TEXT, category TEXT, phase TEXT, value_semantics TEXT, opportunity_unit TEXT)"),
            QStringLiteral("CREATE TABLE board_structure_player_games(structural_player_game_id TEXT PRIMARY KEY, source_game_id TEXT, player_id TEXT, opponent_id TEXT, player_color TEXT)"),
            QStringLiteral("CREATE TABLE board_structure_measurements(structural_player_game_id TEXT, metric_code TEXT, status TEXT, numerator INTEGER, denominator INTEGER, value_ppm INTEGER)"),
        };
        for (const QString &statement : statements) {
            ok = ok && query.exec(statement);
        }
        const QString schema = validSchema
            ? QStringLiteral("chess-player-analysis-catalog-sqlite-v1")
            : QStringLiteral("wrong-schema");
        const QList<QPair<QString, QString>> metadata {
            {QStringLiteral("catalog_schema_version"), schema},
            {QStringLiteral("schema_version"), QStringLiteral("chess-player-game-explorer-sqlite-v1")},
            {QStringLiteral("catalog_authenticates_source_replay"), QStringLiteral("false")},
            {QStringLiteral("same_game_vectors_are_pregame_features"), QStringLiteral("false")},
            {QStringLiteral("source_plan_v2_id"), QStringLiteral("chess-cohort-source-chunk-plan-v2:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")},
            {QStringLiteral("board_structure_measurement_count"), QStringLiteral("8")},
        };
        query.prepare(QStringLiteral("INSERT INTO metadata VALUES (?, ?)"));
        for (const auto &[key, value] : metadata) {
            query.bindValue(0, key);
            query.bindValue(1, value);
            ok = ok && query.exec();
        }
        ok = ok && query.exec(QStringLiteral(
            "INSERT INTO games VALUES "
            "('g1','2026-08-01T12:00:00Z','2026-08-01','classified','C20','King Pawn'),"
            "('g2','2026-08-02T12:00:00Z','2026-08-02','unknown',NULL,NULL)"));
        ok = ok && query.exec(QStringLiteral(
            "INSERT INTO player_games VALUES "
            "('g1','alpha','beta','white','win'),"
            "('g1','beta','alpha','black','loss'),"
            "('g2','beta','alpha','white','draw'),"
            "('g2','alpha','beta','black','draw')"));
        ok = ok && query.exec(QStringLiteral(
            "INSERT INTO board_structure_metric_definitions VALUES "
            "('focal_castling.any',0,'d1','castling','game','proportion_ppm','complete_player_game'),"
            "('binary.both_queens_absent.all.share',1,'d2','position','all','proportion_ppm','legal_decision')"));
        ok = ok && query.exec(QStringLiteral(
            "INSERT INTO board_structure_player_games VALUES "
            "('s1','g1','alpha','beta','white'),"
            "('s2','g1','beta','alpha','black'),"
            "('s3','g2','beta','alpha','white'),"
            "('s4','g2','alpha','beta','black')"));
        ok = ok && query.exec(QStringLiteral(
            "INSERT INTO board_structure_measurements VALUES "
            "('s1','focal_castling.any','observed',1,1,1000000),"
            "('s2','focal_castling.any','observed',0,1,0),"
            "('s3','focal_castling.any','observed',1,1,1000000),"
            "('s4','focal_castling.any','observed',0,1,0),"
            "('s1','binary.both_queens_absent.all.share','observed',1,4,250000),"
            "('s2','binary.both_queens_absent.all.share','observed',2,4,500000),"
            "('s3','binary.both_queens_absent.all.share','not_applicable',NULL,0,NULL),"
            "('s4','binary.both_queens_absent.all.share','not_applicable',NULL,0,NULL)"));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

} // namespace


class TestUnitPlayerAnalysisCatalog : public QObject {
    Q_OBJECT

private slots:
    void opensReadOnlyAndQueriesExactRows();
    void rejectsWrongSchema();
};

void TestUnitPlayerAnalysisCatalog::opensReadOnlyAndQueriesExactRows()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("analysis.sqlite3"));
    QVERIFY(createCatalog(path, true));

    PlayerAnalysisCatalog catalog;
    QString error;
    QVERIFY2(catalog.open(path, &error), qPrintable(error));
    QVERIFY(catalog.isOpen());
    QVERIFY(catalog.sourcePlanId().startsWith(
        QStringLiteral("chess-cohort-source-chunk-plan-v2:")));
    QCOMPARE(catalog.playerIds(&error), QStringList({QStringLiteral("alpha"), QStringLiteral("beta")}));

    const auto alpha = catalog.playerSummary(QStringLiteral("alpha"), &error);
    QVERIFY2(alpha.has_value(), qPrintable(error));
    QCOMPARE(
        QList<qint64>({alpha->gameCount, alpha->whiteGameCount,
            alpha->blackGameCount, alpha->winCount, alpha->drawCount,
            alpha->lossCount, alpha->scoreRatePpm,
            alpha->distinctOpponentCount, alpha->distinctUtcDayCount}),
        QList<qint64>({2, 1, 1, 1, 1, 0, 750'000, 1, 2}));

    const auto comparisons = catalog.compareMetrics(
        QStringLiteral("alpha"), QStringLiteral("beta"),
        QStringLiteral("castling"), &error);
    QCOMPARE(comparisons.size(), 1);
    QCOMPARE(comparisons[0].metricCode, QStringLiteral("focal_castling.any"));
    QCOMPARE(comparisons[0].first.aggregateValuePpm, std::optional<qint64>(500'000));
    QCOMPARE(comparisons[0].second.aggregateValuePpm, std::optional<qint64>(500'000));
    QCOMPARE(catalog.compareMetrics(
        QStringLiteral("alpha"), QStringLiteral("beta")).size(), 2);

    const auto games = catalog.measurementGames(
        QStringLiteral("alpha"), QStringLiteral("focal_castling.any"), 10, &error);
    QCOMPARE(games.size(), 2);
    QCOMPARE(games[0].sourceGameId, QStringLiteral("g1"));
    QCOMPARE(games[0].openingEco, QStringLiteral("C20"));
    QCOMPARE(games[0].outcome, QStringLiteral("win"));
    QCOMPARE(games[1].sourceGameId, QStringLiteral("g2"));
    QCOMPARE(games[1].outcome, QStringLiteral("draw"));
}

void TestUnitPlayerAnalysisCatalog::rejectsWrongSchema()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("wrong.sqlite3"));
    QVERIFY(createCatalog(path, false));
    PlayerAnalysisCatalog catalog;
    QString error;
    QVERIFY(!catalog.open(path, &error));
    QVERIFY(error.contains(QStringLiteral("metadata")));
    QVERIFY(!catalog.isOpen());
}

QTEST_GUILESS_MAIN(TestUnitPlayerAnalysisCatalog)

#include "test_unit_player_analysis_catalog.moc"
