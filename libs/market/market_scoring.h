#pragma once

// Per-ply and calibration arithmetic, expressed over plain values so it can be
// tested without a continuation anywhere in scope. `market_grader.h` composes
// these against a sealed key.

#include <optional>

#include <QString>

#include "market_score_card.h"
#include "market_types.h"

namespace parlawl::market {

//! Brier score for one categorical forecast: (p - outcome)^2, lower is better.
double brierScore(double probability, bool outcome);

//! Winkler interval score at level `intervalLevel` (e.g. 0.8 for an 80% band).
//! Width, plus a 2/alpha penalty per unit of miss. Lower is better.
double winklerIntervalScore(double lower, double upper, double realized, double intervalLevel);

bool intervalCovers(double lower, double upper, double realized);

//! Exact-match grading of one categorical ply.
PlyMatch matchCategorical(const QString &response, const QString &key, bool answered);

//! Exact-match grading of a bracket ply. ATR multiples come off a declared grid,
//! so equality is the right comparison and a tolerance would only hide a bug.
PlyMatch matchBracket(
    const std::optional<BracketChoice> &response,
    const BracketChoice &key,
    bool answered);

double scoreForMatch(PlyMatch match);

//! Implementation shortfall against the two reference lines. Absent inputs
//! produce absent shortfalls rather than a zero that reads as "no shortfall".
std::optional<double> shortfall(
    const std::optional<double> &reference,
    const std::optional<double> &achieved);

//! One operator's (or the declared rule's) trade line, expressed in the pack's
//! own vocabulary so the simulator never sees a dollar.
struct TradeLinePlan
{
    QString entry;              //!< "long" | "short" | "pass"
    QString sizeBand;           //!< "0" | "0.25R" | "0.5R" | "1R"
    BracketChoice bracket;
    //! Follow-up action per horizon bar, index 0 == bar_offset 1.
    QStringList followUps;
};

struct TradeLineSimulation
{
    std::optional<double> rMultiple;
    QString exitReason;         //!< target | stop | follow_up_exit | horizon | no_position
    int exitBarOffset = 0;
};

double sizeBandMultiplier(const QString &sizeBand);

//! Replay a plan across the bars after T.
//!
//! `atrUnit` is one ATR in the window's normalized price units and `entryPrice`
//! is the close at T. Within a bar the stop is checked before the target, which
//! is the pessimistic reading of an OHLC bar and the only honest one when the
//! intra-bar path is unknown.
TradeLineSimulation simulateTradeLine(
    const QVector<MarketBar> &continuationBars,
    double atrUnit,
    double entryPrice,
    const TradeLinePlan &plan,
    int horizonBars);

} // namespace parlawl::market
