#pragma once

// Pure market-rep domain. No Qt Widgets, no SQL, no chess. Follows the house
// style of `libs/domain/`: Qt Core value types only.
//
// THE LOOK-AHEAD LAW: nothing declared in this header may name, hold, or reach
// a continuation. The continuation types live in `market_continuation.h`, which
// no widget translation unit is permitted to include.

#include <optional>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace parlawl::market {

// Consumer profile bounds. Stricter than the producer contract permits, on the
// same reasoning the engine-line importer uses: a permissive producer must not
// be able to force the consumer into an unbounded shape.
inline constexpr int kMinimumVisibleBars = 20;
inline constexpr int kMaximumBars = 512;
inline constexpr int kMaximumHudStats = 64;
inline constexpr int kMaximumPlies = 32;
inline constexpr int kMaximumThemes = 16;
inline constexpr qsizetype kMaximumMarketLineBytes = 1024 * 1024;
inline constexpr qsizetype kMaximumMarketPackBytes = 64 * 1024 * 1024;
inline constexpr int kMaximumMarketRecords = 100'000;
inline constexpr int kMinimumRatingSeed = 400;
inline constexpr int kMaximumRatingSeed = 3000;

enum class TaskKind {
    AnomalyFlag,
    PatternCall,
    TradeLine,
};

enum class PlyKind {
    ArtifactClass,
    Bracket,
    Confidence,
    Entry,
    FollowUp,
    Label,
    SizeBand,
    Verdict,
};

QString taskKindText(TaskKind kind);
std::optional<TaskKind> taskKindFromText(const QString &text);
QString plyKindText(PlyKind kind);
std::optional<PlyKind> plyKindFromText(const QString &text);

//! One normalized bar. A missing field is absent, never zero and never carried
//! forward — the loader-side face of `scout/loader.py`'s masking law.
struct MarketBar
{
    int barIndex = 0;
    std::optional<double> open;
    std::optional<double> high;
    std::optional<double> low;
    std::optional<double> close;
    std::optional<double> volume;
    std::optional<double> tradeCount;

    [[nodiscard]] bool hasOhlc() const
    {
        return open.has_value() && high.has_value() && low.has_value() && close.has_value();
    }
};

struct MarketWindow
{
    QString grain;
    int barCount = 0;
    QVector<MarketBar> bars;
    QVector<bool> sessionBreakAfter;
    QString windowDigest;
};

struct MarketHudStat
{
    QString statId;
    double value = 0.0;
    QString unit;
};

struct BracketChoice
{
    double stopAtr = 0.0;
    double targetAtr = 0.0;

    [[nodiscard]] bool operator==(const BracketChoice &other) const
    {
        return stopAtr == other.stopAtr && targetAtr == other.targetAtr;
    }
};

struct MarketPlySpec
{
    int plyIndex = 0;
    PlyKind kind = PlyKind::Entry;
    std::optional<int> barOffset;
};

struct MarketCalibrationQuestion
{
    QString questionId;
    QString quantity;
    QString unit;
    double intervalLevel = 0.8;
    double lowerBound = 0.0;
    double upperBound = 0.0;
};

struct MarketTaskSpec
{
    QStringList patternLabels;
    QStringList entries;
    QStringList sizeBands;
    QVector<double> stopAtrMultiples;
    QVector<double> targetAtrMultiples;
    QStringList followUpActions;
    QStringList artifactClasses;
};

struct MarketPackSource
{
    QString scanManifestId;
    QString scanCitation;
    QString corpusDatasetVersion;
    QString adjustmentTableSha256;
    QString grain;
};

