#include "player_statistics_panel.h"
#include "player_analysis_catalog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <tuple>

namespace {

constexpr qsizetype kMaximumSnapshotBytes = 64 * 1024 * 1024;
constexpr int kMaximumPlayers = 5'000;
constexpr int kMaximumOpponents = 5'000;
constexpr qint64 kMaximumDecisions = 1'000'000;
constexpr qint64 kMaximumMilliseconds = 86'400'000;
constexpr qint64 kPpm = 1'000'000;
constexpr qint64 kMaximumExplorerBytes = 512LL * 1024 * 1024;
constexpr int kBoardStructureMetricCodeRole = Qt::UserRole + 20;
constexpr int kBoardStructurePlayerIdRole = Qt::UserRole + 21;
constexpr int kBoardStructureSourceGameIdRole = Qt::UserRole + 22;

struct ExplorerGameRow {
    QString sourceGameId;
    QString canonicalUrl;
    QString eventStartUtc;
    QString utcDay;
    QString opponentId;
    QString color;
    QString outcome;
    int playerRating = 0;
    int opponentRating = 0;
    QString openingStatus;
    QString openingEco;
    QString openingName;
    int openingLastBookPly = -1;
    int plyCount = 0;
};

struct ExplorerDecisionRow {
    QString sourceGameId;
    QString eventStartUtc;
    QString playerId;
    QString opponentId;
    QString playerColor;
    int ply = 0;
    int moveNumber = 0;
    QString san;
    QString uci;
    QString phase;
    QString forcedness;
    int legalMoveCount = 0;
    qint64 decisionStartClockMs = -1;
    qint64 clockAfterMs = -1;
    qint64 elapsedMs = -1;
    QString elapsedStatus;
};

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool exactInteger(
    const QJsonValue &value,
    qint64 minimum,
    qint64 maximum,
    qint64 *output = nullptr)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < static_cast<double>(minimum)
        || number > static_cast<double>(maximum)) {
        return false;
    }
    if (output != nullptr) {
        *output = static_cast<qint64>(number);
    }
    return true;
}

