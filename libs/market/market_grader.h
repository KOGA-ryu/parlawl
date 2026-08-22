#pragma once

// Grading composes a sealed key with what the operator answered. It therefore
// names `MarketContinuation`, and therefore lives on the sealed side of the
// wall: no widget translation unit may include this header.

#include "market_continuation.h"
#include "market_reveal.h"
#include "market_score_card.h"
#include "market_types.h"

namespace parlawl::market {

MarketScoreCard gradeAttempt(
    const MarketPuzzleVisible &visible,
    const MarketContinuation &continuation,
    const MarketAttemptAnswers &answers);

//! Build the disclosure a surface may hold, once a terminal is committed.
MarketReveal buildReveal(
    const MarketPuzzleVisible &visible,
    const MarketContinuation &continuation,
    const MarketAttemptAnswers &answers);

QString renderKeyText(const TradeLineKeyPly &ply);

} // namespace parlawl::market
