#include "report_formatter.h"

#include "opening_resolution.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString cpField(bool hasValue, int value)
{
    return hasValue ? QString::number(value) : QStringLiteral("n/a");
}

QString candidateScoreDisplay(const QJsonObject &candidate)
{
    auto signedNumber = [](int value) {
        return value >= 0 ? QStringLiteral("+%1").arg(value) : QString::number(value);
    };
    const QString scoreKind = candidate.value(QStringLiteral("score_kind")).toString();
    if (scoreKind == QStringLiteral("mate")) {
        const QJsonValue mateDistanceValue = candidate.value(QStringLiteral("mate_distance"));
        if (mateDistanceValue.isDouble()) {
            return QStringLiteral("mate %1").arg(signedNumber(mateDistanceValue.toInt()));
        }
    }
    const QJsonValue evalValue = candidate.value(QStringLiteral("eval_cp"));
    if (evalValue.isDouble()) {
        return QStringLiteral("cp %1").arg(signedNumber(evalValue.toInt()));
    }
    return QStringLiteral("n/a");
}

QString candidateDisplayCompact(const QJsonObject &candidate)
{
    const QString compact = candidate.value(QStringLiteral("candidate_display_compact")).toString();
    if (!compact.isEmpty()) {
        return compact;
    }

    QStringList tags;
    if (candidate.value(QStringLiteral("is_best_move")).toBool()) {
        tags.append(QStringLiteral("best"));
    }
    if (candidate.value(QStringLiteral("is_played_move")).toBool()) {
        tags.append(QStringLiteral("played"));
    }
    const QString tagFragment = tags.isEmpty() ? QString() : QStringLiteral(" | %1").arg(tags.join(QStringLiteral(",")));
    return QStringLiteral("%1. %2 | %3%4")
        .arg(candidate.value(QStringLiteral("rank")).toInt())
        .arg(candidate.value(QStringLiteral("move_san")).toString(candidate.value(QStringLiteral("move_uci")).toString()))
        .arg(candidateScoreDisplay(candidate))
        .arg(tagFragment);
}

QStringList candidateDisplayLines(const QString &candidateMovesJson)
{
    const QJsonDocument document = QJsonDocument::fromJson(candidateMovesJson.toUtf8());
    if (!document.isArray()) {
        return {};
    }

    QStringList lines;
    const QJsonArray candidates = document.array();
    for (const QJsonValue &value : candidates) {
        if (!value.isObject()) {
            continue;
        }
        lines.append(candidateDisplayCompact(value.toObject()));
    }
    return lines;
}

} // namespace

