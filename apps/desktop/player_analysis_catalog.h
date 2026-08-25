#pragma once

#include <optional>

#include <QString>
#include <QStringList>
#include <QVector>


struct PlayerAnalysisCatalogPlayerSummary {
    QString playerId;
    qint64 gameCount = 0;
    qint64 whiteGameCount = 0;
    qint64 blackGameCount = 0;
    qint64 winCount = 0;
    qint64 drawCount = 0;
    qint64 lossCount = 0;
    qint64 scoreRatePpm = 0;
    qint64 distinctOpponentCount = 0;
    qint64 distinctUtcDayCount = 0;
};

struct PlayerAnalysisCatalogGlobalSummary {
    qint64 gameCount = 0;
    qint64 playerGameCount = 0;
    qint64 distinctPlayerCount = 0;
    qint64 unorderedPairCount = 0;
    qint64 distinctUtcDayCount = 0;
    qint64 whitePlayerGameCount = 0;
    qint64 blackPlayerGameCount = 0;
    qint64 playerGameWinCount = 0;
    qint64 playerGameDrawCount = 0;
    qint64 playerGameLossCount = 0;
    qint64 whiteWinGameCount = 0;
    qint64 drawGameCount = 0;
    qint64 blackWinGameCount = 0;
    QString largestPlayerId;
    qint64 largestPlayerGameCount = 0;
    qint64 largestPlayerGameSharePpm = 0;
    qint64 largestPlayerExposureSharePpm = 0;
    qint64 playerExposureHhiPpm = 0;
    QString largestUnorderedPairId;
    QString largestPairFirstPlayerId;
    QString largestPairSecondPlayerId;
    qint64 largestPairGameCount = 0;
    qint64 largestPairGameSharePpm = 0;
    qint64 pairHhiPpm = 0;
};

struct PlayerAnalysisCatalogHeadToHead {
    QString playerId;
    QString opponentId;
    QString unorderedPairId;
    qint64 gameCount = 0;
    qint64 whiteGameCount = 0;
    qint64 blackGameCount = 0;
    qint64 winCount = 0;
    qint64 drawCount = 0;
    qint64 lossCount = 0;
    qint64 scoreRatePpm = 0;
};

struct PlayerAnalysisCatalogMetricSide {
    qint64 observedPlayerGameCount = 0;
    qint64 notApplicablePlayerGameCount = 0;
    std::optional<qint64> numeratorSum;
    qint64 denominatorSum = 0;
    std::optional<qint64> aggregateValuePpm;
    qint64 pairedPlayerGameCount = 0;
    std::optional<qint64> meanPlayerMinusOpponentPpm;
};

struct PlayerAnalysisCatalogMetricAggregate {
    QString metricCode;
    QString category;
    QString phase;
    QString valueSemantics;
    PlayerAnalysisCatalogMetricSide aggregate;
};

struct PlayerAnalysisCatalogMetricComparison {
    QString metricCode;
    QString category;
    QString phase;
    QString valueSemantics;
    PlayerAnalysisCatalogMetricSide first;
    PlayerAnalysisCatalogMetricSide second;
};

struct PlayerAnalysisCatalogMeasurementGame {
    QString sourceGameId;
    QString eventStartUtc;
    QString opponentId;
    QString playerColor;
    QString outcome;
    QString openingStatus;
    QString openingEco;
    QString openingName;
    QString measurementStatus;
    std::optional<qint64> numerator;
    qint64 denominator = 0;
    std::optional<qint64> valuePpm;
};

class PlayerAnalysisCatalog final {
public:
    PlayerAnalysisCatalog();
    ~PlayerAnalysisCatalog();

    PlayerAnalysisCatalog(const PlayerAnalysisCatalog &) = delete;
    PlayerAnalysisCatalog &operator=(const PlayerAnalysisCatalog &) = delete;

    bool open(const QString &path, QString *errorMessage = nullptr);
    void close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString sourcePlanId() const;
    [[nodiscard]] QStringList playerIds(QString *errorMessage = nullptr) const;
    [[nodiscard]] std::optional<PlayerAnalysisCatalogGlobalSummary> globalSummary(
        QString *errorMessage = nullptr) const;
    [[nodiscard]] std::optional<PlayerAnalysisCatalogPlayerSummary> playerSummary(
        const QString &playerId,
        QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<PlayerAnalysisCatalogHeadToHead> headToHead(
        const QString &playerId,
        QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<PlayerAnalysisCatalogMetricAggregate> metricAggregates(
        const QString &playerId = QString(),
        const QString &category = QString(),
        QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<PlayerAnalysisCatalogMetricComparison> compareMetrics(
        const QString &firstPlayerId,
        const QString &secondPlayerId,
        const QString &category = QString(),
        QString *errorMessage = nullptr) const;
    [[nodiscard]] QVector<PlayerAnalysisCatalogMeasurementGame> measurementGames(
        const QString &playerId,
        const QString &metricCode,
        int maximumRows = 500,
        QString *errorMessage = nullptr) const;

private:
    QString m_connectionName;
    QString m_sourcePlanId;
};
