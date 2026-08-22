#pragma once

// The four HUD stats ParlAWL recomputes from the visible bars rather than
// trusting. A wrong HUD number does not sit inertly in a detail panel here — it
// is what the operator decides on, so it trains a reflex against a number
// nobody checked.

#include <optional>

#include <QStringList>

#include "market_types.h"

namespace parlawl::market {

inline constexpr double kHudVerificationTolerance = 1e-6;

//! Stat ids ParlAWL derives independently. Everything else in `hud_spec.stats`
//! is displayed as producer-supplied.
QStringList verifiedHudStatIds();

//! Recompute one verified stat over the normalized visible window.
//! Returns nullopt when the window cannot support the derivation (too short, or
//! a null close inside the lookback, or a degenerate 20-bar range).
std::optional<double> computeVerifiedHudStat(const QString &statId, const QVector<MarketBar> &bars);

//! Wilder ATR(20) as a percentage of the last close.
std::optional<double> atrPercent20(const QVector<MarketBar> &bars);

std::optional<double> simpleReturnPercent(const QVector<MarketBar> &bars, int lookback);

std::optional<double> rangePosition20(const QVector<MarketBar> &bars);

} // namespace parlawl::market
