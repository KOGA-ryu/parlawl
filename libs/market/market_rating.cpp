#include "market_rating.h"

#include <algorithm>
#include <cmath>

namespace parlawl::market {

double expectedScore(double solverRating, double seedRating)
{
    return 1.0 / (1.0 + std::pow(10.0, (seedRating - solverRating) / 400.0));
}

double kFactorForReps(int reps)
{
    if (reps < 30) {
        return 40.0;
    }
    if (reps < 100) {
        return 20.0;
    }
    return 10.0;
}

MarketSolverRating updateSolverRating(
    const MarketSolverRating &current,
    int ratingSeed,
    double repScore)
{
    const double clamped = std::clamp(repScore, 0.0, 1.0);
    const double expected = expectedScore(current.rating, static_cast<double>(ratingSeed));
    MarketSolverRating updated;
    updated.reps = current.reps + 1;
    updated.rating = current.rating + kFactorForReps(current.reps) * (clamped - expected);
    return updated;
}

double repScoreFromCard(const MarketScoreCard &card)
{
    if (card.lineScore.pliesTotal <= 0) {
        return 0.0;
    }
    return static_cast<double>(card.lineScore.pliesExact)
        / static_cast<double>(card.lineScore.pliesTotal);
}

} // namespace parlawl::market