QString ReportFormatter::formatAnalysisReport(
    const PuzzleRound &puzzleRound,
    const SourceGame &sourceGame,
    const TacticalEvent &tacticalEvent,
    const QList<CriticalMove> &criticalMoves
)
{
    QString report;
    report += QStringLiteral("Extracted Evidence\n");
    report += QStringLiteral("==================\n");
    report += QStringLiteral("puzzle id: %1\n").arg(puzzleRound.puzzleId);
    report += QStringLiteral("source game id: %1\n").arg(sourceGame.sourceGameId);
    report += QStringLiteral(
        "players: %1 (%2) vs %3 (%4)\n"
    ).arg(puzzleRound.whitePlayer)
         .arg(puzzleRound.whiteRating)
         .arg(puzzleRound.blackPlayer)
         .arg(puzzleRound.blackRating);
    report += QStringLiteral("time control: %1\n").arg(puzzleRound.timeControl);
    report += QStringLiteral("mapped start ply: %1\n").arg(tacticalEvent.mappedStartPly);
    report += QStringLiteral("mapping status: %1\n").arg(tacticalEvent.mappingStatus);
    report += QStringLiteral("mapping method: %1\n").arg(tacticalEvent.mappingMethod);
    report += QStringLiteral("mapping confidence: %1\n").arg(tacticalEvent.mappingConfidence);
    report += QStringLiteral("mapping notes: %1\n").arg(
        tacticalEvent.mappingNotes.isEmpty() ? QStringLiteral("none") : tacticalEvent.mappingNotes
    );
    const OpeningResolution opening = resolveOpeningDisplay(sourceGame, tacticalEvent);
    report += QStringLiteral("opening candidate: %1\n").arg(opening.display);
    report += QStringLiteral("opening family candidate: %1\n").arg(
        tacticalEvent.openingFamily.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.openingFamily
    );
    report += QStringLiteral("game phase candidate: %1\n").arg(tacticalEvent.gamePhase);
    report += QStringLiteral(
        "analysis window: ply %1 to %2\n"
    ).arg(tacticalEvent.analysisWindowStartPly).arg(tacticalEvent.analysisWindowEndPly);
    report += QStringLiteral("primary break ply: %1\n").arg(
        tacticalEvent.primaryBreakPly > 0 ? QString::number(tacticalEvent.primaryBreakPly) : QStringLiteral("n/a")
    );
    report += QStringLiteral("primary break reason: %1\n").arg(
        tacticalEvent.primaryBreakReason.isEmpty() ? QStringLiteral("none") : tacticalEvent.primaryBreakReason
    );
    report += QStringLiteral("retained break summary: %1\n").arg(
        tacticalEvent.retainedBreakSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.retainedBreakSummary
    );
    report += QStringLiteral("divergence type: %1\n").arg(
        tacticalEvent.divergenceType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.divergenceType
    );
    report += QStringLiteral("divergence severity: %1\n").arg(
        tacticalEvent.divergenceSeverity.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.divergenceSeverity
    );
    report += QStringLiteral("compact divergence: %1\n").arg(
        tacticalEvent.divergenceCompactSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.divergenceCompactSummary
    );
    report += QStringLiteral("best-vs-played divergence: %1\n").arg(
        tacticalEvent.bestVsPlayedDivergenceSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.bestVsPlayedDivergenceSummary
    );
    report += QStringLiteral("local sequence confidence: %1\n").arg(
        tacticalEvent.localSequenceConfidence.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.localSequenceConfidence
    );
    report += QStringLiteral("collapse sequence type: %1\n").arg(
        tacticalEvent.collapseSequenceType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.collapseSequenceType
    );
    report += QStringLiteral("retained roles: %1\n").arg(
        tacticalEvent.retainedRoleSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.retainedRoleSummary
    );
    report += QStringLiteral("collapse sequence: %1\n").arg(
        tacticalEvent.collapseSequenceSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.collapseSequenceSummary
    );
    report += QStringLiteral("omitted adjacent candidates: %1\n").arg(tacticalEvent.omittedAdjacentCandidateCount);
    report += QStringLiteral("omission reasons: %1\n").arg(
        tacticalEvent.omissionReasonSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.omissionReasonSummary
    );
    report += QStringLiteral("engine limits: %1\n").arg(tacticalEvent.engineLimitSummary);
    report += QStringLiteral("tactical candidates: %1\n").arg(tacticalEvent.tacticalCandidatesJson);
    report += QStringLiteral("structural candidates: %1\n").arg(tacticalEvent.structuralCandidatesJson);
    report += QStringLiteral("solution summary: %1\n").arg(tacticalEvent.solutionSummary);
    report += QStringLiteral("material balance: %1\n").arg(tacticalEvent.materialBalance);
    report += QStringLiteral("king safety state: %1\n").arg(tacticalEvent.kingSafetyState);
    report += QStringLiteral("piece activity facts: %1\n").arg(tacticalEvent.pieceActivity);
    report += QStringLiteral("key weakness candidates: %1\n").arg(tacticalEvent.keyWeakness);
    report += QStringLiteral("structural feature summary: %1\n").arg(
        tacticalEvent.structuralFeatureSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.structuralFeatureSummary
    );
    report += QStringLiteral("structural feature confidence: %1\n").arg(
        tacticalEvent.structuralFeatureConfidence.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.structuralFeatureConfidence
    );
    report += QStringLiteral("king exposure: %1 (%2)\n").arg(
        tacticalEvent.kingExposureType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.kingExposureType,
        tacticalEvent.kingExposureSeverity.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.kingExposureSeverity
    );
    report += QStringLiteral("loose pieces: %1 | %2\n").arg(
        QString::number(tacticalEvent.loosePieceCount),
        tacticalEvent.loosePieceSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.loosePieceSummary
    );
    report += QStringLiteral("overloaded defenders: %1 | %2\n").arg(
        QString::number(tacticalEvent.overloadedDefenderCount),
        tacticalEvent.overloadedDefenderSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.overloadedDefenderSummary
    );
    report += QStringLiteral("back rank state: %1\n").arg(
        tacticalEvent.backRankState.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.backRankState
    );
    report += QStringLiteral("luft state: %1\n").arg(
        tacticalEvent.luftState.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.luftState
    );
    report += QStringLiteral("king line pressure: %1\n").arg(
        tacticalEvent.kingLinePressureType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.kingLinePressureType
    );
    report += QStringLiteral("king square pressure: %1\n").arg(
        tacticalEvent.kingSquarePressureType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.kingSquarePressureType
    );
    report += QStringLiteral("critical piece imbalance: %1\n").arg(
        tacticalEvent.criticalPieceImbalanceSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.criticalPieceImbalanceSummary
    );
    report += QStringLiteral("local target summary: %1\n").arg(
        tacticalEvent.localTargetSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.localTargetSummary
    );
    report += QStringLiteral("local target confidence: %1\n").arg(
        tacticalEvent.localTargetConfidence.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.localTargetConfidence
    );
    report += QStringLiteral("king-zone target: %1 | %2\n").arg(
        tacticalEvent.kingZoneTargetType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.kingZoneTargetType,
        tacticalEvent.kingZoneTargetSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.kingZoneTargetSummary
    );
    report += QStringLiteral("vulnerable-piece target: %1 | %2\n").arg(
        tacticalEvent.vulnerablePieceTargetType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.vulnerablePieceTargetType,
        tacticalEvent.vulnerablePieceTargetSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.vulnerablePieceTargetSummary
    );
    report += QStringLiteral("pressure lane target: %1 | %2\n").arg(
        tacticalEvent.pressureLaneTargetType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.pressureLaneTargetType,
        tacticalEvent.pressureLaneTargetSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.pressureLaneTargetSummary
    );
    report += QStringLiteral("decisive imbalance target: %1\n").arg(
        tacticalEvent.decisiveImbalanceTarget.isEmpty() ? QStringLiteral("none") : tacticalEvent.decisiveImbalanceTarget
    );
    report += QStringLiteral("structural v2 summary: %1\n").arg(
        tacticalEvent.structuralV2Summary.isEmpty() ? QStringLiteral("none") : tacticalEvent.structuralV2Summary
    );
    report += QStringLiteral("structural v2 confidence: %1\n").arg(
        tacticalEvent.structuralV2Confidence.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.structuralV2Confidence
    );
    report += QStringLiteral("pinned critical piece: %1 | %2\n").arg(
        tacticalEvent.pinnedCriticalPieceType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.pinnedCriticalPieceType,
        tacticalEvent.pinnedCriticalPieceSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.pinnedCriticalPieceSummary
    );
    report += QStringLiteral("defender-removal exposure: %1 | %2\n").arg(
        tacticalEvent.defenderRemovalExposureType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.defenderRemovalExposureType,
        tacticalEvent.defenderRemovalExposureSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.defenderRemovalExposureSummary
    );
    report += QStringLiteral("king color-complex: %1 | %2\n").arg(
        tacticalEvent.kingColorComplexState.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.kingColorComplexState,
        tacticalEvent.kingColorComplexSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.kingColorComplexSummary
    );
    report += QStringLiteral("target-zone imbalance: %1 | %2\n").arg(
        tacticalEvent.targetZoneImbalanceType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.targetZoneImbalanceType,
        tacticalEvent.targetZoneImbalanceSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.targetZoneImbalanceSummary
    );
    report += QStringLiteral("structural v3 summary: %1\n").arg(
        tacticalEvent.structuralV3Summary.isEmpty() ? QStringLiteral("none") : tacticalEvent.structuralV3Summary
    );
    report += QStringLiteral("structural v3 confidence: %1\n").arg(
        tacticalEvent.structuralV3Confidence.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.structuralV3Confidence
    );
    report += QStringLiteral("escape geometry: %1 | %2\n").arg(
        tacticalEvent.escapeGeometryState.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.escapeGeometryState,
        tacticalEvent.escapeGeometrySummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.escapeGeometrySummary
    );
    report += QStringLiteral("flight control: %1 | %2\n").arg(
        tacticalEvent.flightControlType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.flightControlType,
        tacticalEvent.flightControlSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.flightControlSummary
    );
    report += QStringLiteral("defensive escape fragility: %1 | %2\n").arg(
        tacticalEvent.defensiveEscapeFragilityType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.defensiveEscapeFragilityType,
        tacticalEvent.defensiveEscapeFragilitySummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.defensiveEscapeFragilitySummary
    );
    report += QStringLiteral("structural v4 summary: %1\n").arg(
        tacticalEvent.structuralV4Summary.isEmpty() ? QStringLiteral("none") : tacticalEvent.structuralV4Summary
    );
    report += QStringLiteral("structural v4 confidence: %1\n").arg(
        tacticalEvent.structuralV4Confidence.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.structuralV4Confidence
    );
    report += QStringLiteral("attacker coordination: %1 | %2\n").arg(
        tacticalEvent.attackerCoordinationType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.attackerCoordinationType,
        tacticalEvent.attackerCoordinationSummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.attackerCoordinationSummary
    );
    report += QStringLiteral("defensive network fragility: %1 | %2\n").arg(
        tacticalEvent.defensiveNetworkFragilityType.isEmpty() ? QStringLiteral("unknown") : tacticalEvent.defensiveNetworkFragilityType,
        tacticalEvent.defensiveNetworkFragilitySummary.isEmpty() ? QStringLiteral("none") : tacticalEvent.defensiveNetworkFragilitySummary
    );
    report += QStringLiteral("\nCritical Moves\n");
    report += QStringLiteral("==============\n");
    for (const CriticalMove &move : criticalMoves) {
        const QString breakMarker = move.ply == tacticalEvent.primaryBreakPly ? QStringLiteral(" [primary break]") : QString();
        report += QStringLiteral(
            "- role=%1%2 | ply=%3 | played=%4 | best=%5 | eval_before=%6 | eval_after_played=%7 | eval_after_best=%8 | "
            "eval_delta=%9 | decisive_swing=%10 | analysis_depth=%11 | mate_flag=%12 | only_move=%13 | only_move_margin=%14\n"
        ).arg(move.role)
             .arg(breakMarker)
             .arg(move.ply)
             .arg(move.playedMove)
             .arg(move.bestMove)
             .arg(cpField(move.hasEvalBeforeCp, move.evalBeforeCp))
             .arg(cpField(move.hasEvalAfterPlayedCp, move.evalAfterPlayedCp))
             .arg(cpField(move.hasEvalAfterBestCp, move.evalAfterBestCp))
             .arg(cpField(move.hasEvalDeltaCp, move.evalDeltaCp))
             .arg(move.decisiveSwing)
             .arg(move.analysisDepth)
             .arg(move.mateFlag.isEmpty() ? QStringLiteral("none") : move.mateFlag)
             .arg(move.onlyMoveStatus.isEmpty() ? QStringLiteral("unknown") : move.onlyMoveStatus)
             .arg(move.hasOnlyMoveMarginCp ? QString::number(move.onlyMoveMarginCp) : QStringLiteral("n/a"));
        report += QStringLiteral("  critical reason type: %1\n").arg(
            move.criticalReasonType.isEmpty() ? QStringLiteral("unknown") : move.criticalReasonType
        );
        report += QStringLiteral("  critical reason severity: %1\n").arg(
            move.criticalReasonSeverity.isEmpty() ? QStringLiteral("unknown") : move.criticalReasonSeverity
        );
        report += QStringLiteral("  critical reason: %1\n").arg(
            move.criticalReasonCompactSummary.isEmpty() ? QStringLiteral("none") : move.criticalReasonCompactSummary
        );
        report += QStringLiteral("  candidate ranking type: %1\n").arg(
            move.candidateRankingType.isEmpty() ? QStringLiteral("unknown") : move.candidateRankingType
        );
        report += QStringLiteral("  candidate ranking severity: %1\n").arg(
            move.candidateRankingSeverity.isEmpty() ? QStringLiteral("unknown") : move.candidateRankingSeverity
        );
        report += QStringLiteral("  candidate ranking: %1\n").arg(
            move.candidateRankingCompactSummary.isEmpty() ? QStringLiteral("none") : move.candidateRankingCompactSummary
        );
        report += QStringLiteral("  evidence note type: %1\n").arg(
            move.evidenceNoteType.isEmpty() ? QStringLiteral("unknown") : move.evidenceNoteType
        );
        report += QStringLiteral("  evidence note severity: %1\n").arg(
            move.evidenceNoteSeverity.isEmpty() ? QStringLiteral("unknown") : move.evidenceNoteSeverity
        );
        report += QStringLiteral("  evidence note: %1\n").arg(
            move.evidenceNoteCompact.isEmpty() ? QStringLiteral("none") : move.evidenceNoteCompact
        );
        report += QStringLiteral("  structural link: %1\n").arg(
            move.structuralLinkSummary.isEmpty() ? QStringLiteral("none") : move.structuralLinkSummary
        );
        report += QStringLiteral("  linked king-zone target: %1\n").arg(
            move.linkedKingZoneTarget.isEmpty() ? QStringLiteral("none") : move.linkedKingZoneTarget
        );
        report += QStringLiteral("  linked vulnerable-piece target: %1\n").arg(
            move.linkedVulnerablePieceTarget.isEmpty() ? QStringLiteral("none") : move.linkedVulnerablePieceTarget
        );
        report += QStringLiteral("  linked pressure-lane target: %1\n").arg(
            move.linkedPressureLaneTarget.isEmpty() ? QStringLiteral("none") : move.linkedPressureLaneTarget
        );
        report += QStringLiteral("  linked decisive-imbalance target: %1\n").arg(
            move.linkedDecisiveImbalanceTarget.isEmpty() ? QStringLiteral("none") : move.linkedDecisiveImbalanceTarget
        );
        report += QStringLiteral("  linked pinned critical piece: %1\n").arg(
            move.linkedPinnedCriticalPiece.isEmpty() ? QStringLiteral("none") : move.linkedPinnedCriticalPiece
        );
        report += QStringLiteral("  linked defender-removal exposure: %1\n").arg(
            move.linkedDefenderRemovalExposure.isEmpty() ? QStringLiteral("none") : move.linkedDefenderRemovalExposure
        );
        report += QStringLiteral("  linked king color-complex: %1\n").arg(
            move.linkedKingColorComplex.isEmpty() ? QStringLiteral("none") : move.linkedKingColorComplex
        );
        report += QStringLiteral("  linked target-zone imbalance: %1\n").arg(
            move.linkedTargetZoneImbalance.isEmpty() ? QStringLiteral("none") : move.linkedTargetZoneImbalance
        );
        report += QStringLiteral("  linked attacker coordination: %1\n").arg(
            move.linkedAttackerCoordination.isEmpty() ? QStringLiteral("none") : move.linkedAttackerCoordination
        );
        report += QStringLiteral("  linked defensive network fragility: %1\n").arg(
            move.linkedDefensiveNetworkFragility.isEmpty() ? QStringLiteral("none") : move.linkedDefensiveNetworkFragility
        );
        report += QStringLiteral("  best continuation compact: %1\n").arg(
            move.bestContinuationCompact.isEmpty() ? QStringLiteral("none") : move.bestContinuationCompact
        );
        report += QStringLiteral("  played continuation compact: %1\n").arg(
            move.playedContinuationCompact.isEmpty() ? QStringLiteral("none") : move.playedContinuationCompact
        );
        if (!move.onlyMoveReasoning.isEmpty()) {
            report += QStringLiteral("  only-move reasoning: %1\n").arg(move.onlyMoveReasoning);
        }
        if (!move.pvSan.isEmpty()) {
            report += QStringLiteral("  pv_san: %1\n").arg(move.pvSan);
        }
        if (!move.bestContinuationSummary.isEmpty()) {
            report += QStringLiteral("  best continuation: %1\n").arg(move.bestContinuationSummary);
        }
        if (!move.playedContinuationSummary.isEmpty()) {
            report += QStringLiteral("  played continuation: %1\n").arg(move.playedContinuationSummary);
        }
        report += QStringLiteral("  stronger alternatives: %1\n").arg(move.strongerAlternativeCount);
        report += QStringLiteral("  ranking summary: %1\n").arg(
            move.candidateRankingSummary.isEmpty() ? QStringLiteral("none") : move.candidateRankingSummary
        );
        const QStringList candidateLines = candidateDisplayLines(move.candidateMovesJson);
        if (!candidateLines.isEmpty()) {
            report += QStringLiteral("  candidates:\n");
            for (const QString &line : candidateLines) {
                report += QStringLiteral("    - %1\n").arg(line);
            }
        }
    }

    report += QStringLiteral("\nAssistant Inference\n");
    report += QStringLiteral("===================\n");
    report += QStringLiteral("status: %1\n").arg(tacticalEvent.assistantInferenceStatus);
    report += QStringLiteral("labels json: %1\n").arg(
        tacticalEvent.assistantLabelsJson.isEmpty() ? QStringLiteral("null") : tacticalEvent.assistantLabelsJson
    );
    report += QStringLiteral("summary markdown: %1\n").arg(
        tacticalEvent.assistantSummaryMarkdown.isEmpty() ? QStringLiteral("null") : tacticalEvent.assistantSummaryMarkdown
    );

    return report.trimmed();
}
