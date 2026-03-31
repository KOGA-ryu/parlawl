#pragma once

#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include "critical_move.h"
#include "tactical_event.h"

namespace storage::insert_specs {

namespace detail {

struct TacticalEventField
{
    const char *column;
    void (*bind)(QSqlQuery &, const TacticalEvent &);
};

struct CriticalMoveField
{
    const char *column;
    void (*bind)(QSqlQuery &, const CriticalMove &);
};

inline QVariant nullableInt(bool present, int value)
{
    return present ? QVariant(value) : QVariant();
}

inline QVariant nullablePositiveInt(int value)
{
    return value > 0 ? QVariant(value) : QVariant();
}

inline QString placeholders(int count)
{
    QStringList values;
    values.reserve(count);
    for (int index = 0; index < count; ++index) {
        values.append(QStringLiteral("?"));
    }
    return values.join(QStringLiteral(", "));
}

inline QString insertSql(const QString &tableName, const QStringList &columns, bool replace = false)
{
    return QStringLiteral("INSERT %1 INTO %2 (%3) VALUES (%4)")
        .arg(replace ? QStringLiteral("OR REPLACE") : QString())
        .arg(tableName)
        .arg(columns.join(QStringLiteral(", ")))
        .arg(placeholders(columns.size()));
}

inline const QList<TacticalEventField> &tacticalEventFields()
{
    static const QList<TacticalEventField> fields = {
        {"event_id", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.eventId); }},
        {"run_id", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.runId); }},
        {"puzzle_id", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.puzzleId); }},
        {"source_game_id", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.sourceGameId); }},
        {"opening_family", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.openingFamily); }},
        {"game_phase", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.gamePhase); }},
        {"solution_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.solutionSummary); }},
        {"side_to_move", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.sideToMove); }},
        {"material_balance", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.materialBalance); }},
        {"king_safety_state", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.kingSafetyState); }},
        {"piece_activity", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.pieceActivity); }},
        {"key_weakness", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.keyWeakness); }},
        {"king_exposure_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.kingExposureType); }},
        {"king_exposure_severity", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.kingExposureSeverity); }},
        {"loose_piece_count", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.loosePieceCount); }},
        {"loose_piece_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.loosePieceSummary); }},
        {"overloaded_defender_count", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.overloadedDefenderCount); }},
        {"overloaded_defender_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.overloadedDefenderSummary); }},
        {"back_rank_state", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.backRankState); }},
        {"luft_state", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.luftState); }},
        {"king_line_pressure_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.kingLinePressureType); }},
        {"king_square_pressure_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.kingSquarePressureType); }},
        {"critical_piece_imbalance_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.criticalPieceImbalanceSummary); }},
        {"structural_feature_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.structuralFeatureSummary); }},
        {"structural_feature_confidence", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.structuralFeatureConfidence); }},
        {"king_zone_target_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.kingZoneTargetType); }},
        {"king_zone_target_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.kingZoneTargetSummary); }},
        {"vulnerable_piece_target_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.vulnerablePieceTargetType); }},
        {"vulnerable_piece_target_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.vulnerablePieceTargetSummary); }},
        {"pressure_lane_target_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.pressureLaneTargetType); }},
        {"pressure_lane_target_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.pressureLaneTargetSummary); }},
        {"decisive_imbalance_target", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.decisiveImbalanceTarget); }},
        {"pinned_critical_piece_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.pinnedCriticalPieceType); }},
        {"pinned_critical_piece_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.pinnedCriticalPieceSummary); }},
        {"defender_removal_exposure_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.defenderRemovalExposureType); }},
        {"defender_removal_exposure_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.defenderRemovalExposureSummary); }},
        {"king_color_complex_state", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.kingColorComplexState); }},
        {"king_color_complex_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.kingColorComplexSummary); }},
        {"target_zone_imbalance_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.targetZoneImbalanceType); }},
        {"target_zone_imbalance_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.targetZoneImbalanceSummary); }},
        {"structural_v2_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.structuralV2Summary); }},
        {"structural_v2_confidence", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.structuralV2Confidence); }},
        {"escape_geometry_state", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.escapeGeometryState); }},
        {"escape_geometry_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.escapeGeometrySummary); }},
        {"flight_control_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.flightControlType); }},
        {"flight_control_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.flightControlSummary); }},
        {"defensive_escape_fragility_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.defensiveEscapeFragilityType); }},
        {"defensive_escape_fragility_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.defensiveEscapeFragilitySummary); }},
        {"structural_v3_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.structuralV3Summary); }},
        {"structural_v3_confidence", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.structuralV3Confidence); }},
        {"attacker_coordination_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.attackerCoordinationType); }},
        {"attacker_coordination_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.attackerCoordinationSummary); }},
        {"defensive_network_fragility_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.defensiveNetworkFragilityType); }},
        {"defensive_network_fragility_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.defensiveNetworkFragilitySummary); }},
        {"structural_v4_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.structuralV4Summary); }},
        {"structural_v4_confidence", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.structuralV4Confidence); }},
        {"local_target_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.localTargetSummary); }},
        {"local_target_confidence", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.localTargetConfidence); }},
        {"trigger_event", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.triggerEvent); }},
        {"player_rating_range", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.playerRatingRange); }},
        {"mapping_status", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.mappingStatus); }},
        {"mapped_start_ply", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.mappedStartPly); }},
        {"mapping_method", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.mappingMethod); }},
        {"mapping_confidence", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.mappingConfidence); }},
        {"mapping_notes", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.mappingNotes); }},
        {"analysis_window_start_ply", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.analysisWindowStartPly); }},
        {"analysis_window_end_ply", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.analysisWindowEndPly); }},
        {"primary_break_ply", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(nullablePositiveInt(event.primaryBreakPly)); }},
        {"primary_break_reason", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.primaryBreakReason); }},
        {"retained_break_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.retainedBreakSummary); }},
        {"retained_break_format_version", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.retainedBreakFormatVersion); }},
        {"retained_break_role", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.retainedBreakRole); }},
        {"retained_break_played_move", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.retainedBreakPlayedMove); }},
        {"retained_break_best_move", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.retainedBreakBestMove); }},
        {"retained_break_compact_sequence", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.retainedBreakCompactSequence); }},
        {"best_vs_played_divergence_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.bestVsPlayedDivergenceSummary); }},
        {"divergence_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.divergenceType); }},
        {"divergence_severity", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.divergenceSeverity); }},
        {"divergence_compact_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.divergenceCompactSummary); }},
        {"divergence_evidence_notes", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.divergenceEvidenceNotes); }},
        {"local_sequence_confidence", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.localSequenceConfidence); }},
        {"collapse_sequence_format_version", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.collapseSequenceFormatVersion); }},
        {"collapse_sequence_type", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.collapseSequenceType); }},
        {"collapse_sequence_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.collapseSequenceSummary); }},
        {"retained_role_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.retainedRoleSummary); }},
        {"omitted_adjacent_candidate_count", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.omittedAdjacentCandidateCount); }},
        {"omission_reason_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.omissionReasonSummary); }},
        {"omission_reason_counts_json", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.omissionReasonCountsJson); }},
        {"engine_limit_summary", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.engineLimitSummary); }},
        {"tactical_candidates_json", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.tacticalCandidatesJson); }},
        {"structural_candidates_json", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.structuralCandidatesJson); }},
        {"evidence_payload_json", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.evidencePayloadJson); }},
        {"assistant_inference_status", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.assistantInferenceStatus); }},
        {"assistant_labels_json", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.assistantLabelsJson); }},
        {"assistant_summary_markdown", [](QSqlQuery &query, const TacticalEvent &event) { query.addBindValue(event.assistantSummaryMarkdown); }},
    };
    return fields;
}