bool boundedText(const QJsonValue &value, int maximumLength = 1'024)
{
    return value.isString() && !value.toString().trimmed().isEmpty()
        && value.toString().size() <= maximumLength;
}

bool exactFalse(const QJsonObject &object, const QString &key)
{
    return object.contains(key) && object.value(key).isBool()
        && !object.value(key).toBool();
}

qint64 roundedCoverage(qint64 observed, qint64 decisions)
{
    return decisions == 0 ? -1 : (observed * kPpm + decisions / 2) / decisions;
}

bool validateAggregate(
    const QJsonObject &aggregate,
    qint64 expectedDecisions,
    QString *errorMessage)
{
    qint64 decisions = 0;
    qint64 starts = 0;
    qint64 observed = 0;
    qint64 missing = 0;
    if (!exactInteger(aggregate.value(QStringLiteral("decision_count")), 0, kMaximumDecisions, &decisions)
        || decisions != expectedDecisions
        || !exactInteger(aggregate.value(QStringLiteral("decision_start_clock_observed_count")), 0, decisions, &starts)
        || !exactInteger(aggregate.value(QStringLiteral("elapsed_observed_count")), 0, decisions, &observed)
        || !exactInteger(aggregate.value(QStringLiteral("elapsed_missing_count")), 0, decisions, &missing)
        || observed + missing != decisions) {
        setError(errorMessage, QStringLiteral("move-time aggregate counts do not conserve"));
        return false;
    }

    const QJsonValue coverage = aggregate.value(QStringLiteral("elapsed_coverage_ppm"));
    if ((decisions == 0 && !coverage.isNull())
        || (decisions > 0
            && (!exactInteger(coverage, 0, kPpm)
                || static_cast<qint64>(coverage.toDouble()) != roundedCoverage(observed, decisions)))) {
        setError(errorMessage, QStringLiteral("move-time aggregate coverage is inconsistent"));
        return false;
    }

    const QJsonArray statuses = aggregate.value(QStringLiteral("elapsed_status_counts")).toArray();
    qint64 statusTotal = 0;
    QStringList statusNames;
    for (const QJsonValue &value : statuses) {
        const QJsonObject item = value.toObject();
        qint64 count = 0;
        const QString status = item.value(QStringLiteral("status")).toString();
        if (!boundedText(item.value(QStringLiteral("status")), 128)
            || statusNames.contains(status)
            || !exactInteger(item.value(QStringLiteral("move_count")), 0, decisions, &count)) {
            setError(errorMessage, QStringLiteral("move-time status counts are invalid"));
            return false;
        }
        statusNames.append(status);
        statusTotal += count;
    }
    if (statusTotal != decisions) {
        setError(errorMessage, QStringLiteral("move-time status counts do not conserve"));
        return false;
    }

    const QList<qint64> thresholds {1'000, 5'000, 10'000, 30'000, 60'000};
    const QJsonArray pressure = aggregate.value(QStringLiteral("pressure_counts")).toArray();
    if (pressure.size() != thresholds.size()) {
        setError(errorMessage, QStringLiteral("clock-pressure thresholds are incomplete"));
        return false;
    }
    qint64 previousCount = 0;
    for (qsizetype index = 0; index < pressure.size(); ++index) {
        const QJsonObject item = pressure.at(index).toObject();
        qint64 threshold = 0;
        qint64 count = 0;
        if (!exactInteger(item.value(QStringLiteral("threshold_ms")), thresholds.at(index), thresholds.at(index), &threshold)
            || !exactInteger(item.value(QStringLiteral("move_count")), previousCount, starts, &count)) {
            setError(errorMessage, QStringLiteral("clock-pressure counts are invalid"));
            return false;
        }
        previousCount = count;
    }

    const QStringList elapsedFields {
        QStringLiteral("observed_elapsed_sum_ms"),
        QStringLiteral("p50_observed_elapsed_ms"),
        QStringLiteral("p90_observed_elapsed_ms"),
        QStringLiteral("maximum_observed_elapsed_ms"),
    };
    if (observed == 0) {
        if (std::any_of(elapsedFields.cbegin(), elapsedFields.cend(), [&](const QString &field) {
                return !aggregate.value(field).isNull();
            })) {
            setError(errorMessage, QStringLiteral("missing elapsed observations became numeric"));
            return false;
        }
        return true;
    }

    qint64 sum = 0;
    qint64 p50 = 0;
    qint64 p90 = 0;
    qint64 maximum = 0;
    if (!exactInteger(aggregate.value(elapsedFields.at(0)), 0, kMaximumMilliseconds * observed, &sum)
        || !exactInteger(aggregate.value(elapsedFields.at(1)), 0, kMaximumMilliseconds, &p50)
        || !exactInteger(aggregate.value(elapsedFields.at(2)), p50, kMaximumMilliseconds, &p90)
        || !exactInteger(aggregate.value(elapsedFields.at(3)), p90, kMaximumMilliseconds, &maximum)
        || sum < maximum) {
        setError(errorMessage, QStringLiteral("elapsed summaries are invalid"));
        return false;
    }
    return true;
}

bool validateNamedGroups(
    const QJsonArray &groups,
    const QStringList &expected,
    qint64 expectedDecisions,
    const QString &groupLabel,
    QString *errorMessage)
{
    if (groups.size() != expected.size()) {
        setError(errorMessage, groupLabel + QStringLiteral(" groups are incomplete"));
        return false;
    }
    qint64 total = 0;
    for (qsizetype index = 0; index < groups.size(); ++index) {
        const QJsonObject group = groups.at(index).toObject();
        qint64 decisions = 0;
        const QJsonObject aggregate = group.value(QStringLiteral("statistics")).toObject();
        if (group.value(QStringLiteral("group")).toString() != expected.at(index)
            || !exactInteger(aggregate.value(QStringLiteral("decision_count")), 0, expectedDecisions, &decisions)
            || !validateAggregate(aggregate, decisions, errorMessage)) {
            setError(errorMessage, groupLabel + QStringLiteral(" group is invalid"));
            return false;
        }
        total += decisions;
    }
    if (total != expectedDecisions) {
        setError(errorMessage, groupLabel + QStringLiteral(" move counts do not conserve"));
        return false;
    }
    return true;
}

bool validateLongestMoves(
    const QJsonArray &moves,
    int maximumCount,
    const QString &expectedPlayer,
    QString *errorMessage)
{
    if (moves.size() > maximumCount) {
        setError(errorMessage, QStringLiteral("longest-move list exceeds its display bound"));
        return false;
    }
    qint64 previousElapsed = kMaximumMilliseconds;
    for (const QJsonValue &value : moves) {
        const QJsonObject move = value.toObject();
        qint64 elapsed = 0;
        qint64 ply = 0;
        qint64 startClock = 0;
        const QString color = move.value(QStringLiteral("player_color")).toString();
        const QJsonValue startClockValue = move.value(QStringLiteral("decision_start_clock_ms"));
        if (!boundedText(move.value(QStringLiteral("source_game_id")))
            || !boundedText(move.value(QStringLiteral("event_start_utc")), 64)
            || !boundedText(move.value(QStringLiteral("player_id")), 128)
            || !boundedText(move.value(QStringLiteral("opponent_id")), 128)
            || (!expectedPlayer.isEmpty()
                && move.value(QStringLiteral("player_id")).toString() != expectedPlayer)
            || !boundedText(move.value(QStringLiteral("played_move_san")), 64)
            || !boundedText(move.value(QStringLiteral("played_move_uci")), 8)
            || !boundedText(move.value(QStringLiteral("position_phase")), 32)
            || (color != QStringLiteral("white") && color != QStringLiteral("black"))
            || (!startClockValue.isNull()
                && !exactInteger(startClockValue, 0, kMaximumMilliseconds, &startClock))
            || !exactInteger(move.value(QStringLiteral("ply")), 1, 1'000, &ply)
            || !exactInteger(move.value(QStringLiteral("elapsed_move_ms")), 0, previousElapsed, &elapsed)) {
            setError(errorMessage, QStringLiteral("longest-move row is invalid"));
            return false;
        }
        previousElapsed = elapsed;
    }
    return true;
}

bool validatePlayer(
    const QJsonObject &player,
    qint64 globalGames,
    qint64 *gameCount,
    qint64 *decisionCount,
    QString *errorMessage)
{
    const QString playerId = player.value(QStringLiteral("player_id")).toString();
    qint64 games = 0;
    qint64 white = 0;
    qint64 black = 0;
    qint64 opponents = 0;
    qint64 utcDays = 0;
    qint64 decisions = 0;
    const QJsonObject aggregate = player.value(QStringLiteral("statistics")).toObject();
    if (!boundedText(player.value(QStringLiteral("player_id")), 128)
        || !exactInteger(player.value(QStringLiteral("game_count")), 1, globalGames, &games)
        || !exactInteger(player.value(QStringLiteral("white_game_count")), 0, games, &white)
        || !exactInteger(player.value(QStringLiteral("black_game_count")), 0, games, &black)
        || white + black != games
        || !exactInteger(player.value(QStringLiteral("distinct_opponent_count")), 1, kMaximumOpponents, &opponents)
        || !exactInteger(player.value(QStringLiteral("distinct_utc_day_count")), 1, games, &utcDays)
        || !exactInteger(aggregate.value(QStringLiteral("decision_count")), 1, kMaximumDecisions, &decisions)
        || !validateAggregate(aggregate, decisions, errorMessage)
        || !validateNamedGroups(
            player.value(QStringLiteral("by_phase")).toArray(),
            {QStringLiteral("opening"), QStringLiteral("middlegame"), QStringLiteral("endgame")},
            decisions,
            QStringLiteral("phase"),
            errorMessage)
        || !validateNamedGroups(
            player.value(QStringLiteral("by_color")).toArray(),
            {QStringLiteral("white"), QStringLiteral("black")},
            decisions,
            QStringLiteral("color"),
            errorMessage)
        || !validateNamedGroups(
            player.value(QStringLiteral("by_forcedness")).toArray(),
            {QStringLiteral("forced-single-legal-move"), QStringLiteral("nonforced")},
            decisions,
            QStringLiteral("forcedness"),
            errorMessage)
        || !validateLongestMoves(player.value(QStringLiteral("longest_observed_moves")).toArray(), 5, playerId, errorMessage)) {
        setError(errorMessage, QStringLiteral("player move-time row is invalid"));
        return false;
    }

    const QJsonArray opponentRows = player.value(QStringLiteral("opponents")).toArray();
    if (opponentRows.size() != opponents) {
        setError(errorMessage, QStringLiteral("player opponent rows are incomplete"));
        return false;
    }
    QStringList opponentIds;
    qint64 opponentGameTotal = 0;
    qint64 opponentDecisionTotal = 0;
    for (const QJsonValue &value : opponentRows) {
        const QJsonObject opponent = value.toObject();
        const QString opponentId = opponent.value(QStringLiteral("opponent_id")).toString();
        qint64 opponentGames = 0;
        qint64 opponentDecisions = 0;
        const QJsonObject opponentAggregate = opponent.value(QStringLiteral("statistics")).toObject();
        if (opponent.value(QStringLiteral("player_id")).toString() != playerId
            || !boundedText(opponent.value(QStringLiteral("opponent_id")), 128)
            || opponentId == playerId
            || opponentIds.contains(opponentId)
            || !exactInteger(opponent.value(QStringLiteral("game_count")), 1, games, &opponentGames)
            || !exactInteger(opponentAggregate.value(QStringLiteral("decision_count")), 1, decisions, &opponentDecisions)
            || !validateAggregate(opponentAggregate, opponentDecisions, errorMessage)) {
            setError(errorMessage, QStringLiteral("player opponent row is invalid"));
            return false;
        }
        opponentIds.append(opponentId);
        opponentGameTotal += opponentGames;
        opponentDecisionTotal += opponentDecisions;
    }
    if (opponentGameTotal != games || opponentDecisionTotal != decisions) {
        setError(errorMessage, QStringLiteral("opponent game or move counts do not conserve"));
        return false;
    }
    *gameCount = games;
    *decisionCount = decisions;
    return true;
}

bool validateSnapshot(
    const QJsonObject &root,
    QHash<QString, QJsonObject> *playersById,
    QString *errorMessage)
{
    if (root.value(QStringLiteral("display_schema")).toString()
            != QStringLiteral("chess-player-move-time-statistics-display-v1")
        || !boundedText(root.value(QStringLiteral("source_plan_v2_id")))
        || !root.value(QStringLiteral("source_plan_v2_id")).toString().startsWith(
            QStringLiteral("chess-cohort-source-chunk-plan-v2:"))) {
        setError(errorMessage, QStringLiteral("file is not a supported player-statistics snapshot"));
        return false;
    }
    const QJsonObject claim = root.value(QStringLiteral("claim_boundary")).toObject();
    if (claim.value(QStringLiteral("clock_semantics")).toString()
            != QStringLiteral("server-accounted-not-cognitive-time")
        || !exactFalse(claim, QStringLiteral("mapping_authenticates_source_replay"))
        || !exactFalse(claim, QStringLiteral("game_outcome_used"))
        || !exactFalse(claim, QStringLiteral("engine_or_move_quality_joined"))
        || !exactFalse(claim, QStringLiteral("model_prediction_or_pregame_feature"))) {
        setError(errorMessage, QStringLiteral("snapshot claim boundary is unsupported"));
        return false;
    }

    const QJsonObject global = root.value(QStringLiteral("global_statistics")).toObject();
    qint64 games = 0;
    qint64 decisions = 0;
    qint64 distinctPlayers = 0;
    qint64 sourceBundles = 0;
    qint64 unorderedPairs = 0;
    qint64 utcDays = 0;
    qint64 targetLineages = 0;
    qint64 parsedLineages = 0;
    const QJsonObject aggregate = global.value(QStringLiteral("statistics")).toObject();
    if (!exactInteger(global.value(QStringLiteral("game_count")), 1, 5'000, &games)
        || !exactInteger(global.value(QStringLiteral("decision_count")), 1, kMaximumDecisions, &decisions)
        || !exactInteger(global.value(QStringLiteral("distinct_player_count")), 2, kMaximumPlayers, &distinctPlayers)
        || !exactInteger(global.value(QStringLiteral("source_bundle_count")), 1, 384, &sourceBundles)
        || !exactInteger(global.value(QStringLiteral("unordered_pair_count")), 1, games, &unorderedPairs)
        || !exactInteger(global.value(QStringLiteral("utc_day_count")), 1, games, &utcDays)
        || !exactInteger(global.value(QStringLiteral("target_lineage_occurrence_count")), games, 20'000, &targetLineages)
        || !exactInteger(global.value(QStringLiteral("parsed_target_lineage_occurrence_count")), games, 20'000, &parsedLineages)
        || parsedLineages != targetLineages
        || !validateAggregate(aggregate, decisions, errorMessage)
        || !validateNamedGroups(
            global.value(QStringLiteral("by_phase")).toArray(),
            {QStringLiteral("opening"), QStringLiteral("middlegame"), QStringLiteral("endgame")},
            decisions,
            QStringLiteral("phase"),
            errorMessage)
        || !validateNamedGroups(
            global.value(QStringLiteral("by_color")).toArray(),
            {QStringLiteral("white"), QStringLiteral("black")},
            decisions,
            QStringLiteral("color"),
            errorMessage)
        || !validateNamedGroups(
            global.value(QStringLiteral("by_forcedness")).toArray(),
            {QStringLiteral("forced-single-legal-move"), QStringLiteral("nonforced")},
            decisions,
            QStringLiteral("forcedness"),
            errorMessage)
        || !validateLongestMoves(global.value(QStringLiteral("longest_observed_moves")).toArray(), 20, QString(), errorMessage)) {
        setError(errorMessage, QStringLiteral("global move-time summary is invalid"));
        return false;
    }

    const QJsonArray players = root.value(QStringLiteral("players")).toArray();
    if (players.size() != distinctPlayers || players.isEmpty() || players.size() > kMaximumPlayers) {
        setError(errorMessage, QStringLiteral("player population is incomplete"));
        return false;
    }
    QHash<QString, QJsonObject> parsedPlayers;
    QString previousPlayerId;
    qint64 playerGameTotal = 0;
    qint64 playerDecisionTotal = 0;
    for (const QJsonValue &value : players) {
        const QJsonObject player = value.toObject();
        const QString playerId = player.value(QStringLiteral("player_id")).toString();
        qint64 playerGames = 0;
        qint64 playerDecisions = 0;
        if ((!previousPlayerId.isEmpty() && playerId <= previousPlayerId)
            || parsedPlayers.contains(playerId)
            || !validatePlayer(player, games, &playerGames, &playerDecisions, errorMessage)) {
            setError(errorMessage, QStringLiteral("player rows are duplicated, unordered, or invalid"));
            return false;
        }
        previousPlayerId = playerId;
        parsedPlayers.insert(playerId, player);
        playerGameTotal += playerGames;
        playerDecisionTotal += playerDecisions;
    }
    if (playerGameTotal != 2 * games || playerDecisionTotal != decisions) {
        setError(errorMessage, QStringLiteral("global and player move-time totals differ"));
        return false;
    }
    *playersById = parsedPlayers;
    return true;
}

QStringList boardStructureMetricCodes()
{
    static const QStringList codes = [] {
        QStringList output;
        const QStringList phases {
            QStringLiteral("all"),
            QStringLiteral("opening"),
            QStringLiteral("middlegame"),
            QStringLiteral("endgame"),
        };
        const QStringList predicates {
            QStringLiteral("both_queens_absent"),
            QStringLiteral("own_passed_pawn_present"),
            QStringLiteral("own_isolated_pawn_present"),
            QStringLiteral("own_doubled_pawn_excess_present"),
            QStringLiteral("own_two_or_more_bishops_present"),
        };
        for (const QString &predicate : predicates) {
            for (const QString &phase : phases) {
                output.append(QStringLiteral("binary.%1.%2.share").arg(predicate, phase));
            }
        }
        for (const QString &state : {
                 QStringLiteral("ahead"),
                 QStringLiteral("equal"),
                 QStringLiteral("behind"),
             }) {
            for (const QString &phase : phases) {
                output.append(QStringLiteral("material_relation.%1.%2.share").arg(state, phase));
            }
        }
        for (const QString &phase : phases) {
            output.append(QStringLiteral("material_delta_mean.%1").arg(phase));
        }
        for (const QString &category : {
                 QStringLiteral("any"),
                 QStringLiteral("kingside"),
                 QStringLiteral("queenside"),
             }) {
            output.append(QStringLiteral("focal_castling.%1").arg(category));
        }
        return output;
    }();
    return codes;
}

struct BoardStructureComparisonMetricRow {
    QString code;
    QString label;
    QString category;
};

const QVector<BoardStructureComparisonMetricRow> &boardStructureComparisonMetricRows()
{
    static const QVector<BoardStructureComparisonMetricRow> rows = [] {
        QVector<BoardStructureComparisonMetricRow> output;
        struct Phase {
            QString code;
            QString label;
        };
        const QVector<Phase> phases {
            {QStringLiteral("all"), QStringLiteral("Overall")},
            {QStringLiteral("opening"), QStringLiteral("Opening")},
            {QStringLiteral("middlegame"), QStringLiteral("Middlegame")},
            {QStringLiteral("endgame"), QStringLiteral("Endgame")},
        };
        struct Predicate {
            QString code;
            QString label;
            QString category;
        };
        const QVector<Predicate> predicates {
            {QStringLiteral("both_queens_absent"), QStringLiteral("Queens off"),
             QStringLiteral("position")},
            {QStringLiteral("own_passed_pawn_present"), QStringLiteral("Passed pawn present"),
             QStringLiteral("pawns")},
            {QStringLiteral("own_isolated_pawn_present"), QStringLiteral("Isolated pawn present"),
             QStringLiteral("pawns")},
            {QStringLiteral("own_doubled_pawn_excess_present"), QStringLiteral("Doubled pawn present"),
             QStringLiteral("pawns")},
            {QStringLiteral("own_two_or_more_bishops_present"), QStringLiteral("Bishop pair present"),
             QStringLiteral("position")},
        };
        for (const Predicate &predicate : predicates) {
            for (const Phase &phase : phases) {
                output.append({
                    QStringLiteral("binary.%1.%2.share").arg(predicate.code, phase.code),
                    QStringLiteral("%1 · %2").arg(predicate.label, phase.label),
                    predicate.category,
                });
            }
        }
        for (const QString &state : {
                 QStringLiteral("ahead"),
                 QStringLiteral("equal"),
                 QStringLiteral("behind"),
             }) {
            QString stateLabel = state;
            stateLabel[0] = stateLabel.at(0).toUpper();
            for (const Phase &phase : phases) {
                output.append({
                    QStringLiteral("material_relation.%1.%2.share").arg(state, phase.code),
                    QStringLiteral("Material %1 · %2").arg(stateLabel.toLower(), phase.label),
                    QStringLiteral("material"),
                });
            }
        }
        for (const Phase &phase : phases) {
            output.append({
                QStringLiteral("material_delta_mean.%1").arg(phase.code),
                QStringLiteral("Mean material edge · %1").arg(phase.label),
                QStringLiteral("material"),
            });
        }
        for (const auto &[code, label] : QVector<QPair<QString, QString>> {
                 {QStringLiteral("any"), QStringLiteral("Any castle")},
                 {QStringLiteral("kingside"), QStringLiteral("Kingside castle")},
                 {QStringLiteral("queenside"), QStringLiteral("Queenside castle")},
             }) {
            output.append({
                QStringLiteral("focal_castling.%1").arg(code),
                label,
                QStringLiteral("castling"),
            });
        }
        return output;
    }();
    return rows;
}

qint64 roundedSignedPpm(qint64 numerator, qint64 denominator)
{
    const qint64 magnitude = (std::abs(numerator) * kPpm + denominator / 2)
        / denominator;
    return numerator < 0 ? -magnitude : magnitude;
}

bool validateBoardStructureMetrics(
    const QJsonArray &metrics,
    qint64 expectedPlayerGames,
    QString *errorMessage)
{
    const QStringList codes = boardStructureMetricCodes();
    if (metrics.size() != codes.size()) {
        setError(errorMessage, QStringLiteral("BoardStructure metrics must contain the exact 39-cell registry"));
        return false;
    }
    for (qsizetype index = 0; index < metrics.size(); ++index) {
        if (!metrics.at(index).isObject()) {
            setError(errorMessage, QStringLiteral("BoardStructure metric row is malformed"));
            return false;
        }
        const QJsonObject metric = metrics.at(index).toObject();
        const QString code = metric.value(QStringLiteral("metric_code")).toString();
        const QString status = metric.value(QStringLiteral("aggregate_status")).toString();
        const bool signedMaterial = code.startsWith(QStringLiteral("material_delta_mean."));
        qint64 observed = 0;
        qint64 notApplicable = 0;
        qint64 denominator = 0;
        qint64 paired = 0;
        if (code != codes.at(index)
            || !exactInteger(metric.value(QStringLiteral("observed_player_game_count")), 0, expectedPlayerGames, &observed)
            || !exactInteger(metric.value(QStringLiteral("not_applicable_player_game_count")), 0, expectedPlayerGames, &notApplicable)
            || observed + notApplicable != expectedPlayerGames
            || !exactInteger(metric.value(QStringLiteral("denominator_sum")), 0, kMaximumDecisions, &denominator)
            || !exactInteger(metric.value(QStringLiteral("paired_player_game_count")), 0, expectedPlayerGames, &paired)) {
            setError(errorMessage, QStringLiteral("BoardStructure metric counts do not conserve"));
            return false;
        }

        const QJsonValue numeratorValue = metric.value(QStringLiteral("numerator_sum"));
        const QJsonValue aggregateValue = metric.value(QStringLiteral("aggregate_value_ppm"));
        if (status == QStringLiteral("observed")) {
            qint64 numerator = 0;
            qint64 aggregate = 0;
            // Promotions can legitimately exceed the starting 39-point material total.
            const qint64 minimumNumerator = signedMaterial ? -128 * denominator : 0;
            const qint64 maximumNumerator = signedMaterial ? 128 * denominator : denominator;
            if (observed == 0 || denominator == 0
                || !exactInteger(numeratorValue, minimumNumerator, maximumNumerator, &numerator)
                || !exactInteger(aggregateValue, -128 * kPpm, 128 * kPpm, &aggregate)
                || aggregate != roundedSignedPpm(numerator, denominator)) {
                setError(errorMessage, QStringLiteral("observed BoardStructure metric value is inconsistent"));
                return false;
            }
        } else if (status == QStringLiteral("not_applicable")) {
            if (observed != 0 || denominator != 0 || !numeratorValue.isNull()
                || !aggregateValue.isNull()) {
                setError(errorMessage, QStringLiteral("not-applicable BoardStructure metric became numeric"));
                return false;
            }
        } else {
            setError(errorMessage, QStringLiteral("BoardStructure metric status is unsupported"));
            return false;
        }

        const QJsonValue pairedMean = metric.value(
            QStringLiteral("mean_player_minus_opponent_ppm"));
        qint64 ignoredMean = 0;
        if ((paired == 0 && !pairedMean.isNull())
            || (paired > 0
                && !exactInteger(pairedMean, -256 * kPpm, 256 * kPpm, &ignoredMean))) {
            setError(errorMessage, QStringLiteral("BoardStructure paired comparison is inconsistent"));
            return false;
        }
    }
    return true;
}

bool validateBoardStructurePlayer(
    const QJsonObject &player,
    qint64 globalGames,
    QHash<QString, QJsonObject> *headToHeadByOpponent,
    QString *errorMessage)
{
    const QString playerId = player.value(QStringLiteral("player_id")).toString();
    qint64 games = 0;
    qint64 white = 0;
    qint64 black = 0;
    qint64 wins = 0;
    qint64 draws = 0;
    qint64 losses = 0;
    qint64 opponents = 0;
    qint64 score = 0;
    qint64 largestOpponentGames = 0;
    qint64 largestOpponentShare = 0;
    qint64 opponentHhi = 0;
    if (!boundedText(player.value(QStringLiteral("player_id")), 128)
        || !exactInteger(player.value(QStringLiteral("game_count")), 1, globalGames, &games)
        || !exactInteger(player.value(QStringLiteral("white_game_count")), 0, games, &white)
        || !exactInteger(player.value(QStringLiteral("black_game_count")), 0, games, &black)
        || white + black != games
        || !exactInteger(player.value(QStringLiteral("win_count")), 0, games, &wins)
        || !exactInteger(player.value(QStringLiteral("draw_count")), 0, games, &draws)
        || !exactInteger(player.value(QStringLiteral("loss_count")), 0, games, &losses)
        || wins + draws + losses != games
        || !exactInteger(player.value(QStringLiteral("score_rate_ppm")), 0, kPpm, &score)
        || score != roundedSignedPpm(2 * wins + draws, 2 * games)
        || !exactInteger(player.value(QStringLiteral("distinct_opponent_count")), 1, games, &opponents)
        || !boundedText(player.value(QStringLiteral("largest_opponent_id")), 128)
        || player.value(QStringLiteral("largest_opponent_id")).toString() == playerId
        || !exactInteger(player.value(QStringLiteral("largest_opponent_game_count")), 1, games, &largestOpponentGames)
        || !exactInteger(player.value(QStringLiteral("largest_opponent_game_share_ppm")), 0, kPpm, &largestOpponentShare)
        || !exactInteger(player.value(QStringLiteral("opponent_hhi_ppm")), 0, kPpm, &opponentHhi)
        || !validateBoardStructureMetrics(
            player.value(QStringLiteral("metrics")).toArray(),
            games,
            errorMessage)) {
        setError(errorMessage, QStringLiteral("BoardStructure player summary is invalid"));
        return false;
    }

    const QJsonValue headToHeadValue = player.value(QStringLiteral("head_to_head"));
    if (!headToHeadValue.isArray()
        || headToHeadValue.toArray().size() != opponents) {
        setError(errorMessage, QStringLiteral("BoardStructure head-to-head rows are incomplete"));
        return false;
    }
    const QJsonArray headToHead = headToHeadValue.toArray();
    QHash<QString, QJsonObject> parsedRows;
    QString previousOpponent;
    QString computedLargestOpponent;
    qint64 computedLargestOpponentGames = -1;
    qint64 gameTotal = 0;
    qint64 whiteTotal = 0;
    qint64 blackTotal = 0;
    qint64 winTotal = 0;
    qint64 drawTotal = 0;
    qint64 lossTotal = 0;
    qint64 squaredGameTotal = 0;
    for (const QJsonValue &value : headToHead) {
        if (!value.isObject()) {
            setError(errorMessage, QStringLiteral("BoardStructure head-to-head row is malformed"));
            return false;
        }
        const QJsonObject row = value.toObject();
        const QString opponentId = row.value(QStringLiteral("opponent_id")).toString();
        const QString unorderedPairId = row.value(
            QStringLiteral("unordered_pair_id")).toString();
        qint64 rowGames = 0;
        qint64 rowWhite = 0;
        qint64 rowBlack = 0;
        qint64 rowWins = 0;
        qint64 rowDraws = 0;
        qint64 rowLosses = 0;
        qint64 rowScore = 0;
        if (row.value(QStringLiteral("player_id")).toString() != playerId
            || !boundedText(row.value(QStringLiteral("opponent_id")), 128)
            || opponentId == playerId
            || parsedRows.contains(opponentId)
            || (!previousOpponent.isEmpty() && opponentId <= previousOpponent)
            || !boundedText(row.value(QStringLiteral("unordered_pair_id")), 128)
            || !unorderedPairId.startsWith(
                QStringLiteral("chess-unordered-player-pair-v1:"))
            || !exactInteger(row.value(QStringLiteral("game_count")), 1, games, &rowGames)
            || !exactInteger(row.value(QStringLiteral("white_game_count")), 0, rowGames, &rowWhite)
            || !exactInteger(row.value(QStringLiteral("black_game_count")), 0, rowGames, &rowBlack)
            || rowWhite + rowBlack != rowGames
            || !exactInteger(row.value(QStringLiteral("win_count")), 0, rowGames, &rowWins)
            || !exactInteger(row.value(QStringLiteral("draw_count")), 0, rowGames, &rowDraws)
            || !exactInteger(row.value(QStringLiteral("loss_count")), 0, rowGames, &rowLosses)
            || rowWins + rowDraws + rowLosses != rowGames
            || !exactInteger(row.value(QStringLiteral("score_rate_ppm")), 0, kPpm, &rowScore)
            || rowScore != roundedSignedPpm(2 * rowWins + rowDraws, 2 * rowGames)) {
            setError(errorMessage, QStringLiteral(
                "BoardStructure head-to-head rows are duplicated, unordered, unbound, or inconsistent"));
            return false;
        }
        parsedRows.insert(opponentId, row);
        previousOpponent = opponentId;
        if (rowGames > computedLargestOpponentGames
            || (rowGames == computedLargestOpponentGames
                && opponentId < computedLargestOpponent)) {
            computedLargestOpponent = opponentId;
            computedLargestOpponentGames = rowGames;
        }
        gameTotal += rowGames;
        whiteTotal += rowWhite;
        blackTotal += rowBlack;
        winTotal += rowWins;
        drawTotal += rowDraws;
        lossTotal += rowLosses;
        squaredGameTotal += rowGames * rowGames;
    }
    if (gameTotal != games || whiteTotal != white || blackTotal != black
        || winTotal != wins || drawTotal != draws || lossTotal != losses
        || player.value(QStringLiteral("largest_opponent_id")).toString()
            != computedLargestOpponent
        || largestOpponentGames != computedLargestOpponentGames
        || largestOpponentShare != roundedSignedPpm(largestOpponentGames, games)
        || opponentHhi != roundedSignedPpm(
            squaredGameTotal, games * games)) {
        setError(errorMessage, QStringLiteral(
            "BoardStructure opponent concentration does not conserve"));
        return false;
    }
    *headToHeadByOpponent = parsedRows;
    return true;
}

bool validateBoardStructureSnapshot(
    const QJsonObject &root,
    QHash<QString, QJsonObject> *playersById,
    QString *errorMessage)
{
    if (root.value(QStringLiteral("display_schema")).toString()
            != QStringLiteral("chess-board-structure-player-statistics-display-v1")
        || root.value(QStringLiteral("descriptive_metric_registry_id")).toString()
            != QStringLiteral("chess-board-structure-descriptive-metric-registry-v1:f7d33c22e381a574ea1f0a29ebbf5eae5c7da332b9b8fa2d500b2f3e2585676e")
        || !boundedText(root.value(QStringLiteral("source_plan_v2_id")))
        || !root.value(QStringLiteral("source_plan_v2_id")).toString().startsWith(
            QStringLiteral("chess-cohort-source-chunk-plan-v2:"))) {
        setError(errorMessage, QStringLiteral("file is not a supported BoardStructure player snapshot"));
        return false;
    }
    const QJsonObject claim = root.value(QStringLiteral("claim_boundary")).toObject();
    if (claim.value(QStringLiteral("board_metrics_are_postgame_mechanical_descriptions")).toBool() != true
        || claim.value(QStringLiteral("descriptive_outcomes_only")).toBool() != true
        || !exactFalse(claim, QStringLiteral("chunk_membership_defines_history"))
        || !exactFalse(claim, QStringLiteral("effective_sample_size_claim"))
        || !exactFalse(claim, QStringLiteral("independence_claim"))
        || !exactFalse(claim, QStringLiteral("model_or_prediction"))
        || !exactFalse(claim, QStringLiteral("pregame_feature_claim"))
        || !exactFalse(claim, QStringLiteral("style_intent_skill_quality_or_causality"))) {
        setError(errorMessage, QStringLiteral("BoardStructure snapshot claim boundary is unsupported"));
        return false;
    }

    const QJsonObject global = root.value(QStringLiteral("global_statistics")).toObject();
    qint64 games = 0;
    qint64 playerGames = 0;
    qint64 players = 0;
    qint64 whiteWins = 0;
    qint64 draws = 0;
    qint64 blackWins = 0;
    qint64 unorderedPairs = 0;
    qint64 largestPlayerGames = 0;
    qint64 largestPlayerGameShare = 0;
    qint64 largestPlayerExposureShare = 0;
    qint64 playerExposureHhi = 0;
    qint64 largestPairGames = 0;
    qint64 largestPairGameShare = 0;
    qint64 pairHhi = 0;
    const QJsonArray largestPairPlayerIds = global.value(
        QStringLiteral("largest_pair_player_ids")).toArray();
    if (!exactInteger(global.value(QStringLiteral("game_count")), 1, 5'000, &games)
        || !exactInteger(global.value(QStringLiteral("player_game_count")), 2, 10'000, &playerGames)
        || playerGames != 2 * games
        || !exactInteger(global.value(QStringLiteral("distinct_player_count")), 2, kMaximumPlayers, &players)
        || !exactInteger(global.value(QStringLiteral("white_win_game_count")), 0, games, &whiteWins)
        || !exactInteger(global.value(QStringLiteral("draw_game_count")), 0, games, &draws)
        || !exactInteger(global.value(QStringLiteral("black_win_game_count")), 0, games, &blackWins)
        || whiteWins + draws + blackWins != games
        || !exactInteger(global.value(QStringLiteral("unordered_pair_count")), 1, games, &unorderedPairs)
        || !boundedText(global.value(QStringLiteral("largest_player_id")), 128)
        || !exactInteger(global.value(QStringLiteral("largest_player_game_count")), 1, games, &largestPlayerGames)
        || !exactInteger(global.value(QStringLiteral("largest_player_game_share_ppm")), 0, kPpm, &largestPlayerGameShare)
        || !exactInteger(global.value(QStringLiteral("largest_player_exposure_share_ppm")), 0, kPpm, &largestPlayerExposureShare)
        || !exactInteger(global.value(QStringLiteral("player_exposure_hhi_ppm")), 0, kPpm, &playerExposureHhi)
        || !boundedText(global.value(QStringLiteral("largest_unordered_pair_id")), 128)
        || !global.value(QStringLiteral("largest_unordered_pair_id")).toString().startsWith(
            QStringLiteral("chess-unordered-player-pair-v1:"))
        || !global.value(QStringLiteral("largest_pair_player_ids")).isArray()
        || largestPairPlayerIds.size() != 2
        || !boundedText(largestPairPlayerIds.at(0), 128)
        || !boundedText(largestPairPlayerIds.at(1), 128)
        || largestPairPlayerIds.at(0).toString()
            >= largestPairPlayerIds.at(1).toString()
        || !exactInteger(global.value(QStringLiteral("largest_pair_game_count")), 1, games, &largestPairGames)
        || !exactInteger(global.value(QStringLiteral("largest_pair_game_share_ppm")), 0, kPpm, &largestPairGameShare)
        || !exactInteger(global.value(QStringLiteral("pair_hhi_ppm")), 0, kPpm, &pairHhi)
        || !validateBoardStructureMetrics(
            global.value(QStringLiteral("metrics")).toArray(),
            playerGames,
            errorMessage)) {
        setError(errorMessage, QStringLiteral("global BoardStructure summary is invalid"));
        return false;
    }

    const QJsonArray playerRows = root.value(QStringLiteral("players")).toArray();
    if (playerRows.size() != players) {
        setError(errorMessage, QStringLiteral("BoardStructure player population is incomplete"));
        return false;
    }
    QHash<QString, QJsonObject> parsedPlayers;
    QHash<QString, QHash<QString, QJsonObject>> headToHeadByPlayer;
    QString previousPlayer;
    qint64 playerGameTotal = 0;
    qint64 playerWhiteTotal = 0;
    qint64 playerBlackTotal = 0;
    qint64 playerWinTotal = 0;
    qint64 playerDrawTotal = 0;
    qint64 playerLossTotal = 0;
    qint64 squaredPlayerGameTotal = 0;
    QString computedLargestPlayer;
    qint64 computedLargestPlayerGames = -1;
    for (const QJsonValue &value : playerRows) {
        const QJsonObject player = value.toObject();
        const QString playerId = player.value(QStringLiteral("player_id")).toString();
        QHash<QString, QJsonObject> headToHead;
        if ((!previousPlayer.isEmpty() && playerId <= previousPlayer)
            || parsedPlayers.contains(playerId)
            || !validateBoardStructurePlayer(
                player, games, &headToHead, errorMessage)) {
            setError(errorMessage, QStringLiteral("BoardStructure player rows are duplicated, unordered, or invalid"));
            return false;
        }
        previousPlayer = playerId;
        parsedPlayers.insert(playerId, player);
        headToHeadByPlayer.insert(playerId, headToHead);
        const qint64 playerGameCount = static_cast<qint64>(
            player.value(QStringLiteral("game_count")).toDouble());
        playerGameTotal += playerGameCount;
        playerWhiteTotal += static_cast<qint64>(
            player.value(QStringLiteral("white_game_count")).toDouble());
        playerBlackTotal += static_cast<qint64>(
            player.value(QStringLiteral("black_game_count")).toDouble());
        playerWinTotal += static_cast<qint64>(
            player.value(QStringLiteral("win_count")).toDouble());
        playerDrawTotal += static_cast<qint64>(
            player.value(QStringLiteral("draw_count")).toDouble());
        playerLossTotal += static_cast<qint64>(
            player.value(QStringLiteral("loss_count")).toDouble());
        squaredPlayerGameTotal += playerGameCount * playerGameCount;
        if (playerGameCount > computedLargestPlayerGames
            || (playerGameCount == computedLargestPlayerGames
                && playerId < computedLargestPlayer)) {
            computedLargestPlayer = playerId;
            computedLargestPlayerGames = playerGameCount;
        }
    }
    if (playerGameTotal != playerGames
        || playerWhiteTotal != games || playerBlackTotal != games
        || playerWinTotal != whiteWins + blackWins
        || playerDrawTotal != 2 * draws
        || playerLossTotal != whiteWins + blackWins
        || global.value(QStringLiteral("largest_player_id")).toString()
            != computedLargestPlayer
        || largestPlayerGames != computedLargestPlayerGames
        || largestPlayerGameShare != roundedSignedPpm(largestPlayerGames, games)
        || largestPlayerExposureShare
            != roundedSignedPpm(largestPlayerGames, playerGames)
        || playerExposureHhi != roundedSignedPpm(
            squaredPlayerGameTotal, playerGames * playerGames)) {
        setError(errorMessage, QStringLiteral("BoardStructure player games do not conserve"));
        return false;
    }

    struct PairCount {
        QString playerA;
        QString playerB;
        QString unorderedPairId;
        qint64 games;
    };
    QVector<PairCount> pairs;
    QSet<QString> pairIds;
    qint64 pairGameTotal = 0;
    qint64 squaredPairGameTotal = 0;
    for (auto player = headToHeadByPlayer.cbegin();
         player != headToHeadByPlayer.cend(); ++player) {
        for (auto opponent = player.value().cbegin();
             opponent != player.value().cend(); ++opponent) {
            const QString &playerId = player.key();
            const QString &opponentId = opponent.key();
            if (!headToHeadByPlayer.contains(opponentId)
                || !headToHeadByPlayer.value(opponentId).contains(playerId)) {
                setError(errorMessage, QStringLiteral(
                    "BoardStructure head-to-head opponents are not reciprocal"));
                return false;
            }
            if (playerId >= opponentId) {
                continue;
            }
            const QJsonObject row = opponent.value();
            const QJsonObject reciprocal = headToHeadByPlayer.value(
                opponentId).value(playerId);
            const QString unorderedPairId = row.value(
                QStringLiteral("unordered_pair_id")).toString();
            const qint64 pairGames = static_cast<qint64>(
                row.value(QStringLiteral("game_count")).toDouble());
            if (unorderedPairId != reciprocal.value(
                    QStringLiteral("unordered_pair_id")).toString()
                || pairIds.contains(unorderedPairId)
                || pairGames != static_cast<qint64>(
                    reciprocal.value(QStringLiteral("game_count")).toDouble())
                || row.value(QStringLiteral("white_game_count")).toDouble()
                    != reciprocal.value(QStringLiteral("black_game_count")).toDouble()
                || row.value(QStringLiteral("black_game_count")).toDouble()
                    != reciprocal.value(QStringLiteral("white_game_count")).toDouble()
                || row.value(QStringLiteral("win_count")).toDouble()
                    != reciprocal.value(QStringLiteral("loss_count")).toDouble()
                || row.value(QStringLiteral("draw_count")).toDouble()
                    != reciprocal.value(QStringLiteral("draw_count")).toDouble()
                || row.value(QStringLiteral("loss_count")).toDouble()
                    != reciprocal.value(QStringLiteral("win_count")).toDouble()) {
                setError(errorMessage, QStringLiteral(
                    "BoardStructure reciprocal head-to-head rows differ"));
                return false;
            }
            pairIds.insert(unorderedPairId);
            pairs.append({playerId, opponentId, unorderedPairId, pairGames});
            pairGameTotal += pairGames;
            squaredPairGameTotal += pairGames * pairGames;
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const PairCount &left, const PairCount &right) {
        if (left.games != right.games) {
            return left.games > right.games;
        }
        return left.unorderedPairId < right.unorderedPairId;
    });
    const PairCount &largestPair = pairs.first();
    if (pairs.size() != unorderedPairs || pairGameTotal != games
        || global.value(QStringLiteral("largest_unordered_pair_id")).toString()
            != largestPair.unorderedPairId
        || largestPairPlayerIds.at(0).toString() != largestPair.playerA
        || largestPairPlayerIds.at(1).toString() != largestPair.playerB
        || largestPairGames != largestPair.games
        || largestPairGameShare != roundedSignedPpm(largestPairGames, games)
        || pairHhi != roundedSignedPpm(
            squaredPairGameTotal, games * games)) {
        setError(errorMessage, QStringLiteral(
            "BoardStructure unordered-pair concentration does not conserve"));
        return false;
    }
    *playersById = parsedPlayers;
    return true;
}

QString durationText(const QJsonValue &value)
{
    if (!value.isDouble()) {
        return QStringLiteral("N/A");
    }
    const qint64 milliseconds = static_cast<qint64>(value.toDouble());
    return QStringLiteral("%1 s").arg(
        QString::number(static_cast<double>(milliseconds) / 1'000.0, 'f', 1));
}

QString coverageText(const QJsonValue &value)
{
    if (!value.isDouble()) {
        return QStringLiteral("N/A");
    }
    return QStringLiteral("%1%").arg(
        QString::number(value.toDouble() / 10'000.0, 'f', 1));
}

QString numberText(qint64 value)
{
    return QLocale().toString(value);
}

QJsonObject boardStructureMetric(const QJsonArray &metrics, const QString &code)
{
    for (const QJsonValue &value : metrics) {
        const QJsonObject metric = value.toObject();
        if (metric.value(QStringLiteral("metric_code")).toString() == code) {
            return metric;
        }
    }
    return {};
}

QString signedFixedPointText(qint64 value, double divisor, const QString &suffix)
{
    const double converted = static_cast<double>(value) / divisor;
    const QString sign = converted > 0.0 ? QStringLiteral("+") : QString();
    return sign + QString::number(converted, 'f', suffix.isEmpty() ? 2 : 1) + suffix;
}

QString boardStructureValueText(const QJsonObject &metric)
{
    const QJsonValue value = metric.value(QStringLiteral("aggregate_value_ppm"));
    if (!value.isDouble()) {
        return QStringLiteral("N/A");
    }
    const qint64 ppm = static_cast<qint64>(value.toDouble());
    if (metric.value(QStringLiteral("metric_code")).toString().startsWith(
            QStringLiteral("material_delta_mean."))) {
        return signedFixedPointText(ppm, static_cast<double>(kPpm), QString());
    }
    return QStringLiteral("%1%").arg(
        QString::number(static_cast<double>(ppm) / 10'000.0, 'f', 1));
}

QString boardStructureDifferenceText(
    const QJsonObject &left,
    const QJsonObject &right)
{
    const QJsonValue leftValue = left.value(QStringLiteral("aggregate_value_ppm"));
    const QJsonValue rightValue = right.value(QStringLiteral("aggregate_value_ppm"));
    if (!leftValue.isDouble() || !rightValue.isDouble()) {
        return QStringLiteral("N/A");
    }
    const qint64 difference = static_cast<qint64>(leftValue.toDouble())
        - static_cast<qint64>(rightValue.toDouble());
    if (left.value(QStringLiteral("metric_code")).toString().startsWith(
            QStringLiteral("material_delta_mean."))) {
        return signedFixedPointText(
            difference, static_cast<double>(kPpm), QString());
    }
    return signedFixedPointText(difference, 10'000.0, QStringLiteral(" pp"));
}

QString boardStructureObservationText(const QJsonObject &metric)
{
    return QStringLiteral("%1 / %2")
        .arg(numberText(static_cast<qint64>(metric.value(
            QStringLiteral("observed_player_game_count")).toDouble())))
        .arg(numberText(static_cast<qint64>(metric.value(
            QStringLiteral("not_applicable_player_game_count")).toDouble())));
}

QJsonObject boardStructureHeadToHeadRow(
    const QJsonObject &player,
    const QString &opponentId)
{
    for (const QJsonValue &value : player.value(
             QStringLiteral("head_to_head")).toArray()) {
        const QJsonObject row = value.toObject();
        if (row.value(QStringLiteral("opponent_id")).toString() == opponentId) {
            return row;
        }
    }
    return {};
}

QString boardStructurePairedText(const QJsonObject &metric)
{
    const QJsonValue value = metric.value(
        QStringLiteral("mean_player_minus_opponent_ppm"));
    if (!value.isDouble()) {
        return QStringLiteral("N/A");
    }
    const qint64 ppm = static_cast<qint64>(value.toDouble());
    if (metric.value(QStringLiteral("metric_code")).toString().startsWith(
            QStringLiteral("material_delta_mean."))) {
        return signedFixedPointText(ppm, static_cast<double>(kPpm), QString());
    }
    return signedFixedPointText(ppm, 10'000.0, QStringLiteral(" pp"));
}

QString boardStructureCellText(const QJsonObject &metric)
{
    const qint64 observed = static_cast<qint64>(metric.value(
        QStringLiteral("observed_player_game_count")).toDouble());
    const qint64 notApplicable = static_cast<qint64>(metric.value(
        QStringLiteral("not_applicable_player_game_count")).toDouble());
    return QStringLiteral("%1\n%2 obs · %3 N/A")
        .arg(boardStructureValueText(metric))
        .arg(numberText(observed))
        .arg(numberText(notApplicable));
}

QString boardStructureTooltip(const QJsonObject &metric)
{
    const QJsonValue numerator = metric.value(QStringLiteral("numerator_sum"));
    const QString numeratorText = numerator.isDouble()
        ? numberText(static_cast<qint64>(numerator.toDouble()))
        : QStringLiteral("N/A");
    QString output = QStringLiteral(
        "%1\nNumerator %2 · denominator %3\nObserved rows %4 · N/A rows %5\n"
        "Paired rows %6 · mean player minus opponent %7")
        .arg(metric.value(QStringLiteral("metric_code")).toString())
        .arg(numeratorText)
        .arg(numberText(static_cast<qint64>(metric.value(
            QStringLiteral("denominator_sum")).toDouble())))
        .arg(numberText(static_cast<qint64>(metric.value(
            QStringLiteral("observed_player_game_count")).toDouble())))
        .arg(numberText(static_cast<qint64>(metric.value(
            QStringLiteral("not_applicable_player_game_count")).toDouble())))
        .arg(numberText(static_cast<qint64>(metric.value(
            QStringLiteral("paired_player_game_count")).toDouble())))
        .arg(boardStructurePairedText(metric));
    if (metric.value(QStringLiteral("metric_code")).toString().startsWith(
            QStringLiteral("material_delta_mean."))) {
        output += QStringLiteral(
            "\nFixed 1/3/3/5/9 material units; not engine evaluation or winning chance.");
    }
    return output;
}

QString phaseText(QString value)
{
    if (!value.isEmpty()) {
        value[0] = value.at(0).toUpper();
    }
    return value;
}

QString compactUtc(QString value)
{
    if (value.size() >= 20 && value.at(10) == QLatin1Char('T')) {
        return value.mid(5, 14).replace(QLatin1Char('T'), QLatin1Char(' '))
            + QLatin1Char('Z');
    }
    return value;
}

QString compactSourcePlan(const QString &value)
{
    const qsizetype separator = value.lastIndexOf(QLatin1Char(':'));
    const QString digest = separator < 0 ? value : value.mid(separator + 1);
    if (digest.size() <= 16) {
        return digest;
    }
    return digest.left(8) + QChar(0x2026) + digest.right(4);
}

QJsonValue optionalIntegerValue(const std::optional<qint64> &value)
{
    return value.has_value()
        ? QJsonValue(*value)
        : QJsonValue(QJsonValue::Null);
}

QJsonObject catalogMetricMapping(
    const PlayerAnalysisCatalogMetricAggregate &metric)
{
    return QJsonObject {
        {QStringLiteral("aggregate_status"),
         metric.aggregate.denominatorSum > 0
             ? QStringLiteral("observed")
             : QStringLiteral("not_applicable")},
        {QStringLiteral("aggregate_value_ppm"),
         optionalIntegerValue(metric.aggregate.aggregateValuePpm)},
        {QStringLiteral("denominator_sum"), metric.aggregate.denominatorSum},
        {QStringLiteral("mean_player_minus_opponent_ppm"),
         optionalIntegerValue(metric.aggregate.meanPlayerMinusOpponentPpm)},
        {QStringLiteral("metric_code"), metric.metricCode},
        {QStringLiteral("not_applicable_player_game_count"),
         metric.aggregate.notApplicablePlayerGameCount},
        {QStringLiteral("numerator_sum"),
         optionalIntegerValue(metric.aggregate.numeratorSum)},
        {QStringLiteral("observed_player_game_count"),
         metric.aggregate.observedPlayerGameCount},
        {QStringLiteral("paired_player_game_count"),
         metric.aggregate.pairedPlayerGameCount},
    };
}

qint64 pressureCount(const QJsonObject &aggregate, qint64 threshold)
{
    for (const QJsonValue &value : aggregate.value(QStringLiteral("pressure_counts")).toArray()) {
        const QJsonObject item = value.toObject();
        if (static_cast<qint64>(item.value(QStringLiteral("threshold_ms")).toDouble()) == threshold) {
            return static_cast<qint64>(item.value(QStringLiteral("move_count")).toDouble());
        }
    }
    return 0;
}

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

class NumericTableWidgetItem final : public QTableWidgetItem
{
public:
    explicit NumericTableWidgetItem(qint64 value, const QString &suffix = QString())
        : QTableWidgetItem(numberText(value) + suffix)
    {
        setData(Qt::UserRole, value);
        setFlags(flags() & ~Qt::ItemIsEditable);
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        return data(Qt::UserRole).toLongLong() < other.data(Qt::UserRole).toLongLong();
    }
};

qint64 nearestRank(QVector<qint64> values, int numerator, int denominator)
{
    if (values.isEmpty()) {
        return -1;
    }
    std::sort(values.begin(), values.end());
    const qsizetype rank = std::max<qsizetype>(
        1,
        (values.size() * numerator + denominator - 1) / denominator);
    return values.at(rank - 1);
}

QJsonObject explorerAggregate(const QVector<ExplorerDecisionRow> &rows)
{
    QVector<qint64> elapsed;
    QHash<QString, qint64> statuses;
    qint64 starts = 0;
    qint64 elapsedSum = 0;
    for (const ExplorerDecisionRow &row : rows) {
        ++statuses[row.elapsedStatus];
        if (row.decisionStartClockMs >= 0) {
            ++starts;
        }
        if (row.elapsedMs >= 0) {
            elapsed.append(row.elapsedMs);
            elapsedSum += row.elapsedMs;
        }
    }
    QJsonArray statusRows;
    QStringList statusNames = statuses.keys();
    std::sort(statusNames.begin(), statusNames.end());
    for (const QString &status : statusNames) {
        statusRows.append(QJsonObject {
            {QStringLiteral("move_count"), statuses.value(status)},
            {QStringLiteral("status"), status},
        });
    }
    QJsonArray pressureRows;
    for (const qint64 threshold : {1'000LL, 5'000LL, 10'000LL, 30'000LL, 60'000LL}) {
        qint64 count = 0;
        for (const ExplorerDecisionRow &row : rows) {
            count += row.decisionStartClockMs >= 0
                && row.decisionStartClockMs <= threshold;
        }
        pressureRows.append(QJsonObject {
            {QStringLiteral("move_count"), count},
            {QStringLiteral("threshold_ms"), threshold},
        });
    }
    const qint64 p50 = nearestRank(elapsed, 1, 2);
    const qint64 p90 = nearestRank(elapsed, 9, 10);
    const qint64 maximum = elapsed.isEmpty()
        ? -1
        : *std::max_element(elapsed.cbegin(), elapsed.cend());
    return QJsonObject {
        {QStringLiteral("decision_count"), rows.size()},
        {QStringLiteral("decision_start_clock_observed_count"), starts},
        {QStringLiteral("elapsed_coverage_ppm"), rows.isEmpty()
                ? QJsonValue(QJsonValue::Null)
                : QJsonValue((elapsed.size() * kPpm + rows.size() / 2) / rows.size())},
        {QStringLiteral("elapsed_missing_count"), rows.size() - elapsed.size()},
        {QStringLiteral("elapsed_observed_count"), elapsed.size()},
        {QStringLiteral("elapsed_status_counts"), statusRows},
        {QStringLiteral("maximum_observed_elapsed_ms"), maximum < 0
                ? QJsonValue(QJsonValue::Null)
                : QJsonValue(maximum)},
        {QStringLiteral("observed_elapsed_sum_ms"), elapsed.isEmpty()
                ? QJsonValue(QJsonValue::Null)
                : QJsonValue(elapsedSum)},
        {QStringLiteral("p50_observed_elapsed_ms"), p50 < 0
                ? QJsonValue(QJsonValue::Null)
                : QJsonValue(p50)},
        {QStringLiteral("p90_observed_elapsed_ms"), p90 < 0
                ? QJsonValue(QJsonValue::Null)
                : QJsonValue(p90)},
        {QStringLiteral("pressure_counts"), pressureRows},
    };
}

QJsonArray explorerGroups(
    const QVector<ExplorerDecisionRow> &rows,
    const QStringList &groups,
    const std::function<QString(const ExplorerDecisionRow &)> &selector)
{
    QJsonArray output;
    for (const QString &group : groups) {
        QVector<ExplorerDecisionRow> selected;
        for (const ExplorerDecisionRow &row : rows) {
            if (selector(row) == group) {
                selected.append(row);
            }
        }
        output.append(QJsonObject {
            {QStringLiteral("group"), group},
            {QStringLiteral("statistics"), explorerAggregate(selected)},
        });
    }
    return output;
}

QJsonArray explorerLongest(QVector<ExplorerDecisionRow> rows, int maximum)
{
    rows.erase(
        std::remove_if(rows.begin(), rows.end(), [](const ExplorerDecisionRow &row) {
            return row.elapsedMs < 0;
        }),
        rows.end());
    std::sort(rows.begin(), rows.end(), [](const ExplorerDecisionRow &left, const ExplorerDecisionRow &right) {
        if (left.elapsedMs != right.elapsedMs) {
            return left.elapsedMs > right.elapsedMs;
        }
        return std::tie(left.eventStartUtc, left.sourceGameId, left.ply)
            < std::tie(right.eventStartUtc, right.sourceGameId, right.ply);
    });
    if (rows.size() > maximum) {
        rows.resize(maximum);
    }
    QJsonArray output;
    for (const ExplorerDecisionRow &row : rows) {
        output.append(QJsonObject {
            {QStringLiteral("clock_remaining_after_move_ms"), row.clockAfterMs < 0
                    ? QJsonValue(QJsonValue::Null)
                    : QJsonValue(row.clockAfterMs)},
            {QStringLiteral("decision_start_clock_ms"), row.decisionStartClockMs < 0
                    ? QJsonValue(QJsonValue::Null)
                    : QJsonValue(row.decisionStartClockMs)},
            {QStringLiteral("elapsed_move_ms"), row.elapsedMs},
            {QStringLiteral("event_start_utc"), row.eventStartUtc},
            {QStringLiteral("forcedness_status"), row.forcedness},
            {QStringLiteral("legal_move_count"), row.legalMoveCount},
            {QStringLiteral("move_number"), row.moveNumber},
            {QStringLiteral("opponent_id"), row.opponentId},
            {QStringLiteral("played_move_san"), row.san},
            {QStringLiteral("played_move_uci"), row.uci},
            {QStringLiteral("player_color"), row.playerColor},
            {QStringLiteral("player_id"), row.playerId},
            {QStringLiteral("ply"), row.ply},
            {QStringLiteral("position_phase"), row.phase},
            {QStringLiteral("source_game_id"), row.sourceGameId},
        });
    }
    return output;
}

QFrame *metricCard(
    QWidget *parent,
    const QString &title,
    QLabel **valueLabel,
    QLabel **titleLabel = nullptr)
{
    auto *frame = new QFrame(parent);
    frame->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(10, 7, 10, 7);
    layout->setSpacing(2);
    auto *heading = new QLabel(title, frame);
    heading->setTextFormat(Qt::PlainText);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    auto *value = new QLabel(QStringLiteral("—"), frame);
    value->setTextFormat(Qt::PlainText);
    QFont valueFont = value->font();
    valueFont.setBold(true);
    valueFont.setPointSizeF(valueFont.pointSizeF() + 3.0);
    value->setFont(valueFont);
    layout->addWidget(heading);
    layout->addWidget(value);
    if (titleLabel != nullptr) {
        *titleLabel = heading;
    }
    *valueLabel = value;
    return frame;
}

void configureTable(QTableWidget *table, const QStringList &headers)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
}

QString explorerFilterSql()
{
    return QStringLiteral(
        "pg.player_id = :player "
        "AND g.utc_day BETWEEN :from_day AND :to_day "
        "AND (:color = '' OR pg.player_color = :color) "
        "AND (:outcome = '' OR pg.outcome = :outcome) "
        "AND (:opponent = '' OR pg.opponent_id = :opponent) "
        "AND (:opening_status = '' OR ("
        "g.opening_status = :opening_status "
        "AND COALESCE(g.opening_eco, '') = :opening_eco "
        "AND COALESCE(g.opening_name, '') = :opening_name))");
}

} // namespace

PlayerStatisticsPanel::PlayerStatisticsPanel(QWidget *parent)
    : QGroupBox(QStringLiteral("player statistics"), parent)
    , m_openButton(new QPushButton(QStringLiteral("Open Player Statistics"), this))
    , m_openStructureButton(new QPushButton(QStringLiteral("Open Structure Stats"), this))
    , m_playerCombo(new QComboBox(this))
    , m_explorerFilterPanel(new QWidget(this))
    , m_fromDateEdit(new QDateEdit(this))
    , m_toDateEdit(new QDateEdit(this))
    , m_colorFilter(new QComboBox(this))
    , m_resultFilter(new QComboBox(this))
    , m_opponentFilter(new QComboBox(this))
    , m_openingFilter(new QComboBox(this))
    , m_resetFiltersButton(new QPushButton(QStringLiteral("Reset"), this))
    , m_replayGameButton(new QPushButton(QStringLiteral("Replay Selected Game"), this))
    , m_gamesHintLabel(nullptr)
    , m_statusLabel(new QLabel(this))
    , m_summaryLabel(new QLabel(this))
    , m_gameMetricLabel(nullptr)
    , m_moveMetricLabel(nullptr)
    , m_populationMetricTitleLabel(nullptr)
    , m_populationMetricLabel(nullptr)
    , m_coverageMetricLabel(nullptr)
    , m_medianMetricLabel(nullptr)
    , m_p90MetricLabel(nullptr)
    , m_pressureLabel(new QLabel(this))
    , m_opponentHintLabel(new QLabel(this))
    , m_structureStatusLabel(new QLabel(this))
    , m_structureSummaryLabel(new QLabel(this))
    , m_structureConcentrationLabel(new QLabel(this))
    , m_structureComparisonPanel(new QWidget(this))
    , m_structureComparisonPlayerCombo(new QComboBox(m_structureComparisonPanel))
    , m_structureComparisonCategoryCombo(new QComboBox(m_structureComparisonPanel))
    , m_structureComparisonSummaryLabel(new QLabel(this))
    , m_structureComparisonTable(new QTableWidget(this))
    , m_structureDrilldownLabel(new QLabel(this))
    , m_structureDrilldownTable(new QTableWidget(this))
    , m_structureHeadToHeadLabel(new QLabel(QStringLiteral("Head-to-head results"), this))
    , m_structureHeadToHeadFilterPanel(new QWidget(this))
    , m_structureOpponentSearch(new QLineEdit(m_structureHeadToHeadFilterPanel))
    , m_structureMinimumGamesSpin(new QSpinBox(m_structureHeadToHeadFilterPanel))
    , m_detailTabs(new QTabWidget(this))
    , m_phaseTable(new QTableWidget(this))
    , m_decisionContextTable(new QTableWidget(this))
    , m_gameTable(new QTableWidget(this))
    , m_openingTable(new QTableWidget(this))
    , m_opponentTable(new QTableWidget(this))
    , m_longestTable(new QTableWidget(this))
    , m_structureMetricTable(new QTableWidget(this))
    , m_structureCastlingTable(new QTableWidget(this))
    , m_structureHeadToHeadTable(new QTableWidget(this))
    , m_updatingExplorerFilters(false)
    , m_dedicatedExplorerMode(false)
{
    setMinimumWidth(520);
    auto *layout = new QVBoxLayout(this);
    auto *actions = new QHBoxLayout();
    actions->addWidget(m_openButton);
    actions->addWidget(m_openStructureButton);
    actions->addWidget(new QLabel(QStringLiteral("Player"), this));
    actions->addWidget(m_playerCombo, 1);
    layout->addLayout(actions);

    m_playerCombo->setEditable(true);
    m_playerCombo->setInsertPolicy(QComboBox::NoInsert);
    m_playerCombo->setMaxVisibleItems(20);
    m_playerCombo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    m_playerCombo->completer()->setFilterMode(Qt::MatchContains);

    auto *filterLayout = new QGridLayout(m_explorerFilterPanel);
    m_explorerFilterPanel->setObjectName(
        QStringLiteral("playerStatisticsExplorerFilters"));
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setHorizontalSpacing(6);
    filterLayout->setVerticalSpacing(4);
    m_fromDateEdit->setObjectName(QStringLiteral("playerStatisticsFromDate"));
    m_toDateEdit->setObjectName(QStringLiteral("playerStatisticsToDate"));
    m_colorFilter->setObjectName(QStringLiteral("playerStatisticsColorFilter"));
    m_resultFilter->setObjectName(QStringLiteral("playerStatisticsResultFilter"));
    m_opponentFilter->setObjectName(QStringLiteral("playerStatisticsOpponentFilter"));
    m_openingFilter->setObjectName(QStringLiteral("playerStatisticsOpeningFilter"));
    m_resetFiltersButton->setObjectName(QStringLiteral("playerStatisticsResetFilters"));
    for (QDateEdit *dateEdit : {m_fromDateEdit, m_toDateEdit}) {
        dateEdit->setCalendarPopup(true);
        dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        dateEdit->setMinimumWidth(112);
    }
    m_colorFilter->addItem(QStringLiteral("All colors"), QString());
    m_colorFilter->addItem(QStringLiteral("White"), QStringLiteral("white"));
    m_colorFilter->addItem(QStringLiteral("Black"), QStringLiteral("black"));
    m_resultFilter->addItem(QStringLiteral("All results"), QString());
    m_resultFilter->addItem(QStringLiteral("Wins"), QStringLiteral("win"));
    m_resultFilter->addItem(QStringLiteral("Draws"), QStringLiteral("draw"));
    m_resultFilter->addItem(QStringLiteral("Losses"), QStringLiteral("loss"));
    filterLayout->addWidget(new QLabel(QStringLiteral("From"), m_explorerFilterPanel), 0, 0);
    filterLayout->addWidget(m_fromDateEdit, 0, 1);
    filterLayout->addWidget(new QLabel(QStringLiteral("To"), m_explorerFilterPanel), 0, 2);
    filterLayout->addWidget(m_toDateEdit, 0, 3);
    filterLayout->addWidget(m_resetFiltersButton, 0, 4);
    filterLayout->addWidget(m_colorFilter, 1, 0, 1, 2);
    filterLayout->addWidget(m_resultFilter, 1, 2, 1, 2);
    filterLayout->addWidget(new QLabel(QStringLiteral("Opponent"), m_explorerFilterPanel), 2, 0);
    filterLayout->addWidget(m_opponentFilter, 2, 1, 1, 4);
    filterLayout->addWidget(new QLabel(QStringLiteral("Opening"), m_explorerFilterPanel), 3, 0);
    filterLayout->addWidget(m_openingFilter, 3, 1, 1, 4);
    filterLayout->setColumnStretch(1, 1);
    filterLayout->setColumnStretch(3, 1);
    m_explorerFilterPanel->setVisible(false);
    layout->addWidget(m_explorerFilterPanel);

    for (QLabel *label : {m_statusLabel, m_summaryLabel}) {
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
    }
    m_statusLabel->setObjectName(QStringLiteral("playerStatisticsSourceStatus"));
    m_summaryLabel->setObjectName(QStringLiteral("playerStatisticsContext"));
    layout->addWidget(m_statusLabel);

    auto *overviewPage = new QWidget(m_detailTabs);
    auto *overviewLayout = new QVBoxLayout(overviewPage);
    overviewLayout->setContentsMargins(8, 8, 8, 8);
    overviewLayout->setSpacing(8);
    auto *metricLayout = new QGridLayout();
    metricLayout->setContentsMargins(0, 0, 0, 0);
    metricLayout->setSpacing(6);
    metricLayout->addWidget(metricCard(overviewPage, QStringLiteral("Games"), &m_gameMetricLabel), 0, 0);
    metricLayout->addWidget(metricCard(overviewPage, QStringLiteral("Moves"), &m_moveMetricLabel), 0, 1);
    metricLayout->addWidget(metricCard(
        overviewPage,
        QStringLiteral("Players"),
        &m_populationMetricLabel,
        &m_populationMetricTitleLabel), 0, 2);
    metricLayout->addWidget(metricCard(overviewPage, QStringLiteral("Coverage"), &m_coverageMetricLabel), 1, 0);
    metricLayout->addWidget(metricCard(overviewPage, QStringLiteral("Median"), &m_medianMetricLabel), 1, 1);
    metricLayout->addWidget(metricCard(overviewPage, QStringLiteral("P90"), &m_p90MetricLabel), 1, 2);
    for (int column = 0; column < 3; ++column) {
        metricLayout->setColumnStretch(column, 1);
    }
    overviewLayout->addLayout(metricLayout);
    overviewLayout->addWidget(m_summaryLabel);

    configureTable(
        m_phaseTable,
        {QStringLiteral("Phase"), QStringLiteral("Moves"), QStringLiteral("Coverage"),
         QStringLiteral("Median"), QStringLiteral("P90"), QStringLiteral("Max")});
    configureTable(
        m_decisionContextTable,
        {QStringLiteral("Context"), QStringLiteral("Moves"), QStringLiteral("Coverage"),
         QStringLiteral("Median"), QStringLiteral("P90"), QStringLiteral("Max")});
    configureTable(
        m_gameTable,
        {QStringLiteral("UTC"), QStringLiteral("Opponent"), QStringLiteral("Color"),
         QStringLiteral("Result"), QStringLiteral("Opening"), QStringLiteral("Player moves"),
         QStringLiteral("Longest"), QStringLiteral("Rating"), QStringLiteral("Opp rating")});
    configureTable(
        m_openingTable,
        {QStringLiteral("Opening"), QStringLiteral("Games"), QStringLiteral("W-D-L"),
         QStringLiteral("Score"), QStringLiteral("W / B")});
    configureTable(
        m_opponentTable,
        {QStringLiteral("Opponent"), QStringLiteral("Games"), QStringLiteral("W-D-L"),
         QStringLiteral("Score"), QStringLiteral("Median"), QStringLiteral("P90")});
    configureTable(
        m_longestTable,
        {QStringLiteral("Elapsed"), QStringLiteral("UTC"), QStringLiteral("Opponent"),
         QStringLiteral("Color"), QStringLiteral("Move"), QStringLiteral("Phase"),
         QStringLiteral("Clock before")});
    configureTable(
        m_structureMetricTable,
        {QStringLiteral("Measure"), QStringLiteral("Overall"),
         QStringLiteral("Opening"), QStringLiteral("Middlegame"),
         QStringLiteral("Endgame")});
    configureTable(
        m_structureCastlingTable,
        {QStringLiteral("Castling"), QStringLiteral("Rate"),
         QStringLiteral("Observed / N/A"), QStringLiteral("Paired vs opponent")});
    configureTable(
        m_structureComparisonTable,
        {QStringLiteral("Measure"), QStringLiteral("Player A"),
         QStringLiteral("Player B"), QStringLiteral("A \u2212 B"),
         QStringLiteral("A obs / N/A"), QStringLiteral("B obs / N/A")});
    configureTable(
        m_structureHeadToHeadTable,
        {QStringLiteral("Opponent"), QStringLiteral("Games"),
         QStringLiteral("White games"), QStringLiteral("Black games"),
         QStringLiteral("Wins-Draws-Losses"), QStringLiteral("Score rate")});
    for (QTableWidget *table : {m_phaseTable, m_decisionContextTable}) {
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    for (int column = 0; column < m_gameTable->columnCount(); ++column) {
        m_gameTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Fixed);
    }
    const QList<int> gameColumnWidths {118, 150, 58, 58, 240, 88, 84, 72, 84};
    for (int column = 0; column < gameColumnWidths.size(); ++column) {
        m_gameTable->setColumnWidth(column, gameColumnWidths.at(column));
    }
    m_openingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < m_openingTable->columnCount(); ++column) {
        m_openingTable->horizontalHeader()->setSectionResizeMode(
            column,
            QHeaderView::Fixed);
    }
    const QList<int> openingColumnWidths {0, 58, 72, 68, 64};
    for (int column = 1; column < openingColumnWidths.size(); ++column) {
        m_openingTable->setColumnWidth(column, openingColumnWidths.at(column));
    }
    m_openingTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *phaseHeading = new QLabel(QStringLiteral("Move time by phase"), overviewPage);
    QFont phaseFont = phaseHeading->font();
    phaseFont.setBold(true);
    phaseHeading->setFont(phaseFont);
    overviewLayout->addWidget(phaseHeading);
    m_phaseTable->setMaximumHeight(155);
    overviewLayout->addWidget(m_phaseTable);
    auto *contextHeading = new QLabel(QStringLiteral("Decision context"), overviewPage);
    contextHeading->setFont(phaseFont);
    overviewLayout->addWidget(contextHeading);
    m_decisionContextTable->setMaximumHeight(190);
    overviewLayout->addWidget(m_decisionContextTable);
    m_pressureLabel->setTextFormat(Qt::PlainText);
    m_pressureLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pressureLabel->setWordWrap(true);
    overviewLayout->addWidget(m_pressureLabel);

    auto *gamesPage = new QWidget(m_detailTabs);
    auto *gamesLayout = new QVBoxLayout(gamesPage);
    gamesLayout->setContentsMargins(8, 8, 8, 8);
    m_gamesHintLabel = new QLabel(
        QStringLiteral("One row per filtered game. Double-click a row to replay it. Ratings are source-observed postgame ratings."),
        gamesPage);
    m_gamesHintLabel->setTextFormat(Qt::PlainText);
    m_gamesHintLabel->setWordWrap(true);
    auto *gamesHeader = new QHBoxLayout();
    gamesHeader->addWidget(m_gamesHintLabel, 1);
    m_replayGameButton->setObjectName(QStringLiteral("playerStatisticsReplayGame"));
    m_replayGameButton->setEnabled(false);
    gamesHeader->addWidget(m_replayGameButton);
    gamesLayout->addLayout(gamesHeader);
    m_gameTable->setObjectName(QStringLiteral("playerStatisticsGamesTable"));
    m_gameTable->setSortingEnabled(true);
    gamesLayout->addWidget(m_gameTable, 1);

    auto *openingsPage = new QWidget(m_detailTabs);
    auto *openingsLayout = new QVBoxLayout(openingsPage);
    openingsLayout->setContentsMargins(8, 8, 8, 8);
    auto *openingsHint = new QLabel(
        QStringLiteral("Pinned exact-position opening labels over the currently filtered games."),
        openingsPage);
    openingsHint->setTextFormat(Qt::PlainText);
    openingsHint->setWordWrap(true);
    openingsLayout->addWidget(openingsHint);
    m_openingTable->setObjectName(QStringLiteral("playerStatisticsOpeningsTable"));
    m_openingTable->setSortingEnabled(true);
    openingsLayout->addWidget(m_openingTable, 1);

    auto *opponentPage = new QWidget(m_detailTabs);
    auto *opponentLayout = new QVBoxLayout(opponentPage);
    opponentLayout->setContentsMargins(8, 8, 8, 8);
    m_opponentHintLabel->setTextFormat(Qt::PlainText);
    m_opponentHintLabel->setWordWrap(true);
    opponentLayout->addWidget(m_opponentHintLabel);
    opponentLayout->addWidget(m_opponentTable, 1);

    auto *longestPage = new QWidget(m_detailTabs);
    auto *longestLayout = new QVBoxLayout(longestPage);
    longestLayout->setContentsMargins(8, 8, 8, 8);
    auto *longestHint = new QLabel(
        QStringLiteral("Largest server-recorded clock deltas. They are not direct measurements of cognitive thinking time."),
        longestPage);
    longestHint->setTextFormat(Qt::PlainText);
    longestHint->setWordWrap(true);
    longestLayout->addWidget(longestHint);
    longestLayout->addWidget(m_longestTable, 1);

    auto *structurePage = new QWidget(m_detailTabs);
    auto *structureLayout = new QVBoxLayout(structurePage);
    structureLayout->setContentsMargins(8, 8, 8, 8);
    structureLayout->setSpacing(8);
    for (QLabel *label : {
             m_structureStatusLabel,
             m_structureSummaryLabel,
             m_structureConcentrationLabel,
         }) {
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
        structureLayout->addWidget(label);
    }
    m_structureStatusLabel->setObjectName(QStringLiteral("playerStatisticsStructureStatus"));
    m_structureSummaryLabel->setObjectName(QStringLiteral("playerStatisticsStructureSummary"));
    m_structureConcentrationLabel->setObjectName(
        QStringLiteral("playerStatisticsStructureConcentration"));
    auto *structureHint = new QLabel(
        QStringLiteral(
            "Display-only mechanical postgame descriptions labeled for the complete target corpus; "
            "ParlAWL does not authenticate the catalog or snapshot. Explorer filters do not alter this tab. "
            "Opponent controls only narrow the head-to-head rows; summaries and metrics remain complete. "
            "Hover a cell for its numerator, denominator, observability, and same-game paired comparison. "
            "With an analysis catalog open, double-click a metric to inspect its exact supporting games."),
        structurePage);
    structureHint->setTextFormat(Qt::PlainText);
    structureHint->setWordWrap(true);
    structureLayout->addWidget(structureHint);
    auto *structureComparisonLayout = new QHBoxLayout(m_structureComparisonPanel);
    structureComparisonLayout->setContentsMargins(0, 0, 0, 0);
    structureComparisonLayout->addWidget(new QLabel(
        QStringLiteral("Compare with"), m_structureComparisonPanel));
    m_structureComparisonPlayerCombo->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureComparePlayer"));
    m_structureComparisonPlayerCombo->setEditable(true);
    m_structureComparisonPlayerCombo->setInsertPolicy(QComboBox::NoInsert);
    m_structureComparisonPlayerCombo->setMaxVisibleItems(20);
    m_structureComparisonPlayerCombo->completer()->setCaseSensitivity(
        Qt::CaseInsensitive);
    m_structureComparisonPlayerCombo->completer()->setFilterMode(
        Qt::MatchContains);
    structureComparisonLayout->addWidget(m_structureComparisonPlayerCombo, 1);
    structureComparisonLayout->addWidget(new QLabel(
        QStringLiteral("Category"), m_structureComparisonPanel));
    m_structureComparisonCategoryCombo->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureCompareCategory"));
    m_structureComparisonCategoryCombo->addItem(
        QStringLiteral("All metrics"), QString());
    m_structureComparisonCategoryCombo->addItem(
        QStringLiteral("Position & pieces"), QStringLiteral("position"));
    m_structureComparisonCategoryCombo->addItem(
        QStringLiteral("Pawns"), QStringLiteral("pawns"));
    m_structureComparisonCategoryCombo->addItem(
        QStringLiteral("Material"), QStringLiteral("material"));
    m_structureComparisonCategoryCombo->addItem(
        QStringLiteral("Castling"), QStringLiteral("castling"));
    structureComparisonLayout->addWidget(m_structureComparisonCategoryCombo);
    m_structureComparisonPanel->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureComparisonPanel"));
    m_structureComparisonPanel->setVisible(false);
    structureLayout->addWidget(m_structureComparisonPanel);
    m_structureComparisonSummaryLabel->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureComparisonSummary"));
    m_structureComparisonSummaryLabel->setTextFormat(Qt::PlainText);
    m_structureComparisonSummaryLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    m_structureComparisonSummaryLabel->setWordWrap(true);
    m_structureComparisonSummaryLabel->setVisible(false);
    structureLayout->addWidget(m_structureComparisonSummaryLabel);
    m_structureComparisonTable->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureComparisonTable"));
    m_structureComparisonTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    for (int column = 1; column < m_structureComparisonTable->columnCount(); ++column) {
        m_structureComparisonTable->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    }
    m_structureComparisonTable->horizontalHeader()->setStretchLastSection(false);
    m_structureComparisonTable->setVisible(false);
    structureLayout->addWidget(m_structureComparisonTable, 1);
    m_structureMetricTable->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureTable"));
    m_structureMetricTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int column = 1; column < m_structureMetricTable->columnCount(); ++column) {
        m_structureMetricTable->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::Stretch);
    }
    m_structureMetricTable->verticalHeader()->setDefaultSectionSize(46);
    structureLayout->addWidget(m_structureMetricTable, 1);
    m_structureCastlingTable->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureCastlingTable"));
    m_structureCastlingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_structureCastlingTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_structureCastlingTable->setMaximumHeight(145);
    structureLayout->addWidget(m_structureCastlingTable);
    m_structureDrilldownLabel->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureDrilldownLabel"));
    m_structureDrilldownLabel->setTextFormat(Qt::PlainText);
    m_structureDrilldownLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    m_structureDrilldownLabel->setWordWrap(true);
    m_structureDrilldownLabel->setVisible(false);
    structureLayout->addWidget(m_structureDrilldownLabel);
    configureTable(
        m_structureDrilldownTable,
        {QStringLiteral("UTC"), QStringLiteral("Opponent"),
         QStringLiteral("Color"), QStringLiteral("Result"),
         QStringLiteral("Opening"), QStringLiteral("Value"),
         QStringLiteral("Numerator / denominator"), QStringLiteral("Status"),
         QStringLiteral("Source game ID")});
    m_structureDrilldownTable->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureDrilldownTable"));
    m_structureDrilldownTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_structureDrilldownTable->horizontalHeader()->setSectionResizeMode(
        4, QHeaderView::Stretch);
    m_structureDrilldownTable->setMaximumHeight(260);
    m_structureDrilldownTable->setVisible(false);
    structureLayout->addWidget(m_structureDrilldownTable);
    QFont structureHeadingFont = m_structureHeadToHeadLabel->font();
    structureHeadingFont.setBold(true);
    m_structureHeadToHeadLabel->setFont(structureHeadingFont);
    m_structureHeadToHeadLabel->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureHeadToHeadLabel"));
    structureLayout->addWidget(m_structureHeadToHeadLabel);
    auto *structureHeadToHeadFilters = new QHBoxLayout(
        m_structureHeadToHeadFilterPanel);
    structureHeadToHeadFilters->setContentsMargins(0, 0, 0, 0);
    structureHeadToHeadFilters->addWidget(new QLabel(
        QStringLiteral("Opponent"), m_structureHeadToHeadFilterPanel));
    m_structureOpponentSearch->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureOpponentSearch"));
    m_structureOpponentSearch->setPlaceholderText(
        QStringLiteral("Search opponents"));
    m_structureOpponentSearch->setClearButtonEnabled(true);
    structureHeadToHeadFilters->addWidget(m_structureOpponentSearch, 1);
    structureHeadToHeadFilters->addWidget(new QLabel(
        QStringLiteral("Minimum games"), m_structureHeadToHeadFilterPanel));
    m_structureMinimumGamesSpin->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureMinimumGames"));
    m_structureMinimumGamesSpin->setRange(1, 5'000);
    m_structureMinimumGamesSpin->setValue(1);
    structureHeadToHeadFilters->addWidget(m_structureMinimumGamesSpin);
    structureLayout->addWidget(m_structureHeadToHeadFilterPanel);
    m_structureHeadToHeadTable->setObjectName(
        QStringLiteral("playerStatisticsBoardStructureHeadToHeadTable"));
    m_structureHeadToHeadTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    for (int column = 1;
         column < m_structureHeadToHeadTable->columnCount(); ++column) {
        m_structureHeadToHeadTable->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::ResizeToContents);
    }
    m_structureHeadToHeadTable->setMaximumHeight(180);
    m_structureHeadToHeadTable->setSortingEnabled(true);
    m_structureHeadToHeadTable->horizontalHeader()->setSortIndicator(
        1, Qt::DescendingOrder);
    structureLayout->addWidget(m_structureHeadToHeadTable);

    m_detailTabs->setObjectName(QStringLiteral("playerStatisticsDetailTabs"));
    m_detailTabs->addTab(overviewPage, QStringLiteral("Overview"));
    m_detailTabs->addTab(gamesPage, QStringLiteral("Games"));
    m_detailTabs->addTab(openingsPage, QStringLiteral("Openings"));
    m_detailTabs->addTab(opponentPage, QStringLiteral("Opponents"));
    m_detailTabs->addTab(longestPage, QStringLiteral("Longest Moves"));
    m_detailTabs->addTab(structurePage, QStringLiteral("Board Structure"));
    layout->addWidget(m_detailTabs, 1);

    connect(m_openButton, &QPushButton::clicked, this, &PlayerStatisticsPanel::openSnapshotRequested);
    connect(
        m_openStructureButton,
        &QPushButton::clicked,
        this,
        &PlayerStatisticsPanel::openBoardStructureSnapshotRequested);
    connect(m_structureOpponentSearch, &QLineEdit::textChanged, this, [this] {
        rebuildBoardStructureView();
    });
    connect(
        m_structureMinimumGamesSpin,
        &QSpinBox::valueChanged,
        this,
        [this](int) { rebuildBoardStructureView(); });
    connect(
        m_structureComparisonPlayerCombo,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) { rebuildBoardStructureView(); });
    connect(
        m_structureComparisonCategoryCombo,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) { rebuildBoardStructureView(); });
    connect(m_detailTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (!m_explorerConnectionName.isEmpty()) {
            m_explorerFilterPanel->setVisible(
                index != m_detailTabs->count() - 1);
        }
    });
    const auto showMetricDrilldown = [this](QTableWidget *table, int row, int column) {
        const QTableWidgetItem *item = table->item(row, column);
        if (item == nullptr) {
            return;
        }
        showBoardStructureDrilldown(
            item->data(kBoardStructurePlayerIdRole).toString(),
            item->data(kBoardStructureMetricCodeRole).toString());
    };
    connect(
        m_structureMetricTable,
        &QTableWidget::cellDoubleClicked,
        this,
        [showMetricDrilldown, this](int row, int column) {
            showMetricDrilldown(m_structureMetricTable, row, column);
        });
    connect(
        m_structureCastlingTable,
        &QTableWidget::cellDoubleClicked,
        this,
        [showMetricDrilldown, this](int row, int column) {
            showMetricDrilldown(m_structureCastlingTable, row, column);
        });
    connect(
        m_structureComparisonTable,
        &QTableWidget::cellDoubleClicked,
        this,
        [showMetricDrilldown, this](int row, int column) {
            showMetricDrilldown(m_structureComparisonTable, row, column);
        });
    connect(
        m_structureDrilldownTable,
        &QTableWidget::cellDoubleClicked,
        this,
        [this](int row, int) {
            const QTableWidgetItem *item = m_structureDrilldownTable->item(row, 0);
            if (item == nullptr) {
                return;
            }
            const QString sourceGameId = item->data(
                kBoardStructureSourceGameIdRole).toString();
            if (!sourceGameId.isEmpty()) {
                emit gameBreakdownRequested(sourceGameId);
            }
        });
    connect(m_gameTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QTableWidgetItem *item = m_gameTable->item(row, 0);
        if (item == nullptr) {
            return;
        }
        const QString sourceGameId = item->data(Qt::UserRole + 1).toString();
        if (!sourceGameId.isEmpty()) {
            emit gameBreakdownRequested(sourceGameId);
        }
    });
    auto *openGameShortcut = new QShortcut(QKeySequence(Qt::Key_Return), m_gameTable);
    openGameShortcut->setObjectName(
        QStringLiteral("playerStatisticsOpenSelectedGameShortcut"));
    openGameShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(openGameShortcut, &QShortcut::activated, m_replayGameButton, [this] {
        if (m_replayGameButton->isEnabled()) {
            m_replayGameButton->click();
        }
    });
    connect(m_gameTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = m_gameTable->currentRow();
        const QTableWidgetItem *item = row >= 0 ? m_gameTable->item(row, 0) : nullptr;
        m_replayGameButton->setEnabled(
            item != nullptr && !item->data(Qt::UserRole + 1).toString().isEmpty());
    });
    connect(m_replayGameButton, &QPushButton::clicked, this, [this] {
        const int row = m_gameTable->currentRow();
        const QTableWidgetItem *item = row >= 0 ? m_gameTable->item(row, 0) : nullptr;
        if (item == nullptr) {
            return;
        }
        const QString sourceGameId = item->data(Qt::UserRole + 1).toString();
        if (!sourceGameId.isEmpty()) {
            emit gameBreakdownRequested(sourceGameId);
        }
    });
    connect(m_playerCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_explorerConnectionName.isEmpty()) {
            populateExplorerDependentFilters();
        }
        rebuildView();
    });
    connect(m_playerCombo->lineEdit(), &QLineEdit::returnPressed, this, [this] {
        selectTypedPlayer();
    });
    const auto rebuildForFilter = [this] {
        if (!m_updatingExplorerFilters && !m_explorerConnectionName.isEmpty()) {
            rebuildExplorerView();
        }
    };
    connect(m_fromDateEdit, &QDateEdit::dateChanged, this, rebuildForFilter);
    connect(m_toDateEdit, &QDateEdit::dateChanged, this, rebuildForFilter);
    connect(m_colorFilter, &QComboBox::currentIndexChanged, this, rebuildForFilter);
    connect(m_resultFilter, &QComboBox::currentIndexChanged, this, rebuildForFilter);
    connect(m_opponentFilter, &QComboBox::currentIndexChanged, this, rebuildForFilter);
    connect(m_openingFilter, &QComboBox::currentIndexChanged, this, rebuildForFilter);
    connect(m_resetFiltersButton, &QPushButton::clicked, this, [this] {
        resetExplorerFilters();
        rebuildExplorerView();
    });
    clearSnapshot();
}

PlayerStatisticsPanel::~PlayerStatisticsPanel()
{
    closeExplorerDatabase();
}

void PlayerStatisticsPanel::setDedicatedExplorerMode(bool enabled)
{
    m_dedicatedExplorerMode = enabled;
    setTitle(enabled ? QString() : QStringLiteral("player statistics"));
    m_openButton->setVisible(!enabled);
    m_openStructureButton->setVisible(!enabled);
    m_replayGameButton->setText(
        enabled ? QStringLiteral("Open Game Review")
                : QStringLiteral("Replay Selected Game"));
    m_gamesHintLabel->setText(enabled
        ? QStringLiteral(
              "Double-click a game to open or switch its Review tab. Return here to add another game.")
        : QStringLiteral(
              "One row per filtered game. Double-click a row to replay it. Ratings are source-observed postgame ratings."));
    m_gameTable->setAccessibleName(
        enabled ? QStringLiteral("Game library") : QStringLiteral("Filtered games"));
    m_gameTable->setAccessibleDescription(
        enabled
            ? QStringLiteral("Filtered games. Double-click a row or press Return to open Game Review.")
            : QStringLiteral("Filtered player games."));
    m_replayGameButton->setAccessibleDescription(
        enabled
            ? QStringLiteral("Open the selected game in the Review window")
            : QStringLiteral("Replay the selected game"));
    setStyleSheet(enabled
        ? QStringLiteral("QGroupBox { border: 0; margin: 0; padding: 0; }")
        : QString());
}

void PlayerStatisticsPanel::closeExplorerDatabase()
{
    m_analysisCatalog.reset();
    m_catalogStructurePlayerIds.clear();
    m_catalogGlobalStructureView = {};
    m_catalogStructureViewsByPlayer.clear();
    if (m_explorerConnectionName.isEmpty()) {
        m_explorerMetadata.clear();
        return;
    }
    const QString connectionName = m_explorerConnectionName;
    m_explorerConnectionName.clear();
    {
        QSqlDatabase database = QSqlDatabase::database(connectionName, false);
        if (database.isValid()) {
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    m_explorerMetadata.clear();
}

bool PlayerStatisticsPanel::loadExplorerDatabase(
    const QString &path,
    QString *errorMessage)
{
    const QFileInfo fileInfo(path);
    if (
        path.isEmpty()
        || !fileInfo.isAbsolute()
        || fileInfo.isSymLink()
        || !fileInfo.isFile()
        || !fileInfo.isReadable()
        || fileInfo.size() < 1
        || fileInfo.size() > kMaximumExplorerBytes
    ) {
        setError(errorMessage, QStringLiteral("player explorer must be a direct readable SQLite file under 512 MiB"));
        return false;
    }

    const QString connectionName = QStringLiteral("parlawl-player-explorer-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    database.setDatabaseName(fileInfo.absoluteFilePath());
    const auto discard = [&database, &connectionName] {
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    };
    if (!database.open()) {
        setError(errorMessage, QStringLiteral("player explorer could not be opened read-only: ")
                + database.lastError().text());
        discard();
        return false;
    }
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA query_only = ON"))) {
        setError(errorMessage, QStringLiteral("player explorer could not enable query-only mode"));
        query = QSqlQuery();
        discard();
        return false;
    }

    QHash<QString, QString> metadata;
    if (!query.exec(QStringLiteral("SELECT key, value FROM metadata ORDER BY key"))) {
        setError(errorMessage, QStringLiteral("player explorer metadata is unavailable"));
        query = QSqlQuery();
        discard();
        return false;
    }
    while (query.next()) {
        const QString key = query.value(0).toString();
        if (key.isEmpty() || metadata.contains(key)) {
            setError(errorMessage, QStringLiteral("player explorer metadata is duplicated"));
            query = QSqlQuery();
            discard();
            return false;
        }
        metadata.insert(key, query.value(1).toString());
    }
    const QStringList commonMetadata {
        QStringLiteral("clock_semantics"),
        QStringLiteral("decision_count"),
        QStringLiteral("distinct_player_count"),
        QStringLiteral("engine_evidence_present"),
        QStringLiteral("game_count"),
        QStringLiteral("game_outcomes_are_retrospective"),
        QStringLiteral("mapping_authenticates_source_replay"),
        QStringLiteral("opening_classifier_version"),
        QStringLiteral("opening_corpus_id"),
        QStringLiteral("parsed_target_lineage_occurrence_count"),
        QStringLiteral("player_game_count"),
        QStringLiteral("schema_version"),
        QStringLiteral("source_bundle_count"),
        QStringLiteral("source_plan_v2_id"),
        QStringLiteral("target_lineage_occurrence_count"),
        QStringLiteral("unordered_pair_count"),
    };
    const QString schemaVersion = metadata.value(QStringLiteral("schema_version"));
    const bool engineExplorer = schemaVersion
        == QStringLiteral("chess-player-game-engine-explorer-sqlite-v2");
    const bool analysisCatalog = metadata.value(
        QStringLiteral("catalog_schema_version"))
        == QStringLiteral("chess-player-analysis-catalog-sqlite-v1");
    const QStringList engineMetadata {
        QStringLiteral("engine_adapter_version"),
        QStringLiteral("engine_analyzed_game_count"),
        QStringLiteral("engine_author"),
        QStringLiteral("engine_binary_sha256"),
        QStringLiteral("engine_byte_identical_evidence_fallback_store_count"),
        QStringLiteral("engine_claim_boundary"),
        QStringLiteral("engine_config_id"),
        QStringLiteral("engine_hash_mebibytes"),
        QStringLiteral("engine_lineage_count"),
        QStringLiteral("engine_manifest_id"),
        QStringLiteral("engine_name"),
        QStringLiteral("engine_node_limit"),
        QStringLiteral("engine_target_coverage_ppm"),
        QStringLiteral("engine_threads"),
        QStringLiteral("engine_transition_count"),
        QStringLiteral("engine_unanalyzed_game_count"),
        QStringLiteral("engine_wdl_loss_thresholds"),
        QStringLiteral("engine_winning_expectation_millionths"),
        QStringLiteral("viewer_runs_engine_process"),
    };
    const QStringList catalogMetadata {
        QStringLiteral("board_structure_game_count"),
        QStringLiteral("board_structure_measurement_count"),
        QStringLiteral("board_structure_player_game_count"),
        QStringLiteral("board_structure_postgame_descriptive_only"),
        QStringLiteral("catalog_authenticates_source_replay"),
        QStringLiteral("catalog_schema_version"),
        QStringLiteral("descriptive_metric_registry_id"),
        QStringLiteral("same_game_vectors_are_pregame_features"),
    };
    const int expectedMetadataCount = commonMetadata.size()
        + (engineExplorer ? engineMetadata.size() : 0)
        + (analysisCatalog ? catalogMetadata.size() : 0);
    bool metadataKeysMatch = metadata.size() == expectedMetadataCount;
    for (const QString &key : commonMetadata) {
        metadataKeysMatch = metadataKeysMatch && metadata.contains(key);
    }
    if (engineExplorer) {
        for (const QString &key : engineMetadata) {
            metadataKeysMatch = metadataKeysMatch && metadata.contains(key);
        }
    }
    if (analysisCatalog) {
        for (const QString &key : catalogMetadata) {
            metadataKeysMatch = metadataKeysMatch && metadata.contains(key);
        }
    }
    const bool sourceExplorer = schemaVersion
        == QStringLiteral("chess-player-game-explorer-sqlite-v1");
    const QString engineAvailability = metadata.value(QStringLiteral("engine_evidence_present"));
    if (
        !metadataKeysMatch
        || (!sourceExplorer && !engineExplorer)
        || (analysisCatalog && !sourceExplorer)
        || metadata.value(QStringLiteral("clock_semantics"))
            != QStringLiteral("server-accounted-not-cognitive-time")
        || (sourceExplorer && engineAvailability != QStringLiteral("false"))
        || (engineExplorer
            && engineAvailability != QStringLiteral("partial")
            && engineAvailability != QStringLiteral("complete"))
        || (engineExplorer
            && metadata.value(QStringLiteral("engine_claim_boundary"))
                != QStringLiteral("persisted-fixed-node-display-not-objective-truth"))
        || (engineExplorer
            && metadata.value(QStringLiteral("viewer_runs_engine_process"))
                != QStringLiteral("false"))
        || metadata.value(QStringLiteral("game_outcomes_are_retrospective")) != QStringLiteral("true")
        || metadata.value(QStringLiteral("mapping_authenticates_source_replay")) != QStringLiteral("false")
        || (analysisCatalog
            && (metadata.value(QStringLiteral("board_structure_postgame_descriptive_only"))
                    != QStringLiteral("true")
                || metadata.value(QStringLiteral("catalog_authenticates_source_replay"))
                    != QStringLiteral("false")
                || metadata.value(QStringLiteral("same_game_vectors_are_pregame_features"))
                    != QStringLiteral("false")
                || !metadata.value(QStringLiteral("descriptive_metric_registry_id"))
                    .startsWith(QStringLiteral(
                        "chess-board-structure-descriptive-metric-registry-v1:"))))
        || !metadata.value(QStringLiteral("source_plan_v2_id")).startsWith(
            QStringLiteral("chess-cohort-source-chunk-plan-v2:"))
    ) {
        setError(errorMessage, QStringLiteral("player explorer metadata or claim boundary is unsupported"));
        query = QSqlQuery();
        discard();
        return false;
    }

    const auto metadataInteger = [&metadata](const QString &key, bool *ok) {
        const qint64 value = metadata.value(key).toLongLong(ok);
        return value;
    };
    bool numbersValid = true;
    const qint64 expectedGames = metadataInteger(QStringLiteral("game_count"), &numbersValid);
    bool currentOk = true;
    const qint64 expectedPlayerGames = metadataInteger(QStringLiteral("player_game_count"), &currentOk);
    numbersValid = numbersValid && currentOk;
    const qint64 expectedMoves = metadataInteger(QStringLiteral("decision_count"), &currentOk);
    numbersValid = numbersValid && currentOk;
    const qint64 expectedPlayers = metadataInteger(QStringLiteral("distinct_player_count"), &currentOk);
    numbersValid = numbersValid && currentOk;
    qint64 expectedEngineGames = 0;
    qint64 expectedMissingEngineGames = expectedGames;
    qint64 expectedEngineMoves = 0;
    qint64 expectedEngineLineages = 0;
    qint64 expectedEngineCoveragePpm = 0;
    qint64 expectedBoardStructurePlayerGames = 0;
    qint64 expectedBoardStructureMeasurements = 0;
    if (analysisCatalog) {
        const qint64 boardGames = metadataInteger(
            QStringLiteral("board_structure_game_count"), &currentOk);
        numbersValid = numbersValid && currentOk && boardGames == expectedGames;
        expectedBoardStructurePlayerGames = metadataInteger(
            QStringLiteral("board_structure_player_game_count"), &currentOk);
        numbersValid = numbersValid && currentOk
            && expectedBoardStructurePlayerGames == expectedPlayerGames;
        expectedBoardStructureMeasurements = metadataInteger(
            QStringLiteral("board_structure_measurement_count"), &currentOk);
        numbersValid = numbersValid && currentOk
            && expectedBoardStructureMeasurements
                == 39 * expectedBoardStructurePlayerGames;
    }
    if (engineExplorer) {
        expectedEngineGames = metadataInteger(QStringLiteral("engine_analyzed_game_count"), &currentOk);
        numbersValid = numbersValid && currentOk;
        expectedMissingEngineGames = metadataInteger(QStringLiteral("engine_unanalyzed_game_count"), &currentOk);
        numbersValid = numbersValid && currentOk;
        expectedEngineMoves = metadataInteger(QStringLiteral("engine_transition_count"), &currentOk);
        numbersValid = numbersValid && currentOk;
        expectedEngineLineages = metadataInteger(QStringLiteral("engine_lineage_count"), &currentOk);
        numbersValid = numbersValid && currentOk;
        expectedEngineCoveragePpm = metadataInteger(QStringLiteral("engine_target_coverage_ppm"), &currentOk);
        numbersValid = numbersValid && currentOk;
        const qint64 nodeLimit = metadataInteger(QStringLiteral("engine_node_limit"), &currentOk);
        numbersValid = numbersValid && currentOk && nodeLimit >= 1'000 && nodeLimit <= 1'000'000;
        const qint64 hashMebibytes = metadataInteger(QStringLiteral("engine_hash_mebibytes"), &currentOk);
        numbersValid = numbersValid && currentOk && hashMebibytes >= 1 && hashMebibytes <= 1'024;
        const qint64 threads = metadataInteger(QStringLiteral("engine_threads"), &currentOk);
        numbersValid = numbersValid && currentOk && threads == 1;
        const qint64 winning = metadataInteger(QStringLiteral("engine_winning_expectation_millionths"), &currentOk);
        numbersValid = numbersValid && currentOk && winning >= 500'000 && winning <= 1'000'000;
        const qint64 fallbackStores = metadataInteger(
            QStringLiteral("engine_byte_identical_evidence_fallback_store_count"), &currentOk);
        numbersValid = numbersValid && currentOk
            && fallbackStores >= 0 && fallbackStores <= 384;
        const QStringList thresholds = metadata.value(
            QStringLiteral("engine_wdl_loss_thresholds")).split(QLatin1Char(','));
        int previous = 0;
        bool thresholdsValid = thresholds.size() == 3;
        for (const QString &threshold : thresholds) {
            bool ok = false;
            const int value = threshold.toInt(&ok);
            thresholdsValid = thresholdsValid && ok && value > previous && value <= 1'000'000;
            previous = value;
        }
        numbersValid = numbersValid && thresholdsValid
            && metadata.value(QStringLiteral("engine_config_id")).startsWith(
                QStringLiteral("performance-engine-config-v1:"))
            && metadata.value(QStringLiteral("engine_binary_sha256")).size() == 64
            && !metadata.value(QStringLiteral("engine_name")).trimmed().isEmpty()
            && !metadata.value(QStringLiteral("engine_author")).trimmed().isEmpty()
            && !metadata.value(QStringLiteral("engine_adapter_version")).trimmed().isEmpty();
    }
    const auto scalar = [&query](const QString &sql, qint64 *output) {
        if (!query.exec(sql) || !query.next()) {
            return false;
        }
        bool ok = false;
        const qint64 value = query.value(0).toLongLong(&ok);
        if (!ok || query.next()) {
            return false;
        }
        *output = value;
        return true;
    };
    qint64 actualGames = 0;
    qint64 actualPlayerGames = 0;
    qint64 actualMoves = 0;
    qint64 actualPlayers = 0;
    qint64 plyTotal = 0;
    qint64 brokenReciprocity = 0;
    qint64 prohibitedSchemaObjects = 0;
    qint64 userTableCount = 0;
    qint64 actualEngineGames = 0;
    qint64 actualEngineMoves = 0;
    qint64 actualEngineLineages = 0;
    qint64 brokenEngineMoveCoverage = 0;
    qint64 brokenEngineMoveBindings = 0;
    qint64 brokenEngineLineageCounts = 0;
    qint64 actualBoardStructureDefinitions = 0;
    qint64 actualBoardStructurePlayerGames = 0;
    qint64 actualBoardStructureMeasurements = 0;
    qint64 brokenBoardStructureCardinality = 0;
    if (
        !numbersValid
        || expectedGames < 1
        || expectedGames > 5'000
        || expectedPlayerGames != 2 * expectedGames
        || expectedMoves < expectedGames
        || expectedMoves > kMaximumDecisions
        || expectedPlayers < 2
        || expectedPlayers > kMaximumPlayers
        || !scalar(QStringLiteral("SELECT COUNT(*) FROM games"), &actualGames)
        || !scalar(QStringLiteral("SELECT COUNT(*) FROM player_games"), &actualPlayerGames)
        || !scalar(QStringLiteral("SELECT COUNT(*) FROM moves"), &actualMoves)
        || !scalar(QStringLiteral("SELECT COUNT(DISTINCT player_id) FROM player_games"), &actualPlayers)
        || !scalar(QStringLiteral("SELECT COALESCE(SUM(ply_count), 0) FROM games"), &plyTotal)
        || !scalar(
            QStringLiteral(
                "SELECT COUNT(*) FROM (SELECT source_game_id FROM player_games "
                "GROUP BY source_game_id HAVING COUNT(*) <> 2)"),
            &brokenReciprocity)
        || !scalar(
            QStringLiteral(
                "SELECT COUNT(*) FROM sqlite_master WHERE type IN ('trigger', 'view')"),
            &prohibitedSchemaObjects)
        || !scalar(
            QStringLiteral(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='table' "
                "AND name NOT LIKE 'sqlite_%'"),
            &userTableCount)
        || actualGames != expectedGames
        || actualPlayerGames != expectedPlayerGames
        || actualMoves != expectedMoves
        || actualPlayers != expectedPlayers
        || plyTotal != actualMoves
        || brokenReciprocity != 0
        || prohibitedSchemaObjects != 0
        || userTableCount != ((engineExplorer || analysisCatalog) ? 7 : 4)
    ) {
        setError(errorMessage, QStringLiteral("player explorer table counts do not conserve"));
        query = QSqlQuery();
        discard();
        return false;
    }
    if (analysisCatalog
        && (!scalar(QStringLiteral(
                "SELECT COUNT(*) FROM board_structure_metric_definitions"),
                &actualBoardStructureDefinitions)
            || !scalar(QStringLiteral(
                "SELECT COUNT(*) FROM board_structure_player_games"),
                &actualBoardStructurePlayerGames)
            || !scalar(QStringLiteral(
                "SELECT COUNT(*) FROM board_structure_measurements"),
                &actualBoardStructureMeasurements)
            || !scalar(QStringLiteral(
                "SELECT COUNT(*) FROM board_structure_player_games pg "
                "WHERE (SELECT COUNT(*) FROM board_structure_measurements m "
                "WHERE m.structural_player_game_id = pg.structural_player_game_id) <> 39"),
                &brokenBoardStructureCardinality)
            || actualBoardStructureDefinitions != 39
            || actualBoardStructurePlayerGames != expectedBoardStructurePlayerGames
            || actualBoardStructureMeasurements != expectedBoardStructureMeasurements
            || brokenBoardStructureCardinality != 0)) {
        setError(errorMessage, QStringLiteral(
            "player analysis catalog BoardStructure coverage does not conserve"));
        query = QSqlQuery();
        discard();
        return false;
    }
    if (engineExplorer
        && (
            expectedEngineGames < 1
            || expectedEngineGames > expectedGames
            || expectedMissingEngineGames != expectedGames - expectedEngineGames
            || expectedEngineMoves < expectedEngineGames
            || expectedEngineMoves > expectedMoves
            || expectedEngineLineages < expectedEngineGames
            || expectedEngineLineages > 20'000
            || expectedEngineCoveragePpm
                != expectedEngineGames * kPpm / expectedGames
            || (engineAvailability == QStringLiteral("complete")
                ? expectedMissingEngineGames != 0
                : expectedMissingEngineGames == 0)
            || !scalar(QStringLiteral("SELECT COUNT(*) FROM engine_games"), &actualEngineGames)
            || !scalar(QStringLiteral("SELECT COUNT(*) FROM engine_moves"), &actualEngineMoves)
            || !scalar(QStringLiteral("SELECT COUNT(*) FROM engine_game_lineages"), &actualEngineLineages)
            || !scalar(
                QStringLiteral(
                    "SELECT COUNT(*) FROM engine_games eg JOIN games g USING(source_game_id) "
                    "WHERE (SELECT COUNT(*) FROM engine_moves em "
                    "WHERE em.source_game_id=eg.source_game_id) <> g.ply_count"),
                &brokenEngineMoveCoverage)
            || !scalar(
                QStringLiteral(
                    "SELECT COUNT(*) FROM engine_moves em LEFT JOIN moves m "
                    "ON m.source_game_id=em.source_game_id AND m.ply=em.ply "
                    "WHERE m.source_game_id IS NULL OR m.played_move_uci<>em.played_move_uci "
                    "OR m.player_color<>em.mover"),
                &brokenEngineMoveBindings)
            || !scalar(
                QStringLiteral(
                    "SELECT COUNT(*) FROM engine_games eg WHERE eg.lineage_count <> "
                    "(SELECT COUNT(*) FROM engine_game_lineages el "
                    "WHERE el.source_game_id=eg.source_game_id)"),
                &brokenEngineLineageCounts)
            || actualEngineGames != expectedEngineGames
            || actualEngineMoves != expectedEngineMoves
            || actualEngineLineages != expectedEngineLineages
            || brokenEngineMoveCoverage != 0
            || brokenEngineMoveBindings != 0
            || brokenEngineLineageCounts != 0)) {
        setError(errorMessage, QStringLiteral("player explorer engine coverage does not conserve"));
        query = QSqlQuery();
        discard();
        return false;
    }

    std::unique_ptr<PlayerAnalysisCatalog> candidateCatalog;
    QStringList catalogPlayerIds;
    if (analysisCatalog) {
        candidateCatalog = std::make_unique<PlayerAnalysisCatalog>();
        QString catalogError;
        if (!candidateCatalog->open(fileInfo.absoluteFilePath(), &catalogError)) {
            setError(errorMessage, catalogError);
            query = QSqlQuery();
            discard();
            return false;
        }
        catalogPlayerIds = candidateCatalog->playerIds(&catalogError);
        if (!catalogError.isEmpty()
            || catalogPlayerIds.size() != expectedPlayers) {
            setError(errorMessage, QStringLiteral(
                "player analysis catalog player identities do not conserve"));
            query = QSqlQuery();
            discard();
            return false;
        }
    }

    QString preferredPlayer = selectedPlayerId();
    closeExplorerDatabase();
    m_snapshot = {};
    m_playersById.clear();
    if (analysisCatalog) {
        m_structureSnapshot = {};
        m_structurePlayersById.clear();
        m_structureComparisonSourcePlayerId.clear();
    }
    m_explorerConnectionName = connectionName;
    m_explorerMetadata = metadata;
    m_analysisCatalog = std::move(candidateCatalog);
    m_catalogStructurePlayerIds = catalogPlayerIds;
    database = QSqlDatabase();
    m_explorerFilterPanel->setVisible(
        m_detailTabs->currentIndex() != m_detailTabs->count() - 1);
    for (int index = 0; index < 5; ++index) {
        m_detailTabs->setTabEnabled(index, true);
    }
    m_detailTabs->setTabEnabled(
        m_detailTabs->count() - 1,
        analysisCatalog || !m_structureSnapshot.isEmpty());
    populateExplorerPlayers(preferredPlayer);
    resetExplorerFilters();
    populateExplorerDependentFilters();
    m_openButton->setText(QStringLiteral("Replace Player Data"));
    m_openStructureButton->setText(analysisCatalog
            ? QStringLiteral("Load Legacy Structure JSON")
            : (!m_structureSnapshot.isEmpty()
                ? QStringLiteral("Replace Structure Stats")
                : QStringLiteral("Open Structure Stats")));
    rebuildExplorerView();
    if (m_dedicatedExplorerMode && m_detailTabs->isTabEnabled(1)) {
        m_detailTabs->setCurrentIndex(1);
    }
    return true;
}

bool PlayerStatisticsPanel::loadSnapshot(const QByteArray &raw, QString *errorMessage)
{
    if (raw.isEmpty() || raw.size() > kMaximumSnapshotBytes) {
        setError(errorMessage, QStringLiteral("player-statistics snapshot is empty or exceeds 64 MiB"));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("player-statistics snapshot is not valid JSON"));
        return false;
    }
    QHash<QString, QJsonObject> playersById;
    const QJsonObject root = document.object();
    if (!validateSnapshot(root, &playersById, errorMessage)) {
        return false;
    }

    closeExplorerDatabase();
    m_snapshot = root;
    m_playersById = playersById;
    m_explorerFilterPanel->setVisible(false);
    m_detailTabs->setTabEnabled(0, true);
    m_detailTabs->setTabEnabled(1, false);
    m_detailTabs->setTabEnabled(2, false);
    m_detailTabs->setTabEnabled(3, true);
    m_detailTabs->setTabEnabled(4, true);
    m_gameTable->setRowCount(0);
    m_replayGameButton->setEnabled(false);
    m_openingTable->setRowCount(0);
    m_playerCombo->blockSignals(true);
    m_playerCombo->clear();
    m_playerCombo->addItem(QStringLiteral("All players"), QString());
    QStringList ids = m_playersById.keys();
    std::sort(ids.begin(), ids.end());
    for (const QString &playerId : ids) {
        m_playerCombo->addItem(playerId, playerId);
    }
    m_playerCombo->setCurrentIndex(0);
    m_playerCombo->setEnabled(true);
    m_playerCombo->blockSignals(false);
    rebuildView();
    return true;
}

bool PlayerStatisticsPanel::loadBoardStructureSnapshot(
    const QByteArray &raw,
    QString *errorMessage)
{
    if (raw.isEmpty() || raw.size() > kMaximumSnapshotBytes) {
        setError(errorMessage, QStringLiteral("BoardStructure snapshot is empty or exceeds 64 MiB"));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("BoardStructure snapshot is not valid JSON"));
        return false;
    }
    QHash<QString, QJsonObject> playersById;
    const QJsonObject root = document.object();
    if (!validateBoardStructureSnapshot(root, &playersById, errorMessage)) {
        return false;
    }

    const bool primaryDataLoaded = !m_snapshot.isEmpty()
        || !m_explorerConnectionName.isEmpty();
    m_structureSnapshot = root;
    m_structurePlayersById = playersById;
    m_structureComparisonSourcePlayerId.clear();
    if (!primaryDataLoaded) {
        m_playerCombo->blockSignals(true);
        m_playerCombo->clear();
        m_playerCombo->addItem(QStringLiteral("All players"), QString());
        QStringList ids = m_structurePlayersById.keys();
        std::sort(ids.begin(), ids.end());
        for (const QString &playerId : ids) {
            m_playerCombo->addItem(playerId, playerId);
        }
        m_playerCombo->setCurrentIndex(0);
        m_playerCombo->setEnabled(true);
        m_playerCombo->blockSignals(false);
        for (int index = 0; index < m_detailTabs->count() - 1; ++index) {
            m_detailTabs->setTabEnabled(index, false);
        }
        m_statusLabel->setText(
            QStringLiteral("Display-only BoardStructure snapshot; no source replay or engine runs in ParlAWL."));
        m_summaryLabel->setText(
            QStringLiteral("Use the Board Structure tab to inspect global or per-player mechanical descriptions."));
    }
    m_openStructureButton->setText(QStringLiteral("Replace Structure Stats"));
    m_detailTabs->setTabEnabled(m_detailTabs->count() - 1, true);
    m_detailTabs->setCurrentIndex(m_detailTabs->count() - 1);
    rebuildBoardStructureView();
    return true;
}

void PlayerStatisticsPanel::clearSnapshot()
{
    closeExplorerDatabase();
    m_snapshot = {};
    m_playersById.clear();
    m_structureSnapshot = {};
    m_structurePlayersById.clear();
    m_structureComparisonSourcePlayerId.clear();
    {
        const QSignalBlocker comparisonBlocker(m_structureComparisonPlayerCombo);
        m_structureComparisonPlayerCombo->clear();
        m_structureComparisonPlayerCombo->addItem(
            QStringLiteral("No comparison"), QString());
    }
    {
        const QSignalBlocker categoryBlocker(m_structureComparisonCategoryCombo);
        m_structureComparisonCategoryCombo->setCurrentIndex(0);
    }
    m_explorerFilterPanel->setVisible(false);
    m_detailTabs->setTabEnabled(1, false);
    m_detailTabs->setTabEnabled(2, false);
    m_playerCombo->clear();
    m_playerCombo->addItem(QStringLiteral("No snapshot loaded"), QString());
    m_playerCombo->setEnabled(false);
    m_openButton->setText(QStringLiteral("Open Snapshot"));
    m_openStructureButton->setText(QStringLiteral("Open Structure Stats"));
    m_statusLabel->setText(
        QStringLiteral("Display-only viewer. ParlAWL does not authenticate or rebuild source evidence."));
    m_statusLabel->setToolTip(QString());
    m_summaryLabel->setText(
        QStringLiteral("No player statistics loaded. Opening a snapshot does not run Python, Stockfish, or the network."));
    for (QLabel *label : {
             m_gameMetricLabel,
             m_moveMetricLabel,
             m_populationMetricLabel,
             m_coverageMetricLabel,
             m_medianMetricLabel,
             m_p90MetricLabel,
         }) {
        label->setText(QStringLiteral("—"));
    }
    m_populationMetricTitleLabel->setText(QStringLiteral("Players"));
    m_pressureLabel->setText(QStringLiteral("Clock-pressure counts will appear here."));
    m_opponentHintLabel->setText(QStringLiteral("Select a player after opening a snapshot."));
    m_detailTabs->setCurrentIndex(0);
    m_phaseTable->setRowCount(0);
    m_decisionContextTable->setRowCount(0);
    m_gameTable->setRowCount(0);
    m_replayGameButton->setEnabled(false);
    m_openingTable->setRowCount(0);
    m_opponentTable->setRowCount(0);
    m_longestTable->setRowCount(0);
    m_structureStatusLabel->setText(
        QStringLiteral("No BoardStructure player statistics loaded."));
    m_structureSummaryLabel->setText(
        QStringLiteral("This tab reports descriptive mechanical structure only, never engine evaluation or pregame prediction."));
    m_structureConcentrationLabel->setText(
        QStringLiteral("Concentration information will appear after a BoardStructure snapshot is loaded."));
    m_structureMetricTable->setRowCount(0);
    m_structureCastlingTable->setRowCount(0);
    m_structureComparisonTable->setRowCount(0);
    m_structureComparisonPanel->setVisible(false);
    m_structureComparisonSummaryLabel->clear();
    m_structureComparisonSummaryLabel->setVisible(false);
    m_structureComparisonTable->setVisible(false);
    clearBoardStructureDrilldown();
    m_structureHeadToHeadTable->setRowCount(0);
    m_structureHeadToHeadLabel->setText(QStringLiteral("Head-to-head results"));
    m_structureHeadToHeadLabel->setVisible(false);
    m_structureHeadToHeadFilterPanel->setVisible(false);
    m_structureHeadToHeadTable->setVisible(false);
    m_detailTabs->setTabEnabled(m_detailTabs->count() - 1, false);
}

void PlayerStatisticsPanel::populateExplorerPlayers(const QString &preferredPlayerId)
{
    if (m_explorerConnectionName.isEmpty()) {
        return;
    }
    QSqlQuery query(QSqlDatabase::database(m_explorerConnectionName, false));
    if (!query.exec(QStringLiteral(
            "SELECT player_id, COUNT(*) AS games FROM player_games "
            "GROUP BY player_id ORDER BY player_id"))) {
        return;
    }
    QStringList playerIds;
    QString largestPlayer;
    qint64 largestCount = -1;
    while (query.next()) {
        const QString playerId = query.value(0).toString();
        const qint64 count = query.value(1).toLongLong();
        playerIds.append(playerId);
        if (count > largestCount || (count == largestCount && playerId < largestPlayer)) {
            largestPlayer = playerId;
            largestCount = count;
        }
    }
    m_playerCombo->blockSignals(true);
    m_playerCombo->clear();
    for (const QString &playerId : playerIds) {
        m_playerCombo->addItem(playerId, playerId);
    }
    int selectedIndex = m_playerCombo->findData(preferredPlayerId);
    if (selectedIndex < 0) {
        selectedIndex = m_playerCombo->findData(largestPlayer);
    }
    m_playerCombo->setCurrentIndex(std::max(0, selectedIndex));
    m_playerCombo->setEnabled(!playerIds.isEmpty());
    m_playerCombo->blockSignals(false);
}

void PlayerStatisticsPanel::populateExplorerDependentFilters()
{
    if (m_explorerConnectionName.isEmpty() || selectedPlayerId().isEmpty()) {
        return;
    }
    const QString previousOpponent = m_opponentFilter->currentData().toString();
    const QString previousOpening = m_openingFilter->currentData().toString();
    m_updatingExplorerFilters = true;
    m_opponentFilter->clear();
    m_opponentFilter->addItem(QStringLiteral("All opponents"), QString());
    m_openingFilter->clear();
    m_openingFilter->addItem(QStringLiteral("All openings"), QString());
    QSqlDatabase database = QSqlDatabase::database(m_explorerConnectionName, false);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT DISTINCT opponent_id FROM player_games "
        "WHERE player_id = ? ORDER BY opponent_id"));
    query.addBindValue(selectedPlayerId());
    if (query.exec()) {
        while (query.next()) {
            const QString opponentId = query.value(0).toString();
            m_opponentFilter->addItem(opponentId, opponentId);
        }
    }
    query.prepare(QStringLiteral(
        "SELECT g.opening_status, COALESCE(g.opening_eco, ''), "
        "COALESCE(g.opening_name, ''), COUNT(*) "
        "FROM player_games pg JOIN games g USING(source_game_id) "
        "WHERE pg.player_id = ? "
        "GROUP BY g.opening_status, g.opening_eco, g.opening_name "
        "ORDER BY CASE g.opening_status WHEN 'classified' THEN 0 ELSE 1 END, "
        "g.opening_eco, g.opening_name, g.opening_status"));
    query.addBindValue(selectedPlayerId());
    if (query.exec()) {
        while (query.next()) {
            const QString status = query.value(0).toString();
            const QString eco = query.value(1).toString();
            const QString name = query.value(2).toString();
            const QString key = status + QChar(0x1f) + eco + QChar(0x1f) + name;
            const QString label = status == QStringLiteral("classified")
                ? QStringLiteral("%1 · %2").arg(eco, name)
                : phaseText(status);
            m_openingFilter->addItem(
                QStringLiteral("%1 (%2)").arg(label, numberText(query.value(3).toLongLong())),
                key);
        }
    }
    const int opponentIndex = m_opponentFilter->findData(previousOpponent);
    const int openingIndex = m_openingFilter->findData(previousOpening);
    m_opponentFilter->setCurrentIndex(std::max(0, opponentIndex));
    m_openingFilter->setCurrentIndex(std::max(0, openingIndex));
    m_updatingExplorerFilters = false;
}

void PlayerStatisticsPanel::resetExplorerFilters()
{
    if (m_explorerConnectionName.isEmpty()) {
        return;
    }
    QSqlQuery query(QSqlDatabase::database(m_explorerConnectionName, false));
    if (!query.exec(QStringLiteral("SELECT MIN(utc_day), MAX(utc_day) FROM games"))
        || !query.next()) {
        return;
    }
    const QDate minimum = QDate::fromString(query.value(0).toString(), Qt::ISODate);
    const QDate maximum = QDate::fromString(query.value(1).toString(), Qt::ISODate);
    if (!minimum.isValid() || !maximum.isValid() || minimum > maximum) {
        return;
    }
    m_updatingExplorerFilters = true;
    for (QDateEdit *dateEdit : {m_fromDateEdit, m_toDateEdit}) {
        dateEdit->setDateRange(minimum, maximum);
    }
    m_fromDateEdit->setDate(minimum);
    m_toDateEdit->setDate(maximum);
    m_colorFilter->setCurrentIndex(0);
    m_resultFilter->setCurrentIndex(0);
    if (m_opponentFilter->count() > 0) {
        m_opponentFilter->setCurrentIndex(0);
    }
    if (m_openingFilter->count() > 0) {
        m_openingFilter->setCurrentIndex(0);
    }
    m_updatingExplorerFilters = false;
}

void PlayerStatisticsPanel::bindExplorerFilters(QSqlQuery *query) const
{
    QString openingStatus;
    QString openingEco;
    QString openingName;
    const QString openingKey = m_openingFilter->currentData().toString();
    if (!openingKey.isEmpty()) {
        const QStringList parts = openingKey.split(QChar(0x1f));
        if (parts.size() == 3) {
            openingStatus = parts.at(0);
            openingEco = parts.at(1);
            openingName = parts.at(2);
        }
    }
    const auto sqlText = [](QString value) {
        return value.isNull() ? QStringLiteral("") : value;
    };
    query->bindValue(QStringLiteral(":player"), selectedPlayerId());
    query->bindValue(QStringLiteral(":from_day"), m_fromDateEdit->date().toString(Qt::ISODate));
    query->bindValue(QStringLiteral(":to_day"), m_toDateEdit->date().toString(Qt::ISODate));
    query->bindValue(QStringLiteral(":color"), sqlText(m_colorFilter->currentData().toString()));
    query->bindValue(QStringLiteral(":outcome"), sqlText(m_resultFilter->currentData().toString()));
    query->bindValue(QStringLiteral(":opponent"), sqlText(m_opponentFilter->currentData().toString()));
    query->bindValue(QStringLiteral(":opening_status"), sqlText(openingStatus));
    query->bindValue(QStringLiteral(":opening_eco"), sqlText(openingEco));
    query->bindValue(QStringLiteral(":opening_name"), sqlText(openingName));
}

void PlayerStatisticsPanel::rebuildExplorerView()
{
    if (m_explorerConnectionName.isEmpty() || selectedPlayerId().isEmpty()) {
        return;
    }
    QSqlDatabase database = QSqlDatabase::database(m_explorerConnectionName, false);
    QSqlQuery gamesQuery(database);
    gamesQuery.prepare(
        QStringLiteral(
            "SELECT g.source_game_id, g.canonical_game_url, g.event_start_utc, "
            "g.utc_day, pg.opponent_id, pg.player_color, pg.outcome, "
            "pg.player_rating_postgame_observed, pg.opponent_rating_postgame_observed, "
            "g.opening_status, COALESCE(g.opening_eco, ''), "
            "COALESCE(g.opening_name, ''), COALESCE(g.opening_last_book_ply, -1), "
            "g.ply_count FROM player_games pg JOIN games g USING(source_game_id) WHERE ")
        + explorerFilterSql()
        + QStringLiteral(" ORDER BY g.event_start_utc DESC, g.source_game_id"));
    bindExplorerFilters(&gamesQuery);
    if (!gamesQuery.exec()) {
        m_summaryLabel->setText(QStringLiteral("Player explorer query failed: ")
            + gamesQuery.lastError().text());
        return;
    }
    QVector<ExplorerGameRow> games;
    while (gamesQuery.next()) {
        games.append(ExplorerGameRow {
            gamesQuery.value(0).toString(),
            gamesQuery.value(1).toString(),
            gamesQuery.value(2).toString(),
            gamesQuery.value(3).toString(),
            gamesQuery.value(4).toString(),
            gamesQuery.value(5).toString(),
            gamesQuery.value(6).toString(),
            gamesQuery.value(7).toInt(),
            gamesQuery.value(8).toInt(),
            gamesQuery.value(9).toString(),
            gamesQuery.value(10).toString(),
            gamesQuery.value(11).toString(),
            gamesQuery.value(12).toInt(),
            gamesQuery.value(13).toInt(),
        });
    }

    QSqlQuery movesQuery(database);
    movesQuery.prepare(
        QStringLiteral(
            "SELECT m.source_game_id, g.event_start_utc, m.player_id, m.opponent_id, "
            "m.player_color, m.ply, m.move_number, m.played_move_san, "
            "m.played_move_uci, m.position_phase, m.forcedness_status, "
            "m.legal_move_count, m.decision_start_clock_ms, "
            "m.clock_remaining_after_move_ms, m.elapsed_move_ms, m.elapsed_status "
            "FROM player_games pg JOIN games g USING(source_game_id) "
            "JOIN moves m ON m.source_game_id = pg.source_game_id "
            "AND m.player_id = pg.player_id WHERE ")
        + explorerFilterSql()
        + QStringLiteral(" ORDER BY g.event_start_utc, m.source_game_id, m.ply"));
    bindExplorerFilters(&movesQuery);
    if (!movesQuery.exec()) {
        m_summaryLabel->setText(QStringLiteral("Player move query failed: ")
            + movesQuery.lastError().text());
        return;
    }
    QVector<ExplorerDecisionRow> decisions;
    while (movesQuery.next()) {
        decisions.append(ExplorerDecisionRow {
            movesQuery.value(0).toString(),
            movesQuery.value(1).toString(),
            movesQuery.value(2).toString(),
            movesQuery.value(3).toString(),
            movesQuery.value(4).toString(),
            movesQuery.value(5).toInt(),
            movesQuery.value(6).toInt(),
            movesQuery.value(7).toString(),
            movesQuery.value(8).toString(),
            movesQuery.value(9).toString(),
            movesQuery.value(10).toString(),
            movesQuery.value(11).toInt(),
            movesQuery.value(12).isNull() ? -1 : movesQuery.value(12).toLongLong(),
            movesQuery.value(13).isNull() ? -1 : movesQuery.value(13).toLongLong(),
            movesQuery.value(14).isNull() ? -1 : movesQuery.value(14).toLongLong(),
            movesQuery.value(15).toString(),
        });
    }

    const QJsonObject aggregate = explorerAggregate(decisions);
    const qint64 lowClock = pressureCount(aggregate, 10'000);
    QSet<QString> opponentIds;
    qint64 wins = 0;
    qint64 draws = 0;
    qint64 losses = 0;
    qint64 whiteGames = 0;
    qint64 blackGames = 0;
    qint64 opponentRatingSum = 0;
    QHash<QString, int> gamesByOpponent;
    QHash<QString, QVector<ExplorerDecisionRow>> decisionsByOpponent;
    QHash<QString, int> movesByGame;
    QHash<QString, qint64> maximumByGame;
    for (const ExplorerGameRow &game : games) {
        opponentIds.insert(game.opponentId);
        ++gamesByOpponent[game.opponentId];
        wins += game.outcome == QStringLiteral("win");
        draws += game.outcome == QStringLiteral("draw");
        losses += game.outcome == QStringLiteral("loss");
        whiteGames += game.color == QStringLiteral("white");
        blackGames += game.color == QStringLiteral("black");
        opponentRatingSum += game.opponentRating;
    }
    for (const ExplorerDecisionRow &decision : decisions) {
        decisionsByOpponent[decision.opponentId].append(decision);
        ++movesByGame[decision.sourceGameId];
        if (decision.elapsedMs >= 0) {
            maximumByGame[decision.sourceGameId] = std::max(
                maximumByGame.value(decision.sourceGameId, 0),
                decision.elapsedMs);
        }
    }

    QJsonArray opponentRows;
    QStringList sortedOpponents = gamesByOpponent.keys();
    std::sort(sortedOpponents.begin(), sortedOpponents.end());
    for (const QString &opponentId : sortedOpponents) {
        qint64 opponentWins = 0;
        qint64 opponentDraws = 0;
        qint64 opponentLosses = 0;
        for (const ExplorerGameRow &game : games) {
            if (game.opponentId != opponentId) {
                continue;
            }
            opponentWins += game.outcome == QStringLiteral("win");
            opponentDraws += game.outcome == QStringLiteral("draw");
            opponentLosses += game.outcome == QStringLiteral("loss");
        }
        const qint64 opponentGames = gamesByOpponent.value(opponentId);
        opponentRows.append(QJsonObject {
            {QStringLiteral("draw_count"), opponentDraws},
            {QStringLiteral("game_count"), opponentGames},
            {QStringLiteral("loss_count"), opponentLosses},
            {QStringLiteral("opponent_id"), opponentId},
            {QStringLiteral("player_id"), selectedPlayerId()},
            {QStringLiteral("score_rate_ppm"), opponentGames == 0
                    ? QJsonValue(QJsonValue::Null)
                    : QJsonValue(((2 * opponentWins + opponentDraws) * kPpm
                          + opponentGames) / (2 * opponentGames))},
            {QStringLiteral("statistics"), explorerAggregate(
                    decisionsByOpponent.value(opponentId))},
            {QStringLiteral("win_count"), opponentWins},
        });
    }

    const QString sourcePlan = m_explorerMetadata.value(QStringLiteral("source_plan_v2_id"));
    const bool hasPersistedEngine = m_explorerMetadata.value(
        QStringLiteral("schema_version"))
        == QStringLiteral("chess-player-game-engine-explorer-sqlite-v2");
    const bool hasAnalysisCatalog = m_analysisCatalog != nullptr
        && m_analysisCatalog->isOpen();
    if (hasPersistedEngine) {
        const QString fullStatus =
            QStringLiteral("Read-only game explorer · source %1 · persisted fixed-node engine coverage %2/%3 games · no engine process")
                .arg(compactSourcePlan(sourcePlan))
                .arg(m_explorerMetadata.value(QStringLiteral("engine_analyzed_game_count")))
                .arg(m_explorerMetadata.value(QStringLiteral("game_count")));
        m_statusLabel->setText(m_dedicatedExplorerMode
            ? QStringLiteral("Local read-only library · stored engine evidence · no engine running")
            : fullStatus);
        m_statusLabel->setToolTip(
            fullStatus + QLatin1Char('\n') + sourcePlan + QLatin1Char('\n')
            + m_explorerMetadata.value(QStringLiteral("engine_config_id")));
    } else if (hasAnalysisCatalog) {
        const QString fullStatus =
            QStringLiteral("Read-only analysis catalog · source %1 · games, moves, openings, outcomes, and BoardStructure rows · no engine evidence")
                .arg(compactSourcePlan(sourcePlan));
        m_statusLabel->setText(m_dedicatedExplorerMode
            ? QStringLiteral("Local read-only library · descriptive structure data · no engine evidence")
            : fullStatus);
        m_statusLabel->setToolTip(
            fullStatus + QLatin1Char('\n') + sourcePlan + QLatin1Char('\n')
            + m_explorerMetadata.value(
                QStringLiteral("descriptive_metric_registry_id")));
    } else {
        const QString fullStatus =
            QStringLiteral("Read-only game explorer · source %1 · retrospective results/openings · no engine evidence")
                .arg(compactSourcePlan(sourcePlan));
        m_statusLabel->setText(m_dedicatedExplorerMode
            ? QStringLiteral("Local read-only library · results and openings · no engine evidence")
            : fullStatus);
        m_statusLabel->setToolTip(fullStatus + QLatin1Char('\n') + sourcePlan);
    }
    m_gameMetricLabel->setText(numberText(games.size()));
    m_moveMetricLabel->setText(numberText(decisions.size()));
    m_populationMetricTitleLabel->setText(QStringLiteral("Opponents"));
    m_populationMetricLabel->setText(numberText(opponentIds.size()));
    m_coverageMetricLabel->setText(
        coverageText(aggregate.value(QStringLiteral("elapsed_coverage_ppm"))));
    m_medianMetricLabel->setText(
        durationText(aggregate.value(QStringLiteral("p50_observed_elapsed_ms"))));
    m_p90MetricLabel->setText(
        durationText(aggregate.value(QStringLiteral("p90_observed_elapsed_ms"))));
    if (games.isEmpty()) {
        m_summaryLabel->setText(
            QStringLiteral("%1 · no games match the active filters").arg(selectedPlayerId()));
    } else {
        const double score = 100.0 * (static_cast<double>(wins) + 0.5 * draws)
            / games.size();
        m_summaryLabel->setText(
            QStringLiteral("%1 · %2-%3-%4 · score %5% · %6 White / %7 Black · average opponent %8")
                .arg(selectedPlayerId())
                .arg(numberText(wins))
                .arg(numberText(draws))
                .arg(numberText(losses))
                .arg(QString::number(score, 'f', 1))
                .arg(numberText(whiteGames))
                .arg(numberText(blackGames))
                .arg(numberText((opponentRatingSum + games.size() / 2) / games.size())));
    }
    m_opponentHintLabel->setText(
        QStringLiteral("Head-to-head timing and results over the same active game filters."));
    m_pressureLabel->setText(
        QStringLiteral("Clock before move · ≤1s %1 · ≤5s %2 · ≤10s %3 · ≤30s %4 · ≤60s %5")
            .arg(numberText(pressureCount(aggregate, 1'000)))
            .arg(numberText(pressureCount(aggregate, 5'000)))
            .arg(numberText(lowClock))
            .arg(numberText(pressureCount(aggregate, 30'000)))
            .arg(numberText(pressureCount(aggregate, 60'000))));

    populatePhaseTable(explorerGroups(
        decisions,
        {QStringLiteral("opening"), QStringLiteral("middlegame"), QStringLiteral("endgame")},
        [](const ExplorerDecisionRow &row) { return row.phase; }));
    populateDecisionContextTable(
        explorerGroups(
            decisions,
            {QStringLiteral("white"), QStringLiteral("black")},
            [](const ExplorerDecisionRow &row) { return row.playerColor; }),
        explorerGroups(
            decisions,
            {QStringLiteral("forced-single-legal-move"), QStringLiteral("nonforced")},
            [](const ExplorerDecisionRow &row) { return row.forcedness; }));
    populateOpponentTable(opponentRows);
    populateLongestTable(explorerLongest(decisions, 20));

    m_gameTable->setSortingEnabled(false);
    m_replayGameButton->setEnabled(false);
    m_gameTable->clearContents();
    m_gameTable->setRowCount(games.size());
    for (qsizetype row = 0; row < games.size(); ++row) {
        const ExplorerGameRow &game = games.at(row);
        const QString opening = game.openingStatus == QStringLiteral("classified")
            ? QStringLiteral("%1 · %2").arg(game.openingEco, game.openingName)
            : phaseText(game.openingStatus);
        auto *utcItem = readOnlyItem(compactUtc(game.eventStartUtc));
        utcItem->setToolTip(game.canonicalUrl);
        utcItem->setData(Qt::UserRole + 1, game.sourceGameId);
        m_gameTable->setItem(row, 0, utcItem);
        auto *opponentItem = readOnlyItem(game.opponentId);
        opponentItem->setToolTip(game.opponentId);
        m_gameTable->setItem(row, 1, opponentItem);
        m_gameTable->setItem(row, 2, readOnlyItem(phaseText(game.color)));
        m_gameTable->setItem(row, 3, readOnlyItem(phaseText(game.outcome)));
        auto *openingItem = readOnlyItem(opening);
        openingItem->setToolTip(opening);
        m_gameTable->setItem(row, 4, openingItem);
        m_gameTable->setItem(row, 5, new NumericTableWidgetItem(movesByGame.value(game.sourceGameId)));
        const bool hasLongest = maximumByGame.contains(game.sourceGameId);
        auto *longestItem = new NumericTableWidgetItem(
            hasLongest ? maximumByGame.value(game.sourceGameId) : -1);
        longestItem->setText(hasLongest
            ? durationText(QJsonValue(maximumByGame.value(game.sourceGameId)))
            : QStringLiteral("N/A"));
        m_gameTable->setItem(row, 6, longestItem);
        m_gameTable->setItem(row, 7, new NumericTableWidgetItem(game.playerRating));
        m_gameTable->setItem(row, 8, new NumericTableWidgetItem(game.opponentRating));
    }
    m_gameTable->setSortingEnabled(true);

    struct OpeningSummary {
        QString label;
        qint64 games = 0;
        qint64 wins = 0;
        qint64 draws = 0;
        qint64 losses = 0;
        qint64 white = 0;
        qint64 black = 0;
    };
    QHash<QString, OpeningSummary> openingSummaries;
    for (const ExplorerGameRow &game : games) {
        const QString key = game.openingStatus + QChar(0x1f) + game.openingEco
            + QChar(0x1f) + game.openingName;
        OpeningSummary &summary = openingSummaries[key];
        summary.label = game.openingStatus == QStringLiteral("classified")
            ? QStringLiteral("%1 · %2").arg(game.openingEco, game.openingName)
            : phaseText(game.openingStatus);
        ++summary.games;
        summary.wins += game.outcome == QStringLiteral("win");
        summary.draws += game.outcome == QStringLiteral("draw");
        summary.losses += game.outcome == QStringLiteral("loss");
        summary.white += game.color == QStringLiteral("white");
        summary.black += game.color == QStringLiteral("black");
    }
    QVector<OpeningSummary> sortedOpenings = openingSummaries.values();
    std::sort(sortedOpenings.begin(), sortedOpenings.end(), [](const OpeningSummary &left, const OpeningSummary &right) {
        if (left.games != right.games) {
            return left.games > right.games;
        }
        return left.label < right.label;
    });
    m_openingTable->setSortingEnabled(false);
    m_openingTable->clearContents();
    m_openingTable->setRowCount(sortedOpenings.size());
    for (qsizetype row = 0; row < sortedOpenings.size(); ++row) {
        const OpeningSummary &summary = sortedOpenings.at(row);
        auto *openingItem = readOnlyItem(summary.label);
        openingItem->setToolTip(summary.label);
        m_openingTable->setItem(row, 0, openingItem);
        m_openingTable->setItem(row, 1, new NumericTableWidgetItem(summary.games));
        m_openingTable->setItem(row, 2, readOnlyItem(
            QStringLiteral("%1-%2-%3")
                .arg(numberText(summary.wins))
                .arg(numberText(summary.draws))
                .arg(numberText(summary.losses))));
        const qint64 scoreTenths = (2 * summary.wins + summary.draws) * 500
            / summary.games;
        auto *scoreItem = new NumericTableWidgetItem(scoreTenths);
        scoreItem->setText(QStringLiteral("%1%").arg(
            QString::number(static_cast<double>(scoreTenths) / 10.0, 'f', 1)));
        m_openingTable->setItem(row, 3, scoreItem);
        m_openingTable->setItem(row, 4, readOnlyItem(
            QStringLiteral("%1 / %2")
                .arg(numberText(summary.white))
                .arg(numberText(summary.black))));
    }
    m_openingTable->setSortingEnabled(true);
    rebuildBoardStructureView();
}

bool PlayerStatisticsPanel::selectPlayer(const QString &playerId)
{
    const int index = playerId.isEmpty()
        ? 0
        : m_playerCombo->findData(playerId);
    if (index < 0) {
        return false;
    }
    m_playerCombo->setCurrentIndex(index);
    return true;
}

void PlayerStatisticsPanel::selectTypedPlayer()
{
    const QString requestedPlayer = m_playerCombo->currentText().trimmed();
    for (int index = 0; index < m_playerCombo->count(); ++index) {
        const QString playerId = m_playerCombo->itemData(index).toString();
        if (!playerId.isEmpty()
            && playerId.compare(requestedPlayer, Qt::CaseInsensitive) == 0) {
            m_playerCombo->setCurrentIndex(index);
            return;
        }
    }
}

bool PlayerStatisticsPanel::hasSnapshot() const
{
    return !m_snapshot.isEmpty() || !m_explorerConnectionName.isEmpty()
        || !m_structureSnapshot.isEmpty();
}

bool PlayerStatisticsPanel::hasBoardStructureSnapshot() const
{
    return !m_structureSnapshot.isEmpty()
        || (m_analysisCatalog != nullptr && m_analysisCatalog->isOpen());
}

QString PlayerStatisticsPanel::selectedPlayerId() const
{
    return m_playerCombo->currentData().toString();
}

QString PlayerStatisticsPanel::summaryText() const
{
    return m_summaryLabel->text();
}

QStringList PlayerStatisticsPanel::playerIds() const
{
    if (!m_explorerConnectionName.isEmpty()) {
        QStringList ids;
        for (int index = 0; index < m_playerCombo->count(); ++index) {
            const QString playerId = m_playerCombo->itemData(index).toString();
            if (!playerId.isEmpty()) {
                ids.append(playerId);
            }
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }
    QStringList ids = m_playersById.isEmpty()
        ? m_structurePlayersById.keys()
        : m_playersById.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

int PlayerStatisticsPanel::phaseRowCount() const
{
    return m_phaseTable->rowCount();
}

int PlayerStatisticsPanel::decisionContextRowCount() const
{
    return m_decisionContextTable->rowCount();
}

int PlayerStatisticsPanel::opponentRowCount() const
{
    return m_opponentTable->rowCount();
}

int PlayerStatisticsPanel::longestMoveRowCount() const
{
    return m_longestTable->rowCount();
}

int PlayerStatisticsPanel::gameRowCount() const
{
    return m_gameTable->rowCount();
}

int PlayerStatisticsPanel::openingRowCount() const
{
    return m_openingTable->rowCount();
}

int PlayerStatisticsPanel::boardStructureMetricRowCount() const
{
    return m_structureMetricTable->rowCount();
}

int PlayerStatisticsPanel::boardStructureCastlingRowCount() const
{
    return m_structureCastlingTable->rowCount();
}

std::optional<PlayerStatisticsGameBreakdown> PlayerStatisticsPanel::gameBreakdown(
    const QString &sourceGameId,
    QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    const QString viewedPlayer = selectedPlayerId();
    if (m_explorerConnectionName.isEmpty() || sourceGameId.isEmpty()
        || viewedPlayer.isEmpty()) {
        setError(errorMessage, QStringLiteral("open a player-game explorer and select a game first"));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(m_explorerConnectionName, false);
    QSqlQuery gameQuery(database);
    gameQuery.prepare(QStringLiteral(
        "SELECT g.canonical_game_url, g.event_start_utc, g.white_player_id, "
        "g.black_player_id, g.white_rating_postgame_observed, "
        "g.black_rating_postgame_observed, g.result, g.ply_count, "
        "g.opening_status, g.opening_eco, g.opening_name, "
        "g.opening_last_book_ply, pg.player_color "
        "FROM games g JOIN player_games pg USING(source_game_id) "
        "WHERE g.source_game_id = ? AND pg.player_id = ?"));
    gameQuery.addBindValue(sourceGameId);
    gameQuery.addBindValue(viewedPlayer);
    if (!gameQuery.exec() || !gameQuery.next()) {
        setError(errorMessage, QStringLiteral("the selected game is not available for this player"));
        return std::nullopt;
    }

    bool whiteRatingOk = false;
    bool blackRatingOk = false;
    bool plyCountOk = false;
    const int whiteRating = gameQuery.value(4).toInt(&whiteRatingOk);
    const int blackRating = gameQuery.value(5).toInt(&blackRatingOk);
    const int plyCount = gameQuery.value(7).toInt(&plyCountOk);
    PlayerStatisticsGameBreakdown output;
    output.sourceGameId = sourceGameId;
    output.canonicalGameUrl = gameQuery.value(0).toString();
    output.eventStartUtc = gameQuery.value(1).toString();
    output.whitePlayerId = gameQuery.value(2).toString();
    output.blackPlayerId = gameQuery.value(3).toString();
    output.whiteRating = whiteRating;
    output.blackRating = blackRating;
    output.result = gameQuery.value(6).toString();
    output.openingStatus = gameQuery.value(8).toString();
    if (!gameQuery.value(9).isNull()) {
        output.openingEco = gameQuery.value(9).toString();
    }
    if (!gameQuery.value(10).isNull()) {
        output.openingName = gameQuery.value(10).toString();
    }
    if (!gameQuery.value(11).isNull()) {
        bool bookPlyOk = false;
        const int bookPly = gameQuery.value(11).toInt(&bookPlyOk);
        if (!bookPlyOk) {
            setError(errorMessage, QStringLiteral("the selected game has an invalid opening boundary"));
            return std::nullopt;
        }
        output.openingLastBookPly = bookPly;
    }
    output.viewedPlayerId = viewedPlayer;
    output.viewedPlayerColor = gameQuery.value(12).toString();
    if (gameQuery.next()
        || !whiteRatingOk || !blackRatingOk || !plyCountOk
        || whiteRating < 100 || whiteRating > 5'000
        || blackRating < 100 || blackRating > 5'000
        || plyCount < 2 || plyCount > 700
        || output.canonicalGameUrl.isEmpty() || output.eventStartUtc.isEmpty()
        || output.whitePlayerId.isEmpty() || output.blackPlayerId.isEmpty()
        || output.whitePlayerId == output.blackPlayerId
        || (output.result != QStringLiteral("1-0")
            && output.result != QStringLiteral("0-1")
            && output.result != QStringLiteral("1/2-1/2"))
        || (output.viewedPlayerColor != QStringLiteral("white")
            && output.viewedPlayerColor != QStringLiteral("black"))
        || (output.viewedPlayerColor == QStringLiteral("white")
            && output.whitePlayerId != viewedPlayer)
        || (output.viewedPlayerColor == QStringLiteral("black")
            && output.blackPlayerId != viewedPlayer)
        || (output.openingStatus != QStringLiteral("classified")
            && output.openingStatus != QStringLiteral("ambiguous")
            && output.openingStatus != QStringLiteral("unknown"))
        || (output.openingStatus == QStringLiteral("classified")
            && (!output.openingEco.has_value() || !output.openingName.has_value()))
        || (output.openingStatus != QStringLiteral("classified")
            && (output.openingEco.has_value() || output.openingName.has_value()))
        || (output.openingLastBookPly.has_value()
            && (*output.openingLastBookPly < 0 || *output.openingLastBookPly > plyCount))) {
        setError(errorMessage, QStringLiteral("the selected game metadata is inconsistent"));
        return std::nullopt;
    }

    const bool engineExplorer = m_explorerMetadata.value(
        QStringLiteral("schema_version"))
        == QStringLiteral("chess-player-game-engine-explorer-sqlite-v2");
    QHash<int, PlayerStatisticsEngineMoveEvidence> engineMoves;
    if (engineExplorer) {
        QSqlQuery engineGameQuery(database);
        engineGameQuery.prepare(QStringLiteral(
            "SELECT evidence_id, representative_run_id, analysis_recorded_at_utc, "
            "lineage_count FROM engine_games WHERE source_game_id = ?"));
        engineGameQuery.addBindValue(sourceGameId);
        if (!engineGameQuery.exec()) {
            setError(errorMessage, QStringLiteral("the selected game's persisted engine status could not be read"));
            return std::nullopt;
        }
        if (engineGameQuery.next()) {
            bool lineageOk = false;
            PlayerStatisticsEngineGameEvidence evidence;
            evidence.evidenceId = engineGameQuery.value(0).toString();
            evidence.representativeRunId = engineGameQuery.value(1).toString();
            evidence.analysisRecordedAtUtc = engineGameQuery.value(2).toString();
            evidence.lineageCount = engineGameQuery.value(3).toInt(&lineageOk);
            evidence.engineConfigId = m_explorerMetadata.value(QStringLiteral("engine_config_id"));
            evidence.engineName = m_explorerMetadata.value(QStringLiteral("engine_name"));
            evidence.engineAuthor = m_explorerMetadata.value(QStringLiteral("engine_author"));
            evidence.engineBinarySha256 = m_explorerMetadata.value(QStringLiteral("engine_binary_sha256"));
            evidence.engineAdapterVersion = m_explorerMetadata.value(QStringLiteral("engine_adapter_version"));
            bool engineIntegersValid = false;
            evidence.nodeLimit = m_explorerMetadata.value(
                QStringLiteral("engine_node_limit")).toInt(&engineIntegersValid);
            bool valueOk = false;
            evidence.hashMebibytes = m_explorerMetadata.value(
                QStringLiteral("engine_hash_mebibytes")).toInt(&valueOk);
            engineIntegersValid = engineIntegersValid && valueOk;
            evidence.threads = m_explorerMetadata.value(
                QStringLiteral("engine_threads")).toInt(&valueOk);
            engineIntegersValid = engineIntegersValid && valueOk;
            evidence.winningExpectationMillionths = m_explorerMetadata.value(
                QStringLiteral("engine_winning_expectation_millionths")).toInt(&valueOk);
            engineIntegersValid = engineIntegersValid && valueOk;
            for (const QString &part : m_explorerMetadata.value(
                     QStringLiteral("engine_wdl_loss_thresholds")).split(QLatin1Char(','))) {
                const int threshold = part.toInt(&valueOk);
                engineIntegersValid = engineIntegersValid && valueOk;
                evidence.wdlLossThresholds.append(threshold);
            }
            if (engineGameQuery.next() || !lineageOk || evidence.lineageCount < 1
                || evidence.evidenceId.isEmpty() || evidence.representativeRunId.isEmpty()
                || evidence.analysisRecordedAtUtc.isEmpty() || !engineIntegersValid
                || evidence.nodeLimit < 1'000 || evidence.nodeLimit > 1'000'000
                || evidence.hashMebibytes < 1 || evidence.hashMebibytes > 1'024
                || evidence.threads != 1 || evidence.wdlLossThresholds.size() != 3) {
                setError(errorMessage, QStringLiteral("the selected game's persisted engine authority is inconsistent"));
                return std::nullopt;
            }
            output.engineEvidence = evidence;
        }

        QSqlQuery engineMoveQuery(database);
        engineMoveQuery.prepare(QStringLiteral(
            "SELECT ply, played_move_uci, mover, expected_before_millionths, "
            "expected_after_millionths, wdl_loss_millionths, centipawn_loss, "
            "missed_winning_advantage, missed_forced_mate, severity, "
            "before_score_kind, before_centipawns_white, before_mate_for_white, "
            "before_wdl_white_win, before_wdl_white_draw, before_wdl_white_loss, "
            "before_best_move_uci, before_depth, before_selective_depth, before_nodes, "
            "before_pv_uci, after_score_kind, after_centipawns_white, "
            "after_mate_for_white, after_wdl_white_win, after_wdl_white_draw, "
            "after_wdl_white_loss FROM engine_moves WHERE source_game_id = ? ORDER BY ply"));
        engineMoveQuery.addBindValue(sourceGameId);
        if (!engineMoveQuery.exec()) {
            setError(errorMessage, QStringLiteral("the selected game's persisted engine moves could not be read"));
            return std::nullopt;
        }
        while (engineMoveQuery.next()) {
            bool valuesValid = true;
            const auto integer = [&engineMoveQuery, &valuesValid](int column) {
                bool ok = false;
                const int value = engineMoveQuery.value(column).toInt(&ok);
                valuesValid = valuesValid && ok;
                return value;
            };
            const auto optionalSigned = [&engineMoveQuery, &valuesValid](int column) -> std::optional<qint64> {
                if (engineMoveQuery.value(column).isNull()) {
                    return std::nullopt;
                }
                bool ok = false;
                const qint64 value = engineMoveQuery.value(column).toLongLong(&ok);
                valuesValid = valuesValid && ok && std::abs(value) <= 1'000'000;
                return value;
            };
            const int ply = integer(0);
            const QString enginePlayedMove = engineMoveQuery.value(1).toString();
            const QString mover = engineMoveQuery.value(2).toString();
            PlayerStatisticsEngineMoveEvidence evidence;
            evidence.expectedBeforeMillionths = integer(3);
            evidence.expectedAfterMillionths = integer(4);
            evidence.wdlLossMillionths = integer(5);
            if (!engineMoveQuery.value(6).isNull()) {
                const qint64 centipawnLoss = optionalSigned(6).value_or(-1);
                valuesValid = valuesValid && centipawnLoss >= 0;
                evidence.centipawnLoss = centipawnLoss;
            }
            const int missedWin = integer(7);
            const int missedMate = integer(8);
            evidence.missedWinningAdvantage = missedWin == 1;
            evidence.missedForcedMate = missedMate == 1;
            evidence.severity = engineMoveQuery.value(9).toString();
            evidence.beforeScoreKind = engineMoveQuery.value(10).toString();
            evidence.beforeCentipawnsWhite = optionalSigned(11);
            evidence.beforeMateForWhite = optionalSigned(12);
            evidence.beforeWdlWhite = {integer(13), integer(14), integer(15)};
            if (!engineMoveQuery.value(16).isNull()) {
                evidence.beforeBestMoveUci = engineMoveQuery.value(16).toString();
            }
            evidence.beforeDepth = integer(17);
            evidence.beforeSelectiveDepth = integer(18);
            evidence.beforeNodes = integer(19);
            evidence.beforePvUci = engineMoveQuery.value(20).toString();
            evidence.afterScoreKind = engineMoveQuery.value(21).toString();
            evidence.afterCentipawnsWhite = optionalSigned(22);
            evidence.afterMateForWhite = optionalSigned(23);
            evidence.afterWdlWhite = {integer(24), integer(25), integer(26)};
            const bool beforeScoreValid = evidence.beforeScoreKind == QStringLiteral("cp")
                ? evidence.beforeCentipawnsWhite.has_value() && !evidence.beforeMateForWhite.has_value()
                : evidence.beforeScoreKind == QStringLiteral("mate")
                    && !evidence.beforeCentipawnsWhite.has_value()
                    && evidence.beforeMateForWhite.has_value()
                    && *evidence.beforeMateForWhite != 0;
            const bool afterScoreValid = evidence.afterScoreKind == QStringLiteral("cp")
                ? evidence.afterCentipawnsWhite.has_value() && !evidence.afterMateForWhite.has_value()
                : evidence.afterScoreKind == QStringLiteral("mate")
                    ? !evidence.afterCentipawnsWhite.has_value()
                        && evidence.afterMateForWhite.has_value()
                        && *evidence.afterMateForWhite != 0
                    : evidence.afterScoreKind == QStringLiteral("terminal_mate")
                        ? !evidence.afterCentipawnsWhite.has_value()
                            && evidence.afterMateForWhite.value_or(-1) == 0
                        : evidence.afterScoreKind == QStringLiteral("terminal_draw")
                            && evidence.afterCentipawnsWhite.value_or(-1) == 0
                            && !evidence.afterMateForWhite.has_value();
            if (!valuesValid || ply < 1 || ply > plyCount || engineMoves.contains(ply)
                || enginePlayedMove.isEmpty()
                || (mover != QStringLiteral("white") && mover != QStringLiteral("black"))
                || evidence.expectedBeforeMillionths < 0 || evidence.expectedBeforeMillionths > 1'000'000
                || evidence.expectedAfterMillionths < 0 || evidence.expectedAfterMillionths > 1'000'000
                || evidence.wdlLossMillionths
                    != std::max(0, evidence.expectedBeforeMillionths - evidence.expectedAfterMillionths)
                || (missedWin != 0 && missedWin != 1) || (missedMate != 0 && missedMate != 1)
                || (evidence.severity != QStringLiteral("none")
                    && evidence.severity != QStringLiteral("inaccuracy")
                    && evidence.severity != QStringLiteral("mistake")
                    && evidence.severity != QStringLiteral("severe"))
                || !beforeScoreValid || !afterScoreValid
                || evidence.beforeWdlWhite.size() != 3
                || evidence.afterWdlWhite.size() != 3
                || std::accumulate(evidence.beforeWdlWhite.cbegin(), evidence.beforeWdlWhite.cend(), 0) != 1'000
                || std::accumulate(evidence.afterWdlWhite.cbegin(), evidence.afterWdlWhite.cend(), 0) != 1'000
                || evidence.beforeDepth < 1 || evidence.beforeSelectiveDepth < 0
                || evidence.beforeNodes < 1 || evidence.beforePvUci.size() > 16'384) {
                setError(errorMessage, QStringLiteral("the selected game's persisted engine move %1 is inconsistent").arg(ply));
                return std::nullopt;
            }
            engineMoves.insert(ply, evidence);
        }
        if ((output.engineEvidence.has_value() && engineMoves.size() != plyCount)
            || (!output.engineEvidence.has_value() && !engineMoves.isEmpty())) {
            setError(errorMessage, QStringLiteral("the selected game's persisted engine coverage is incomplete"));
            return std::nullopt;
        }
    }

    QSqlQuery moveQuery(database);
    moveQuery.prepare(QStringLiteral(
        "SELECT ply, player_id, opponent_id, player_color, played_move_san, "
        "played_move_uci, position_phase, forcedness_status, legal_move_count, "
        "decision_start_clock_ms, clock_remaining_after_move_ms, elapsed_move_ms, "
        "elapsed_status FROM moves WHERE source_game_id = ? ORDER BY ply"));
    moveQuery.addBindValue(sourceGameId);
    if (!moveQuery.exec()) {
        setError(errorMessage, QStringLiteral("the selected game's moves could not be read"));
        return std::nullopt;
    }
    output.moves.reserve(plyCount);
    while (moveQuery.next()) {
        bool plyOk = false;
        bool legalCountOk = false;
        PlayerStatisticsGameMove move;
        move.ply = moveQuery.value(0).toInt(&plyOk);
        move.playerId = moveQuery.value(1).toString();
        move.opponentId = moveQuery.value(2).toString();
        move.playerColor = moveQuery.value(3).toString();
        move.san = moveQuery.value(4).toString();
        move.uci = moveQuery.value(5).toString();
        move.phase = moveQuery.value(6).toString();
        move.forcedness = moveQuery.value(7).toString();
        move.legalMoveCount = moveQuery.value(8).toInt(&legalCountOk);
        bool clocksValid = true;
        const auto optionalInteger = [&moveQuery, &clocksValid](int column) -> std::optional<qint64> {
            if (moveQuery.value(column).isNull()) {
                return std::nullopt;
            }
            bool ok = false;
            const qint64 value = moveQuery.value(column).toLongLong(&ok);
            clocksValid = clocksValid && ok && value >= 0 && value <= 86'400'000;
            return value;
        };
        move.decisionStartClockMs = optionalInteger(9);
        move.clockAfterMs = optionalInteger(10);
        move.elapsedMs = optionalInteger(11);
        move.elapsedStatus = moveQuery.value(12).toString();
        const int expectedPly = output.moves.size() + 1;
        const QString expectedColor = expectedPly % 2 == 1
            ? QStringLiteral("white") : QStringLiteral("black");
        const QString expectedPlayer = expectedColor == QStringLiteral("white")
            ? output.whitePlayerId : output.blackPlayerId;
        const QString expectedOpponent = expectedColor == QStringLiteral("white")
            ? output.blackPlayerId : output.whitePlayerId;
        if (!plyOk || !legalCountOk || !clocksValid || move.ply != expectedPly
            || move.playerId != expectedPlayer || move.opponentId != expectedOpponent
            || move.playerColor != expectedColor || move.san.isEmpty() || move.uci.isEmpty()
            || move.legalMoveCount < 1 || move.legalMoveCount > 218) {
            setError(errorMessage, QStringLiteral("the selected game has inconsistent move %1").arg(expectedPly));
            return std::nullopt;
        }
        if (engineMoves.contains(move.ply)) {
            move.engineEvidence = engineMoves.value(move.ply);
        }
        output.moves.append(move);
    }
    if (output.moves.size() != plyCount) {
        setError(errorMessage, QStringLiteral("the selected game's move count does not match its game row"));
        return std::nullopt;
    }
    return output;
}

QJsonObject PlayerStatisticsPanel::catalogBoardStructureView(
    const QString &playerId,
    QString *errorMessage)
{
    if (m_analysisCatalog == nullptr || !m_analysisCatalog->isOpen()) {
        setError(errorMessage, QStringLiteral("player analysis catalog is not open"));
        return {};
    }
    if (playerId.isEmpty() && !m_catalogGlobalStructureView.isEmpty()) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return m_catalogGlobalStructureView;
    }
    if (!playerId.isEmpty()
        && m_catalogStructureViewsByPlayer.contains(playerId)) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return m_catalogStructureViewsByPlayer.value(playerId);
    }

    QString queryError;
    const QVector<PlayerAnalysisCatalogMetricAggregate> metricRows =
        m_analysisCatalog->metricAggregates(playerId, QString(), &queryError);
    if (!queryError.isEmpty() || metricRows.size() != 39) {
        setError(errorMessage, queryError.isEmpty()
                ? QStringLiteral("analysis catalog metric registry is incomplete")
                : queryError);
        return {};
    }
    QJsonArray metrics;
    for (const PlayerAnalysisCatalogMetricAggregate &metric : metricRows) {
        metrics.append(catalogMetricMapping(metric));
    }

    if (playerId.isEmpty()) {
        const auto summary = m_analysisCatalog->globalSummary(&queryError);
        if (!summary.has_value()) {
            setError(errorMessage, queryError);
            return {};
        }
        m_catalogGlobalStructureView = QJsonObject {
            {QStringLiteral("black_player_game_count"), summary->blackPlayerGameCount},
            {QStringLiteral("black_win_game_count"), summary->blackWinGameCount},
            {QStringLiteral("distinct_opponent_count"), summary->distinctPlayerCount},
            {QStringLiteral("distinct_player_count"), summary->distinctPlayerCount},
            {QStringLiteral("draw_game_count"), summary->drawGameCount},
            {QStringLiteral("game_count"), summary->gameCount},
            {QStringLiteral("largest_pair_game_count"), summary->largestPairGameCount},
            {QStringLiteral("largest_pair_game_share_ppm"),
             summary->largestPairGameSharePpm},
            {QStringLiteral("largest_pair_player_ids"), QJsonArray {
                 summary->largestPairFirstPlayerId,
                 summary->largestPairSecondPlayerId}},
            {QStringLiteral("largest_player_exposure_share_ppm"),
             summary->largestPlayerExposureSharePpm},
            {QStringLiteral("largest_player_game_count"), summary->largestPlayerGameCount},
            {QStringLiteral("largest_player_game_share_ppm"),
             summary->largestPlayerGameSharePpm},
            {QStringLiteral("largest_player_id"), summary->largestPlayerId},
            {QStringLiteral("largest_unordered_pair_id"),
             summary->largestUnorderedPairId},
            {QStringLiteral("metrics"), metrics},
            {QStringLiteral("pair_hhi_ppm"), summary->pairHhiPpm},
            {QStringLiteral("player_exposure_hhi_ppm"),
             summary->playerExposureHhiPpm},
            {QStringLiteral("player_game_count"), summary->playerGameCount},
            {QStringLiteral("player_game_draw_count"), summary->playerGameDrawCount},
            {QStringLiteral("player_game_loss_count"), summary->playerGameLossCount},
            {QStringLiteral("player_game_win_count"), summary->playerGameWinCount},
            {QStringLiteral("unordered_pair_count"), summary->unorderedPairCount},
            {QStringLiteral("utc_day_count"), summary->distinctUtcDayCount},
            {QStringLiteral("white_player_game_count"), summary->whitePlayerGameCount},
            {QStringLiteral("white_win_game_count"), summary->whiteWinGameCount},
        };
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return m_catalogGlobalStructureView;
    }

    const auto summary = m_analysisCatalog->playerSummary(playerId, &queryError);
    if (!summary.has_value()) {
        setError(errorMessage, queryError.isEmpty()
                ? QStringLiteral("analysis catalog player is unavailable")
                : queryError);
        return {};
    }
    const QVector<PlayerAnalysisCatalogHeadToHead> records =
        m_analysisCatalog->headToHead(playerId, &queryError);
    if (!queryError.isEmpty() || records.isEmpty()) {
        setError(errorMessage, queryError.isEmpty()
                ? QStringLiteral("analysis catalog player has no opponents")
                : queryError);
        return {};
    }
    QJsonArray headToHead;
    QString largestOpponentId;
    qint64 largestOpponentGameCount = -1;
    qint64 squaredOpponentGames = 0;
    for (const PlayerAnalysisCatalogHeadToHead &record : records) {
        headToHead.append(QJsonObject {
            {QStringLiteral("black_game_count"), record.blackGameCount},
            {QStringLiteral("draw_count"), record.drawCount},
            {QStringLiteral("game_count"), record.gameCount},
            {QStringLiteral("loss_count"), record.lossCount},
            {QStringLiteral("opponent_id"), record.opponentId},
            {QStringLiteral("player_id"), record.playerId},
            {QStringLiteral("score_rate_ppm"), record.scoreRatePpm},
            {QStringLiteral("unordered_pair_id"), record.unorderedPairId},
            {QStringLiteral("white_game_count"), record.whiteGameCount},
            {QStringLiteral("win_count"), record.winCount},
        });
        squaredOpponentGames += record.gameCount * record.gameCount;
        if (record.gameCount > largestOpponentGameCount
            || (record.gameCount == largestOpponentGameCount
                && record.opponentId < largestOpponentId)) {
            largestOpponentId = record.opponentId;
            largestOpponentGameCount = record.gameCount;
        }
    }
    const qint64 opponentShare =
        (largestOpponentGameCount * kPpm + summary->gameCount / 2)
        / summary->gameCount;
    const qint64 opponentHhi =
        (squaredOpponentGames * kPpm
            + summary->gameCount * summary->gameCount / 2)
        / (summary->gameCount * summary->gameCount);
    const QJsonObject view {
        {QStringLiteral("black_game_count"), summary->blackGameCount},
        {QStringLiteral("distinct_opponent_count"), summary->distinctOpponentCount},
        {QStringLiteral("distinct_utc_day_count"), summary->distinctUtcDayCount},
        {QStringLiteral("draw_count"), summary->drawCount},
        {QStringLiteral("game_count"), summary->gameCount},
        {QStringLiteral("head_to_head"), headToHead},
        {QStringLiteral("largest_opponent_game_count"), largestOpponentGameCount},
        {QStringLiteral("largest_opponent_game_share_ppm"), opponentShare},
        {QStringLiteral("largest_opponent_id"), largestOpponentId},
        {QStringLiteral("loss_count"), summary->lossCount},
        {QStringLiteral("metrics"), metrics},
        {QStringLiteral("opponent_hhi_ppm"), opponentHhi},
        {QStringLiteral("player_id"), playerId},
        {QStringLiteral("score_rate_ppm"), summary->scoreRatePpm},
        {QStringLiteral("white_game_count"), summary->whiteGameCount},
        {QStringLiteral("win_count"), summary->winCount},
    };
    m_catalogStructureViewsByPlayer.insert(playerId, view);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return view;
}

void PlayerStatisticsPanel::clearBoardStructureDrilldown()
{
    m_structureDrilldownLabel->clear();
    m_structureDrilldownLabel->setVisible(false);
    m_structureDrilldownTable->clearContents();
    m_structureDrilldownTable->setRowCount(0);
    m_structureDrilldownTable->setVisible(false);
}

void PlayerStatisticsPanel::showBoardStructureDrilldown(
    const QString &playerId,
    const QString &metricCode)
{
    if (m_analysisCatalog == nullptr || !m_analysisCatalog->isOpen()
        || !m_structureSnapshot.isEmpty() || playerId.isEmpty()
        || !boardStructureMetricCodes().contains(metricCode)) {
        return;
    }
    QString error;
    const QVector<PlayerAnalysisCatalogMeasurementGame> rows =
        m_analysisCatalog->measurementGames(playerId, metricCode, 5'000, &error);
    if (!error.isEmpty()) {
        clearBoardStructureDrilldown();
        m_structureDrilldownLabel->setText(error);
        m_structureDrilldownLabel->setVisible(true);
        return;
    }
    m_structureDrilldownTable->clearContents();
    m_structureDrilldownTable->setRowCount(rows.size());
    qint64 observed = 0;
    qint64 notApplicable = 0;
    for (qsizetype row = 0; row < rows.size(); ++row) {
        const PlayerAnalysisCatalogMeasurementGame &measurement = rows.at(row);
        observed += measurement.measurementStatus == QStringLiteral("observed");
        notApplicable += measurement.measurementStatus
            == QStringLiteral("not_applicable");
        QString opening = measurement.openingStatus;
        if (measurement.openingStatus == QStringLiteral("classified")) {
            QStringList openingParts;
            if (!measurement.openingEco.isEmpty()) {
                openingParts.append(measurement.openingEco);
            }
            if (!measurement.openingName.isEmpty()) {
                openingParts.append(measurement.openingName);
            }
            opening = openingParts.join(QStringLiteral(" · "));
        }
        QString valueText = QStringLiteral("N/A");
        if (measurement.valuePpm.has_value()) {
            valueText = metricCode.startsWith(QStringLiteral("material_delta_mean."))
                ? signedFixedPointText(
                    *measurement.valuePpm, static_cast<double>(kPpm), QString())
                : QStringLiteral("%1%").arg(QString::number(
                    static_cast<double>(*measurement.valuePpm) / 10'000.0,
                    'f', 1));
        }
        const QString numerator = measurement.numerator.has_value()
            ? numberText(*measurement.numerator)
            : QStringLiteral("N/A");
        const QStringList values {
            compactUtc(measurement.eventStartUtc),
            measurement.opponentId,
            phaseText(measurement.playerColor),
            phaseText(measurement.outcome),
            opening,
            valueText,
            QStringLiteral("%1 / %2")
                .arg(numerator, numberText(measurement.denominator)),
            measurement.measurementStatus == QStringLiteral("observed")
                ? QStringLiteral("Observed") : QStringLiteral("N/A"),
            measurement.sourceGameId,
        };
        for (qsizetype column = 0; column < values.size(); ++column) {
            auto *item = readOnlyItem(values.at(column));
            item->setToolTip(measurement.sourceGameId);
            item->setData(
                kBoardStructureSourceGameIdRole,
                measurement.sourceGameId);
            m_structureDrilldownTable->setItem(row, column, item);
        }
    }
    m_structureDrilldownLabel->setText(
        QStringLiteral(
            "Supporting games · %1 · %2 · %3 rows (%4 observed / %5 N/A). "
            "Results are retrospective; these mechanical rows are not pregame features. "
            "Double-click a game to open its replay.")
            .arg(playerId, metricCode)
            .arg(numberText(rows.size()))
            .arg(numberText(observed))
            .arg(numberText(notApplicable)));
    m_structureDrilldownLabel->setVisible(true);
    m_structureDrilldownTable->setVisible(true);
}

void PlayerStatisticsPanel::refreshBoardStructureComparisonPlayers()
{
    const QString primaryPlayerId = selectedPlayerId();
    if (m_structureComparisonSourcePlayerId == primaryPlayerId
        && m_structureComparisonPlayerCombo->count() > 0) {
        return;
    }

    const QString preferredPlayerId =
        m_structureComparisonPlayerCombo->currentData().toString();
    const QSignalBlocker blocker(m_structureComparisonPlayerCombo);
    m_structureComparisonPlayerCombo->clear();
    m_structureComparisonPlayerCombo->addItem(
        QStringLiteral("No comparison"), QString());
    const bool catalogView = m_structureSnapshot.isEmpty()
        && m_analysisCatalog != nullptr && m_analysisCatalog->isOpen();
    const bool primaryAvailable = catalogView
        ? m_catalogStructurePlayerIds.contains(primaryPlayerId)
        : m_structurePlayersById.contains(primaryPlayerId);
    if (!primaryPlayerId.isEmpty() && primaryAvailable) {
        QStringList playerIds = catalogView
            ? m_catalogStructurePlayerIds
            : m_structurePlayersById.keys();
        std::sort(playerIds.begin(), playerIds.end());
        for (const QString &playerId : playerIds) {
            if (playerId != primaryPlayerId) {
                m_structureComparisonPlayerCombo->addItem(playerId, playerId);
            }
        }
    }
    const int preferredIndex = preferredPlayerId == primaryPlayerId
        ? -1
        : m_structureComparisonPlayerCombo->findData(preferredPlayerId);
    m_structureComparisonPlayerCombo->setCurrentIndex(
        preferredIndex > 0 ? preferredIndex : 0);
    m_structureComparisonSourcePlayerId = primaryPlayerId;
}

void PlayerStatisticsPanel::rebuildBoardStructureView()
{
    clearBoardStructureDrilldown();
    const bool catalogView = m_structureSnapshot.isEmpty()
        && m_analysisCatalog != nullptr && m_analysisCatalog->isOpen();
    if (m_structureSnapshot.isEmpty() && !catalogView) {
        m_structureStatusLabel->setText(
            QStringLiteral("No BoardStructure player statistics loaded."));
        m_structureConcentrationLabel->setText(
            QStringLiteral("Concentration information will appear after a BoardStructure snapshot is loaded."));
        m_structureMetricTable->setRowCount(0);
        m_structureCastlingTable->setRowCount(0);
        m_structureComparisonTable->setRowCount(0);
        m_structureComparisonPanel->setVisible(false);
        m_structureComparisonSummaryLabel->setVisible(false);
        m_structureComparisonTable->setVisible(false);
        m_structureHeadToHeadTable->setRowCount(0);
        m_structureHeadToHeadLabel->setText(QStringLiteral("Head-to-head results"));
        m_structureHeadToHeadLabel->setVisible(false);
        m_structureHeadToHeadFilterPanel->setVisible(false);
        m_structureHeadToHeadTable->setVisible(false);
        return;
    }
    const QString playerId = selectedPlayerId();
    refreshBoardStructureComparisonPlayers();
    const bool globalView = playerId.isEmpty();
    const bool playerAvailable = catalogView
        ? m_catalogStructurePlayerIds.contains(playerId)
        : m_structurePlayersById.contains(playerId);
    if (!globalView && !playerAvailable) {
        m_structureStatusLabel->setText(
            QStringLiteral("No BoardStructure summary is available for %1.").arg(playerId));
        m_structureSummaryLabel->setText(
            QStringLiteral("The player selector may contain players from a different loaded explorer or timing snapshot."));
        m_structureConcentrationLabel->setText(
            QStringLiteral("No opponent concentration is available for this player."));
        m_structureMetricTable->setRowCount(0);
        m_structureCastlingTable->setRowCount(0);
        m_structureComparisonTable->setRowCount(0);
        m_structureComparisonPanel->setVisible(false);
        m_structureComparisonSummaryLabel->setVisible(false);
        m_structureComparisonTable->setVisible(false);
        m_structureHeadToHeadTable->setRowCount(0);
        m_structureHeadToHeadLabel->setText(QStringLiteral("Head-to-head results"));
        m_structureHeadToHeadLabel->setVisible(false);
        m_structureHeadToHeadFilterPanel->setVisible(false);
        m_structureHeadToHeadTable->setVisible(false);
        return;
    }
    QString catalogError;
    const QJsonObject view = catalogView
        ? catalogBoardStructureView(playerId, &catalogError)
        : (globalView
            ? m_structureSnapshot.value(QStringLiteral("global_statistics")).toObject()
            : m_structurePlayersById.value(playerId));
    if (view.isEmpty()) {
        m_structureStatusLabel->setText(catalogError.isEmpty()
                ? QStringLiteral("BoardStructure statistics are unavailable.")
                : catalogError);
        m_structureSummaryLabel->setText(
            QStringLiteral("No mechanical player summary could be loaded."));
        m_structureConcentrationLabel->clear();
        m_structureMetricTable->setRowCount(0);
        m_structureCastlingTable->setRowCount(0);
        m_structureComparisonTable->setRowCount(0);
        m_structureComparisonPanel->setVisible(false);
        m_structureComparisonSummaryLabel->setVisible(false);
        m_structureComparisonTable->setVisible(false);
        m_structureHeadToHeadTable->setRowCount(0);
        m_structureHeadToHeadLabel->setVisible(false);
        m_structureHeadToHeadFilterPanel->setVisible(false);
        m_structureHeadToHeadTable->setVisible(false);
        return;
    }
    const QJsonArray metrics = view.value(QStringLiteral("metrics")).toArray();
    const QString sourcePlan = catalogView
        ? m_analysisCatalog->sourcePlanId()
        : m_structureSnapshot.value(
            QStringLiteral("source_plan_v2_id")).toString();
    if (catalogView) {
        m_structureStatusLabel->setText(
            QStringLiteral("Read-only analysis catalog · source %1 · exact 39-cell rows queried on demand · catalog does not authenticate source replay")
                .arg(compactSourcePlan(sourcePlan)));
        m_structureStatusLabel->setToolTip(
            sourcePlan + QLatin1Char('\n')
            + m_explorerMetadata.value(
                QStringLiteral("descriptive_metric_registry_id")));
    } else {
        m_structureStatusLabel->setText(
            QStringLiteral("Display snapshot · source %1 · not source-authenticated · explorer filters do not alter these values")
                .arg(compactSourcePlan(sourcePlan)));
        m_structureStatusLabel->setToolTip(
            sourcePlan + QLatin1Char('\n')
            + m_structureSnapshot.value(
                QStringLiteral("descriptive_metric_registry_id")).toString());
    }
    if (globalView) {
        m_structureComparisonPanel->setVisible(false);
        m_structureComparisonSummaryLabel->setVisible(false);
        m_structureComparisonTable->setVisible(false);
        m_structureMetricTable->setVisible(true);
        m_structureCastlingTable->setVisible(true);
        m_structureSummaryLabel->setText(
            QStringLiteral("%1 games · %2 player rows · %3 players · White wins %4 · draws %5 · Black wins %6")
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("game_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("player_game_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("distinct_player_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("white_win_game_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("draw_game_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("black_win_game_count")).toDouble()))));
        const QJsonArray largestPairPlayers = view.value(
            QStringLiteral("largest_pair_player_ids")).toArray();
        m_structureConcentrationLabel->setText(
            QStringLiteral(
                "Player concentration · largest %1: %2 games (%3 of games; %4 of player exposures) · exposure HHI %5\n"
                "Pair concentration · %6 unordered pairs · largest %7 / %8: %9 games (%10) · pair HHI %11")
                .arg(view.value(QStringLiteral("largest_player_id")).toString())
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("largest_player_game_count")).toDouble())))
                .arg(coverageText(view.value(
                    QStringLiteral("largest_player_game_share_ppm"))))
                .arg(coverageText(view.value(
                    QStringLiteral("largest_player_exposure_share_ppm"))))
                .arg(coverageText(view.value(
                    QStringLiteral("player_exposure_hhi_ppm"))))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("unordered_pair_count")).toDouble())))
                .arg(largestPairPlayers.at(0).toString())
                .arg(largestPairPlayers.at(1).toString())
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("largest_pair_game_count")).toDouble())))
                .arg(coverageText(view.value(
                    QStringLiteral("largest_pair_game_share_ppm"))))
                .arg(coverageText(view.value(QStringLiteral("pair_hhi_ppm")))));
        m_structureConcentrationLabel->setToolTip(
            view.value(QStringLiteral("largest_unordered_pair_id")).toString());
        m_structureHeadToHeadTable->setRowCount(0);
        m_structureHeadToHeadLabel->setText(QStringLiteral("Head-to-head results"));
        m_structureHeadToHeadLabel->setVisible(false);
        m_structureHeadToHeadFilterPanel->setVisible(false);
        m_structureHeadToHeadTable->setVisible(false);
    } else {
        m_structureComparisonPanel->setVisible(true);
        m_structureSummaryLabel->setText(
            QStringLiteral("%1 · %2 games · %3 White / %4 Black · %5-%6-%7 · score %8% · %9 opponents")
                .arg(playerId)
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("game_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("white_game_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("black_game_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("win_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("draw_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("loss_count")).toDouble())))
                .arg(QString::number(view.value(
                    QStringLiteral("score_rate_ppm")).toDouble() / 10'000.0, 'f', 1))
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("distinct_opponent_count")).toDouble()))));
        m_structureConcentrationLabel->setText(
            QStringLiteral(
                "Opponent concentration · largest %1: %2 games (%3) · opponent HHI %4")
                .arg(view.value(QStringLiteral("largest_opponent_id")).toString())
                .arg(numberText(static_cast<qint64>(view.value(
                    QStringLiteral("largest_opponent_game_count")).toDouble())))
                .arg(coverageText(view.value(
                    QStringLiteral("largest_opponent_game_share_ppm"))))
                .arg(coverageText(view.value(QStringLiteral("opponent_hhi_ppm")))));
        m_structureConcentrationLabel->setToolTip(QString());

        const QString comparisonPlayerId =
            m_structureComparisonPlayerCombo->currentData().toString();
        const bool comparisonView = !comparisonPlayerId.isEmpty()
            && comparisonPlayerId != playerId
            && (catalogView
                ? m_catalogStructurePlayerIds.contains(comparisonPlayerId)
                : m_structurePlayersById.contains(comparisonPlayerId));
        if (comparisonView) {
            const QJsonObject comparisonViewData = catalogView
                ? catalogBoardStructureView(comparisonPlayerId, &catalogError)
                : m_structurePlayersById.value(comparisonPlayerId);
            if (comparisonViewData.isEmpty()) {
                m_structureComparisonSummaryLabel->setText(catalogError);
                m_structureComparisonSummaryLabel->setVisible(true);
                m_structureComparisonTable->setVisible(false);
                return;
            }
            const QJsonArray comparisonMetrics = comparisonViewData.value(
                QStringLiteral("metrics")).toArray();
            const QJsonObject directRecord = boardStructureHeadToHeadRow(
                view, comparisonPlayerId);

            QSet<QString> primaryOpponents;
            for (const QJsonValue &value : view.value(
                     QStringLiteral("head_to_head")).toArray()) {
                primaryOpponents.insert(value.toObject().value(
                    QStringLiteral("opponent_id")).toString());
            }
            QSet<QString> comparisonOpponents;
            for (const QJsonValue &value : comparisonViewData.value(
                     QStringLiteral("head_to_head")).toArray()) {
                comparisonOpponents.insert(value.toObject().value(
                    QStringLiteral("opponent_id")).toString());
            }
            QSet<QString> sharedOpponents = primaryOpponents;
            sharedOpponents.intersect(comparisonOpponents);

            const auto profileSummary = [](const QString &profilePlayerId,
                                            const QJsonObject &profile) {
                return QStringLiteral("%1 profile: %2 games · %3 White / %4 Black · W-D-L %5-%6-%7 · score %8")
                    .arg(profilePlayerId)
                    .arg(numberText(static_cast<qint64>(profile.value(
                        QStringLiteral("game_count")).toDouble())))
                    .arg(numberText(static_cast<qint64>(profile.value(
                        QStringLiteral("white_game_count")).toDouble())))
                    .arg(numberText(static_cast<qint64>(profile.value(
                        QStringLiteral("black_game_count")).toDouble())))
                    .arg(numberText(static_cast<qint64>(profile.value(
                        QStringLiteral("win_count")).toDouble())))
                    .arg(numberText(static_cast<qint64>(profile.value(
                        QStringLiteral("draw_count")).toDouble())))
                    .arg(numberText(static_cast<qint64>(profile.value(
                        QStringLiteral("loss_count")).toDouble())))
                    .arg(coverageText(profile.value(
                        QStringLiteral("score_rate_ppm"))));
            };
            QString directSummary;
            if (directRecord.isEmpty()) {
                directSummary = QStringLiteral(
                    "No direct games between %1 and %2 are present in this snapshot.")
                    .arg(playerId, comparisonPlayerId);
            } else {
                directSummary = QStringLiteral(
                    "Direct record from %1 perspective: %2 games · %3 White / %4 Black · W-D-L %5-%6-%7 · score %8")
                    .arg(playerId)
                    .arg(numberText(static_cast<qint64>(directRecord.value(
                        QStringLiteral("game_count")).toDouble())))
                    .arg(numberText(static_cast<qint64>(directRecord.value(
                        QStringLiteral("white_game_count")).toDouble())))
                    .arg(numberText(static_cast<qint64>(directRecord.value(
                        QStringLiteral("black_game_count")).toDouble())))
                    .arg(numberText(static_cast<qint64>(directRecord.value(
                        QStringLiteral("win_count")).toDouble())))
                    .arg(numberText(static_cast<qint64>(directRecord.value(
                        QStringLiteral("draw_count")).toDouble())))
                    .arg(numberText(static_cast<qint64>(directRecord.value(
                        QStringLiteral("loss_count")).toDouble())))
                    .arg(coverageText(directRecord.value(
                        QStringLiteral("score_rate_ppm"))));
            }
            m_structureComparisonSummaryLabel->setText(
                profileSummary(playerId, view) + QLatin1Char('\n')
                + profileSummary(comparisonPlayerId, comparisonViewData)
                + QLatin1Char('\n') + directSummary
                + QLatin1Char('\n')
                + QStringLiteral(
                    "%1 shared opponents · full-profile mechanical comparison; not opponent-adjusted and not restricted to direct games. Differences do not imply better or worse play.")
                      .arg(numberText(sharedOpponents.size())));

            const QString category =
                m_structureComparisonCategoryCombo->currentData().toString();
            QVector<BoardStructureComparisonMetricRow> visibleMetrics;
            for (const BoardStructureComparisonMetricRow &metricRow :
                 boardStructureComparisonMetricRows()) {
                if (category.isEmpty() || metricRow.category == category) {
                    visibleMetrics.append(metricRow);
                }
            }
            m_structureComparisonTable->setHorizontalHeaderLabels({
                QStringLiteral("Measure"),
                playerId,
                comparisonPlayerId,
                QStringLiteral("%1 \u2212 %2").arg(playerId, comparisonPlayerId),
                QStringLiteral("%1 obs / N/A").arg(playerId),
                QStringLiteral("%1 obs / N/A").arg(comparisonPlayerId),
            });
            m_structureComparisonTable->clearContents();
            m_structureComparisonTable->setRowCount(visibleMetrics.size());
            for (qsizetype row = 0; row < visibleMetrics.size(); ++row) {
                const BoardStructureComparisonMetricRow &metricRow =
                    visibleMetrics.at(row);
                const QJsonObject primaryMetric = boardStructureMetric(
                    metrics, metricRow.code);
                const QJsonObject comparisonMetric = boardStructureMetric(
                    comparisonMetrics, metricRow.code);
                auto *labelItem = readOnlyItem(metricRow.label);
                labelItem->setToolTip(metricRow.code);
                if (catalogView) {
                    labelItem->setData(kBoardStructureMetricCodeRole, metricRow.code);
                    labelItem->setData(kBoardStructurePlayerIdRole, playerId);
                }
                m_structureComparisonTable->setItem(row, 0, labelItem);
                const QStringList cellTexts {
                    boardStructureValueText(primaryMetric),
                    boardStructureValueText(comparisonMetric),
                    boardStructureDifferenceText(primaryMetric, comparisonMetric),
                    boardStructureObservationText(primaryMetric),
                    boardStructureObservationText(comparisonMetric),
                };
                for (qsizetype column = 0; column < cellTexts.size(); ++column) {
                    auto *item = readOnlyItem(cellTexts.at(column));
                    item->setTextAlignment(Qt::AlignCenter);
                    if (column < 3) {
                        item->setToolTip(
                            boardStructureTooltip(column == 1
                                ? comparisonMetric
                                : primaryMetric));
                    }
                    if (catalogView) {
                        const bool comparisonColumn = column == 1 || column == 4;
                        item->setData(kBoardStructureMetricCodeRole, metricRow.code);
                        item->setData(
                            kBoardStructurePlayerIdRole,
                            comparisonColumn ? comparisonPlayerId : playerId);
                    }
                    m_structureComparisonTable->setItem(row, column + 1, item);
                }
            }
            m_structureComparisonSummaryLabel->setVisible(true);
            m_structureComparisonTable->setVisible(true);
            m_structureMetricTable->setVisible(false);
            m_structureCastlingTable->setVisible(false);
            m_structureHeadToHeadTable->setRowCount(0);
            m_structureHeadToHeadLabel->setVisible(false);
            m_structureHeadToHeadFilterPanel->setVisible(false);
            m_structureHeadToHeadTable->setVisible(false);
            return;
        }

        m_structureComparisonSummaryLabel->setVisible(false);
        m_structureComparisonTable->setVisible(false);
        m_structureMetricTable->setVisible(true);
        m_structureCastlingTable->setVisible(true);

        const QJsonArray headToHead = view.value(
            QStringLiteral("head_to_head")).toArray();
        const QString opponentSearch = m_structureOpponentSearch->text().trimmed();
        const qint64 minimumGames = m_structureMinimumGamesSpin->value();
        QVector<QJsonObject> visibleRows;
        visibleRows.reserve(headToHead.size());
        for (const QJsonValue &value : headToHead) {
            const QJsonObject result = value.toObject();
            const QString opponentId = result.value(
                QStringLiteral("opponent_id")).toString();
            const qint64 games = static_cast<qint64>(result.value(
                QStringLiteral("game_count")).toDouble());
            if (games >= minimumGames
                && (opponentSearch.isEmpty()
                    || opponentId.contains(opponentSearch, Qt::CaseInsensitive))) {
                visibleRows.append(result);
            }
        }
        const int sortColumn = m_structureHeadToHeadTable->horizontalHeader()
            ->sortIndicatorSection();
        const Qt::SortOrder sortOrder = m_structureHeadToHeadTable
            ->horizontalHeader()->sortIndicatorOrder();
        m_structureHeadToHeadTable->setSortingEnabled(false);
        m_structureHeadToHeadTable->clearContents();
        m_structureHeadToHeadTable->setRowCount(visibleRows.size());
        for (qsizetype row = 0; row < visibleRows.size(); ++row) {
            const QJsonObject result = visibleRows.at(row);
            auto *opponentItem = readOnlyItem(
                result.value(QStringLiteral("opponent_id")).toString());
            opponentItem->setToolTip(
                result.value(QStringLiteral("unordered_pair_id")).toString());
            m_structureHeadToHeadTable->setItem(row, 0, opponentItem);
            m_structureHeadToHeadTable->setItem(row, 1, new NumericTableWidgetItem(
                static_cast<qint64>(result.value(
                    QStringLiteral("game_count")).toDouble())));
            m_structureHeadToHeadTable->setItem(row, 2, new NumericTableWidgetItem(
                static_cast<qint64>(result.value(
                    QStringLiteral("white_game_count")).toDouble())));
            m_structureHeadToHeadTable->setItem(row, 3, new NumericTableWidgetItem(
                static_cast<qint64>(result.value(
                    QStringLiteral("black_game_count")).toDouble())));
            const qint64 wins = static_cast<qint64>(result.value(
                QStringLiteral("win_count")).toDouble());
            const qint64 draws = static_cast<qint64>(result.value(
                QStringLiteral("draw_count")).toDouble());
            const qint64 losses = static_cast<qint64>(result.value(
                QStringLiteral("loss_count")).toDouble());
            auto *recordItem = new NumericTableWidgetItem(
                wins * 100'000'000 + draws * 10'000 + (5'000 - losses));
            recordItem->setText(QStringLiteral("%1-%2-%3")
                .arg(numberText(wins))
                .arg(numberText(draws))
                .arg(numberText(losses)));
            m_structureHeadToHeadTable->setItem(row, 4, recordItem);
            const qint64 scoreRate = static_cast<qint64>(result.value(
                QStringLiteral("score_rate_ppm")).toDouble());
            auto *scoreItem = new NumericTableWidgetItem(scoreRate);
            scoreItem->setText(coverageText(
                result.value(QStringLiteral("score_rate_ppm"))));
            m_structureHeadToHeadTable->setItem(row, 5, scoreItem);
        }
        m_structureHeadToHeadTable->setSortingEnabled(true);
        m_structureHeadToHeadTable->sortItems(sortColumn, sortOrder);
        m_structureHeadToHeadLabel->setText(
            QStringLiteral("Head-to-head results · %1 of %2 opponents shown")
                .arg(numberText(visibleRows.size()))
                .arg(numberText(headToHead.size())));
        m_structureHeadToHeadLabel->setVisible(true);
        m_structureHeadToHeadFilterPanel->setVisible(true);
        m_structureHeadToHeadTable->setVisible(true);
    }

    struct StructureRow {
        QString label;
        QString prefix;
        bool shareSuffix;
    };
    const QVector<StructureRow> rows {
        {QStringLiteral("Queens off"), QStringLiteral("binary.both_queens_absent"), true},
        {QStringLiteral("Passed pawn present"), QStringLiteral("binary.own_passed_pawn_present"), true},
        {QStringLiteral("Isolated pawn present"), QStringLiteral("binary.own_isolated_pawn_present"), true},
        {QStringLiteral("Doubled pawn present"), QStringLiteral("binary.own_doubled_pawn_excess_present"), true},
        {QStringLiteral("Bishop pair present"), QStringLiteral("binary.own_two_or_more_bishops_present"), true},
        {QStringLiteral("Material ahead"), QStringLiteral("material_relation.ahead"), true},
        {QStringLiteral("Material equal"), QStringLiteral("material_relation.equal"), true},
        {QStringLiteral("Material behind"), QStringLiteral("material_relation.behind"), true},
        {QStringLiteral("Mean material edge"), QStringLiteral("material_delta_mean"), false},
    };
    const QStringList phases {
        QStringLiteral("all"),
        QStringLiteral("opening"),
        QStringLiteral("middlegame"),
        QStringLiteral("endgame"),
    };
    m_structureMetricTable->clearContents();
    m_structureMetricTable->setRowCount(rows.size());
    for (qsizetype row = 0; row < rows.size(); ++row) {
        auto *labelItem = readOnlyItem(rows.at(row).label);
        if (!rows.at(row).shareSuffix) {
            labelItem->setToolTip(
                QStringLiteral("Signed fixed 1/3/3/5/9 material units; not engine evaluation or winning chance."));
        }
        m_structureMetricTable->setItem(row, 0, labelItem);
        for (qsizetype phase = 0; phase < phases.size(); ++phase) {
            QString code = rows.at(row).prefix + QLatin1Char('.') + phases.at(phase);
            if (rows.at(row).shareSuffix) {
                code += QStringLiteral(".share");
            }
            const QJsonObject metric = boardStructureMetric(metrics, code);
            auto *item = readOnlyItem(boardStructureCellText(metric));
            item->setToolTip(boardStructureTooltip(metric));
            item->setTextAlignment(Qt::AlignCenter);
            if (catalogView && !globalView) {
                item->setData(kBoardStructureMetricCodeRole, code);
                item->setData(kBoardStructurePlayerIdRole, playerId);
            }
            m_structureMetricTable->setItem(row, phase + 1, item);
        }
    }

    const QStringList castlingLabels {
        QStringLiteral("Any castle"),
        QStringLiteral("Kingside"),
        QStringLiteral("Queenside"),
    };
    const QStringList castlingCodes {
        QStringLiteral("focal_castling.any"),
        QStringLiteral("focal_castling.kingside"),
        QStringLiteral("focal_castling.queenside"),
    };
    m_structureCastlingTable->clearContents();
    m_structureCastlingTable->setRowCount(castlingCodes.size());
    for (qsizetype row = 0; row < castlingCodes.size(); ++row) {
        const QJsonObject metric = boardStructureMetric(metrics, castlingCodes.at(row));
        const qint64 observed = static_cast<qint64>(metric.value(
            QStringLiteral("observed_player_game_count")).toDouble());
        const qint64 notApplicable = static_cast<qint64>(metric.value(
            QStringLiteral("not_applicable_player_game_count")).toDouble());
        const QStringList values {
            castlingLabels.at(row),
            boardStructureValueText(metric),
            QStringLiteral("%1 / %2").arg(numberText(observed), numberText(notApplicable)),
            boardStructurePairedText(metric),
        };
        for (qsizetype column = 0; column < values.size(); ++column) {
            auto *item = readOnlyItem(values.at(column));
            item->setToolTip(boardStructureTooltip(metric));
            if (catalogView && !globalView) {
                item->setData(
                    kBoardStructureMetricCodeRole, castlingCodes.at(row));
                item->setData(kBoardStructurePlayerIdRole, playerId);
            }
            m_structureCastlingTable->setItem(row, column, item);
        }
    }
}

void PlayerStatisticsPanel::rebuildView()
{
    if (!hasSnapshot()) {
        return;
    }
    if (!m_explorerConnectionName.isEmpty()) {
        rebuildExplorerView();
        return;
    }
    if (m_snapshot.isEmpty()) {
        rebuildBoardStructureView();
        return;
    }
    const QString playerId = selectedPlayerId();
    const bool globalView = playerId.isEmpty();
    const QJsonObject view = globalView
        ? m_snapshot.value(QStringLiteral("global_statistics")).toObject()
        : m_playersById.value(playerId);
    const QJsonObject aggregate = view.value(QStringLiteral("statistics")).toObject();
    const qint64 decisions = static_cast<qint64>(aggregate.value(QStringLiteral("decision_count")).toDouble());
    const qint64 lowClock = pressureCount(aggregate, 10'000);
    const QString sourcePlan = m_snapshot.value(QStringLiteral("source_plan_v2_id")).toString();

    m_statusLabel->setText(
        QStringLiteral("Display snapshot · source %1 · not source-authenticated by ParlAWL")
            .arg(compactSourcePlan(sourcePlan)));
    m_statusLabel->setToolTip(sourcePlan);
    m_openButton->setText(QStringLiteral("Replace Snapshot"));
    m_gameMetricLabel->setText(numberText(
        static_cast<qint64>(view.value(QStringLiteral("game_count")).toDouble())));
    m_moveMetricLabel->setText(numberText(decisions));
    m_coverageMetricLabel->setText(
        coverageText(aggregate.value(QStringLiteral("elapsed_coverage_ppm"))));
    m_medianMetricLabel->setText(
        durationText(aggregate.value(QStringLiteral("p50_observed_elapsed_ms"))));
    m_p90MetricLabel->setText(
        durationText(aggregate.value(QStringLiteral("p90_observed_elapsed_ms"))));
    if (globalView) {
        m_populationMetricTitleLabel->setText(QStringLiteral("Players"));
        m_populationMetricLabel->setText(numberText(
            static_cast<qint64>(view.value(QStringLiteral("distinct_player_count")).toDouble())));
        m_summaryLabel->setText(
            QStringLiteral(
                "All players · %1 unordered pairs · %2 UTC days · maximum %3 · %4 moves began with 10 seconds or less")
                .arg(numberText(static_cast<qint64>(view.value(QStringLiteral("unordered_pair_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(QStringLiteral("utc_day_count")).toDouble())))
                .arg(durationText(aggregate.value(QStringLiteral("maximum_observed_elapsed_ms"))))
                .arg(numberText(lowClock)));
        m_opponentHintLabel->setText(
            QStringLiteral("Select a player to compare that player's timing across opponents."));
    } else {
        m_populationMetricTitleLabel->setText(QStringLiteral("Opponents"));
        m_populationMetricLabel->setText(numberText(
            static_cast<qint64>(view.value(QStringLiteral("distinct_opponent_count")).toDouble())));
        m_summaryLabel->setText(
            QStringLiteral(
                "%1 · %2 White / %3 Black · %4 UTC days · maximum %5 · %6 moves began with 10 seconds or less")
                .arg(playerId)
                .arg(numberText(static_cast<qint64>(view.value(QStringLiteral("white_game_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(QStringLiteral("black_game_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(view.value(QStringLiteral("distinct_utc_day_count")).toDouble())))
                .arg(durationText(aggregate.value(QStringLiteral("maximum_observed_elapsed_ms"))))
                .arg(numberText(lowClock)));
        m_opponentHintLabel->setText(
            QStringLiteral("Move-time summaries from %1's perspective. Descriptive clock evidence only.")
                .arg(playerId));
    }

    m_pressureLabel->setText(
        QStringLiteral("Clock before move · ≤1s %1 · ≤5s %2 · ≤10s %3 · ≤30s %4 · ≤60s %5")
            .arg(numberText(pressureCount(aggregate, 1'000)))
            .arg(numberText(pressureCount(aggregate, 5'000)))
            .arg(numberText(lowClock))
            .arg(numberText(pressureCount(aggregate, 30'000)))
            .arg(numberText(pressureCount(aggregate, 60'000))));

    populatePhaseTable(view.value(QStringLiteral("by_phase")).toArray());
    populateDecisionContextTable(
        view.value(QStringLiteral("by_color")).toArray(),
        view.value(QStringLiteral("by_forcedness")).toArray());
    populateOpponentTable(
        globalView ? QJsonArray {} : view.value(QStringLiteral("opponents")).toArray());
    populateLongestTable(view.value(QStringLiteral("longest_observed_moves")).toArray());
    rebuildBoardStructureView();
}

void PlayerStatisticsPanel::populateDecisionContextTable(
    const QJsonArray &colors,
    const QJsonArray &forcedness)
{
    QJsonArray groups = colors;
    for (const QJsonValue &value : forcedness) {
        groups.append(value);
    }
    const QStringList labels {
        QStringLiteral("White"),
        QStringLiteral("Black"),
        QStringLiteral("Only legal move"),
        QStringLiteral("Multiple legal moves"),
    };
    m_decisionContextTable->clearContents();
    m_decisionContextTable->setRowCount(groups.size());
    for (qsizetype row = 0; row < groups.size(); ++row) {
        const QJsonObject aggregate = groups.at(row).toObject()
            .value(QStringLiteral("statistics")).toObject();
        const QStringList values {
            labels.at(row),
            numberText(static_cast<qint64>(aggregate.value(QStringLiteral("decision_count")).toDouble())),
            coverageText(aggregate.value(QStringLiteral("elapsed_coverage_ppm"))),
            durationText(aggregate.value(QStringLiteral("p50_observed_elapsed_ms"))),
            durationText(aggregate.value(QStringLiteral("p90_observed_elapsed_ms"))),
            durationText(aggregate.value(QStringLiteral("maximum_observed_elapsed_ms"))),
        };
        for (qsizetype column = 0; column < values.size(); ++column) {
            m_decisionContextTable->setItem(row, column, readOnlyItem(values.at(column)));
        }
    }
}

void PlayerStatisticsPanel::populatePhaseTable(const QJsonArray &groups)
{
    m_phaseTable->clearContents();
    m_phaseTable->setRowCount(groups.size());
    for (qsizetype row = 0; row < groups.size(); ++row) {
        const QJsonObject group = groups.at(row).toObject();
        const QJsonObject aggregate = group.value(QStringLiteral("statistics")).toObject();
        const QStringList values {
            phaseText(group.value(QStringLiteral("group")).toString()),
            numberText(static_cast<qint64>(aggregate.value(QStringLiteral("decision_count")).toDouble())),
            coverageText(aggregate.value(QStringLiteral("elapsed_coverage_ppm"))),
            durationText(aggregate.value(QStringLiteral("p50_observed_elapsed_ms"))),
            durationText(aggregate.value(QStringLiteral("p90_observed_elapsed_ms"))),
            durationText(aggregate.value(QStringLiteral("maximum_observed_elapsed_ms"))),
        };
        for (qsizetype column = 0; column < values.size(); ++column) {
            m_phaseTable->setItem(row, column, readOnlyItem(values.at(column)));
        }
    }
}

void PlayerStatisticsPanel::populateOpponentTable(const QJsonArray &opponents)
{
    m_opponentTable->clearContents();
    m_opponentTable->setRowCount(opponents.size());
    for (qsizetype row = 0; row < opponents.size(); ++row) {
        const QJsonObject opponent = opponents.at(row).toObject();
        const QJsonObject aggregate = opponent.value(QStringLiteral("statistics")).toObject();
        const bool hasResults = opponent.contains(QStringLiteral("win_count"));
        const QString record = hasResults
            ? QStringLiteral("%1-%2-%3")
                .arg(numberText(static_cast<qint64>(opponent.value(QStringLiteral("win_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(opponent.value(QStringLiteral("draw_count")).toDouble())))
                .arg(numberText(static_cast<qint64>(opponent.value(QStringLiteral("loss_count")).toDouble())))
            : QStringLiteral("—");
        const QString score = hasResults
            ? QStringLiteral("%1%").arg(QString::number(
                opponent.value(QStringLiteral("score_rate_ppm")).toDouble() / 10'000.0,
                'f',
                1))
            : QStringLiteral("—");
        const QStringList values {
            opponent.value(QStringLiteral("opponent_id")).toString(),
            numberText(static_cast<qint64>(opponent.value(QStringLiteral("game_count")).toDouble())),
            record,
            score,
            durationText(aggregate.value(QStringLiteral("p50_observed_elapsed_ms"))),
            durationText(aggregate.value(QStringLiteral("p90_observed_elapsed_ms"))),
        };
        for (qsizetype column = 0; column < values.size(); ++column) {
            m_opponentTable->setItem(row, column, readOnlyItem(values.at(column)));
        }
    }
}

void PlayerStatisticsPanel::populateLongestTable(const QJsonArray &moves)
{
    m_longestTable->clearContents();
    m_longestTable->setRowCount(moves.size());
    for (qsizetype row = 0; row < moves.size(); ++row) {
        const QJsonObject move = moves.at(row).toObject();
        const QStringList values {
            durationText(move.value(QStringLiteral("elapsed_move_ms"))),
            compactUtc(move.value(QStringLiteral("event_start_utc")).toString()),
            move.value(QStringLiteral("opponent_id")).toString(),
            phaseText(move.value(QStringLiteral("player_color")).toString()),
            QStringLiteral("%1 (%2)")
                .arg(move.value(QStringLiteral("played_move_san")).toString(),
                     move.value(QStringLiteral("played_move_uci")).toString()),
            phaseText(move.value(QStringLiteral("position_phase")).toString()),
            durationText(move.value(QStringLiteral("decision_start_clock_ms"))),
        };
        for (qsizetype column = 0; column < values.size(); ++column) {
            m_longestTable->setItem(row, column, readOnlyItem(values.at(column)));
        }
    }
}
