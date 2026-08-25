#include "player_analysis_catalog.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <cmath>


namespace {

constexpr qint64 kPpm = 1'000'000;
constexpr qint64 kMaximumCatalogBytes = 1024LL * 1024 * 1024;
const QString kCatalogSchemaVersion =
    QStringLiteral("chess-player-analysis-catalog-sqlite-v1");
const QString kExplorerSchemaVersion =
    QStringLiteral("chess-player-game-explorer-sqlite-v1");

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

std::optional<qint64> roundedRatioPpm(qint64 numerator, qint64 denominator)
{
    if (denominator <= 0) {
        return std::nullopt;
    }
    const qint64 magnitude =
        (std::abs(numerator) * kPpm + denominator / 2) / denominator;
    return numerator < 0 ? -magnitude : magnitude;
}

std::optional<qint64> roundedSignedRatio(qint64 numerator, qint64 denominator)
{
    if (denominator <= 0) {
        return std::nullopt;
    }
    const qint64 magnitude =
        (std::abs(numerator) + denominator / 2) / denominator;
    return numerator < 0 ? -magnitude : magnitude;
}

QSqlDatabase catalogDatabase(const QString &connectionName)
{
    return QSqlDatabase::database(connectionName, false);
}

QString aggregateKey(const QString &playerId, const QString &metricCode)
{
    return playerId + QChar(0x1f) + metricCode;
}

} // namespace


PlayerAnalysisCatalog::PlayerAnalysisCatalog()
    : m_connectionName(
          QStringLiteral("parlawl-player-analysis-%1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

PlayerAnalysisCatalog::~PlayerAnalysisCatalog()
{
    close();
}

bool PlayerAnalysisCatalog::open(const QString &path, QString *errorMessage)
{
    close();
    const QFileInfo fileInfo(path);
    const QString canonicalPath = fileInfo.canonicalFilePath();
    if (canonicalPath.isEmpty() || !fileInfo.isFile()
        || fileInfo.size() < 1 || fileInfo.size() > kMaximumCatalogBytes) {
        setError(errorMessage, QStringLiteral("analysis catalog path is invalid"));
        return false;
    }

    QString failure;
    QString sourcePlan;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), m_connectionName);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        database.setDatabaseName(canonicalPath);
        if (!database.open()) {
            failure = QStringLiteral("analysis catalog could not be opened read-only: %1")
                .arg(database.lastError().text());
        } else {
            QSqlQuery pragma(database);
            if (!pragma.exec(QStringLiteral("PRAGMA query_only = ON"))) {
                failure = QStringLiteral("analysis catalog could not enter query-only mode");
            }
        }

        QHash<QString, QString> metadata;
        if (failure.isEmpty()) {
            QSqlQuery query(database);
            if (!query.exec(QStringLiteral("SELECT key, value FROM metadata"))) {
                failure = QStringLiteral("analysis catalog metadata is unavailable");
            } else {
                while (query.next()) {
                    const QString key = query.value(0).toString();
                    if (metadata.contains(key)) {
                        failure = QStringLiteral("analysis catalog metadata is duplicated");
                        break;
                    }
                    metadata.insert(key, query.value(1).toString());
                }
            }
        }
        if (failure.isEmpty()
            && (metadata.value(QStringLiteral("catalog_schema_version"))
                    != kCatalogSchemaVersion
                || metadata.value(QStringLiteral("schema_version"))
                    != kExplorerSchemaVersion
                || metadata.value(QStringLiteral("catalog_authenticates_source_replay"))
                    != QStringLiteral("false")
                || metadata.value(QStringLiteral("same_game_vectors_are_pregame_features"))
                    != QStringLiteral("false"))) {
            failure = QStringLiteral("analysis catalog metadata is unsupported");
        }
        sourcePlan = metadata.value(QStringLiteral("source_plan_v2_id"));
        if (failure.isEmpty()
            && !sourcePlan.startsWith(
                QStringLiteral("chess-cohort-source-chunk-plan-v2:"))) {
            failure = QStringLiteral("analysis catalog source-plan identity is malformed");
        }

        if (failure.isEmpty()) {
            const QSet<QString> requiredTables {
                QStringLiteral("metadata"),
                QStringLiteral("games"),
                QStringLiteral("player_games"),
                QStringLiteral("moves"),
                QStringLiteral("board_structure_metric_definitions"),
                QStringLiteral("board_structure_player_games"),
                QStringLiteral("board_structure_measurements"),
            };
            const QStringList tableNames = database.tables(QSql::Tables);
            const QSet<QString> actualTables(
                tableNames.cbegin(), tableNames.cend());
            QSet<QString> missingTables = requiredTables;
            missingTables.subtract(actualTables);
            if (!missingTables.isEmpty()) {
                failure = QStringLiteral("analysis catalog tables are incomplete");
            }
        }

        if (failure.isEmpty()) {
            QSqlQuery count(database);
            if (!count.exec(QStringLiteral(
                    "SELECT COUNT(*) FROM board_structure_measurements"))
                || !count.next()
                || count.value(0).toString()
                    != metadata.value(
                        QStringLiteral("board_structure_measurement_count"))) {
                failure = QStringLiteral("analysis catalog measurement count differs");
            }
        }
    }

    if (!failure.isEmpty()) {
        close();
        setError(errorMessage, failure);
        return false;
    }
    m_sourcePlanId = sourcePlan;
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

