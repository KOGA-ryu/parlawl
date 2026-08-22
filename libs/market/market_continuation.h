#pragma once

// ============================================================================
// THE FUTURE. NOT FOR THE UI.
//
// No translation unit under `apps/desktop/` may include this header, and
// `tests/unit/test_unit_market_continuation_segregation.cpp` fails the build's
// test suite if one does. The look-ahead law is enforced here by absence: the
// types below are not reachable from `MarketPuzzleVisible`, are owned only by
// `SealedContinuationVault`, and leave the vault only as the `MarketReveal`
// disclosure in `market_reveal.h`, which is minted after a committed terminal
// event.
// ============================================================================

#include <optional>

#include <QString>
#include <QStringList>
#include <QVector>

#include "market_types.h"

namespace parlawl::market {

struct RevealIdentity
{
    QString ticker;
    QString decisionTimeUtc;
    QString exchange;
    QString instrumentClass;
};

struct SourceIdentity
{
    QString sourceWindowDigest;
    QString sourceContinuationDigest;
    QString barsRootId;
    QStringList partitionPaths;
};

struct TradeLineKeyPly
{
    int plyIndex = 0;
    PlyKind kind = PlyKind::Entry;
    std::optional<int> barOffset;
    QString categoricalKey;
    std::optional<BracketChoice> bracketKey;
};

struct TradeLineOutcome
{
    double rMultiple = 0.0;
    QString exitReason;
    int exitBarOffset = 0;
};

struct MarketScoringKey
{
    TaskKind taskKind = TaskKind::TradeLine;

    // pattern_call
    QString correctLabel;
    QString labelDerivation;

    // trade_line
    QString ruleId;
    QString ruleDeclarationDigest;
    QVector<TradeLineKeyPly> line;
    std::optional<TradeLineOutcome> lineOutcome;
    std::optional<double> perfectRMultiple;
    QString perfectNote;

    // anomaly_flag
    bool planted = false;
    QString artifactClass;
    QStringList injectionBars;
    QString injectionTransform;
    std::optional<double> injectionMagnitude;

    //! What the compiler's screen actually checked, for a `planted: false`
    //! record. Empty on a planted one.
    //!
    //! Contract §5.2 says only that this "names the quality findings the
    //! compiler checked to assert cleanliness", and the producer answers with
    //! an object rather than a list, because a list cannot carry the second
    //! half of the answer: `cleanLimit` is the sentence saying what passing the
    //! screen does NOT establish. Any surface that calls a record clean has to
    //! show that sentence with it, the same way a scoring key never appears
    //! without `scoringKeyDisclaimer()`.
    QString cleanScreenId;
    QString cleanLimit;
    //! The screen's terms, retained as the canonical JSON that arrived, so the
    //! reveal can print the thresholds it was actually judged against without
    //! this header having to name a check set that will grow.
    QString cleanChecksJson;
};

struct MarketCalibrationKey
{
    QString questionId;
    double realizedValue = 0.0;
    QString derivation;
};

struct MarketContinuation
{
    QString puzzleId;
    QString recordId;
    QString continuationCommitment;
    RevealIdentity identity;
    MarketWindow continuation;
    MarketScoringKey scoringKey;
    MarketCalibrationKey calibrationKey;
    QString outcomeTheme;
    QString difficultyNoteBasis;
    std::optional<double> difficultyNoteValue;
    SourceIdentity sourceIdentity;
};

} // namespace parlawl::market
