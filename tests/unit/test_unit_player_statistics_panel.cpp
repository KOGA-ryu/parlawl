#include <QtTest>

#include <algorithm>

#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUuid>

#include "player_statistics_panel.h"

namespace {

QJsonObject aggregate(int decisions, int p50, int p90, int maximum)
{
    const bool observed = decisions > 0;
    QJsonArray statuses;
    if (observed) {
        statuses.append(QJsonObject {
            {QStringLiteral("move_count"), decisions},
            {QStringLiteral("status"), QStringLiteral("derived_clock_difference")},
        });
    }
    QJsonArray pressure;
    for (const int threshold : {1'000, 5'000, 10'000, 30'000, 60'000}) {
        pressure.append(QJsonObject {
            {QStringLiteral("move_count"), 0},
            {QStringLiteral("threshold_ms"), threshold},
        });
    }
    return QJsonObject {
        {QStringLiteral("decision_count"), decisions},
        {QStringLiteral("decision_start_clock_observed_count"), decisions},
        {QStringLiteral("elapsed_coverage_ppm"), observed ? QJsonValue(1'000'000) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("elapsed_missing_count"), 0},
        {QStringLiteral("elapsed_observed_count"), decisions},
        {QStringLiteral("elapsed_status_counts"), statuses},
        {QStringLiteral("maximum_observed_elapsed_ms"), observed ? QJsonValue(maximum) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("observed_elapsed_sum_ms"), observed ? QJsonValue(p50 + p90) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("p50_observed_elapsed_ms"), observed ? QJsonValue(p50) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("p90_observed_elapsed_ms"), observed ? QJsonValue(p90) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("pressure_counts"), pressure},
    };
}

QJsonArray phases(int decisions, int p50, int p90, int maximum)
{
    return QJsonArray {
        QJsonObject {
            {QStringLiteral("group"), QStringLiteral("opening")},
            {QStringLiteral("statistics"), aggregate(decisions, p50, p90, maximum)},
        },
        QJsonObject {
            {QStringLiteral("group"), QStringLiteral("middlegame")},
            {QStringLiteral("statistics"), aggregate(0, 0, 0, 0)},
        },
        QJsonObject {
            {QStringLiteral("group"), QStringLiteral("endgame")},
            {QStringLiteral("statistics"), aggregate(0, 0, 0, 0)},
        },
    };
}

QJsonArray colors(
    int whiteDecisions,
    int blackDecisions,
    int p50,
    int p90,
    int maximum)
{
    return QJsonArray {
        QJsonObject {
            {QStringLiteral("group"), QStringLiteral("white")},
            {QStringLiteral("statistics"), aggregate(whiteDecisions, p50, p90, maximum)},
        },
        QJsonObject {
            {QStringLiteral("group"), QStringLiteral("black")},
            {QStringLiteral("statistics"), aggregate(blackDecisions, p50, p90, maximum)},
        },
    };
}

QJsonArray forcedness(int decisions, int p50, int p90, int maximum)
{
    return QJsonArray {
        QJsonObject {
            {QStringLiteral("group"), QStringLiteral("forced-single-legal-move")},
            {QStringLiteral("statistics"), aggregate(0, 0, 0, 0)},
        },
        QJsonObject {
            {QStringLiteral("group"), QStringLiteral("nonforced")},
            {QStringLiteral("statistics"), aggregate(decisions, p50, p90, maximum)},
        },
    };
}

QJsonObject longestMove(
    const QString &player,
    const QString &opponent,
    const QString &color,
    int elapsed)
{
    return QJsonObject {
        {QStringLiteral("decision_start_clock_ms"), 180'000},
        {QStringLiteral("elapsed_move_ms"), elapsed},
        {QStringLiteral("event_start_utc"), QStringLiteral("2026-08-01T12:00:00Z")},
        {QStringLiteral("opponent_id"), opponent},
        {QStringLiteral("played_move_san"), QStringLiteral("e4")},
        {QStringLiteral("played_move_uci"), QStringLiteral("e2e4")},
        {QStringLiteral("player_color"), color},
        {QStringLiteral("player_id"), player},
        {QStringLiteral("ply"), color == QStringLiteral("white") ? 1 : 2},
        {QStringLiteral("position_phase"), QStringLiteral("opening")},
        {QStringLiteral("source_game_id"), QStringLiteral("chesscom-game-v1:fixture")},
    };
}

QJsonObject player(
    const QString &playerId,
    const QString &opponentId,
    const QString &color,
    int elapsed)
{
    const QJsonObject stats = aggregate(1, elapsed, elapsed, elapsed);
    return QJsonObject {
        {QStringLiteral("black_game_count"), color == QStringLiteral("black") ? 1 : 0},
        {QStringLiteral("by_color"), colors(
            color == QStringLiteral("white") ? 1 : 0,
            color == QStringLiteral("black") ? 1 : 0,
            elapsed,
            elapsed,
            elapsed)},
        {QStringLiteral("by_forcedness"), forcedness(1, elapsed, elapsed, elapsed)},
        {QStringLiteral("by_phase"), phases(1, elapsed, elapsed, elapsed)},
        {QStringLiteral("distinct_opponent_count"), 1},
        {QStringLiteral("distinct_utc_day_count"), 1},
        {QStringLiteral("game_count"), 1},
        {QStringLiteral("longest_observed_moves"), QJsonArray {longestMove(playerId, opponentId, color, elapsed)}},
        {QStringLiteral("opponents"), QJsonArray {QJsonObject {
            {QStringLiteral("game_count"), 1},
            {QStringLiteral("opponent_id"), opponentId},
            {QStringLiteral("player_id"), playerId},
            {QStringLiteral("statistics"), stats},
        }}},
        {QStringLiteral("player_id"), playerId},
        {QStringLiteral("statistics"), stats},
        {QStringLiteral("white_game_count"), color == QStringLiteral("white") ? 1 : 0},
    };
}

QByteArray snapshotBytes()
{
    const QString alpha = QStringLiteral("<b>alpha</b>");
    const QString beta = QStringLiteral("beta");
    const QJsonObject root {
        {QStringLiteral("claim_boundary"), QJsonObject {
            {QStringLiteral("clock_semantics"), QStringLiteral("server-accounted-not-cognitive-time")},
            {QStringLiteral("engine_or_move_quality_joined"), false},
            {QStringLiteral("game_outcome_used"), false},
            {QStringLiteral("mapping_authenticates_source_replay"), false},
            {QStringLiteral("model_prediction_or_pregame_feature"), false},
        }},
        {QStringLiteral("display_schema"), QStringLiteral("chess-player-move-time-statistics-display-v1")},
        {QStringLiteral("global_statistics"), QJsonObject {
            {QStringLiteral("by_color"), colors(1, 1, 1'000, 2'000, 2'000)},
            {QStringLiteral("by_forcedness"), forcedness(2, 1'000, 2'000, 2'000)},
            {QStringLiteral("by_phase"), phases(2, 1'000, 2'000, 2'000)},
            {QStringLiteral("decision_count"), 2},
            {QStringLiteral("distinct_player_count"), 2},
            {QStringLiteral("game_count"), 1},
            {QStringLiteral("longest_observed_moves"), QJsonArray {
                longestMove(alpha, beta, QStringLiteral("white"), 2'000),
                longestMove(beta, alpha, QStringLiteral("black"), 1'000),
            }},
            {QStringLiteral("parsed_target_lineage_occurrence_count"), 1},
            {QStringLiteral("source_bundle_count"), 1},
            {QStringLiteral("statistics"), aggregate(2, 1'000, 2'000, 2'000)},
            {QStringLiteral("target_lineage_occurrence_count"), 1},
            {QStringLiteral("unordered_pair_count"), 1},
            {QStringLiteral("utc_day_count"), 1},
        }},
        {QStringLiteral("players"), QJsonArray {
            player(alpha, beta, QStringLiteral("white"), 2'000),
            player(beta, alpha, QStringLiteral("black"), 1'000),
        }},
        {QStringLiteral("source_plan_v2_id"), QStringLiteral("chess-cohort-source-chunk-plan-v2:fixture")},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
}

QString createExplorerDatabase(QTemporaryDir *directory, bool includeEngine = false)
{
    const QString path = directory->filePath(QStringLiteral("player-explorer.sqlite3"));
    const QString connectionName = QStringLiteral("player-explorer-test-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        if (!database.open()) {
            return QString();
        }
        QSqlQuery query(database);
        const QStringList statements {
            QStringLiteral("CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL)"),
            QStringLiteral(
                "CREATE TABLE games(source_game_id TEXT PRIMARY KEY, canonical_game_url TEXT, "
                "event_start_utc TEXT, event_end_utc TEXT, utc_day TEXT, duration_ms INTEGER, "
                "rated INTEGER, rules TEXT, time_class TEXT, time_control TEXT, "
                "white_player_id TEXT, black_player_id TEXT, "
                "white_rating_postgame_observed INTEGER, black_rating_postgame_observed INTEGER, "
                "result TEXT, ply_count INTEGER, opening_status TEXT, opening_eco TEXT, "
                "opening_name TEXT, opening_last_book_ply INTEGER, opening_classification_id TEXT)"),
            QStringLiteral(
                "CREATE TABLE player_games(source_game_id TEXT, player_id TEXT, opponent_id TEXT, "
                "player_color TEXT, outcome TEXT, player_rating_postgame_observed INTEGER, "
                "opponent_rating_postgame_observed INTEGER, PRIMARY KEY(source_game_id, player_id))"),
            QStringLiteral(
                "CREATE TABLE moves(source_game_id TEXT, ply INTEGER, player_id TEXT, opponent_id TEXT, "
                "player_color TEXT, move_number INTEGER, played_move_san TEXT, played_move_uci TEXT, "
                "position_phase TEXT, forcedness_status TEXT, legal_move_count INTEGER, "
                "decision_start_clock_ms INTEGER, clock_remaining_after_move_ms INTEGER, "
                "elapsed_move_ms INTEGER, elapsed_status TEXT, PRIMARY KEY(source_game_id, ply))"),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                database.close();
                return QString();
            }
        }
        const QMap<QString, QString> metadata {
            {QStringLiteral("clock_semantics"), QStringLiteral("server-accounted-not-cognitive-time")},
            {QStringLiteral("decision_count"), QStringLiteral("8")},
            {QStringLiteral("distinct_player_count"), QStringLiteral("2")},
            {QStringLiteral("engine_evidence_present"), QStringLiteral("false")},
            {QStringLiteral("game_count"), QStringLiteral("2")},
            {QStringLiteral("game_outcomes_are_retrospective"), QStringLiteral("true")},
            {QStringLiteral("mapping_authenticates_source_replay"), QStringLiteral("false")},
            {QStringLiteral("opening_classifier_version"), QStringLiteral("fixture")},
            {QStringLiteral("opening_corpus_id"), QStringLiteral("opening-corpus-v1:fixture")},
            {QStringLiteral("parsed_target_lineage_occurrence_count"), QStringLiteral("2")},
            {QStringLiteral("player_game_count"), QStringLiteral("4")},
            {QStringLiteral("schema_version"), QStringLiteral("chess-player-game-explorer-sqlite-v1")},
            {QStringLiteral("source_bundle_count"), QStringLiteral("1")},
            {QStringLiteral("source_plan_v2_id"), QStringLiteral("chess-cohort-source-chunk-plan-v2:fixture")},
            {QStringLiteral("target_lineage_occurrence_count"), QStringLiteral("2")},
            {QStringLiteral("unordered_pair_count"), QStringLiteral("1")},
        };
        query.prepare(QStringLiteral("INSERT INTO metadata VALUES (?, ?)"));
        for (auto item = metadata.cbegin(); item != metadata.cend(); ++item) {
            query.addBindValue(item.key());
            query.addBindValue(item.value());
            if (!query.exec()) {
                database.close();
                return QString();
            }
        }
        if (!query.exec(QStringLiteral(
            "INSERT INTO games VALUES "
            "('chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111','https://example.test/1','2026-08-01T12:00:00Z','2026-08-01T12:04:00Z','2026-08-01',240000,1,'chess','blitz','180','alpha','beta',2100,2050,'1-0',4,'classified','C20','King''s Pawn Game',2,'opening-1'),"
            "('chesscom-game-v1:2222222222222222222222222222222222222222222222222222222222222222','https://example.test/2','2026-08-02T12:00:00Z','2026-08-02T12:04:00Z','2026-08-02',240000,1,'chess','blitz','180','beta','alpha',2060,2110,'1/2-1/2',4,'unknown',NULL,NULL,NULL,'opening-2')"))) {
            database.close();
            return QString();
        }
        if (!query.exec(QStringLiteral(
            "INSERT INTO player_games VALUES "
            "('chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111','alpha','beta','white','win',2100,2050),"
            "('chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111','beta','alpha','black','loss',2050,2100),"
            "('chesscom-game-v1:2222222222222222222222222222222222222222222222222222222222222222','beta','alpha','white','draw',2060,2110),"
            "('chesscom-game-v1:2222222222222222222222222222222222222222222222222222222222222222','alpha','beta','black','draw',2110,2060)"))) {
            database.close();
            return QString();
        }
        const QStringList moveRows {
            QStringLiteral("('chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111',1,'alpha','beta','white',1,'e4','e2e4','opening','nonforced',20,180000,179000,1000,'derived_clock_difference')"),
            QStringLiteral("('chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111',2,'beta','alpha','black',1,'e5','e7e5','opening','nonforced',20,180000,178000,2000,'derived_clock_difference')"),
            QStringLiteral("('chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111',3,'alpha','beta','white',2,'Nf3','g1f3','opening','nonforced',29,179000,176000,3000,'derived_clock_difference')"),
            QStringLiteral("('chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111',4,'beta','alpha','black',2,'Nc6','b8c6','opening','nonforced',29,178000,174000,4000,'derived_clock_difference')"),
            QStringLiteral("('chesscom-game-v1:2222222222222222222222222222222222222222222222222222222222222222',1,'beta','alpha','white',1,'d4','d2d4','opening','nonforced',20,180000,179500,500,'derived_clock_difference')"),
            QStringLiteral("('chesscom-game-v1:2222222222222222222222222222222222222222222222222222222222222222',2,'alpha','beta','black',1,'d5','d7d5','opening','nonforced',20,180000,178500,1500,'derived_clock_difference')"),
            QStringLiteral("('chesscom-game-v1:2222222222222222222222222222222222222222222222222222222222222222',3,'beta','alpha','white',2,'c4','c2c4','opening','nonforced',28,179500,177000,2500,'derived_clock_difference')"),
            QStringLiteral("('chesscom-game-v1:2222222222222222222222222222222222222222222222222222222222222222',4,'alpha','beta','black',2,'e6','e7e6','opening','nonforced',28,178500,175000,3500,'derived_clock_difference')"),
        };
        if (!query.exec(QStringLiteral("INSERT INTO moves VALUES ") + moveRows.join(QLatin1Char(',')))) {
            database.close();
            return QString();
        }
        if (includeEngine) {
            const QStringList engineStatements {
                QStringLiteral(
                    "CREATE TABLE engine_games(source_game_id TEXT PRIMARY KEY, evidence_id TEXT, "
                    "representative_run_id TEXT, analysis_recorded_at_utc TEXT, lineage_count INTEGER)"),
                QStringLiteral(
                    "CREATE TABLE engine_game_lineages(source_game_id TEXT, source_run_id TEXT, "
                    "cohort_player_id TEXT, source_bundle_id TEXT, json_acquisition_id TEXT, "
                    "pgn_acquisition_id TEXT, analysis_recorded_at_utc TEXT, performance_snapshot_id TEXT, "
                    "PRIMARY KEY(source_game_id, source_run_id))"),
                QStringLiteral(
                    "CREATE TABLE engine_moves(source_game_id TEXT, ply INTEGER, played_move_uci TEXT, mover TEXT, "
                    "expected_before_millionths INTEGER, expected_after_millionths INTEGER, "
                    "wdl_loss_millionths INTEGER, centipawn_loss INTEGER, missed_winning_advantage INTEGER, "
                    "missed_forced_mate INTEGER, severity TEXT, before_score_kind TEXT, "
                    "before_centipawns_white INTEGER, before_mate_for_white INTEGER, "
                    "before_wdl_white_win INTEGER, before_wdl_white_draw INTEGER, before_wdl_white_loss INTEGER, "
                    "before_best_move_uci TEXT, before_depth INTEGER, before_selective_depth INTEGER, "
                    "before_nodes INTEGER, before_pv_uci TEXT, after_score_kind TEXT, "
                    "after_centipawns_white INTEGER, after_mate_for_white INTEGER, "
                    "after_wdl_white_win INTEGER, after_wdl_white_draw INTEGER, after_wdl_white_loss INTEGER, "
                    "PRIMARY KEY(source_game_id, ply))"),
            };
            for (const QString &statement : engineStatements) {
                if (!query.exec(statement)) {
                    database.close();
                    return QString();
                }
            }
            if (!query.exec(QStringLiteral(
                    "UPDATE metadata SET value='chess-player-game-engine-explorer-sqlite-v2' "
                    "WHERE key='schema_version'"))
                || !query.exec(QStringLiteral(
                    "UPDATE metadata SET value='partial' WHERE key='engine_evidence_present'"))) {
                database.close();
                return QString();
            }
            const QMap<QString, QString> engineMetadata {
                {QStringLiteral("engine_adapter_version"), QStringLiteral("stockfish-complete-position-v1")},
                {QStringLiteral("engine_analyzed_game_count"), QStringLiteral("1")},
                {QStringLiteral("engine_author"), QStringLiteral("Stockfish developers")},
                {QStringLiteral("engine_binary_sha256"), QString(64, QLatin1Char('a'))},
                {QStringLiteral("engine_byte_identical_evidence_fallback_store_count"), QStringLiteral("0")},
                {QStringLiteral("engine_claim_boundary"), QStringLiteral("persisted-fixed-node-display-not-objective-truth")},
                {QStringLiteral("engine_config_id"), QStringLiteral("performance-engine-config-v1:") + QString(64, QLatin1Char('b'))},
                {QStringLiteral("engine_hash_mebibytes"), QStringLiteral("16")},
                {QStringLiteral("engine_lineage_count"), QStringLiteral("1")},
                {QStringLiteral("engine_manifest_id"), QStringLiteral("chess-cohort-manifest-v1:") + QString(64, QLatin1Char('c'))},
                {QStringLiteral("engine_name"), QStringLiteral("Stockfish 18")},
                {QStringLiteral("engine_node_limit"), QStringLiteral("1000")},
                {QStringLiteral("engine_target_coverage_ppm"), QStringLiteral("500000")},
                {QStringLiteral("engine_threads"), QStringLiteral("1")},
                {QStringLiteral("engine_transition_count"), QStringLiteral("4")},
                {QStringLiteral("engine_unanalyzed_game_count"), QStringLiteral("1")},
                {QStringLiteral("engine_wdl_loss_thresholds"), QStringLiteral("25000,50000,100000")},
                {QStringLiteral("engine_winning_expectation_millionths"), QStringLiteral("750000")},
                {QStringLiteral("viewer_runs_engine_process"), QStringLiteral("false")},
            };
            query.prepare(QStringLiteral("INSERT INTO metadata VALUES (?, ?)"));
            for (auto item = engineMetadata.cbegin(); item != engineMetadata.cend(); ++item) {
                query.addBindValue(item.key());
                query.addBindValue(item.value());
                if (!query.exec()) {
                    database.close();
                    return QString();
                }
            }
            const QString gameId = QStringLiteral(
                "chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111");
            if (!query.exec(QStringLiteral(
                    "INSERT INTO engine_games VALUES ('%1','player-game-engine-view-v1:%2',"
                    "'performance-analysis-run-v2:%3','2026-08-03T12:00:00Z',1)")
                    .arg(gameId, QString(64, QLatin1Char('d')), QString(64, QLatin1Char('e'))))
                || !query.exec(QStringLiteral(
                    "INSERT INTO engine_game_lineages VALUES ('%1','performance-analysis-run-v2:%2',"
                    "'alpha','bundle','json','pgn','2026-08-03T12:00:00Z','snapshot')")
                    .arg(gameId, QString(64, QLatin1Char('e'))))) {
                database.close();
                return QString();
            }
            const QStringList engineUcis {
                QStringLiteral("e2e4"), QStringLiteral("e7e5"),
                QStringLiteral("g1f3"), QStringLiteral("b8c6"),
            };
            query.prepare(QStringLiteral(
                "INSERT INTO engine_moves VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
            for (int index = 0; index < engineUcis.size(); ++index) {
                const int ply = index + 1;
                const int loss = ply == 3 ? 166'000 : 10'000;
                const QString severity = ply == 3
                    ? QStringLiteral("severe") : QStringLiteral("none");
                const QVariantList values {
                    gameId, ply, engineUcis.at(index),
                    ply % 2 ? QStringLiteral("white") : QStringLiteral("black"),
                    750'000, 750'000 - loss, loss, ply == 3 ? 40 : 5,
                    ply == 3 ? 1 : 0, 0, severity,
                    QStringLiteral("cp"), 100 - index * 10, QVariant(),
                    500, 500, 0, QStringLiteral("d2d4"), 7, 9, 1'000,
                    QStringLiteral("d2d4 d7d5"), QStringLiteral("cp"),
                    90 - index * 10, QVariant(), 450, 550, 0,
                };
                for (const QVariant &value : values) {
                    query.addBindValue(value);
                }
                if (!query.exec()) {
                    database.close();
                    return QString();
                }
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return path;
}

} // namespace

class TestUnitPlayerStatisticsPanel : public QObject
{
    Q_OBJECT

private slots:
    void loadsGlobalSummaryAndConservesRows();
    void selectsPlayerAndTreatsMarkupAsPlainText();
    void rejectsWrongClaimsAndBrokenConservationWithoutReplacingState();
    void exposesOneExplicitOpenAction();
    void loadsExplorerAndAppliesSharedFilters();
    void exposesExactGameBreakdownAndActivation();
    void loadsPartialPersistedEngineEvidenceWithoutFabricatingMissingGame();
    void loadsRealSnapshotWhenProvided();
};

void TestUnitPlayerStatisticsPanel::loadsGlobalSummaryAndConservesRows()
{
    PlayerStatisticsPanel panel;
    QString error;
    QVERIFY2(panel.loadSnapshot(snapshotBytes(), &error), qPrintable(error));
    QVERIFY(panel.hasSnapshot());
    QCOMPARE(panel.playerIds(), QStringList({QStringLiteral("<b>alpha</b>"), QStringLiteral("beta")}));
    QCOMPARE(panel.selectedPlayerId(), QString());
    QVERIFY(panel.summaryText().contains(QStringLiteral("All players · 1 unordered pairs")));
    const QComboBox *selector = panel.findChild<QComboBox *>();
    QVERIFY(selector != nullptr);
    QVERIFY(selector->isEditable());
    QCOMPARE(selector->completer()->caseSensitivity(), Qt::CaseInsensitive);
    QCOMPARE(selector->completer()->filterMode(), Qt::MatchContains);
    const QTabWidget *tabs = panel.findChild<QTabWidget *>(
        QStringLiteral("playerStatisticsDetailTabs"));
    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 5);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Overview"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("Games"));
    QCOMPARE(tabs->tabText(2), QStringLiteral("Openings"));
    QCOMPARE(tabs->tabText(3), QStringLiteral("Opponents"));
    QCOMPARE(tabs->tabText(4), QStringLiteral("Longest Moves"));
    QCOMPARE(panel.phaseRowCount(), 3);
    QCOMPARE(panel.decisionContextRowCount(), 4);
    QCOMPARE(panel.opponentRowCount(), 0);
    QCOMPARE(panel.longestMoveRowCount(), 2);
}

void TestUnitPlayerStatisticsPanel::selectsPlayerAndTreatsMarkupAsPlainText()
{
    PlayerStatisticsPanel panel;
    QString error;
    QVERIFY2(panel.loadSnapshot(snapshotBytes(), &error), qPrintable(error));
    QVERIFY(panel.selectPlayer(QStringLiteral("<b>alpha</b>")));
    QCOMPARE(panel.selectedPlayerId(), QStringLiteral("<b>alpha</b>"));
    QVERIFY(panel.summaryText().contains(QStringLiteral("<b>alpha</b> · 1 White / 0 Black")));
    QCOMPARE(panel.opponentRowCount(), 1);
    QCOMPARE(panel.decisionContextRowCount(), 4);
    QCOMPARE(panel.longestMoveRowCount(), 1);
    bool foundDynamicSummary = false;
    for (const QLabel *label : panel.findChildren<QLabel *>()) {
        if (label->text().contains(QStringLiteral("<b>alpha</b>"))) {
            foundDynamicSummary = true;
            QCOMPARE(label->textFormat(), Qt::PlainText);
        }
    }
    QVERIFY(foundDynamicSummary);

    QComboBox *selector = panel.findChild<QComboBox *>();
    QVERIFY(selector != nullptr);
    selector->setEditText(QStringLiteral("BETA"));
    QTest::keyClick(selector->lineEdit(), Qt::Key_Return);
    QCOMPARE(panel.selectedPlayerId(), QStringLiteral("beta"));
    QVERIFY(panel.summaryText().contains(QStringLiteral("beta · 0 White / 1 Black")));
}

void TestUnitPlayerStatisticsPanel::rejectsWrongClaimsAndBrokenConservationWithoutReplacingState()
{
    PlayerStatisticsPanel panel;
    QString error;
    QVERIFY2(panel.loadSnapshot(snapshotBytes(), &error), qPrintable(error));
    const QString stableSummary = panel.summaryText();

    QJsonObject wrongClaim = QJsonDocument::fromJson(snapshotBytes()).object();
    QJsonObject claims = wrongClaim.value(QStringLiteral("claim_boundary")).toObject();
    claims.insert(QStringLiteral("mapping_authenticates_source_replay"), true);
    wrongClaim.insert(QStringLiteral("claim_boundary"), claims);
    QVERIFY(!panel.loadSnapshot(QJsonDocument(wrongClaim).toJson(QJsonDocument::Compact), &error));
    QVERIFY(error.contains(QStringLiteral("claim boundary")));
    QCOMPARE(panel.summaryText(), stableSummary);

    QJsonObject broken = QJsonDocument::fromJson(snapshotBytes()).object();
    QJsonObject global = broken.value(QStringLiteral("global_statistics")).toObject();
    global.insert(QStringLiteral("decision_count"), 3);
    broken.insert(QStringLiteral("global_statistics"), global);
    QVERIFY(!panel.loadSnapshot(QJsonDocument(broken).toJson(QJsonDocument::Compact), &error));
    QVERIFY(error.contains(QStringLiteral("global")));
    QCOMPARE(panel.summaryText(), stableSummary);
}

void TestUnitPlayerStatisticsPanel::exposesOneExplicitOpenAction()
{
    PlayerStatisticsPanel panel;
    QPushButton *openButton = nullptr;
    for (QPushButton *button : panel.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Open Snapshot")) {
            openButton = button;
            break;
        }
    }
    QVERIFY(openButton != nullptr);
    QSignalSpy spy(&panel, &PlayerStatisticsPanel::openSnapshotRequested);
    openButton->click();
    QCOMPARE(spy.count(), 1);
}

void TestUnitPlayerStatisticsPanel::loadsExplorerAndAppliesSharedFilters()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createExplorerDatabase(&directory);
    QVERIFY(!path.isEmpty());
    PlayerStatisticsPanel panel;
    QString error;
    QVERIFY2(panel.loadExplorerDatabase(path, &error), qPrintable(error));
    QVERIFY(panel.hasSnapshot());
    QCOMPARE(panel.playerIds(), QStringList({QStringLiteral("alpha"), QStringLiteral("beta")}));
    QCOMPARE(panel.selectedPlayerId(), QStringLiteral("alpha"));
    QCOMPARE(panel.gameRowCount(), 2);
    QCOMPARE(panel.openingRowCount(), 2);
    QCOMPARE(panel.phaseRowCount(), 3);
    QCOMPARE(panel.decisionContextRowCount(), 4);
    QCOMPARE(panel.opponentRowCount(), 1);
    QCOMPARE(panel.longestMoveRowCount(), 4);
    QVERIFY(panel.summaryText().contains(QStringLiteral("alpha · 1-1-0")));

    QComboBox *resultFilter = panel.findChild<QComboBox *>(
        QStringLiteral("playerStatisticsResultFilter"));
    QVERIFY(resultFilter != nullptr);
    resultFilter->setCurrentIndex(resultFilter->findData(QStringLiteral("win")));
    QCOMPARE(panel.gameRowCount(), 1);
    QCOMPARE(panel.openingRowCount(), 1);
    QCOMPARE(panel.longestMoveRowCount(), 2);
    QVERIFY(panel.summaryText().contains(QStringLiteral("alpha · 1-0-0")));

    QComboBox *colorFilter = panel.findChild<QComboBox *>(
        QStringLiteral("playerStatisticsColorFilter"));
    QVERIFY(colorFilter != nullptr);
    resultFilter->setCurrentIndex(0);
    colorFilter->setCurrentIndex(colorFilter->findData(QStringLiteral("black")));
    QCOMPARE(panel.gameRowCount(), 1);
    QVERIFY(panel.summaryText().contains(QStringLiteral("alpha · 0-1-0")));
}

void TestUnitPlayerStatisticsPanel::exposesExactGameBreakdownAndActivation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createExplorerDatabase(&directory);
    QVERIFY(!path.isEmpty());
    PlayerStatisticsPanel panel;
    QString error;
    QVERIFY2(panel.loadExplorerDatabase(path, &error), qPrintable(error));

    const QString gameId = QStringLiteral(
        "chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111");
    const auto breakdown = panel.gameBreakdown(gameId, &error);
    QVERIFY2(breakdown.has_value(), qPrintable(error));
    QCOMPARE(breakdown->viewedPlayerId, QStringLiteral("alpha"));
    QCOMPARE(breakdown->viewedPlayerColor, QStringLiteral("white"));
    QVERIFY(breakdown->openingEco.has_value());
    QCOMPARE(*breakdown->openingEco, QStringLiteral("C20"));
    QVERIFY(breakdown->openingLastBookPly.has_value());
    QCOMPARE(*breakdown->openingLastBookPly, 2);
    QCOMPARE(breakdown->moves.size(), 4);
    QCOMPARE(breakdown->moves.at(2).san, QStringLiteral("Nf3"));
    QVERIFY(breakdown->moves.at(3).elapsedMs.has_value());
    QCOMPARE(*breakdown->moves.at(3).elapsedMs, 4'000);

    QTableWidget *table = panel.findChild<QTableWidget *>(
        QStringLiteral("playerStatisticsGamesTable"));
    QVERIFY(table != nullptr);
    QSignalSpy spy(&panel, &PlayerStatisticsPanel::gameBreakdownRequested);
    table->selectRow(0);
    QPushButton *replayButton = panel.findChild<QPushButton *>(
        QStringLiteral("playerStatisticsReplayGame"));
    QVERIFY(replayButton != nullptr);
    QVERIFY(replayButton->isEnabled());
    replayButton->click();
    QCOMPARE(spy.count(), 1);
    const QString emittedId = spy.first().at(0).toString();
    QVERIFY(emittedId == gameId
        || emittedId == QStringLiteral(
            "chesscom-game-v1:2222222222222222222222222222222222222222222222222222222222222222"));
}

void TestUnitPlayerStatisticsPanel::loadsPartialPersistedEngineEvidenceWithoutFabricatingMissingGame()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createExplorerDatabase(&directory, true);
    QVERIFY(!path.isEmpty());
    PlayerStatisticsPanel panel;
    QString error;
    QVERIFY2(panel.loadExplorerDatabase(path, &error), qPrintable(error));
    QVERIFY(panel.summaryText().contains(QStringLiteral("alpha")));

    const QString analyzedId = QStringLiteral(
        "chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111");
    const auto analyzed = panel.gameBreakdown(analyzedId, &error);
    QVERIFY2(analyzed.has_value(), qPrintable(error));
    QVERIFY(analyzed->engineEvidence.has_value());
    QCOMPARE(analyzed->engineEvidence->engineName, QStringLiteral("Stockfish 18"));
    QCOMPARE(analyzed->engineEvidence->nodeLimit, 1'000);
    QCOMPARE(analyzed->moves.size(), 4);
    QVERIFY(analyzed->moves.at(2).engineEvidence.has_value());
    QCOMPARE(analyzed->moves.at(2).engineEvidence->severity, QStringLiteral("severe"));
    QCOMPARE(analyzed->moves.at(2).engineEvidence->beforeBestMoveUci.value_or(QString()), QStringLiteral("d2d4"));
    QCOMPARE(analyzed->moves.at(2).engineEvidence->beforePvUci, QStringLiteral("d2d4 d7d5"));

    const QString missingId = QStringLiteral(
        "chesscom-game-v1:2222222222222222222222222222222222222222222222222222222222222222");
    const auto missing = panel.gameBreakdown(missingId, &error);
    QVERIFY2(missing.has_value(), qPrintable(error));
    QVERIFY(!missing->engineEvidence.has_value());
    QVERIFY(std::all_of(missing->moves.cbegin(), missing->moves.cend(), [](const PlayerStatisticsGameMove &move) {
        return !move.engineEvidence.has_value();
    }));
}

void TestUnitPlayerStatisticsPanel::loadsRealSnapshotWhenProvided()
{
    const QString path = qEnvironmentVariable("PARLAWL_REAL_PLAYER_STATISTICS_SNAPSHOT");
    if (path.isEmpty()) {
        QSKIP("real player-statistics snapshot was not requested");
    }
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const QByteArray raw = file.readAll();
    PlayerStatisticsPanel panel;
    QString error;
    QVERIFY2(panel.loadSnapshot(raw, &error), qPrintable(error));
    QCOMPARE(panel.playerIds().size(), 701);
    QVERIFY(panel.summaryText().contains(QStringLiteral("975 unordered pairs")));
    QCOMPARE(panel.phaseRowCount(), 3);
    QCOMPARE(panel.decisionContextRowCount(), 4);
    QCOMPARE(panel.opponentRowCount(), 0);
    QCOMPARE(panel.longestMoveRowCount(), 20);
}

QTEST_MAIN(TestUnitPlayerStatisticsPanel)

#include "test_unit_player_statistics_panel.moc"
