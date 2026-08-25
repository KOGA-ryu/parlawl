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
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUuid>

#include "game_explorer_window.h"
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

QStringList structureMetricCodes()
{
    QStringList output;
    const QStringList phases {
        QStringLiteral("all"), QStringLiteral("opening"),
        QStringLiteral("middlegame"), QStringLiteral("endgame"),
    };
    for (const QString &predicate : {
             QStringLiteral("both_queens_absent"),
             QStringLiteral("own_passed_pawn_present"),
             QStringLiteral("own_isolated_pawn_present"),
             QStringLiteral("own_doubled_pawn_excess_present"),
             QStringLiteral("own_two_or_more_bishops_present"),
         }) {
        for (const QString &phase : phases) {
            output.append(QStringLiteral("binary.%1.%2.share").arg(predicate, phase));
        }
    }
    for (const QString &state : {
             QStringLiteral("ahead"), QStringLiteral("equal"),
             QStringLiteral("behind"),
         }) {
        for (const QString &phase : phases) {
            output.append(QStringLiteral("material_relation.%1.%2.share").arg(state, phase));
        }
    }
    for (const QString &phase : phases) {
        output.append(QStringLiteral("material_delta_mean.%1").arg(phase));
    }
    output.append(QStringLiteral("focal_castling.any"));
    output.append(QStringLiteral("focal_castling.kingside"));
    output.append(QStringLiteral("focal_castling.queenside"));
    return output;
}

qint64 structurePlayerNumerator(const QString &code, bool alpha)
{
    if (code.startsWith(QStringLiteral("material_delta_mean."))) {
        return alpha ? 1 : -1;
    }
    if (code.startsWith(QStringLiteral("material_relation.equal."))
        || code == QStringLiteral("focal_castling.queenside")) {
        return 0;
    }
    if (code.startsWith(QStringLiteral("material_relation.behind."))) {
        return alpha ? 0 : 1;
    }
    return alpha ? 1 : 0;
}

QJsonObject structureMetric(const QString &code, bool alpha, qint64 games)
{
    const qint64 value = structurePlayerNumerator(code, alpha);
    const qint64 numerator = value * games;
    return QJsonObject {
        {QStringLiteral("aggregate_status"), QStringLiteral("observed")},
        {QStringLiteral("aggregate_value_ppm"), value * 1'000'000},
        {QStringLiteral("denominator_sum"), games},
        {QStringLiteral("mean_player_minus_opponent_ppm"), 0},
        {QStringLiteral("metric_code"), code},
        {QStringLiteral("not_applicable_player_game_count"), 0},
        {QStringLiteral("numerator_sum"), numerator},
        {QStringLiteral("observed_player_game_count"), games},
        {QStringLiteral("paired_player_game_count"), games},
    };
}

QJsonArray structurePlayerMetrics(bool alpha, qint64 games)
{
    QJsonArray output;
    for (const QString &code : structureMetricCodes()) {
        output.append(structureMetric(code, alpha, games));
    }
    return output;
}