struct MarketPackHeader
{
    QString packId;
    QString continuationPackId;
    QString compilerId;
    QString compilerVersion;
    QString gitCommit;
    bool gitDirty = false;
    QString builtAtUtc;
    int recordCount = 0;
    int anomalyFlagCount = 0;
    int patternCallCount = 0;
    int tradeLineCount = 0;
    QString grain;
    int visibleBarCount = 0;
    int continuationBarCount = 0;
    int responseHorizonBars = 0;
    double priceAnchor = 100.0;
    int priceDecimals = 6;
    QString volumeBasis;
    int volumeDecimals = 6;
    QString symbolScheme;
    bool calendarDisclosed = false;
    bool sessionBreaksDisclosed = false;
    QStringList themes;
    MarketTaskSpec taskSpec;
    QStringList hudStats;
    QStringList verifiedHudStats;
    MarketCalibrationQuestion calibration;
    QString ratingBasisId;
    int ratingBandMinimum = kMinimumRatingSeed;
    int ratingBandMaximum = kMaximumRatingSeed;
    MarketPackSource source;
    QString evidenceGrade;
};

//! What a panel is allowed to hold while the operator is deciding.
//!
//! There is no continuation member here, at any depth. The absence is the
//! guarantee: a debug tooltip cannot leak a future that the type cannot name.
struct MarketPuzzleVisible
{
    QString puzzleId;
    QString recordId;
    QString continuationCommitment;
    TaskKind taskKind = TaskKind::TradeLine;
    QString theme;
    int ratingSeed = 1500;
    QString ratingSeedBasis;
    QString displaySymbol;
    MarketWindow window;
    QVector<MarketHudStat> hud;
    int responseHorizonBars = 0;
    QVector<MarketPlySpec> plies;
    MarketCalibrationQuestion calibrationQuestion;
    MarketPackSource source;
    //! The exact canonical visible line as read from disk, retained so the
    //! journal can bind an attempt to the bytes the operator actually saw.
    QByteArray canonicalLine;
};

//! One answered (or expired) question.
struct MarketResponse
{
    int plyIndex = 0;
    PlyKind kind = PlyKind::Entry;
    std::optional<int> barOffset;
    QString choice;
    std::optional<BracketChoice> bracket;
    std::optional<double> confidence;
    qint64 exposedAtMs = 0;
    qint64 answeredAtMs = 0;
    bool timedOut = false;

    [[nodiscard]] qint64 latencyMs() const { return answeredAtMs - exposedAtMs; }
};

struct MarketCalibrationAnswer
{
    QString questionId;
    double lower = 0.0;
    double upper = 0.0;
    double intervalLevel = 0.8;
    qint64 exposedAtMs = 0;
    qint64 answeredAtMs = 0;
    bool answered = false;

    [[nodiscard]] qint64 latencyMs() const { return answeredAtMs - exposedAtMs; }
};

//! Re-emit the bars ParlAWL parsed and hash them, so the digest a result cites
//! is a fact about what ParlAWL held rather than an echo of the record's claim.
QString marketBarsDigest(const QVector<MarketBar> &bars);

//! Digest over the HUD array as ParlAWL holds it, for the results contract's
//! `displayed.hud_digest` — "the operator decided on these numbers".
QString marketHudDigest(const QVector<MarketHudStat> &hud);

struct MarketAttemptAnswers
{
    QVector<MarketResponse> responses;
    MarketCalibrationAnswer calibration;
};

//! The sentence every surface that shows a scoring key must carry, verbatim.
inline QString scoringKeyDisclaimer()
{
    return QStringLiteral(
        "The scoring key is the declared rule's line replayed on the continuation. "
        "It is not the right answer, not the optimal answer, and not evidence that "
        "the rule has an edge.");
}

inline QString marketPackEvidenceGrade()
{
    return QStringLiteral(
        "NOT EVIDENCE. A puzzle pack is training material compiled from the corpus. "
        "Its scoring key is a DECLARED RULE'S line replayed on the continuation, not "
        "the optimal line and not a claim that the rule has an edge.");
}

inline QString ratingSeedDisclaimer()
{
    return QStringLiteral(
        "Producer-declared seed, not a measured difficulty and not a solver rating.");
}

} // namespace parlawl::market
