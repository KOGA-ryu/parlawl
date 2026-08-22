#pragma once

// The history lesson. This is the only shape in which anything after T reaches
// a widget, and it exists only once a terminal event is committed.
//
// It deliberately does NOT carry `MarketScoringKey`. The key is rendered here
// as display text beside `scoringKeyDisclaimer()`, so a surface cannot show the
// rule's line without the sentence that says what the line is not.

#include <optional>

#include <QString>
#include <QStringList>
#include <QVector>

#include "market_score_card.h"
#include "market_types.h"

namespace parlawl::market {

struct MarketRevealKeyLine
{
    int plyIndex = 0;
    QString plyKindText;
    QString keyText;
};

struct MarketReveal
{
    QString puzzleId;

    // Identity, disclosed only now.
    QString ticker;
    QString decisionTimeUtc;
    QString exchange;
    QString instrumentClass;

    // The bars after T, for stepping in study mode.
    QVector<MarketBar> continuationBars;
    QVector<bool> continuationSessionBreakAfter;

    QString outcomeTheme;
    QString difficultyNoteBasis;
    std::optional<double> difficultyNoteValue;

    // The declared rule's line, as text, always shown with the disclaimer.
    QString ruleId;
    QString ruleDeclarationDigest;
    QVector<MarketRevealKeyLine> keyLine;
    QString keyDisclaimer;

    // Citation chain, identifying half.
    QString sourceWindowDigest;
    QString sourceContinuationDigest;
    QString barsRootId;
    QStringList partitionPaths;

    // Pack-level, non-identifying half, carried through for one printable chain.
    QString scanManifestId;
    QString scanCitation;
    QString corpusDatasetVersion;
    QString adjustmentTableSha256;

    MarketScoreCard scoreCard;
};

} // namespace parlawl::market
