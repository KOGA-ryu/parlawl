#include "market_hud.h"

#include <algorithm>
#include <cmath>

namespace parlawl::market {

namespace {

constexpr int kPeriod = 20;

std::optional<double> closeAt(const QVector<MarketBar> &bars, int index)
{
    if (index < 0 || index >= bars.size()) {
        return std::nullopt;
    }
    return bars.at(index).close;
}

} // namespace

QStringList verifiedHudStatIds()
{
    return {
        QStringLiteral("atr_pct_20"),
        QStringLiteral("range_position_20"),
        QStringLiteral("return_20"),
        QStringLiteral("return_5"),
    };
}

std::optional<double> simpleReturnPercent(const QVector<MarketBar> &bars, int lookback)
{
    const int last = bars.size() - 1;
    const auto latest = closeAt(bars, last);
    const auto earlier = closeAt(bars, last - lookback);
    if (!latest.has_value() || !earlier.has_value() || *earlier == 0.0) {
        return std::nullopt;
    }
    return 100.0 * (*latest / *earlier - 1.0);
}

std::optional<double> atrPercent20(const QVector<MarketBar> &bars)
{
    // Wilder's SEED ATR over the LAST 20 bars, which is what the contract's
    // "Wilder ATR(20) over the last 20 bars" names: the arithmetic mean of
    // TR[N-20 .. N-1]. TR[i] needs close[i - 1], so the read spans 21 bars and
    // no more.
    //
    // The forward recursion — seed on the window's first 20 true ranges, then
    // smooth to the end — is the other reading, and it is the wrong one twice
    // over. It makes the value depend on how much history the window happened
    // to carry, which is precisely the dependence contract D3 removes from the
    // visible surface; and it forces the whole window to be unmasked, so a
    // single absent bar 40 places behind T would refuse a record the producer
    // accepted. Measured against Arc's two compiled packs, the recursion
    // reproduced 0 of 674 shipped values and this mean reproduced all 674 to
    // 5e-7.
    if (bars.size() < kPeriod + 1) {
        return std::nullopt;
    }
    const int first = bars.size() - kPeriod;
    double total = 0.0;
    for (int index = first; index < bars.size(); ++index) {
        const MarketBar &bar = bars.at(index);
        const auto previousClose = bars.at(index - 1).close;
        if (!bar.high.has_value() || !bar.low.has_value() || !previousClose.has_value()) {
            return std::nullopt;
        }
        const double range = *bar.high - *bar.low;
        const double highGap = std::fabs(*bar.high - *previousClose);
        const double lowGap = std::fabs(*bar.low - *previousClose);
        total += std::max(range, std::max(highGap, lowGap));
    }
    const double average = total / static_cast<double>(kPeriod);

    const auto lastClose = closeAt(bars, bars.size() - 1);
    if (!lastClose.has_value() || *lastClose == 0.0) {
        return std::nullopt;
    }
    return 100.0 * average / *lastClose;
}

std::optional<double> rangePosition20(const QVector<MarketBar> &bars)
{
    if (bars.size() < kPeriod) {
        return std::nullopt;
    }
    double lowest = 0.0;
    double highest = 0.0;
    bool seeded = false;
    for (int index = bars.size() - kPeriod; index < bars.size(); ++index) {
        const MarketBar &bar = bars.at(index);
        if (!bar.high.has_value() || !bar.low.has_value()) {
            return std::nullopt;
        }
        if (!seeded) {
            lowest = *bar.low;
            highest = *bar.high;
            seeded = true;
            continue;
        }
        lowest = std::min(lowest, *bar.low);
        highest = std::max(highest, *bar.high);
    }
    const auto lastClose = closeAt(bars, bars.size() - 1);
    if (!seeded || !lastClose.has_value() || highest <= lowest) {
        return std::nullopt;
    }
    return (*lastClose - lowest) / (highest - lowest);
}

std::optional<double> computeVerifiedHudStat(const QString &statId, const QVector<MarketBar> &bars)
{
    if (statId == QStringLiteral("return_5")) {
        return simpleReturnPercent(bars, 5);
    }
    if (statId == QStringLiteral("return_20")) {
        return simpleReturnPercent(bars, 20);
    }
    if (statId == QStringLiteral("atr_pct_20")) {
        return atrPercent20(bars);
    }
    if (statId == QStringLiteral("range_position_20")) {
        return rangePosition20(bars);
    }
    return std::nullopt;
}

} // namespace parlawl::market
