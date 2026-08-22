#include "market_grader.h"

#include <QStringList>

#include "market_hud.h"
#include "market_scoring.h"

namespace parlawl::market {

namespace {

const MarketResponse *responseForPly(const MarketAttemptAnswers &answers, int plyIndex)
{
    for (const MarketResponse &response : answers.responses) {
        if (response.plyIndex == plyIndex) {
            return &response;
        }
    }
    return nullptr;
}

const TradeLineKeyPly *keyForPly(const MarketScoringKey &key, int plyIndex)
{
    for (const TradeLineKeyPly &ply : key.line) {
        if (ply.plyIndex == plyIndex) {
            return &ply;
        }
    }
    return nullptr;
}

double hudValue(const MarketPuzzleVisible &visible, const QString &statId, double fallback)
{
    for (const MarketHudStat &stat : visible.hud) {
        if (stat.statId == statId) {
            return stat.value;
        }
    }
    return fallback;
}

//! The plan the operator actually entered, read back off the answered plies.
TradeLinePlan planFromAnswers(
    const MarketPuzzleVisible &visible,
    const MarketAttemptAnswers &answers)
{
    TradeLinePlan plan;
    for (const MarketPlySpec &spec : visible.plies) {
        const MarketResponse *response = responseForPly(answers, spec.plyIndex);
        if (response == nullptr || response->timedOut) {
            continue;
        }
        switch (spec.kind) {
        case PlyKind::Entry:
            plan.entry = response->choice;
            break;
        case PlyKind::SizeBand:
            plan.sizeBand = response->choice;
            break;
        case PlyKind::Bracket:
            if (response->bracket.has_value()) {
                plan.bracket = *response->bracket;
            }
            break;
        case PlyKind::FollowUp:
            while (plan.followUps.size() < spec.barOffset.value_or(0)) {
                plan.followUps.append(QString());
            }
            if (spec.barOffset.has_value() && *spec.barOffset >= 1) {
                plan.followUps[*spec.barOffset - 1] = response->choice;
            }
            break;
        default:
            break;
        }
    }
    return plan;
}

std::optional<double> confidenceFromAnswers(
    const MarketPuzzleVisible &visible,
    const MarketAttemptAnswers &answers)
{
    for (const MarketPlySpec &spec : visible.plies) {
        if (spec.kind != PlyKind::Confidence) {
            continue;
        }
        const MarketResponse *response = responseForPly(answers, spec.plyIndex);
        if (response != nullptr && !response->timedOut && response->confidence.has_value()) {
            return response->confidence;
        }
    }
    return std::nullopt;
}

} // namespace

QString renderKeyText(const TradeLineKeyPly &ply)
{
    if (ply.bracketKey.has_value()) {
        return QStringLiteral("stop %1 ATR / target %2 ATR")
            .arg(ply.bracketKey->stopAtr)
            .arg(ply.bracketKey->targetAtr);
    }
    return ply.categoricalKey;
}

MarketScoreCard gradeAttempt(
    const MarketPuzzleVisible &visible,
    const MarketContinuation &continuation,
    const MarketAttemptAnswers &answers)
{
    MarketScoreCard card;
    card.puzzleId = visible.puzzleId;
    card.taskKind = visible.taskKind;
    card.scoringPolicyId = marketScoringPolicyId();

    const MarketScoringKey &key = continuation.scoringKey;
    bool categoricalVerdictCorrect = false;
    bool sawCategoricalVerdict = false;

    for (const MarketPlySpec &spec : visible.plies) {
        const MarketResponse *response = responseForPly(answers, spec.plyIndex);
        const bool answered = response != nullptr && !response->timedOut;

        PlyScore score;
        score.plyIndex = spec.plyIndex;
        score.kind = spec.kind;

        switch (spec.kind) {
        case PlyKind::Confidence:
            // Confidence is Brier-scored below, not exact-matched. It has no key
            // and contributes no ply score, which is why it is reported with an
            // empty key rather than a fabricated one.
            score.keyText.clear();
            score.match = answered ? PlyMatch::Exact : PlyMatch::Unanswered;
            score.score = 0.0;
            card.plyScores.append(score);
            continue;
        case PlyKind::Label:
            score.keyText = key.correctLabel;
            score.match = matchCategorical(
                answered ? response->choice : QString(), key.correctLabel, answered);
            sawCategoricalVerdict = true;
            categoricalVerdictCorrect = score.match == PlyMatch::Exact;
            break;
        case PlyKind::Verdict:
            score.keyText = key.planted ? QStringLiteral("planted") : QStringLiteral("clean");
            score.match = matchCategorical(
                answered ? response->choice : QString(), score.keyText, answered);
            sawCategoricalVerdict = true;
            categoricalVerdictCorrect = score.match == PlyMatch::Exact;
            break;
        case PlyKind::ArtifactClass:
            score.keyText = key.artifactClass;
            if (!key.planted) {
                // A clean record has no artifact class to name; not answering is
                // the right answer, and is scored as one.
                score.match = answered ? PlyMatch::Miss : PlyMatch::Exact;
            } else {
                score.match = matchCategorical(
                    answered ? response->choice : QString(), key.artifactClass, answered);
            }
            break;
        case PlyKind::Bracket: {
            const TradeLineKeyPly *keyPly = keyForPly(key, spec.plyIndex);
            if (keyPly == nullptr || !keyPly->bracketKey.has_value()) {
                score.match = PlyMatch::Miss;
                break;
            }
            score.keyText = renderKeyText(*keyPly);
            score.match = matchBracket(
                answered ? response->bracket : std::nullopt, *keyPly->bracketKey, answered);
            break;
        }
        case PlyKind::Entry:
        case PlyKind::SizeBand:
        case PlyKind::FollowUp: {
            const TradeLineKeyPly *keyPly = keyForPly(key, spec.plyIndex);
            if (keyPly == nullptr) {
                score.match = PlyMatch::Miss;
                break;
            }
            score.keyText = renderKeyText(*keyPly);
            score.match = matchCategorical(
                answered ? response->choice : QString(), keyPly->categoricalKey, answered);
            break;
        }
        }

        score.score = scoreForMatch(score.match);
        card.plyScores.append(score);
    }

    card.lineScore.pliesTotal = 0;
    card.lineScore.pliesExact = 0;
    for (const PlyScore &score : card.plyScores) {
        if (score.kind == PlyKind::Confidence) {
            continue;
        }
        ++card.lineScore.pliesTotal;
        if (score.match == PlyMatch::Exact) {
            ++card.lineScore.pliesExact;
        }
    }

    if (visible.taskKind == TaskKind::TradeLine) {
        const double atrUnit = hudValue(visible, QStringLiteral("atr_pct_20"), 0.0);
        const auto lastVisibleClose = visible.window.bars.isEmpty()
            ? std::optional<double>()
            : visible.window.bars.last().close;
        const TradeLinePlan plan = planFromAnswers(visible, answers);
        if (lastVisibleClose.has_value()) {
            const TradeLineSimulation simulation = simulateTradeLine(
                continuation.continuation.bars,
                atrUnit,
                *lastVisibleClose,
                plan,
                visible.responseHorizonBars);
            card.lineScore.humanRMultiple = simulation.rMultiple;
        }
        if (key.lineOutcome.has_value()) {
            card.lineScore.ruleRMultiple = key.lineOutcome->rMultiple;
        }
        card.lineScore.perfectRMultiple = key.perfectRMultiple;
        card.lineScore.shortfallVsRule =
            shortfall(card.lineScore.ruleRMultiple, card.lineScore.humanRMultiple);
        card.lineScore.shortfallVsPerfect =
            shortfall(card.lineScore.perfectRMultiple, card.lineScore.humanRMultiple);
    }

    if (sawCategoricalVerdict) {
        const auto confidence = confidenceFromAnswers(visible, answers);
        if (confidence.has_value()) {
            card.brierScore = brierScore(*confidence, categoricalVerdictCorrect);
        }
    }

    card.calibration.questionId = visible.calibrationQuestion.questionId;
    card.calibration.intervalLevel = visible.calibrationQuestion.intervalLevel;
    card.calibration.realizedValue = continuation.calibrationKey.realizedValue;
    card.calibration.answered = answers.calibration.answered;
    if (answers.calibration.answered) {
        card.calibration.lower = answers.calibration.lower;
        card.calibration.upper = answers.calibration.upper;
        card.calibration.covered = intervalCovers(
            answers.calibration.lower,
            answers.calibration.upper,
            continuation.calibrationKey.realizedValue);
        card.calibration.winklerScore = winklerIntervalScore(
            answers.calibration.lower,
            answers.calibration.upper,
            continuation.calibrationKey.realizedValue,
            visible.calibrationQuestion.intervalLevel);
    }
    return card;
}

MarketReveal buildReveal(
    const MarketPuzzleVisible &visible,
    const MarketContinuation &continuation,
    const MarketAttemptAnswers &answers)
{
    MarketReveal reveal;
    reveal.puzzleId = visible.puzzleId;
    reveal.ticker = continuation.identity.ticker;
    reveal.decisionTimeUtc = continuation.identity.decisionTimeUtc;
    reveal.exchange = continuation.identity.exchange;
    reveal.instrumentClass = continuation.identity.instrumentClass;
    reveal.continuationBars = continuation.continuation.bars;
    reveal.continuationSessionBreakAfter = continuation.continuation.sessionBreakAfter;
    reveal.outcomeTheme = continuation.outcomeTheme;
    reveal.difficultyNoteBasis = continuation.difficultyNoteBasis;
    reveal.difficultyNoteValue = continuation.difficultyNoteValue;
    reveal.ruleId = continuation.scoringKey.ruleId;
    reveal.ruleDeclarationDigest = continuation.scoringKey.ruleDeclarationDigest;
    for (const TradeLineKeyPly &ply : continuation.scoringKey.line) {
        MarketRevealKeyLine line;
        line.plyIndex = ply.plyIndex;
        line.plyKindText = plyKindText(ply.kind);
        line.keyText = renderKeyText(ply);
        reveal.keyLine.append(line);
    }
    if (visible.taskKind == TaskKind::PatternCall) {
        MarketRevealKeyLine line;
        line.plyIndex = 0;
        line.plyKindText = plyKindText(PlyKind::Label);
        line.keyText = continuation.scoringKey.correctLabel;
        reveal.keyLine.append(line);
    }
    if (visible.taskKind == TaskKind::AnomalyFlag) {
        MarketRevealKeyLine verdict;
        verdict.plyIndex = 0;
        verdict.plyKindText = plyKindText(PlyKind::Verdict);
        verdict.keyText = continuation.scoringKey.planted
            ? QStringLiteral("planted")
            : QStringLiteral("clean");
        reveal.keyLine.append(verdict);
        if (continuation.scoringKey.planted) {
            MarketRevealKeyLine artifact;
            artifact.plyIndex = 1;
            artifact.plyKindText = plyKindText(PlyKind::ArtifactClass);
            artifact.keyText = continuation.scoringKey.artifactClass;
            reveal.keyLine.append(artifact);
        }
    }
    reveal.keyDisclaimer = scoringKeyDisclaimer();
    reveal.sourceWindowDigest = continuation.sourceIdentity.sourceWindowDigest;
    reveal.sourceContinuationDigest = continuation.sourceIdentity.sourceContinuationDigest;
    reveal.barsRootId = continuation.sourceIdentity.barsRootId;
    reveal.partitionPaths = continuation.sourceIdentity.partitionPaths;
    reveal.scanManifestId = visible.source.scanManifestId;
    reveal.scanCitation = visible.source.scanCitation;
    reveal.corpusDatasetVersion = visible.source.corpusDatasetVersion;
    reveal.adjustmentTableSha256 = visible.source.adjustmentTableSha256;
    reveal.scoreCard = gradeAttempt(visible, continuation, answers);
    return reveal;
}

} // namespace parlawl::market
