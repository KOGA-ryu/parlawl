#pragma once

// The graded result of one rep, in a shape a widget may hold. It is produced
// only at reveal time, and it carries no bars from the future beyond the ones
// the reveal deliberately discloses.

#include <optional>

#include <QString>
#include <QVector>

#include "market_types.h"

namespace parlawl::market {

enum class PlyMatch {
    Exact,
    Miss,
    Unanswered,
};

QString plyMatchText(PlyMatch match);

struct PlyScore
{
    int plyIndex = 0;
    PlyKind kind = PlyKind::Entry;
    //! The declared rule's key for this ply, rendered for display. Never
    //! labelled "correct" — see `scoringKeyDisclaimer()`.
    QString keyText;
    PlyMatch match = PlyMatch::Miss;
    double score = 0.0;
};

struct LineScore
{
    int pliesExact = 0;
    int pliesTotal = 0;
    std::optional<double> humanRMultiple;
    std::optional<double> ruleRMultiple;
    std::optional<double> perfectRMultiple;
    std::optional<double> shortfallVsRule;
    std::optional<double> shortfallVsPerfect;
};

struct CalibrationScore
{
    QString questionId;
    double lower = 0.0;
    double upper = 0.0;
    double intervalLevel = 0.8;
    double realizedValue = 0.0;
    bool answered = false;
    bool covered = false;
    double winklerScore = 0.0;
};

struct MarketScoreCard
{
    QString puzzleId;
    TaskKind taskKind = TaskKind::TradeLine;
    QVector<PlyScore> plyScores;
    LineScore lineScore;
    CalibrationScore calibration;
    std::optional<double> brierScore;
    QString scoringPolicyId;
};

inline QString marketScoringPolicyId()
{
    return QStringLiteral("parlawl-market-scoring-v1");
}

inline QString marketCalibrationPolicyId()
{
    return QStringLiteral("parlawl-market-calibration-v1");
}

} // namespace parlawl::market
