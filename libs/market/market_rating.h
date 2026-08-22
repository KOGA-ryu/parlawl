#pragma once

// ParlAWL had no rating engine at all before this: `PuzzleMetadata::rating` is a
// display integer, imported chess records set it to 0 with `ratingHidden`, and
// `ratingRangeFor()` buckets the source game's players rather than the solver.
// So this is new machinery, and it is deliberately small.
//
// What it is: a local, provisional Elo-style number for one operator, updated
// against the pack's producer-declared seed. What it is not: a measurement of
// the market, a difficulty score for the puzzle, or anything Arc should treat as
// authoritative. Arc measures real difficulty later, from ingested results.

#include "market_score_card.h"

namespace parlawl::market {

struct MarketSolverRating
{
    double rating = 1500.0;
    int reps = 0;
};

//! Logistic expectation, 400-point scale, as Elo.
double expectedScore(double solverRating, double seedRating);

//! Provisional while the sample is thin, then progressively stickier.
double kFactorForReps(int reps);

//! `repScore` is clamped into [0, 1]; anything else is a caller bug, not a
//! licence to move the rating further than a rep can.
MarketSolverRating updateSolverRating(
    const MarketSolverRating &current,
    int ratingSeed,
    double repScore);

//! Fraction of scorable plies answered exactly. Confidence plies are excluded
//! because they are Brier-scored, not exact-matched.
double repScoreFromCard(const MarketScoreCard &card);

} // namespace parlawl::market