QJsonArray structureGlobalMetrics()
{
    QJsonArray output;
    for (const QString &code : structureMetricCodes()) {
        const qint64 numerator = 3 * structurePlayerNumerator(code, true)
            + 7 * structurePlayerNumerator(code, false);
        output.append(QJsonObject {
            {QStringLiteral("aggregate_status"), QStringLiteral("observed")},
            {QStringLiteral("aggregate_value_ppm"), numerator * 100'000},
            {QStringLiteral("denominator_sum"), 10},
            {QStringLiteral("mean_player_minus_opponent_ppm"), 0},
            {QStringLiteral("metric_code"), code},
            {QStringLiteral("not_applicable_player_game_count"), 0},
            {QStringLiteral("numerator_sum"), numerator},
            {QStringLiteral("observed_player_game_count"), 10},
            {QStringLiteral("paired_player_game_count"), 10},
        });
    }
    return output;
}

QString structurePairId(QChar digestCharacter)
{
    return QStringLiteral("chess-unordered-player-pair-v1:")
        + QString(64, digestCharacter);
}

QJsonObject structureHeadToHeadRow(
    const QString &playerId,
    const QString &opponentId,
    const QString &pairId,
    qint64 games,
    qint64 white,
    qint64 black,
    qint64 wins,
    qint64 draws,
    qint64 losses)
{
    const qint64 score = ((2 * wins + draws) * 1'000'000 + games)
        / (2 * games);
    return QJsonObject {
        {QStringLiteral("black_game_count"), black},
        {QStringLiteral("draw_count"), draws},
        {QStringLiteral("game_count"), games},
        {QStringLiteral("loss_count"), losses},
        {QStringLiteral("opponent_id"), opponentId},
        {QStringLiteral("player_id"), playerId},
        {QStringLiteral("score_rate_ppm"), score},
        {QStringLiteral("unordered_pair_id"), pairId},
        {QStringLiteral("white_game_count"), white},
        {QStringLiteral("win_count"), wins},
    };
}

QJsonObject structurePlayer(const QString &playerId, const QJsonArray &headToHead)
{
    qint64 games = 0;
    qint64 white = 0;
    qint64 black = 0;
    qint64 wins = 0;
    qint64 draws = 0;
    qint64 losses = 0;
    qint64 squaredGames = 0;
    qint64 largestOpponentGames = -1;
    QString largestOpponent;
    for (const QJsonValue &value : headToHead) {
        const QJsonObject row = value.toObject();
        const qint64 rowGames = static_cast<qint64>(
            row.value(QStringLiteral("game_count")).toDouble());
        const QString opponent = row.value(QStringLiteral("opponent_id")).toString();
        games += rowGames;
        white += static_cast<qint64>(row.value(
            QStringLiteral("white_game_count")).toDouble());
        black += static_cast<qint64>(row.value(
            QStringLiteral("black_game_count")).toDouble());
        wins += static_cast<qint64>(row.value(
            QStringLiteral("win_count")).toDouble());
        draws += static_cast<qint64>(row.value(
            QStringLiteral("draw_count")).toDouble());
        losses += static_cast<qint64>(row.value(
            QStringLiteral("loss_count")).toDouble());
        squaredGames += rowGames * rowGames;
        if (rowGames > largestOpponentGames
            || (rowGames == largestOpponentGames && opponent < largestOpponent)) {
            largestOpponent = opponent;
            largestOpponentGames = rowGames;
        }
    }
    return QJsonObject {
        {QStringLiteral("black_game_count"), black},
        {QStringLiteral("distinct_opponent_count"), headToHead.size()},
        {QStringLiteral("distinct_utc_day_count"), 1},
        {QStringLiteral("draw_count"), draws},
        {QStringLiteral("game_count"), games},
        {QStringLiteral("head_to_head"), headToHead},
        {QStringLiteral("largest_opponent_game_count"), largestOpponentGames},
        {QStringLiteral("largest_opponent_game_share_ppm"),
            (largestOpponentGames * 1'000'000 + games / 2) / games},
        {QStringLiteral("largest_opponent_id"), largestOpponent},
        {QStringLiteral("loss_count"), losses},
        {QStringLiteral("metrics"), structurePlayerMetrics(
            playerId == QStringLiteral("alpha"), games)},
        {QStringLiteral("opponent_hhi_ppm"),
            (squaredGames * 1'000'000 + games * games / 2) / (games * games)},
        {QStringLiteral("player_id"), playerId},
        {QStringLiteral("score_rate_ppm"),
            ((2 * wins + draws) * 1'000'000 + games) / (2 * games)},
        {QStringLiteral("white_game_count"), white},
        {QStringLiteral("win_count"), wins},
    };
}

QByteArray structureSnapshotBytes()
{
    const QString alphaBetaPair = structurePairId(QLatin1Char('d'));
    const QString alphaGammaPair = structurePairId(QLatin1Char('a'));
    const QString betaDeltaPair = structurePairId(QLatin1Char('b'));
    const QJsonArray alphaRows {
        structureHeadToHeadRow(
            QStringLiteral("alpha"), QStringLiteral("beta"), alphaBetaPair,
            1, 1, 0, 1, 0, 0),
        structureHeadToHeadRow(
            QStringLiteral("alpha"), QStringLiteral("gamma"), alphaGammaPair,
            2, 1, 1, 0, 1, 1),
    };
    const QJsonArray betaRows {
        structureHeadToHeadRow(
            QStringLiteral("beta"), QStringLiteral("alpha"), alphaBetaPair,
            1, 0, 1, 0, 0, 1),
        structureHeadToHeadRow(
            QStringLiteral("beta"), QStringLiteral("delta"), betaDeltaPair,
            2, 1, 1, 1, 0, 1),
    };
    const QJsonArray deltaRows {
        structureHeadToHeadRow(
            QStringLiteral("delta"), QStringLiteral("beta"), betaDeltaPair,
            2, 1, 1, 1, 0, 1),
    };
    const QJsonArray gammaRows {
        structureHeadToHeadRow(
            QStringLiteral("gamma"), QStringLiteral("alpha"), alphaGammaPair,
            2, 1, 1, 1, 1, 0),
    };
    const QJsonObject root {
        {QStringLiteral("claim_boundary"), QJsonObject {
            {QStringLiteral("board_metrics_are_postgame_mechanical_descriptions"), true},
            {QStringLiteral("chunk_membership_defines_history"), false},
            {QStringLiteral("descriptive_outcomes_only"), true},
            {QStringLiteral("effective_sample_size_claim"), false},
            {QStringLiteral("independence_claim"), false},
            {QStringLiteral("model_or_prediction"), false},
            {QStringLiteral("pregame_feature_claim"), false},
            {QStringLiteral("style_intent_skill_quality_or_causality"), false},
        }},
        {QStringLiteral("descriptive_metric_registry_id"), QStringLiteral(
            "chess-board-structure-descriptive-metric-registry-v1:f7d33c22e381a574ea1f0a29ebbf5eae5c7da332b9b8fa2d500b2f3e2585676e")},
        {QStringLiteral("display_schema"), QStringLiteral(
            "chess-board-structure-player-statistics-display-v1")},
        {QStringLiteral("global_statistics"), QJsonObject {
            {QStringLiteral("black_player_game_count"), 5},
            {QStringLiteral("black_win_game_count"), 0},
            {QStringLiteral("distinct_opponent_count"), 4},
            {QStringLiteral("distinct_player_count"), 4},
            {QStringLiteral("draw_game_count"), 1},
            {QStringLiteral("game_count"), 5},
            {QStringLiteral("largest_pair_game_count"), 2},
            {QStringLiteral("largest_pair_game_share_ppm"), 400'000},
            {QStringLiteral("largest_pair_player_ids"), QJsonArray {
                QStringLiteral("alpha"), QStringLiteral("gamma")}},
            {QStringLiteral("largest_player_exposure_share_ppm"), 300'000},
            {QStringLiteral("largest_player_game_count"), 3},
            {QStringLiteral("largest_player_game_share_ppm"), 600'000},
            {QStringLiteral("largest_player_id"), QStringLiteral("alpha")},
            {QStringLiteral("largest_unordered_pair_id"), alphaGammaPair},
            {QStringLiteral("metrics"), structureGlobalMetrics()},
            {QStringLiteral("pair_hhi_ppm"), 360'000},
            {QStringLiteral("player_exposure_hhi_ppm"), 260'000},
            {QStringLiteral("player_game_count"), 10},
            {QStringLiteral("player_game_draw_count"), 2},
            {QStringLiteral("player_game_loss_count"), 4},
            {QStringLiteral("player_game_win_count"), 4},
            {QStringLiteral("unordered_pair_count"), 3},
            {QStringLiteral("utc_day_count"), 1},
            {QStringLiteral("white_player_game_count"), 5},
            {QStringLiteral("white_win_game_count"), 4},
        }},
        {QStringLiteral("players"), QJsonArray {
            structurePlayer(QStringLiteral("alpha"), alphaRows),
            structurePlayer(QStringLiteral("beta"), betaRows),
            structurePlayer(QStringLiteral("delta"), deltaRows),
            structurePlayer(QStringLiteral("gamma"), gammaRows),
        }},
        {QStringLiteral("source_plan_v2_id"), QStringLiteral("chess-cohort-source-chunk-plan-v2:fixture")},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
}

QString createExplorerDatabase(
    QTemporaryDir *directory,
    bool includeEngine = false,
    bool includeAnalysisCatalog = false)
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
        QStringList statements {
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
        if (includeAnalysisCatalog) {
            statements.append({
                QStringLiteral(
                    "CREATE TABLE board_structure_metric_definitions("
                    "metric_code TEXT PRIMARY KEY, ordinal INTEGER, definition_id TEXT, "
                    "category TEXT, phase TEXT, value_semantics TEXT, opportunity_unit TEXT)"),
                QStringLiteral(
                    "CREATE TABLE board_structure_player_games("
                    "structural_player_game_id TEXT PRIMARY KEY, vector_id TEXT, "
                    "source_game_id TEXT, player_id TEXT, opponent_id TEXT, player_color TEXT, "
                    "player_manifest_tracked INTEGER, opponent_manifest_tracked INTEGER, "
                    "unordered_pair_id TEXT, utc_day TEXT, normalized_control_id TEXT, "
                    "event_start_utc TEXT, retrospective_available_after_utc TEXT, "
                    "source_available_at_utc TEXT, history_eligibility_status TEXT, "
                    "history_eligibility_reason_code TEXT, metric_registry_id TEXT)"),
                QStringLiteral(
                    "CREATE TABLE board_structure_measurements("
                    "structural_player_game_id TEXT, metric_code TEXT, measurement_id TEXT, "
                    "definition_id TEXT, upstream_receipt_id TEXT, status TEXT, reason_code TEXT, "
                    "numerator INTEGER, denominator INTEGER, value_ppm INTEGER, "
                    "PRIMARY KEY(structural_player_game_id, metric_code))"),
            });
        }
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                database.close();
                return QString();
            }
        }
        QMap<QString, QString> metadata {
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
        if (includeAnalysisCatalog) {
            metadata.insert(QStringLiteral("board_structure_game_count"), QStringLiteral("2"));
            metadata.insert(
                QStringLiteral("board_structure_measurement_count"),
                QString::number(4 * structureMetricCodes().size()));
            metadata.insert(
                QStringLiteral("board_structure_player_game_count"),
                QStringLiteral("4"));
            metadata.insert(
                QStringLiteral("board_structure_postgame_descriptive_only"),
                QStringLiteral("true"));
            metadata.insert(
                QStringLiteral("catalog_authenticates_source_replay"),
                QStringLiteral("false"));
            metadata.insert(
                QStringLiteral("catalog_schema_version"),
                QStringLiteral("chess-player-analysis-catalog-sqlite-v1"));
            metadata.insert(
                QStringLiteral("descriptive_metric_registry_id"),
                QStringLiteral(
                    "chess-board-structure-descriptive-metric-registry-v1:")
                    + QString(64, QLatin1Char('f')));
            metadata.insert(
                QStringLiteral("same_game_vectors_are_pregame_features"),
                QStringLiteral("false"));
        }
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
        if (includeAnalysisCatalog) {
            const QString registryId = QStringLiteral(
                "chess-board-structure-descriptive-metric-registry-v1:")
                + QString(64, QLatin1Char('f'));
            query.prepare(QStringLiteral(
                "INSERT INTO board_structure_metric_definitions VALUES (?,?,?,?,?,?,?)"));
            const QStringList metricCodes = structureMetricCodes();
            for (qsizetype ordinal = 0; ordinal < metricCodes.size(); ++ordinal) {
                const QString code = metricCodes.at(ordinal);
                QString category = QStringLiteral("material");
                if (code.startsWith(QStringLiteral("binary.own_passed"))
                    || code.startsWith(QStringLiteral("binary.own_isolated"))
                    || code.startsWith(QStringLiteral("binary.own_doubled"))) {
                    category = QStringLiteral("pawns");
                } else if (code.startsWith(QStringLiteral("binary."))) {
                    category = QStringLiteral("position");
                } else if (code.startsWith(QStringLiteral("focal_castling."))) {
                    category = QStringLiteral("castling");
                }
                const QStringList parts = code.split(QLatin1Char('.'));
                const QString phase = code.startsWith(QStringLiteral("focal_castling."))
                    ? QStringLiteral("game")
                    : (code.startsWith(QStringLiteral("material_delta_mean."))
                        ? parts.at(1) : parts.at(2));
                const QVariantList values {
                    code,
                    ordinal,
                    QStringLiteral("definition-%1").arg(ordinal),
                    category,
                    phase,
                    code.startsWith(QStringLiteral("material_delta_mean."))
                        ? QStringLiteral("signed_mean_ppm")
                        : QStringLiteral("proportion_ppm"),
                    QStringLiteral("fixture-opportunity"),
                };
                for (int index = 0; index < values.size(); ++index) {
                    query.bindValue(index, values.at(index));
                }
                if (!query.exec()) {
                    database.close();
                    return QString();
                }
            }

            const QString firstGame = QStringLiteral(
                "chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111");
            const QString secondGame = QStringLiteral(
                "chesscom-game-v1:2222222222222222222222222222222222222222222222222222222222222222");
            struct StructuralRow {
                QString structuralId;
                QString gameId;
                QString playerId;
                QString opponentId;
                QString color;
                QString day;
                QString eventStart;
            };
            const QVector<StructuralRow> structuralRows {
                {QStringLiteral("s1"), firstGame, QStringLiteral("alpha"),
                 QStringLiteral("beta"), QStringLiteral("white"),
                 QStringLiteral("2026-08-01"), QStringLiteral("2026-08-01T12:00:00Z")},
                {QStringLiteral("s2"), firstGame, QStringLiteral("beta"),
                 QStringLiteral("alpha"), QStringLiteral("black"),
                 QStringLiteral("2026-08-01"), QStringLiteral("2026-08-01T12:00:00Z")},
                {QStringLiteral("s3"), secondGame, QStringLiteral("beta"),
                 QStringLiteral("alpha"), QStringLiteral("white"),
                 QStringLiteral("2026-08-02"), QStringLiteral("2026-08-02T12:00:00Z")},
                {QStringLiteral("s4"), secondGame, QStringLiteral("alpha"),
                 QStringLiteral("beta"), QStringLiteral("black"),
                 QStringLiteral("2026-08-02"), QStringLiteral("2026-08-02T12:00:00Z")},
            };
            query.prepare(QStringLiteral(
                "INSERT INTO board_structure_player_games VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
            for (const StructuralRow &row : structuralRows) {
                const QVariantList values {
                    row.structuralId,
                    QStringLiteral("vector-") + row.structuralId,
                    row.gameId,
                    row.playerId,
                    row.opponentId,
                    row.color,
                    1,
                    1,
                    structurePairId(QLatin1Char('d')),
                    row.day,
                    QStringLiteral("blitz:180"),
                    row.eventStart,
                    row.eventStart,
                    row.eventStart,
                    QStringLiteral("descriptive-only"),
                    QStringLiteral("not-pregame"),
                    registryId,
                };
                for (int index = 0; index < values.size(); ++index) {
                    query.bindValue(index, values.at(index));
                }
                if (!query.exec()) {
                    database.close();
                    return QString();
                }
            }

            query.prepare(QStringLiteral(
                "INSERT INTO board_structure_measurements VALUES (?,?,?,?,?,?,?,?,?,?)"));
            for (const StructuralRow &row : structuralRows) {
                for (qsizetype ordinal = 0; ordinal < metricCodes.size(); ++ordinal) {
                    const QString code = metricCodes.at(ordinal);
                    const bool notApplicable = code
                        == QStringLiteral("binary.both_queens_absent.endgame.share");
                    const qint64 numerator = structurePlayerNumerator(
                        code, row.playerId == QStringLiteral("alpha"));
                    const QVariantList values {
                        row.structuralId,
                        code,
                        QStringLiteral("measurement-%1-%2")
                            .arg(row.structuralId).arg(ordinal),
                        QStringLiteral("definition-%1").arg(ordinal),
                        QStringLiteral("upstream-%1-%2")
                            .arg(row.structuralId).arg(ordinal),
                        notApplicable ? QStringLiteral("not_applicable")
                                      : QStringLiteral("observed"),
                        notApplicable ? QVariant(QStringLiteral("no-opportunity"))
                                      : QVariant(),
                        notApplicable ? QVariant() : QVariant(numerator),
                        notApplicable ? 0 : 1,
                        notApplicable ? QVariant() : QVariant(numerator * 1'000'000),
                    };
                    for (int index = 0; index < values.size(); ++index) {
                        query.bindValue(index, values.at(index));
                    }
                    if (!query.exec()) {
                        database.close();
                        return QString();
                    }
                }
            }
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
    void dedicatedExplorerSeparatesStudyFromLegacyShell();
    void loadsPartialPersistedEngineEvidenceWithoutFabricatingMissingGame();
    void loadsBoardStructureAlongsideExplorerAndSwitchesPerspective();
    void rejectsBrokenBoardStructureWithoutReplacingState();
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
    QCOMPARE(tabs->count(), 6);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Overview"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("Games"));
    QCOMPARE(tabs->tabText(2), QStringLiteral("Openings"));
    QCOMPARE(tabs->tabText(3), QStringLiteral("Opponents"));
    QCOMPARE(tabs->tabText(4), QStringLiteral("Longest Moves"));
    QCOMPARE(tabs->tabText(5), QStringLiteral("Board Structure"));
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
    const QString path = createExplorerDatabase(&directory, false, true);
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

    QVERIFY(panel.hasBoardStructureSnapshot());
    QCOMPARE(panel.boardStructureMetricRowCount(), 9);
    QCOMPARE(panel.boardStructureCastlingRowCount(), 3);
    const QLabel *structureStatus = panel.findChild<QLabel *>(
        QStringLiteral("playerStatisticsStructureStatus"));
    QTableWidget *structureMetrics = panel.findChild<QTableWidget *>(
        QStringLiteral("playerStatisticsBoardStructureTable"));
    QComboBox *comparisonPlayer = panel.findChild<QComboBox *>(
        QStringLiteral("playerStatisticsBoardStructureComparePlayer"));
    QTableWidget *comparisonTable = panel.findChild<QTableWidget *>(
        QStringLiteral("playerStatisticsBoardStructureComparisonTable"));
    QLabel *drilldownLabel = panel.findChild<QLabel *>(
        QStringLiteral("playerStatisticsBoardStructureDrilldownLabel"));
    QTableWidget *drilldownTable = panel.findChild<QTableWidget *>(
        QStringLiteral("playerStatisticsBoardStructureDrilldownTable"));
    QVERIFY(structureStatus != nullptr);
    QVERIFY(structureMetrics != nullptr);
    QVERIFY(comparisonPlayer != nullptr);
    QVERIFY(comparisonTable != nullptr);
    QVERIFY(drilldownLabel != nullptr);
    QVERIFY(drilldownTable != nullptr);
    QVERIFY(structureStatus->text().contains(QStringLiteral("Read-only analysis catalog")));
    QVERIFY(structureMetrics->item(0, 1)->text().contains(QStringLiteral("100.0%")));
    QTabWidget *tabs = panel.findChild<QTabWidget *>(
        QStringLiteral("playerStatisticsDetailTabs"));
    QWidget *explorerFilters = panel.findChild<QWidget *>(
        QStringLiteral("playerStatisticsExplorerFilters"));
    QVERIFY(tabs != nullptr);
    QVERIFY(explorerFilters != nullptr);
    tabs->setCurrentIndex(tabs->count() - 1);
    QVERIFY(explorerFilters->isHidden());

    const int betaIndex = comparisonPlayer->findData(QStringLiteral("beta"));
    QVERIFY(betaIndex > 0);
    comparisonPlayer->setCurrentIndex(betaIndex);
    QCOMPARE(comparisonTable->rowCount(), 39);
    int queensOverallRow = -1;
    for (int row = 0; row < comparisonTable->rowCount(); ++row) {
        if (comparisonTable->item(row, 0)->text()
            == QStringLiteral("Queens off · Overall")) {
            queensOverallRow = row;
            break;
        }
    }
    QVERIFY(queensOverallRow >= 0);
    QVERIFY(QMetaObject::invokeMethod(
        comparisonTable,
        "cellDoubleClicked",
        Qt::DirectConnection,
        Q_ARG(int, queensOverallRow),
        Q_ARG(int, 1)));
    QCOMPARE(drilldownTable->rowCount(), 2);
    QVERIFY(drilldownLabel->text().contains(QStringLiteral("alpha")));
    QVERIFY(drilldownLabel->text().contains(QStringLiteral("2 rows (2 observed / 0 N/A)")));
    QCOMPARE(drilldownTable->item(0, 1)->text(), QStringLiteral("beta"));
    QCOMPARE(drilldownTable->item(0, 3)->text(), QStringLiteral("Win"));
    QSignalSpy breakdownSpy(&panel, &PlayerStatisticsPanel::gameBreakdownRequested);
    QVERIFY(QMetaObject::invokeMethod(
        drilldownTable,
        "cellDoubleClicked",
        Qt::DirectConnection,
        Q_ARG(int, 0),
        Q_ARG(int, 0)));
    QCOMPARE(breakdownSpy.count(), 1);
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

void TestUnitPlayerStatisticsPanel::dedicatedExplorerSeparatesStudyFromLegacyShell()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createExplorerDatabase(&directory);
    QVERIFY(!path.isEmpty());

    GameExplorerWindow explorer;
    QString error;
    QVERIFY2(explorer.loadExplorerDatabase(path, &error), qPrintable(error));
    QVERIFY(explorer.selectPlayer(QStringLiteral("alpha")));
    QCOMPARE(explorer.windowTitle(), QStringLiteral("Player Explorer"));

    PlayerStatisticsPanel *panel = explorer.panel();
    QVERIFY(panel != nullptr);
    QCOMPARE(panel->title(), QString());
    auto *tabs = panel->findChild<QTabWidget *>(
        QStringLiteral("playerStatisticsDetailTabs"));
    auto *status = panel->findChild<QLabel *>(
        QStringLiteral("playerStatisticsSourceStatus"));
    auto *gameTable = panel->findChild<QTableWidget *>(
        QStringLiteral("playerStatisticsGamesTable"));
    QVERIFY(tabs != nullptr);
    QVERIFY(status != nullptr);
    QVERIFY(gameTable != nullptr);
    QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(status->text(),
        QStringLiteral("Local read-only library · results and openings · no engine evidence"));
    QCOMPARE(gameTable->accessibleName(), QStringLiteral("Game library"));
    auto *openReview = panel->findChild<QPushButton *>(
        QStringLiteral("playerStatisticsReplayGame"));
    QVERIFY(openReview != nullptr);
    QCOMPARE(openReview->text(), QStringLiteral("Open Game Review"));

    const QString gameId = QStringLiteral("chesscom-game-v1:")
        + QString(64, QLatin1Char('1'));
    QVERIFY2(explorer.gameBreakdown(gameId, &error).has_value(), qPrintable(error));
    QSignalSpy spy(&explorer, &GameExplorerWindow::gameBreakdownRequested);
    explorer.surface();
    QVERIFY(explorer.isVisible());
    gameTable->selectRow(0);
    const QString selectedGameId = gameTable->item(0, 0)->data(
        Qt::UserRole + 1).toString();
    QVERIFY(QMetaObject::invokeMethod(
        gameTable,
        "cellDoubleClicked",
        Qt::DirectConnection,
        Q_ARG(int, 0),
        Q_ARG(int, 0)));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), selectedGameId);
    spy.clear();
    QObject *openShortcut = panel->findChild<QObject *>(
        QStringLiteral("playerStatisticsOpenSelectedGameShortcut"));
    QVERIFY(openShortcut != nullptr);
    QVERIFY(QMetaObject::invokeMethod(
        openShortcut,
        "activated",
        Qt::DirectConnection));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), selectedGameId);
    explorer.close();
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

void TestUnitPlayerStatisticsPanel::loadsBoardStructureAlongsideExplorerAndSwitchesPerspective()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createExplorerDatabase(&directory);
    QVERIFY(!path.isEmpty());
    PlayerStatisticsPanel panel;
    QString error;
    QVERIFY2(panel.loadExplorerDatabase(path, &error), qPrintable(error));
    QCOMPARE(panel.selectedPlayerId(), QStringLiteral("alpha"));
    const int explorerGames = panel.gameRowCount();

    QVERIFY2(panel.loadBoardStructureSnapshot(structureSnapshotBytes(), &error), qPrintable(error));
    QVERIFY(panel.hasBoardStructureSnapshot());
    QCOMPARE(panel.gameRowCount(), explorerGames);
    QCOMPARE(panel.boardStructureMetricRowCount(), 9);
    QCOMPARE(panel.boardStructureCastlingRowCount(), 3);
    const QLabel *summary = panel.findChild<QLabel *>(
        QStringLiteral("playerStatisticsStructureSummary"));
    QVERIFY(summary != nullptr);
    QVERIFY(summary->text().contains(QStringLiteral("alpha · 3 games")));
    const QLabel *concentration = panel.findChild<QLabel *>(
        QStringLiteral("playerStatisticsStructureConcentration"));
    QVERIFY(concentration != nullptr);
    QVERIFY(concentration->text().contains(QStringLiteral("largest gamma: 2 games (66.7%)")));
    QVERIFY(concentration->text().contains(QStringLiteral("opponent HHI 55.6%")));
    const QTableWidget *metrics = panel.findChild<QTableWidget *>(
        QStringLiteral("playerStatisticsBoardStructureTable"));
    QVERIFY(metrics != nullptr);
    QVERIFY(metrics->item(0, 1)->text().contains(QStringLiteral("100.0%")));
    QVERIFY(metrics->item(0, 1)->text().contains(QStringLiteral("3 obs")));
    QVERIFY(metrics->item(8, 1)->toolTip().contains(QStringLiteral("not engine evaluation"), Qt::CaseInsensitive));
    QTableWidget *headToHead = panel.findChild<QTableWidget *>(
        QStringLiteral("playerStatisticsBoardStructureHeadToHeadTable"));
    QVERIFY(headToHead != nullptr);
    QVERIFY(headToHead->isSortingEnabled());
    QCOMPARE(headToHead->rowCount(), 2);
    QCOMPARE(headToHead->item(0, 0)->text(), QStringLiteral("gamma"));
    QCOMPARE(headToHead->item(0, 1)->text(), QStringLiteral("2"));
    QCOMPARE(headToHead->item(0, 4)->text(), QStringLiteral("0-1-1"));
    QCOMPARE(headToHead->item(0, 5)->text(), QStringLiteral("25.0%"));
    QCOMPARE(headToHead->item(1, 0)->text(), QStringLiteral("beta"));
    QCOMPARE(headToHead->item(1, 1)->text(), QStringLiteral("1"));
    QLineEdit *opponentSearch = panel.findChild<QLineEdit *>(
        QStringLiteral("playerStatisticsBoardStructureOpponentSearch"));
    QSpinBox *minimumGames = panel.findChild<QSpinBox *>(
        QStringLiteral("playerStatisticsBoardStructureMinimumGames"));
    const QLabel *headToHeadLabel = panel.findChild<QLabel *>(
        QStringLiteral("playerStatisticsBoardStructureHeadToHeadLabel"));
    QVERIFY(opponentSearch != nullptr);
    QVERIFY(minimumGames != nullptr);
    QVERIFY(headToHeadLabel != nullptr);
    QVERIFY(headToHeadLabel->text().contains(QStringLiteral("2 of 2")));

    opponentSearch->setText(QStringLiteral("BETA"));
    QCOMPARE(headToHead->rowCount(), 1);
    QCOMPARE(headToHead->item(0, 0)->text(), QStringLiteral("beta"));
    QVERIFY(headToHeadLabel->text().contains(QStringLiteral("1 of 2")));
    opponentSearch->clear();
    minimumGames->setValue(2);
    QCOMPARE(headToHead->rowCount(), 1);
    QCOMPARE(headToHead->item(0, 0)->text(), QStringLiteral("gamma"));
    minimumGames->setValue(1);
    QCOMPARE(headToHead->rowCount(), 2);
    headToHead->sortItems(4, Qt::DescendingOrder);
    QCOMPARE(headToHead->item(0, 0)->text(), QStringLiteral("beta"));
    headToHead->sortItems(5, Qt::AscendingOrder);
    QCOMPARE(headToHead->item(0, 0)->text(), QStringLiteral("gamma"));

    QComboBox *comparisonPlayer = panel.findChild<QComboBox *>(
        QStringLiteral("playerStatisticsBoardStructureComparePlayer"));
    QComboBox *comparisonCategory = panel.findChild<QComboBox *>(
        QStringLiteral("playerStatisticsBoardStructureCompareCategory"));
    const QLabel *comparisonSummary = panel.findChild<QLabel *>(
        QStringLiteral("playerStatisticsBoardStructureComparisonSummary"));
    QTableWidget *comparisonTable = panel.findChild<QTableWidget *>(
        QStringLiteral("playerStatisticsBoardStructureComparisonTable"));
    QVERIFY(comparisonPlayer != nullptr);
    QVERIFY(comparisonCategory != nullptr);
    QVERIFY(comparisonSummary != nullptr);
    QVERIFY(comparisonTable != nullptr);
    const int gammaIndex = comparisonPlayer->findData(QStringLiteral("gamma"));
    QVERIFY(gammaIndex > 0);
    comparisonPlayer->setCurrentIndex(gammaIndex);
    QCOMPARE(comparisonTable->rowCount(), 39);
    QVERIFY(!comparisonTable->isHidden());
    QVERIFY(headToHead->isHidden());
    QVERIFY(comparisonSummary->text().contains(QStringLiteral("alpha profile: 3 games")));
    QVERIFY(comparisonSummary->text().contains(QStringLiteral("gamma profile: 2 games")));
    QVERIFY(comparisonSummary->text().contains(
        QStringLiteral("Direct record from alpha perspective: 2 games")));
    QVERIFY(comparisonSummary->text().contains(QStringLiteral("not opponent-adjusted")));
    QCOMPARE(comparisonTable->horizontalHeaderItem(1)->text(), QStringLiteral("alpha"));
    QCOMPARE(comparisonTable->horizontalHeaderItem(2)->text(), QStringLiteral("gamma"));
    int queensOverallRow = -1;
    for (int row = 0; row < comparisonTable->rowCount(); ++row) {
        if (comparisonTable->item(row, 0)->text()
            == QStringLiteral("Queens off \u00b7 Overall")) {
            queensOverallRow = row;
            break;
        }
    }
    QVERIFY(queensOverallRow >= 0);
    QCOMPARE(comparisonTable->item(queensOverallRow, 1)->text(), QStringLiteral("100.0%"));
    QCOMPARE(comparisonTable->item(queensOverallRow, 2)->text(), QStringLiteral("0.0%"));
    QCOMPARE(comparisonTable->item(queensOverallRow, 3)->text(), QStringLiteral("+100.0 pp"));
    QCOMPARE(comparisonTable->item(queensOverallRow, 4)->text(), QStringLiteral("3 / 0"));
    QCOMPARE(comparisonTable->item(queensOverallRow, 5)->text(), QStringLiteral("2 / 0"));
    const int pawnsIndex = comparisonCategory->findData(QStringLiteral("pawns"));
    QVERIFY(pawnsIndex > 0);
    comparisonCategory->setCurrentIndex(pawnsIndex);
    QCOMPARE(comparisonTable->rowCount(), 12);
    comparisonCategory->setCurrentIndex(0);
    comparisonPlayer->setCurrentIndex(0);
    QVERIFY(comparisonTable->isHidden());
    QVERIFY(!headToHead->isHidden());

    QVERIFY(panel.selectPlayer(QStringLiteral("beta")));
    QVERIFY(summary->text().contains(QStringLiteral("beta · 3 games")));
    QVERIFY(metrics->item(0, 1)->text().contains(QStringLiteral("0.0%")));
    QVERIFY(concentration->text().contains(QStringLiteral("largest delta: 2 games (66.7%)")));
    QCOMPARE(headToHead->rowCount(), 2);
    QCOMPARE(headToHead->item(0, 0)->text(), QStringLiteral("alpha"));
    QCOMPARE(headToHead->item(0, 1)->text(), QStringLiteral("1"));
    QCOMPARE(headToHead->item(1, 0)->text(), QStringLiteral("delta"));
    QCOMPARE(headToHead->item(1, 1)->text(), QStringLiteral("2"));

    QPushButton *structureButton = nullptr;
    for (QPushButton *button : panel.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Replace Structure Stats")) {
            structureButton = button;
            break;
        }
    }
    QVERIFY(structureButton != nullptr);
    QSignalSpy spy(&panel, &PlayerStatisticsPanel::openBoardStructureSnapshotRequested);
    structureButton->click();
    QCOMPARE(spy.count(), 1);
}

void TestUnitPlayerStatisticsPanel::rejectsBrokenBoardStructureWithoutReplacingState()
{
    PlayerStatisticsPanel panel;
    QString error;
    QVERIFY2(panel.loadBoardStructureSnapshot(structureSnapshotBytes(), &error), qPrintable(error));
    const QTableWidget *metrics = panel.findChild<QTableWidget *>(
        QStringLiteral("playerStatisticsBoardStructureTable"));
    QVERIFY(metrics != nullptr);
    const QString stableCell = metrics->item(0, 1)->text();
    const QLabel *concentration = panel.findChild<QLabel *>(
        QStringLiteral("playerStatisticsStructureConcentration"));
    QVERIFY(concentration != nullptr);
    QVERIFY(concentration->text().contains(
        QStringLiteral("largest alpha: 3 games (60.0% of games; 30.0% of player exposures)")));
    QVERIFY(concentration->text().contains(
        QStringLiteral("largest alpha / gamma: 2 games (40.0%)")));
    QVERIFY(concentration->text().contains(QStringLiteral("pair HHI 36.0%")));

    QJsonObject wrongSchema = QJsonDocument::fromJson(structureSnapshotBytes()).object();
    wrongSchema.insert(QStringLiteral("display_schema"), QStringLiteral("unsupported"));
    QVERIFY(!panel.loadBoardStructureSnapshot(
        QJsonDocument(wrongSchema).toJson(QJsonDocument::Compact), &error));
    QVERIFY(error.contains(QStringLiteral("not a supported")));
    QCOMPARE(metrics->item(0, 1)->text(), stableCell);

    QJsonObject broken = QJsonDocument::fromJson(structureSnapshotBytes()).object();
    QJsonObject global = broken.value(QStringLiteral("global_statistics")).toObject();
    QJsonArray rows = global.value(QStringLiteral("metrics")).toArray();
    QJsonObject first = rows.at(0).toObject();
    first.insert(QStringLiteral("denominator_sum"), 1);
    rows.replace(0, first);
    global.insert(QStringLiteral("metrics"), rows);
    broken.insert(QStringLiteral("global_statistics"), global);
    QVERIFY(!panel.loadBoardStructureSnapshot(
        QJsonDocument(broken).toJson(QJsonDocument::Compact), &error));
    QVERIFY(error.contains(QStringLiteral("BoardStructure"), Qt::CaseInsensitive));
    QCOMPARE(metrics->item(0, 1)->text(), stableCell);

    QJsonObject wrongReciprocalId = QJsonDocument::fromJson(
        structureSnapshotBytes()).object();
    QJsonArray players = wrongReciprocalId.value(QStringLiteral("players")).toArray();
    QJsonObject alpha = players.at(0).toObject();
    QJsonArray alphaRows = alpha.value(QStringLiteral("head_to_head")).toArray();
    QJsonObject alphaBeta = alphaRows.at(0).toObject();
    alphaBeta.insert(QStringLiteral("unordered_pair_id"), structurePairId(QLatin1Char('f')));
    alphaRows.replace(0, alphaBeta);
    alpha.insert(QStringLiteral("head_to_head"), alphaRows);
    players.replace(0, alpha);
    wrongReciprocalId.insert(QStringLiteral("players"), players);
    QVERIFY(!panel.loadBoardStructureSnapshot(
        QJsonDocument(wrongReciprocalId).toJson(QJsonDocument::Compact), &error));
    QVERIFY(error.contains(QStringLiteral("reciprocal"), Qt::CaseInsensitive));
    QCOMPARE(metrics->item(0, 1)->text(), stableCell);

    QJsonObject brokenReciprocalAlgebra = QJsonDocument::fromJson(
        structureSnapshotBytes()).object();
    players = brokenReciprocalAlgebra.value(QStringLiteral("players")).toArray();
    QJsonObject beta = players.at(1).toObject();
    QJsonArray betaRows = beta.value(QStringLiteral("head_to_head")).toArray();
    QJsonObject betaAlpha = betaRows.at(0).toObject();
    betaAlpha.insert(QStringLiteral("white_game_count"), 1);
    betaAlpha.insert(QStringLiteral("black_game_count"), 0);
    betaRows.replace(0, betaAlpha);
    QJsonObject betaDelta = betaRows.at(1).toObject();
    betaDelta.insert(QStringLiteral("white_game_count"), 0);
    betaDelta.insert(QStringLiteral("black_game_count"), 2);
    betaRows.replace(1, betaDelta);
    beta.insert(QStringLiteral("head_to_head"), betaRows);
    players.replace(1, beta);
    brokenReciprocalAlgebra.insert(QStringLiteral("players"), players);
    QVERIFY(!panel.loadBoardStructureSnapshot(
        QJsonDocument(brokenReciprocalAlgebra).toJson(QJsonDocument::Compact), &error));
    QVERIFY(error.contains(QStringLiteral("reciprocal"), Qt::CaseInsensitive));
    QCOMPARE(metrics->item(0, 1)->text(), stableCell);

    QJsonObject wrongLargestOpponent = QJsonDocument::fromJson(
        structureSnapshotBytes()).object();
    players = wrongLargestOpponent.value(QStringLiteral("players")).toArray();
    alpha = players.at(0).toObject();
    alpha.insert(QStringLiteral("largest_opponent_id"), QStringLiteral("beta"));
    players.replace(0, alpha);
    wrongLargestOpponent.insert(QStringLiteral("players"), players);
    QVERIFY(!panel.loadBoardStructureSnapshot(
        QJsonDocument(wrongLargestOpponent).toJson(QJsonDocument::Compact), &error));
    QVERIFY(error.contains(QStringLiteral("BoardStructure"), Qt::CaseInsensitive));
    QCOMPARE(metrics->item(0, 1)->text(), stableCell);

    QJsonObject wrongPlayerTieBreak = QJsonDocument::fromJson(
        structureSnapshotBytes()).object();
    global = wrongPlayerTieBreak.value(QStringLiteral("global_statistics")).toObject();
    global.insert(QStringLiteral("largest_player_id"), QStringLiteral("beta"));
    wrongPlayerTieBreak.insert(QStringLiteral("global_statistics"), global);
    QVERIFY(!panel.loadBoardStructureSnapshot(
        QJsonDocument(wrongPlayerTieBreak).toJson(QJsonDocument::Compact), &error));
    QVERIFY(error.contains(QStringLiteral("player games"), Qt::CaseInsensitive));
    QCOMPARE(metrics->item(0, 1)->text(), stableCell);

    QJsonObject wrongPairTieBreak = QJsonDocument::fromJson(
        structureSnapshotBytes()).object();
    global = wrongPairTieBreak.value(QStringLiteral("global_statistics")).toObject();
    global.insert(QStringLiteral("largest_unordered_pair_id"), structurePairId(QLatin1Char('b')));
    global.insert(QStringLiteral("largest_pair_player_ids"), QJsonArray {
        QStringLiteral("beta"), QStringLiteral("delta")});
    wrongPairTieBreak.insert(QStringLiteral("global_statistics"), global);
    QVERIFY(!panel.loadBoardStructureSnapshot(
        QJsonDocument(wrongPairTieBreak).toJson(QJsonDocument::Compact), &error));
    QVERIFY(error.contains(QStringLiteral("unordered-pair"), Qt::CaseInsensitive));
    QCOMPARE(metrics->item(0, 1)->text(), stableCell);

    QJsonObject brokenPlayerHhi = QJsonDocument::fromJson(
        structureSnapshotBytes()).object();
    global = brokenPlayerHhi.value(QStringLiteral("global_statistics")).toObject();
    global.insert(QStringLiteral("player_exposure_hhi_ppm"), 260'001);
    brokenPlayerHhi.insert(QStringLiteral("global_statistics"), global);
    QVERIFY(!panel.loadBoardStructureSnapshot(
        QJsonDocument(brokenPlayerHhi).toJson(QJsonDocument::Compact), &error));
    QVERIFY(error.contains(QStringLiteral("player games"), Qt::CaseInsensitive));
    QCOMPARE(metrics->item(0, 1)->text(), stableCell);

    QJsonObject brokenPairHhi = QJsonDocument::fromJson(
        structureSnapshotBytes()).object();
    global = brokenPairHhi.value(QStringLiteral("global_statistics")).toObject();
    global.insert(QStringLiteral("pair_hhi_ppm"), 360'001);
    brokenPairHhi.insert(QStringLiteral("global_statistics"), global);
    QVERIFY(!panel.loadBoardStructureSnapshot(
        QJsonDocument(brokenPairHhi).toJson(QJsonDocument::Compact), &error));
    QVERIFY(error.contains(QStringLiteral("unordered-pair"), Qt::CaseInsensitive));
    QCOMPARE(metrics->item(0, 1)->text(), stableCell);
}

void TestUnitPlayerStatisticsPanel::loadsRealSnapshotWhenProvided()
{
    const QString catalogPath = qEnvironmentVariable(
        "PARLAWL_REAL_PLAYER_ANALYSIS_CATALOG");
    const QString playerPath = qEnvironmentVariable(
        "PARLAWL_REAL_PLAYER_STATISTICS_SNAPSHOT");
    const QString structurePath = qEnvironmentVariable(
        "PARLAWL_REAL_BOARD_STRUCTURE_STATISTICS_SNAPSHOT");
    if (catalogPath.isEmpty() && playerPath.isEmpty() && structurePath.isEmpty()) {
        QSKIP("real player-statistics snapshots were not requested");
    }
    PlayerStatisticsPanel panel;
    QString error;
    if (!catalogPath.isEmpty()) {
        QVERIFY2(panel.loadExplorerDatabase(catalogPath, &error), qPrintable(error));
        QCOMPARE(panel.playerIds().size(), 701);
        QVERIFY(panel.hasBoardStructureSnapshot());
        QVERIFY(panel.selectPlayer(QStringLiteral("caesar")));
        const QLabel *summary = panel.findChild<QLabel *>(
            QStringLiteral("playerStatisticsStructureSummary"));
        QTableWidget *headToHead = panel.findChild<QTableWidget *>(
            QStringLiteral("playerStatisticsBoardStructureHeadToHeadTable"));
        QComboBox *comparisonPlayer = panel.findChild<QComboBox *>(
            QStringLiteral("playerStatisticsBoardStructureComparePlayer"));
        QTableWidget *comparisonTable = panel.findChild<QTableWidget *>(
            QStringLiteral("playerStatisticsBoardStructureComparisonTable"));
        QTableWidget *drilldownTable = panel.findChild<QTableWidget *>(
            QStringLiteral("playerStatisticsBoardStructureDrilldownTable"));
        QVERIFY(summary != nullptr);
        QVERIFY(headToHead != nullptr);
        QVERIFY(comparisonPlayer != nullptr);
        QVERIFY(comparisonTable != nullptr);
        QVERIFY(drilldownTable != nullptr);
        QVERIFY(summary->text().contains(QStringLiteral("caesar · 588 games")));
        QCOMPARE(headToHead->rowCount(), 164);
        const int turboplombirIndex = comparisonPlayer->findData(
            QStringLiteral("turboplombir"));
        QVERIFY(turboplombirIndex > 0);
        comparisonPlayer->setCurrentIndex(turboplombirIndex);
        int castlingRow = -1;
        for (int row = 0; row < comparisonTable->rowCount(); ++row) {
            if (comparisonTable->item(row, 0)->text()
                == QStringLiteral("Any castle")) {
                castlingRow = row;
                break;
            }
        }
        QVERIFY(castlingRow >= 0);
        QVERIFY(QMetaObject::invokeMethod(
            comparisonTable,
            "cellDoubleClicked",
            Qt::DirectConnection,
            Q_ARG(int, castlingRow),
            Q_ARG(int, 1)));
        QCOMPARE(drilldownTable->rowCount(), 588);
        const QString screenshotPath = qEnvironmentVariable(
            "PARLAWL_REAL_PLAYER_ANALYSIS_SCREENSHOT");
        if (!screenshotPath.isEmpty()) {
            QTabWidget *tabs = panel.findChild<QTabWidget *>(
                QStringLiteral("playerStatisticsDetailTabs"));
            QVERIFY(tabs != nullptr);
            tabs->setCurrentIndex(tabs->count() - 1);
            panel.resize(1'500, 1'100);
            panel.show();
            QTest::qWait(50);
            QVERIFY2(panel.grab().save(screenshotPath), qPrintable(screenshotPath));
        }
    }
    if (!playerPath.isEmpty()) {
        QFile file(playerPath);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        QVERIFY2(panel.loadSnapshot(file.readAll(), &error), qPrintable(error));
        QCOMPARE(panel.playerIds().size(), 701);
        QVERIFY(panel.summaryText().contains(QStringLiteral("975 unordered pairs")));
        QCOMPARE(panel.phaseRowCount(), 3);
        QCOMPARE(panel.decisionContextRowCount(), 4);
        QCOMPARE(panel.opponentRowCount(), 0);
        QCOMPARE(panel.longestMoveRowCount(), 20);
    }
    if (!structurePath.isEmpty()) {
        QFile file(structurePath);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        QVERIFY2(panel.loadBoardStructureSnapshot(
            file.readAll(), &error), qPrintable(error));
        QCOMPARE(panel.playerIds().size(), 701);
        QVERIFY(panel.selectPlayer(QStringLiteral("caesar")));
        const QLabel *summary = panel.findChild<QLabel *>(
            QStringLiteral("playerStatisticsStructureSummary"));
        QTableWidget *headToHead = panel.findChild<QTableWidget *>(
            QStringLiteral("playerStatisticsBoardStructureHeadToHeadTable"));
        QLineEdit *search = panel.findChild<QLineEdit *>(
            QStringLiteral("playerStatisticsBoardStructureOpponentSearch"));
        QSpinBox *minimumGames = panel.findChild<QSpinBox *>(
            QStringLiteral("playerStatisticsBoardStructureMinimumGames"));
        QComboBox *comparisonPlayer = panel.findChild<QComboBox *>(
            QStringLiteral("playerStatisticsBoardStructureComparePlayer"));
        const QLabel *comparisonSummary = panel.findChild<QLabel *>(
            QStringLiteral("playerStatisticsBoardStructureComparisonSummary"));
        QTableWidget *comparisonTable = panel.findChild<QTableWidget *>(
            QStringLiteral("playerStatisticsBoardStructureComparisonTable"));
        QVERIFY(summary != nullptr);
        QVERIFY(headToHead != nullptr);
        QVERIFY(search != nullptr);
        QVERIFY(minimumGames != nullptr);
        QVERIFY(comparisonPlayer != nullptr);
        QVERIFY(comparisonSummary != nullptr);
        QVERIFY(comparisonTable != nullptr);
        QVERIFY(summary->text().contains(QStringLiteral("caesar · 588 games")));
        QCOMPARE(headToHead->rowCount(), 164);
        search->setText(QStringLiteral("TURBOPLOMBIR"));
        QCOMPARE(headToHead->rowCount(), 1);
        QCOMPARE(headToHead->item(0, 0)->text(), QStringLiteral("turboplombir"));
        QCOMPARE(headToHead->item(0, 1)->text(), QStringLiteral("28"));
        minimumGames->setValue(29);
        QCOMPARE(headToHead->rowCount(), 0);
        const int turboplombirIndex = comparisonPlayer->findData(
            QStringLiteral("turboplombir"));
        QVERIFY(turboplombirIndex > 0);
        comparisonPlayer->setCurrentIndex(turboplombirIndex);
        QCOMPARE(comparisonTable->rowCount(), 39);
        QVERIFY(comparisonSummary->text().contains(
            QStringLiteral("Direct record from caesar perspective: 28 games")));
        QVERIFY(comparisonSummary->text().contains(QStringLiteral("not opponent-adjusted")));
    }
}

QTEST_MAIN(TestUnitPlayerStatisticsPanel)

#include "test_unit_player_statistics_panel.moc"
