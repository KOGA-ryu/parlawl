#pragma once

#include <QSqlQuery>
#include <QSqlRecord>
#include <QString>

#include "critical_move.h"
#include "tactical_event.h"

namespace storage {

class QueryRow
{
public:
    explicit QueryRow(const QSqlQuery &query)
        : m_query(query)
        , m_record(query.record())
    {
    }

    QString string(const char *name) const
    {
        return m_query.value(index(name)).toString();
    }

    int integer(const char *name) const
    {
        return m_query.value(index(name)).toInt();
    }

    bool hasValue(const char *name) const
    {
        return !m_query.value(index(name)).isNull();
    }

private:
    int index(const char *name) const
    {
        const int idx = m_record.indexOf(QString::fromUtf8(name));
        Q_ASSERT_X(idx >= 0, "QueryRow::index", name);
        return idx;
    }

    const QSqlQuery &m_query;
    const QSqlRecord m_record;
};

inline TacticalEvent mapTacticalEventRow(const QSqlQuery &query)
{
    QueryRow row(query);
    TacticalEvent event;
    event.eventId = row.string("event_id");
    event.runId = row.string("run_id");
    event.puzzleId = row.string("puzzle_id");
    event.sourceGameId = row.string("source_game_id");
    event.openingFamily = row.string("opening_family");
    event.gamePhase = row.string("game_phase");
    event.solutionSummary = row.string("solution_summary");
    event.sideToMove = row.string("side_to_move");
    event.materialBalance = row.string("material_balance");
    event.kingSafetyState = row.string("king_safety_state");
    event.pieceActivity = row.string("piece_activity");
    event.keyWeakness = row.string("key_weakness");
    event.kingExposureType = row.string("king_exposure_type");
    event.kingExposureSeverity = row.string("king_exposure_severity");
    event.loosePieceCount = row.integer("loose_piece_count");
    event.loosePieceSummary = row.string("loose_piece_summary");
    event.overloadedDefenderCount = row.integer("overloaded_defender_count");
    event.overloadedDefenderSummary = row.string("overloaded_defender_summary");
    event.backRankState = row.string("back_rank_state");
    event.luftState = row.string("luft_state");
    event.kingLinePressureType = row.string("king_line_pressure_type");
    event.kingSquarePressureType = row.string("king_square_pressure_type");
    event.criticalPieceImbalanceSummary = row.string("critical_piece_imbalance_summary");
    event.structuralFeatureSummary = row.string("structural_feature_summary");
    event.structuralFeatureConfidence = row.string("structural_feature_confidence");
    event.kingZoneTargetType = row.string("king_zone_target_type");
    event.kingZoneTargetSummary = row.string("king_zone_target_summary");
    event.vulnerablePieceTargetType = row.string("vulnerable_piece_target_type");
    event.vulnerablePieceTargetSummary = row.string("vulnerable_piece_target_summary");
    event.pressureLaneTargetType = row.string("pressure_lane_target_type");
    event.pressureLaneTargetSummary = row.string("pressure_lane_target_summary");
    event.decisiveImbalanceTarget = row.string("decisive_imbalance_target");
    event.pinnedCriticalPieceType = row.string("pinned_critical_piece_type");
    event.pinnedCriticalPieceSummary = row.string("pinned_critical_piece_summary");
    event.defenderRemovalExposureType = row.string("defender_removal_exposure_type");
    event.defenderRemovalExposureSummary = row.string("defender_removal_exposure_summary");
    event.kingColorComplexState = row.string("king_color_complex_state");
    event.kingColorComplexSummary = row.string("king_color_complex_summary");
    event.targetZoneImbalanceType = row.string("target_zone_imbalance_type");
    event.targetZoneImbalanceSummary = row.string("target_zone_imbalance_summary");
    event.structuralV2Summary = row.string("structural_v2_summary");
    event.structuralV2Confidence = row.string("structural_v2_confidence");
    event.escapeGeometryState = row.string("escape_geometry_state");
    event.escapeGeometrySummary = row.string("escape_geometry_summary");
    event.flightControlType = row.string("flight_control_type");
    event.flightControlSummary = row.string("flight_control_summary");
    event.defensiveEscapeFragilityType = row.string("defensive_escape_fragility_type");
    event.defensiveEscapeFragilitySummary = row.string("defensive_escape_fragility_summary");
    event.structuralV3Summary = row.string("structural_v3_summary");
    event.structuralV3Confidence = row.string("structural_v3_confidence");
    event.attackerCoordinationType = row.string("attacker_coordination_type");
    event.attackerCoordinationSummary = row.string("attacker_coordination_summary");
    event.defensiveNetworkFragilityType = row.string("defensive_network_fragility_type");
    event.defensiveNetworkFragilitySummary = row.string("defensive_network_fragility_summary");
    event.structuralV4Summary = row.string("structural_v4_summary");
    event.structuralV4Confidence = row.string("structural_v4_confidence");
    event.localTargetSummary = row.string("local_target_summary");
    event.localTargetConfidence = row.string("local_target_confidence");
    event.triggerEvent = row.string("trigger_event");
    event.playerRatingRange = row.string("player_rating_range");
    event.mappingStatus = row.string("mapping_status");
    event.mappedStartPly = row.integer("mapped_start_ply");
    event.mappingMethod = row.string("mapping_method");
    event.mappingConfidence = row.string("mapping_confidence");
    event.mappingNotes = row.string("mapping_notes");
    event.analysisWindowStartPly = row.integer("analysis_window_start_ply");
    event.analysisWindowEndPly = row.integer("analysis_window_end_ply");
    event.primaryBreakPly = row.integer("primary_break_ply");
    event.primaryBreakReason = row.string("primary_break_reason");
    event.retainedBreakSummary = row.string("retained_break_summary");
    event.retainedBreakFormatVersion = row.string("retained_break_format_version");
    event.retainedBreakRole = row.string("retained_break_role");
    event.retainedBreakPlayedMove = row.string("retained_break_played_move");
    event.retainedBreakBestMove = row.string("retained_break_best_move");
    event.retainedBreakCompactSequence = row.string("retained_break_compact_sequence");
    event.bestVsPlayedDivergenceSummary = row.string("best_vs_played_divergence_summary");
    event.divergenceType = row.string("divergence_type");
    event.divergenceSeverity = row.string("divergence_severity");
    event.divergenceCompactSummary = row.string("divergence_compact_summary");
    event.divergenceEvidenceNotes = row.string("divergence_evidence_notes");
    event.localSequenceConfidence = row.string("local_sequence_confidence");
    event.collapseSequenceFormatVersion = row.string("collapse_sequence_format_version");
    event.collapseSequenceType = row.string("collapse_sequence_type");
    event.collapseSequenceSummary = row.string("collapse_sequence_summary");
    event.retainedRoleSummary = row.string("retained_role_summary");
    event.omittedAdjacentCandidateCount = row.integer("omitted_adjacent_candidate_count");
    event.omissionReasonSummary = row.string("omission_reason_summary");
    event.omissionReasonCountsJson = row.string("omission_reason_counts_json");
    event.engineLimitSummary = row.string("engine_limit_summary");
    event.tacticalCandidatesJson = row.string("tactical_candidates_json");
    event.structuralCandidatesJson = row.string("structural_candidates_json");
    event.evidencePayloadJson = row.string("evidence_payload_json");
    event.assistantInferenceStatus = row.string("assistant_inference_status");
    event.assistantLabelsJson = row.string("assistant_labels_json");
    event.assistantSummaryMarkdown = row.string("assistant_summary_markdown");
    return event;
}

inline CriticalMove mapCriticalMoveRow(const QSqlQuery &query)
{
    QueryRow row(query);
    CriticalMove move;
    move.criticalMoveId = row.string("critical_move_id");
    move.eventId = row.string("event_id");
    move.role = row.string("role");
    move.ply = row.integer("ply");
    move.side = row.string("side");
    move.playedMove = row.string("played_move");
    move.bestMove = row.string("best_move");
    move.whyCritical = row.string("why_critical");
    move.hasEvalBeforeCp = row.hasValue("eval_before_cp");
    move.evalBeforeCp = row.integer("eval_before_cp");
    move.hasEvalAfterPlayedCp = row.hasValue("eval_after_played_cp");
    move.evalAfterPlayedCp = row.integer("eval_after_played_cp");
    move.hasEvalAfterBestCp = row.hasValue("eval_after_best_cp");
    move.evalAfterBestCp = row.integer("eval_after_best_cp");
    move.hasEvalDeltaCp = row.hasValue("eval_delta_cp");
    move.evalDeltaCp = row.integer("eval_delta_cp");
    move.decisiveSwing = row.string("decisive_swing");
    move.analysisDepth = row.integer("analysis_depth");
    move.mateFlag = row.string("mate_flag");
    move.criticalReasonType = row.string("critical_reason_type");
    move.criticalReasonSeverity = row.string("critical_reason_severity");
    move.criticalReasonCompactSummary = row.string("critical_reason_compact_summary");
    move.continuationFormatVersion = row.string("continuation_format_version");
    move.bestContinuationCompact = row.string("best_continuation_compact");
    move.playedContinuationCompact = row.string("played_continuation_compact");
    move.pvUci = row.string("pv_uci");
    move.pvSan = row.string("pv_san");
    move.bestContinuationSummary = row.string("best_continuation_summary");
    move.playedContinuationSummary = row.string("played_continuation_summary");
    move.onlyMoveStatus = row.string("only_move_status");
    move.hasOnlyMoveMarginCp = row.hasValue("only_move_margin_cp");
    move.onlyMoveMarginCp = row.integer("only_move_margin_cp");
    move.onlyMoveReasoning = row.string("only_move_reasoning");
    move.candidateMovesJson = row.string("candidate_moves_json");
    move.candidateRankingType = row.string("candidate_ranking_type");
    move.candidateRankingSeverity = row.string("candidate_ranking_severity");
    move.candidateRankingCompactSummary = row.string("candidate_ranking_compact_summary");
    move.candidateRankingSummary = row.string("candidate_ranking_summary");
    move.evidenceNoteType = row.string("evidence_note_type");
    move.evidenceNoteSeverity = row.string("evidence_note_severity");
    move.evidenceNoteCompact = row.string("evidence_note_compact");
    move.structuralLinkFormatVersion = row.string("structural_link_format_version");
    move.linkedKingZoneTarget = row.string("linked_king_zone_target");
    move.linkedVulnerablePieceTarget = row.string("linked_vulnerable_piece_target");
    move.linkedPressureLaneTarget = row.string("linked_pressure_lane_target");
    move.linkedDecisiveImbalanceTarget = row.string("linked_decisive_imbalance_target");
    move.linkedPinnedCriticalPiece = row.string("linked_pinned_critical_piece");
    move.linkedDefenderRemovalExposure = row.string("linked_defender_removal_exposure");
    move.linkedKingColorComplex = row.string("linked_king_color_complex");
    move.linkedTargetZoneImbalance = row.string("linked_target_zone_imbalance");
    move.linkedAttackerCoordination = row.string("linked_attacker_coordination");
    move.linkedDefensiveNetworkFragility = row.string("linked_defensive_network_fragility");
    move.structuralLinkSummary = row.string("structural_link_summary");
    move.strongerAlternativeCount = row.integer("stronger_alternative_count");
    return move;
}

} // namespace storage