void PlayerAnalysisCatalog::close()
{
    m_sourcePlanId.clear();
    if (!QSqlDatabase::contains(m_connectionName)) {
        return;
    }
    {
        QSqlDatabase database = catalogDatabase(m_connectionName);
        database.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool PlayerAnalysisCatalog::isOpen() const
{
    return QSqlDatabase::contains(m_connectionName)
        && catalogDatabase(m_connectionName).isOpen();
}

QString PlayerAnalysisCatalog::sourcePlanId() const
{
    return m_sourcePlanId;
}

QStringList PlayerAnalysisCatalog::playerIds(QString *errorMessage) const
{
    QStringList output;
    if (!isOpen()) {
        setError(errorMessage, QStringLiteral("analysis catalog is not open"));
        return output;
    }
    QSqlQuery query(catalogDatabase(m_connectionName));
    if (!query.exec(QStringLiteral(
            "SELECT DISTINCT player_id FROM board_structure_player_games "
            "ORDER BY player_id"))) {
        setError(errorMessage, QStringLiteral("analysis catalog player query failed"));
        return {};
    }
    while (query.next()) {
        output.append(query.value(0).toString());
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return output;
}

std::optional<PlayerAnalysisCatalogGlobalSummary>
PlayerAnalysisCatalog::globalSummary(QString *errorMessage) const
{
    if (!isOpen()) {
        setError(errorMessage, QStringLiteral("analysis catalog is not open"));
        return std::nullopt;
    }
    QSqlDatabase database = catalogDatabase(m_connectionName);
    QSqlQuery counts(database);
    if (!counts.exec(QStringLiteral(
            "SELECT COUNT(*), COUNT(DISTINCT utc_day), "
            "SUM(result = '1-0'), SUM(result = '1/2-1/2'), "
            "SUM(result = '0-1') FROM games"))
        || !counts.next()) {
        setError(errorMessage, QStringLiteral("analysis catalog global game summary failed"));
        return std::nullopt;
    }
    PlayerAnalysisCatalogGlobalSummary output;
    output.gameCount = counts.value(0).toLongLong();
    output.distinctUtcDayCount = counts.value(1).toLongLong();
    output.whiteWinGameCount = counts.value(2).toLongLong();
    output.drawGameCount = counts.value(3).toLongLong();
    output.blackWinGameCount = counts.value(4).toLongLong();
    if (output.gameCount < 1) {
        setError(errorMessage, QStringLiteral("analysis catalog has no games"));
        return std::nullopt;
    }

    QSqlQuery playerCounts(database);
    if (!playerCounts.exec(QStringLiteral(
            "SELECT player_id, COUNT(*), "
            "SUM(player_color = 'white'), SUM(player_color = 'black'), "
            "SUM(outcome = 'win'), SUM(outcome = 'draw'), "
            "SUM(outcome = 'loss') FROM player_games "
            "GROUP BY player_id ORDER BY COUNT(*) DESC, player_id"))) {
        setError(errorMessage, QStringLiteral("analysis catalog player concentration failed"));
        return std::nullopt;
    }
    qint64 squaredPlayerExposures = 0;
    while (playerCounts.next()) {
        const qint64 games = playerCounts.value(1).toLongLong();
        if (output.distinctPlayerCount == 0) {
            output.largestPlayerId = playerCounts.value(0).toString();
            output.largestPlayerGameCount = games;
        }
        ++output.distinctPlayerCount;
        output.playerGameCount += games;
        output.whitePlayerGameCount += playerCounts.value(2).toLongLong();
        output.blackPlayerGameCount += playerCounts.value(3).toLongLong();
        output.playerGameWinCount += playerCounts.value(4).toLongLong();
        output.playerGameDrawCount += playerCounts.value(5).toLongLong();
        output.playerGameLossCount += playerCounts.value(6).toLongLong();
        squaredPlayerExposures += games * games;
    }
    if (output.playerGameCount != 2 * output.gameCount) {
        setError(errorMessage, QStringLiteral("analysis catalog player exposures do not conserve"));
        return std::nullopt;
    }
    output.largestPlayerGameSharePpm = roundedRatioPpm(
        output.largestPlayerGameCount, output.gameCount).value_or(0);
    output.largestPlayerExposureSharePpm = roundedRatioPpm(
        output.largestPlayerGameCount, output.playerGameCount).value_or(0);
    output.playerExposureHhiPpm = roundedRatioPpm(
        squaredPlayerExposures,
        output.playerGameCount * output.playerGameCount).value_or(0);

    QSqlQuery pairCounts(database);
    if (!pairCounts.exec(QStringLiteral(
            "SELECT unordered_pair_id, MIN(player_id), MAX(player_id), "
            "COUNT(DISTINCT source_game_id) "
            "FROM board_structure_player_games GROUP BY unordered_pair_id "
            "ORDER BY COUNT(DISTINCT source_game_id) DESC, "
            "MIN(player_id), MAX(player_id), unordered_pair_id"))) {
        setError(errorMessage, QStringLiteral("analysis catalog pair concentration failed"));
        return std::nullopt;
    }
    qint64 squaredPairGames = 0;
    qint64 pairGameTotal = 0;
    while (pairCounts.next()) {
        const qint64 games = pairCounts.value(3).toLongLong();
        if (output.unorderedPairCount == 0) {
            output.largestUnorderedPairId = pairCounts.value(0).toString();
            output.largestPairFirstPlayerId = pairCounts.value(1).toString();
            output.largestPairSecondPlayerId = pairCounts.value(2).toString();
            output.largestPairGameCount = games;
        }
        ++output.unorderedPairCount;
        pairGameTotal += games;
        squaredPairGames += games * games;
    }
    if (pairGameTotal != output.gameCount) {
        setError(errorMessage, QStringLiteral("analysis catalog pair games do not conserve"));
        return std::nullopt;
    }
    output.largestPairGameSharePpm = roundedRatioPpm(
        output.largestPairGameCount, output.gameCount).value_or(0);
    output.pairHhiPpm = roundedRatioPpm(
        squaredPairGames, output.gameCount * output.gameCount).value_or(0);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return output;
}

std::optional<PlayerAnalysisCatalogPlayerSummary>
PlayerAnalysisCatalog::playerSummary(
    const QString &playerId,
    QString *errorMessage) const
{
    if (!isOpen() || playerId.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("analysis catalog player query is invalid"));
        return std::nullopt;
    }
    QSqlQuery query(catalogDatabase(m_connectionName));
    query.prepare(QStringLiteral(
        "SELECT COUNT(*), "
        "SUM(pg.player_color = 'white'), SUM(pg.player_color = 'black'), "
        "SUM(pg.outcome = 'win'), SUM(pg.outcome = 'draw'), "
        "SUM(pg.outcome = 'loss'), COUNT(DISTINCT pg.opponent_id), "
        "COUNT(DISTINCT g.utc_day) "
        "FROM player_games pg JOIN games g USING (source_game_id) "
        "WHERE pg.player_id = ?"));
    query.addBindValue(playerId);
    if (!query.exec() || !query.next()) {
        setError(errorMessage, QStringLiteral("analysis catalog player summary failed"));
        return std::nullopt;
    }
    const qint64 gameCount = query.value(0).toLongLong();
    if (gameCount == 0) {
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return std::nullopt;
    }
    PlayerAnalysisCatalogPlayerSummary result;
    result.playerId = playerId;
    result.gameCount = gameCount;
    result.whiteGameCount = query.value(1).toLongLong();
    result.blackGameCount = query.value(2).toLongLong();
    result.winCount = query.value(3).toLongLong();
    result.drawCount = query.value(4).toLongLong();
    result.lossCount = query.value(5).toLongLong();
    result.distinctOpponentCount = query.value(6).toLongLong();
    result.distinctUtcDayCount = query.value(7).toLongLong();
    result.scoreRatePpm = roundedRatioPpm(
        2 * result.winCount + result.drawCount,
        2 * result.gameCount).value_or(0);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return result;
}

QVector<PlayerAnalysisCatalogHeadToHead>
PlayerAnalysisCatalog::headToHead(
    const QString &playerId,
    QString *errorMessage) const
{
    QVector<PlayerAnalysisCatalogHeadToHead> output;
    if (!isOpen() || playerId.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("analysis catalog head-to-head query is invalid"));
        return output;
    }
    QSqlQuery query(catalogDatabase(m_connectionName));
    query.prepare(QStringLiteral(
        "SELECT pg.opponent_id, bs.unordered_pair_id, COUNT(*), "
        "SUM(pg.player_color = 'white'), SUM(pg.player_color = 'black'), "
        "SUM(pg.outcome = 'win'), SUM(pg.outcome = 'draw'), "
        "SUM(pg.outcome = 'loss') "
        "FROM player_games pg JOIN board_structure_player_games bs "
        "ON bs.source_game_id = pg.source_game_id AND bs.player_id = pg.player_id "
        "WHERE pg.player_id = ? GROUP BY pg.opponent_id, bs.unordered_pair_id "
        "ORDER BY pg.opponent_id, bs.unordered_pair_id"));
    query.addBindValue(playerId);
    if (!query.exec()) {
        setError(errorMessage, QStringLiteral("analysis catalog head-to-head query failed"));
        return {};
    }
    while (query.next()) {
        PlayerAnalysisCatalogHeadToHead row;
        row.playerId = playerId;
        row.opponentId = query.value(0).toString();
        row.unorderedPairId = query.value(1).toString();
        row.gameCount = query.value(2).toLongLong();
        row.whiteGameCount = query.value(3).toLongLong();
        row.blackGameCount = query.value(4).toLongLong();
        row.winCount = query.value(5).toLongLong();
        row.drawCount = query.value(6).toLongLong();
        row.lossCount = query.value(7).toLongLong();
        row.scoreRatePpm = roundedRatioPpm(
            2 * row.winCount + row.drawCount,
            2 * row.gameCount).value_or(0);
        output.append(row);
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return output;
}

QVector<PlayerAnalysisCatalogMetricAggregate>
PlayerAnalysisCatalog::metricAggregates(
    const QString &playerId,
    const QString &category,
    QString *errorMessage) const
{
    QVector<PlayerAnalysisCatalogMetricAggregate> output;
    if (!isOpen()) {
        setError(errorMessage, QStringLiteral("analysis catalog is not open"));
        return output;
    }
    const QString playerFilter = playerId.isNull() ? QStringLiteral("") : playerId;
    const QString categoryFilter = category.isNull() ? QStringLiteral("") : category;
    QSqlDatabase database = catalogDatabase(m_connectionName);
    QHash<QString, PlayerAnalysisCatalogMetricSide> aggregates;
    QSqlQuery values(database);
    values.prepare(QStringLiteral(
        "SELECT m.metric_code, SUM(m.status = 'observed'), "
        "SUM(m.status = 'not_applicable'), "
        "SUM(CASE WHEN m.status = 'observed' THEN m.numerator ELSE 0 END), "
        "SUM(m.denominator), "
        "SUM(m.status = 'observed' AND opponent.status = 'observed'), "
        "SUM(CASE WHEN m.status = 'observed' AND opponent.status = 'observed' "
        "THEN m.value_ppm - opponent.value_ppm ELSE 0 END) "
        "FROM board_structure_measurements m "
        "JOIN board_structure_player_games pg "
        "ON pg.structural_player_game_id = m.structural_player_game_id "
        "JOIN board_structure_player_games opponent_pg "
        "ON opponent_pg.source_game_id = pg.source_game_id "
        "AND opponent_pg.player_id = pg.opponent_id "
        "JOIN board_structure_measurements opponent "
        "ON opponent.structural_player_game_id = opponent_pg.structural_player_game_id "
        "AND opponent.metric_code = m.metric_code "
        "JOIN board_structure_metric_definitions d ON d.metric_code = m.metric_code "
        "WHERE (? = '' OR pg.player_id = ?) AND (? = '' OR d.category = ?) "
        "GROUP BY m.metric_code"));
    values.addBindValue(playerFilter);
    values.addBindValue(playerFilter);
    values.addBindValue(categoryFilter);
    values.addBindValue(categoryFilter);
    if (!values.exec()) {
        setError(errorMessage, QStringLiteral("analysis catalog metric aggregate query failed"));
        return {};
    }
    while (values.next()) {
        PlayerAnalysisCatalogMetricSide side;
        side.observedPlayerGameCount = values.value(1).toLongLong();
        side.notApplicablePlayerGameCount = values.value(2).toLongLong();
        side.denominatorSum = values.value(4).toLongLong();
        if (side.denominatorSum > 0) {
            side.numeratorSum = values.value(3).toLongLong();
            side.aggregateValuePpm = roundedRatioPpm(
                *side.numeratorSum, side.denominatorSum);
        }
        side.pairedPlayerGameCount = values.value(5).toLongLong();
        if (side.pairedPlayerGameCount > 0) {
            side.meanPlayerMinusOpponentPpm = roundedSignedRatio(
                values.value(6).toLongLong(), side.pairedPlayerGameCount);
        }
        aggregates.insert(values.value(0).toString(), side);
    }

    QSqlQuery definitions(database);
    definitions.prepare(QStringLiteral(
        "SELECT metric_code, category, phase, value_semantics "
        "FROM board_structure_metric_definitions "
        "WHERE (? = '' OR category = ?) ORDER BY ordinal"));
    definitions.addBindValue(categoryFilter);
    definitions.addBindValue(categoryFilter);
    if (!definitions.exec()) {
        setError(errorMessage, QStringLiteral("analysis catalog metric definitions failed"));
        return {};
    }
    while (definitions.next()) {
        PlayerAnalysisCatalogMetricAggregate row;
        row.metricCode = definitions.value(0).toString();
        row.category = definitions.value(1).toString();
        row.phase = definitions.value(2).toString();
        row.valueSemantics = definitions.value(3).toString();
        row.aggregate = aggregates.value(row.metricCode);
        output.append(row);
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return output;
}

QVector<PlayerAnalysisCatalogMetricComparison>
PlayerAnalysisCatalog::compareMetrics(
    const QString &firstPlayerId,
    const QString &secondPlayerId,
    const QString &category,
    QString *errorMessage) const
{
    QVector<PlayerAnalysisCatalogMetricComparison> output;
    if (!isOpen() || firstPlayerId.trimmed().isEmpty()
        || secondPlayerId.trimmed().isEmpty()
        || firstPlayerId == secondPlayerId) {
        setError(errorMessage, QStringLiteral("analysis catalog comparison is invalid"));
        return output;
    }
    QSqlDatabase database = catalogDatabase(m_connectionName);
    const QString categoryFilter = category.isNull() ? QStringLiteral("") : category;
    QHash<QString, PlayerAnalysisCatalogMetricSide> aggregates;
    QSqlQuery values(database);
    values.prepare(QStringLiteral(
        "SELECT pg.player_id, m.metric_code, "
        "SUM(m.status = 'observed'), SUM(m.status = 'not_applicable'), "
        "SUM(CASE WHEN m.status = 'observed' THEN m.numerator ELSE 0 END), "
        "SUM(m.denominator) "
        "FROM board_structure_measurements m "
        "JOIN board_structure_player_games pg "
        "ON pg.structural_player_game_id = m.structural_player_game_id "
        "JOIN board_structure_metric_definitions d "
        "ON d.metric_code = m.metric_code "
        "WHERE pg.player_id IN (?, ?) AND (? = '' OR d.category = ?) "
        "GROUP BY pg.player_id, m.metric_code"));
    values.addBindValue(firstPlayerId);
    values.addBindValue(secondPlayerId);
    values.addBindValue(categoryFilter);
    values.addBindValue(categoryFilter);
    if (!values.exec()) {
        setError(errorMessage, QStringLiteral("analysis catalog metric query failed"));
        return {};
    }
    while (values.next()) {
        PlayerAnalysisCatalogMetricSide side;
        side.observedPlayerGameCount = values.value(2).toLongLong();
        side.notApplicablePlayerGameCount = values.value(3).toLongLong();
        side.denominatorSum = values.value(5).toLongLong();
        if (side.denominatorSum > 0) {
            side.numeratorSum = values.value(4).toLongLong();
            side.aggregateValuePpm = roundedRatioPpm(
                *side.numeratorSum, side.denominatorSum);
        }
        aggregates.insert(
            aggregateKey(values.value(0).toString(), values.value(1).toString()),
            side);
    }

    QSqlQuery definitions(database);
    definitions.prepare(QStringLiteral(
        "SELECT metric_code, category, phase, value_semantics "
        "FROM board_structure_metric_definitions "
        "WHERE (? = '' OR category = ?) ORDER BY ordinal"));
    definitions.addBindValue(categoryFilter);
    definitions.addBindValue(categoryFilter);
    if (!definitions.exec()) {
        setError(errorMessage, QStringLiteral("analysis catalog metric definitions failed"));
        return {};
    }
    while (definitions.next()) {
        PlayerAnalysisCatalogMetricComparison comparison;
        comparison.metricCode = definitions.value(0).toString();
        comparison.category = definitions.value(1).toString();
        comparison.phase = definitions.value(2).toString();
        comparison.valueSemantics = definitions.value(3).toString();
        comparison.first = aggregates.value(
            aggregateKey(firstPlayerId, comparison.metricCode));
        comparison.second = aggregates.value(
            aggregateKey(secondPlayerId, comparison.metricCode));
        output.append(comparison);
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return output;
}

QVector<PlayerAnalysisCatalogMeasurementGame>
PlayerAnalysisCatalog::measurementGames(
    const QString &playerId,
    const QString &metricCode,
    int maximumRows,
    QString *errorMessage) const
{
    QVector<PlayerAnalysisCatalogMeasurementGame> output;
    if (!isOpen() || playerId.trimmed().isEmpty() || metricCode.trimmed().isEmpty()
        || maximumRows < 1 || maximumRows > 5'000) {
        setError(errorMessage, QStringLiteral("analysis catalog drill-down query is invalid"));
        return output;
    }
    QSqlQuery query(catalogDatabase(m_connectionName));
    query.prepare(QStringLiteral(
        "SELECT pg.source_game_id, g.event_start_utc, pg.opponent_id, "
        "pg.player_color, perspective.outcome, g.opening_status, "
        "g.opening_eco, g.opening_name, m.status, m.numerator, "
        "m.denominator, m.value_ppm "
        "FROM board_structure_player_games pg "
        "JOIN board_structure_measurements m "
        "ON m.structural_player_game_id = pg.structural_player_game_id "
        "JOIN games g ON g.source_game_id = pg.source_game_id "
        "JOIN player_games perspective "
        "ON perspective.source_game_id = pg.source_game_id "
        "AND perspective.player_id = pg.player_id "
        "WHERE pg.player_id = ? AND m.metric_code = ? "
        "ORDER BY g.event_start_utc, pg.source_game_id LIMIT ?"));
    query.addBindValue(playerId);
    query.addBindValue(metricCode);
    query.addBindValue(maximumRows);
    if (!query.exec()) {
        setError(errorMessage, QStringLiteral("analysis catalog drill-down query failed"));
        return {};
    }
    while (query.next()) {
        PlayerAnalysisCatalogMeasurementGame row;
        row.sourceGameId = query.value(0).toString();
        row.eventStartUtc = query.value(1).toString();
        row.opponentId = query.value(2).toString();
        row.playerColor = query.value(3).toString();
        row.outcome = query.value(4).toString();
        row.openingStatus = query.value(5).toString();
        row.openingEco = query.value(6).toString();
        row.openingName = query.value(7).toString();
        row.measurementStatus = query.value(8).toString();
        if (!query.value(9).isNull()) {
            row.numerator = query.value(9).toLongLong();
        }
        row.denominator = query.value(10).toLongLong();
        if (!query.value(11).isNull()) {
            row.valuePpm = query.value(11).toLongLong();
        }
        output.append(row);
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return output;
}
