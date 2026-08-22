#include "market_scoring.h"

#include <algorithm>
#include <cmath>

namespace parlawl::market {

QString plyMatchText(PlyMatch match)
{
    switch (match) {
    case PlyMatch::Exact:
        return QStringLiteral("exact");
    case PlyMatch::Miss:
        return QStringLiteral("miss");
    case PlyMatch::Unanswered:
        return QStringLiteral("unanswered");
    }
    return {};
}

double brierScore(double probability, bool outcome)
{
    const double target = outcome ? 1.0 : 0.0;
    const double difference = probability - target;
    return difference * difference;
}

bool intervalCovers(double lower, double upper, double realized)
{
    return realized >= lower && realized <= upper;
}

double winklerIntervalScore(double lower, double upper, double realized, double intervalLevel)
{
    const double width = upper - lower;
    const double alpha = 1.0 - intervalLevel;
    if (alpha <= 0.0) {
        // A 100% interval has no miss penalty to scale; the score is its width.
        return width;
    }
    const double penaltyScale = 2.0 / alpha;
    if (realized < lower) {
        return width + penaltyScale * (lower - realized);
    }
    if (realized > upper) {
        return width + penaltyScale * (realized - upper);
    }
    return width;
}

PlyMatch matchCategorical(const QString &response, const QString &key, bool answered)
{
    if (!answered) {
        return PlyMatch::Unanswered;
    }
    return response == key ? PlyMatch::Exact : PlyMatch::Miss;
}

PlyMatch matchBracket(
    const std::optional<BracketChoice> &response,
    const BracketChoice &key,
    bool answered)
{
    if (!answered || !response.has_value()) {
        return PlyMatch::Unanswered;
    }
    return *response == key ? PlyMatch::Exact : PlyMatch::Miss;
}

double scoreForMatch(PlyMatch match)
{
    return match == PlyMatch::Exact ? 1.0 : 0.0;
}

std::optional<double> shortfall(
    const std::optional<double> &reference,
    const std::optional<double> &achieved)
{
    if (!reference.has_value() || !achieved.has_value()) {
        return std::nullopt;
    }
    return *reference - *achieved;
}

double sizeBandMultiplier(const QString &sizeBand)
{
    if (sizeBand == QStringLiteral("0.25R")) {
        return 0.25;
    }
    if (sizeBand == QStringLiteral("0.5R")) {
        return 0.5;
    }
    if (sizeBand == QStringLiteral("1R")) {
        return 1.0;
    }
    return 0.0;
}

TradeLineSimulation simulateTradeLine(
    const QVector<MarketBar> &continuationBars,
    double atrUnit,
    double entryPrice,
    const TradeLinePlan &plan,
    int horizonBars)
{
    TradeLineSimulation result;
    const double size = sizeBandMultiplier(plan.sizeBand);
    const bool isLong = plan.entry == QStringLiteral("long");
    const bool isShort = plan.entry == QStringLiteral("short");
    if ((!isLong && !isShort) || size <= 0.0 || atrUnit <= 0.0 || plan.bracket.stopAtr <= 0.0) {
        // `pass` is a real answer and scores as a real answer: flat, zero R.
        result.rMultiple = 0.0;
        result.exitReason = QStringLiteral("no_position");
        return result;
    }

    const double direction = isLong ? 1.0 : -1.0;
    const double riskPerUnit = plan.bracket.stopAtr * atrUnit;
    double stopPrice = entryPrice - direction * riskPerUnit;
    const double targetPrice = entryPrice + direction * plan.bracket.targetAtr * atrUnit;

    const int lastBar = std::min(horizonBars, static_cast<int>(continuationBars.size()));
    for (int index = 0; index < lastBar; ++index) {
        const MarketBar &bar = continuationBars.at(index);
        if (!bar.hasOhlc()) {
            // A halted bar cannot fill anything; carry the position, do not
            // invent a price.
            continue;
        }
        const int barOffset = index + 1;
        const bool stopHit = isLong ? (*bar.low <= stopPrice) : (*bar.high >= stopPrice);
        if (stopHit) {
            result.rMultiple = direction * (stopPrice - entryPrice) / riskPerUnit * size;
            result.exitReason = QStringLiteral("stop");
            result.exitBarOffset = barOffset;
            return result;
        }
        const bool targetHit = isLong ? (*bar.high >= targetPrice) : (*bar.low <= targetPrice);
        if (targetHit) {
            result.rMultiple = direction * (targetPrice - entryPrice) / riskPerUnit * size;
            result.exitReason = QStringLiteral("target");
            result.exitBarOffset = barOffset;
            return result;
        }

        const QString action = plan.followUps.value(index);
        if (action == QStringLiteral("exit")) {
            result.rMultiple = direction * (*bar.close - entryPrice) / riskPerUnit * size;
            result.exitReason = QStringLiteral("follow_up_exit");
            result.exitBarOffset = barOffset;
            return result;
        }
        if (action == QStringLiteral("tighten")) {
            // Half the remaining risk, never loosened.
            const double tightened = *bar.close - direction * 0.5 * riskPerUnit;
            stopPrice = isLong ? std::max(stopPrice, tightened) : std::min(stopPrice, tightened);
        }
    }

    for (int index = lastBar - 1; index >= 0; --index) {
        const MarketBar &bar = continuationBars.at(index);
        if (!bar.hasOhlc()) {
            continue;
        }
        result.rMultiple = direction * (*bar.close - entryPrice) / riskPerUnit * size;
        result.exitReason = QStringLiteral("horizon");
        result.exitBarOffset = index + 1;
        return result;
    }

    result.rMultiple = 0.0;
    result.exitReason = QStringLiteral("no_position");
    return result;
}

} // namespace parlawl::market