inline const QList<CriticalMoveField> &criticalMoveFields()
{
    static const QList<CriticalMoveField> fields = {
        {"critical_move_id", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.criticalMoveId); }},
        {"event_id", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.eventId); }},
        {"role", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.role); }},
        {"ply", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.ply); }},
        {"side", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.side); }},
        {"played_move", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.playedMove); }},
        {"best_move", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.bestMove); }},
        {"why_critical", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.whyCritical); }},
        {"eval_before_cp", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(nullableInt(move.hasEvalBeforeCp, move.evalBeforeCp)); }},
        {"eval_after_played_cp", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(nullableInt(move.hasEvalAfterPlayedCp, move.evalAfterPlayedCp)); }},
        {"eval_after_best_cp", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(nullableInt(move.hasEvalAfterBestCp, move.evalAfterBestCp)); }},
        {"eval_delta_cp", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(nullableInt(move.hasEvalDeltaCp, move.evalDeltaCp)); }},
        {"decisive_swing", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.decisiveSwing); }},
        {"analysis_depth", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.analysisDepth); }},
        {"mate_flag", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.mateFlag); }},
        {"critical_reason_type", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.criticalReasonType); }},
        {"critical_reason_severity", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.criticalReasonSeverity); }},
        {"critical_reason_compact_summary", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.criticalReasonCompactSummary); }},
        {"continuation_format_version", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.continuationFormatVersion); }},
        {"best_continuation_compact", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.bestContinuationCompact); }},
        {"played_continuation_compact", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.playedContinuationCompact); }},
        {"pv_uci", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.pvUci); }},
        {"pv_san", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.pvSan); }},
        {"best_continuation_summary", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.bestContinuationSummary); }},
        {"played_continuation_summary", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.playedContinuationSummary); }},
        {"only_move_status", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.onlyMoveStatus); }},
        {"only_move_margin_cp", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(nullableInt(move.hasOnlyMoveMarginCp, move.onlyMoveMarginCp)); }},
        {"only_move_reasoning", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.onlyMoveReasoning); }},
        {"candidate_moves_json", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.candidateMovesJson); }},
        {"candidate_ranking_type", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.candidateRankingType); }},
        {"candidate_ranking_severity", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.candidateRankingSeverity); }},
        {"candidate_ranking_compact_summary", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.candidateRankingCompactSummary); }},
        {"candidate_ranking_summary", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.candidateRankingSummary); }},
        {"evidence_note_type", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.evidenceNoteType); }},
        {"evidence_note_severity", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.evidenceNoteSeverity); }},
        {"evidence_note_compact", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.evidenceNoteCompact); }},
        {"structural_link_format_version", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.structuralLinkFormatVersion); }},
        {"linked_king_zone_target", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.linkedKingZoneTarget); }},
        {"linked_vulnerable_piece_target", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.linkedVulnerablePieceTarget); }},
        {"linked_pressure_lane_target", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.linkedPressureLaneTarget); }},
        {"linked_decisive_imbalance_target", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.linkedDecisiveImbalanceTarget); }},
        {"linked_pinned_critical_piece", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.linkedPinnedCriticalPiece); }},
        {"linked_defender_removal_exposure", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.linkedDefenderRemovalExposure); }},
        {"linked_king_color_complex", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.linkedKingColorComplex); }},
        {"linked_target_zone_imbalance", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.linkedTargetZoneImbalance); }},
        {"linked_attacker_coordination", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.linkedAttackerCoordination); }},
        {"linked_defensive_network_fragility", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.linkedDefensiveNetworkFragility); }},
        {"structural_link_summary", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.structuralLinkSummary); }},
        {"stronger_alternative_count", [](QSqlQuery &query, const CriticalMove &move) { query.addBindValue(move.strongerAlternativeCount); }},
    };
    return fields;
}

} // namespace detail

inline QStringList tacticalEventColumns()
{
    QStringList columns;
    const auto &fields = detail::tacticalEventFields();
    columns.reserve(fields.size());
    for (const auto &field : fields) {
        columns.append(QString::fromUtf8(field.column));
    }
    return columns;
}

inline QStringList criticalMoveColumns()
{
    QStringList columns;
    const auto &fields = detail::criticalMoveFields();
    columns.reserve(fields.size());
    for (const auto &field : fields) {
        columns.append(QString::fromUtf8(field.column));
    }
    return columns;
}

inline QString tacticalEventInsertSql()
{
    return detail::insertSql(QStringLiteral("tactical_events"), tacticalEventColumns(), true);
}

inline QString criticalMoveInsertSql()
{
    return detail::insertSql(QStringLiteral("critical_moves"), criticalMoveColumns(), false);
}

inline void bindTacticalEvent(QSqlQuery &query, const TacticalEvent &event)
{
    for (const auto &field : detail::tacticalEventFields()) {
        field.bind(query, event);
    }
}

inline void bindCriticalMove(QSqlQuery &query, const CriticalMove &move)
{
    for (const auto &field : detail::criticalMoveFields()) {
        field.bind(query, move);
    }
}

} // namespace storage::insert_specs
