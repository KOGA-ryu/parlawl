#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "analysis_run.h"
#include "critical_move.h"
#include "puzzle_round.h"
#include "source_game.h"
#include "tactical_event.h"

struct WorkerAnalysisResponse
{
    bool ok = false;
    QString errorMessage;
    AnalysisRun analysisRun;
    TacticalEvent tacticalEvent;
    QList<CriticalMove> criticalMoves;
};

namespace worker_protocol {

inline constexpr auto kVersion = "0.1";
inline constexpr auto kRequestTypeAnalyzeLatest = "analyze_latest_solved_puzzle";
inline constexpr auto kResponseTypeAnalysisResult = "analysis_result";
inline constexpr int kDefaultEngineDepth = 10;
inline constexpr int kDefaultWindowBefore = 4;
inline constexpr int kDefaultWindowAfter = 4;
inline constexpr int kDefaultMultiPv = 3;

inline QJsonObject puzzleRoundToJson(const PuzzleRound &puzzleRound)
{
    return {
        {QStringLiteral("puzzle_id"), puzzleRound.puzzleId},
        {QStringLiteral("puzzle_rating"), puzzleRound.puzzleRating},
        {QStringLiteral("time_control"), puzzleRound.timeControl},
        {QStringLiteral("white_player"), puzzleRound.whitePlayer},
        {QStringLiteral("white_rating"), puzzleRound.whiteRating},
        {QStringLiteral("black_player"), puzzleRound.blackPlayer},
        {QStringLiteral("black_rating"), puzzleRound.blackRating},
        {QStringLiteral("side_to_move"), puzzleRound.sideToMove},
        {QStringLiteral("source_game_id"), puzzleRound.sourceGameId},
        {QStringLiteral("fetched_at"), puzzleRound.fetchedAtUtc.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("initial_fen"), puzzleRound.initialFen},
        {QStringLiteral("last_move"), puzzleRound.lastMove},
        {QStringLiteral("solved"), puzzleRound.solved},
        {QStringLiteral("raw_puzzle_json"), puzzleRound.rawPuzzleJson},
        {QStringLiteral("raw_activity_json"), puzzleRound.rawActivityJson},
        {QStringLiteral("solution_moves_json"), puzzleRound.solutionMovesJson},
        {QStringLiteral("themes_json"), puzzleRound.themesJson},
    };
}

inline QJsonObject sourceGameToJson(const SourceGame &sourceGame)
{
    return {
        {QStringLiteral("source_game_id"), sourceGame.sourceGameId},
        {QStringLiteral("pgn_text"), sourceGame.pgnText},
        {QStringLiteral("opening_name"), sourceGame.openingName},
        {QStringLiteral("fetched_at"), sourceGame.fetchedAtUtc.toUTC().toString(Qt::ISODateWithMs)},
    };
}

inline QJsonDocument makeAnalyzeLatestRequest(
    const AnalysisRun &analysisRun,
    const PuzzleRound &puzzleRound,
    const SourceGame &sourceGame,
    const QString &stockfishPath
)
{
    QJsonObject envelope;
    envelope.insert(QStringLiteral("protocol_version"), QString::fromUtf8(kVersion));
    envelope.insert(QStringLiteral("request_type"), QString::fromUtf8(kRequestTypeAnalyzeLatest));
    envelope.insert(QStringLiteral("analysis_run"), QJsonObject{
                                                    {QStringLiteral("run_id"), analysisRun.runId},
                                                    {QStringLiteral("puzzle_id"), analysisRun.puzzleId},
                                                    {QStringLiteral("status"), analysisRun.status},
                                                    {QStringLiteral("engine_mode"), analysisRun.engineMode},
                                                    {QStringLiteral("engine_name"), analysisRun.engineName},
                                                    {QStringLiteral("engine_depth"), analysisRun.engineDepth},
                                                    {QStringLiteral("created_at"), analysisRun.createdAtUtc.toUTC().toString(Qt::ISODateWithMs)},
                                                });
    envelope.insert(QStringLiteral("engine"), QJsonObject{
                                            {QStringLiteral("stockfish_path"), stockfishPath},
                                            {QStringLiteral("depth"), analysisRun.engineDepth},
                                            {QStringLiteral("window_before"), kDefaultWindowBefore},
                                            {QStringLiteral("window_after"), kDefaultWindowAfter},
                                            {QStringLiteral("multipv"), kDefaultMultiPv},
                                        });
    envelope.insert(QStringLiteral("puzzle_round"), puzzleRoundToJson(puzzleRound));
    envelope.insert(QStringLiteral("source_game"), sourceGameToJson(sourceGame));
    return QJsonDocument(envelope);
}

inline QString requiredString(const QJsonObject &object, const QString &key)
{
    return object.value(key).toString();
}

inline WorkerAnalysisResponse parseAnalysisResponse(const QByteArray &payload)
{
    WorkerAnalysisResponse response;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (!document.isObject()) {
        response.errorMessage = QStringLiteral("worker response is not a JSON object: %1").arg(parseError.errorString());
        return response;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("protocol_version")).toString() != QString::fromUtf8(kVersion)) {
        response.errorMessage = QStringLiteral("worker response protocol_version mismatch");
        return response;
    }
    if (root.value(QStringLiteral("response_type")).toString() != QString::fromUtf8(kResponseTypeAnalysisResult)) {
        response.errorMessage = QStringLiteral("worker response_type mismatch");
        return response;
    }

    const QJsonObject runObject = root.value(QStringLiteral("analysis_run")).toObject();
    response.analysisRun.runId = requiredString(runObject, QStringLiteral("run_id"));
    response.analysisRun.puzzleId = requiredString(runObject, QStringLiteral("puzzle_id"));
    response.analysisRun.status = requiredString(runObject, QStringLiteral("status"));
    response.analysisRun.engineMode = requiredString(runObject, QStringLiteral("engine_mode"));
    response.analysisRun.engineName = requiredString(runObject, QStringLiteral("engine_name"));
    response.analysisRun.engineDepth = runObject.value(QStringLiteral("engine_depth")).toInt();
    response.analysisRun.createdAtUtc = QDateTime::fromString(requiredString(runObject, QStringLiteral("created_at")), Qt::ISODate);
    response.analysisRun.completedAtUtc = QDateTime::fromString(requiredString(runObject, QStringLiteral("completed_at")), Qt::ISODate);

    const QJsonObject eventObject = root.value(QStringLiteral("tactical_event")).toObject();
    response.tacticalEvent.eventId = requiredString(eventObject, QStringLiteral("event_id"));
    response.tacticalEvent.runId = requiredString(eventObject, QStringLiteral("run_id"));
    response.tacticalEvent.puzzleId = requiredString(eventObject, QStringLiteral("puzzle_id"));
    response.tacticalEvent.sourceGameId = requiredString(eventObject, QStringLiteral("source_game_id"));
    response.tacticalEvent.openingFamily = requiredString(eventObject, QStringLiteral("opening_family"));
    response.tacticalEvent.gamePhase = requiredString(eventObject, QStringLiteral("game_phase"));
    response.tacticalEvent.solutionSummary = requiredString(eventObject, QStringLiteral("solution_summary"));
    response.tacticalEvent.sideToMove = requiredString(eventObject, QStringLiteral("side_to_move"));
    response.tacticalEvent.materialBalance = requiredString(eventObject, QStringLiteral("material_balance"));
    response.tacticalEvent.kingSafetyState = requiredString(eventObject, QStringLiteral("king_safety_state"));
    response.tacticalEvent.pieceActivity = requiredString(eventObject, QStringLiteral("piece_activity"));
    response.tacticalEvent.keyWeakness = requiredString(eventObject, QStringLiteral("key_weakness"));
    response.tacticalEvent.kingExposureType = requiredString(eventObject, QStringLiteral("king_exposure_type"));
    response.tacticalEvent.kingExposureSeverity = requiredString(eventObject, QStringLiteral("king_exposure_severity"));
    response.tacticalEvent.loosePieceCount = eventObject.value(QStringLiteral("loose_piece_count")).toInt();
    response.tacticalEvent.loosePieceSummary = requiredString(eventObject, QStringLiteral("loose_piece_summary"));
    response.tacticalEvent.overloadedDefenderCount = eventObject.value(QStringLiteral("overloaded_defender_count")).toInt();
    response.tacticalEvent.overloadedDefenderSummary = requiredString(eventObject, QStringLiteral("overloaded_defender_summary"));
    response.tacticalEvent.backRankState = requiredString(eventObject, QStringLiteral("back_rank_state"));
    response.tacticalEvent.luftState = requiredString(eventObject, QStringLiteral("luft_state"));
    response.tacticalEvent.kingLinePressureType = requiredString(eventObject, QStringLiteral("king_line_pressure_type"));
    response.tacticalEvent.kingSquarePressureType = requiredString(eventObject, QStringLiteral("king_square_pressure_type"));
    response.tacticalEvent.criticalPieceImbalanceSummary = requiredString(eventObject, QStringLiteral("critical_piece_imbalance_summary"));
    response.tacticalEvent.structuralFeatureSummary = requiredString(eventObject, QStringLiteral("structural_feature_summary"));
    response.tacticalEvent.structuralFeatureConfidence = requiredString(eventObject, QStringLiteral("structural_feature_confidence"));
    response.tacticalEvent.kingZoneTargetType = requiredString(eventObject, QStringLiteral("king_zone_target_type"));
    response.tacticalEvent.kingZoneTargetSummary = requiredString(eventObject, QStringLiteral("king_zone_target_summary"));
    response.tacticalEvent.vulnerablePieceTargetType = requiredString(eventObject, QStringLiteral("vulnerable_piece_target_type"));
    response.tacticalEvent.vulnerablePieceTargetSummary = requiredString(eventObject, QStringLiteral("vulnerable_piece_target_summary"));
    response.tacticalEvent.pressureLaneTargetType = requiredString(eventObject, QStringLiteral("pressure_lane_target_type"));
    response.tacticalEvent.pressureLaneTargetSummary = requiredString(eventObject, QStringLiteral("pressure_lane_target_summary"));
    response.tacticalEvent.decisiveImbalanceTarget = requiredString(eventObject, QStringLiteral("decisive_imbalance_target"));
    response.tacticalEvent.pinnedCriticalPieceType = requiredString(eventObject, QStringLiteral("pinned_critical_piece_type"));
    response.tacticalEvent.pinnedCriticalPieceSummary = requiredString(eventObject, QStringLiteral("pinned_critical_piece_summary"));
    response.tacticalEvent.defenderRemovalExposureType = requiredString(eventObject, QStringLiteral("defender_removal_exposure_type"));
    response.tacticalEvent.defenderRemovalExposureSummary = requiredString(eventObject, QStringLiteral("defender_removal_exposure_summary"));
    response.tacticalEvent.kingColorComplexState = requiredString(eventObject, QStringLiteral("king_color_complex_state"));
    response.tacticalEvent.kingColorComplexSummary = requiredString(eventObject, QStringLiteral("king_color_complex_summary"));
    response.tacticalEvent.targetZoneImbalanceType = requiredString(eventObject, QStringLiteral("target_zone_imbalance_type"));
    response.tacticalEvent.targetZoneImbalanceSummary = requiredString(eventObject, QStringLiteral("target_zone_imbalance_summary"));
    response.tacticalEvent.structuralV2Summary = requiredString(eventObject, QStringLiteral("structural_v2_summary"));
    response.tacticalEvent.structuralV2Confidence = requiredString(eventObject, QStringLiteral("structural_v2_confidence"));
    response.tacticalEvent.escapeGeometryState = requiredString(eventObject, QStringLiteral("escape_geometry_state"));
    response.tacticalEvent.escapeGeometrySummary = requiredString(eventObject, QStringLiteral("escape_geometry_summary"));
    response.tacticalEvent.flightControlType = requiredString(eventObject, QStringLiteral("flight_control_type"));
    response.tacticalEvent.flightControlSummary = requiredString(eventObject, QStringLiteral("flight_control_summary"));
    response.tacticalEvent.defensiveEscapeFragilityType = requiredString(eventObject, QStringLiteral("defensive_escape_fragility_type"));
    response.tacticalEvent.defensiveEscapeFragilitySummary = requiredString(eventObject, QStringLiteral("defensive_escape_fragility_summary"));
    response.tacticalEvent.structuralV3Summary = requiredString(eventObject, QStringLiteral("structural_v3_summary"));
    response.tacticalEvent.structuralV3Confidence = requiredString(eventObject, QStringLiteral("structural_v3_confidence"));
    response.tacticalEvent.attackerCoordinationType = requiredString(eventObject, QStringLiteral("attacker_coordination_type"));
    response.tacticalEvent.attackerCoordinationSummary = requiredString(eventObject, QStringLiteral("attacker_coordination_summary"));
    response.tacticalEvent.defensiveNetworkFragilityType = requiredString(eventObject, QStringLiteral("defensive_network_fragility_type"));
    response.tacticalEvent.defensiveNetworkFragilitySummary = requiredString(eventObject, QStringLiteral("defensive_network_fragility_summary"));
    response.tacticalEvent.structuralV4Summary = requiredString(eventObject, QStringLiteral("structural_v4_summary"));
    response.tacticalEvent.structuralV4Confidence = requiredString(eventObject, QStringLiteral("structural_v4_confidence"));
    response.tacticalEvent.localTargetSummary = requiredString(eventObject, QStringLiteral("local_target_summary"));
    response.tacticalEvent.localTargetConfidence = requiredString(eventObject, QStringLiteral("local_target_confidence"));
    response.tacticalEvent.triggerEvent = requiredString(eventObject, QStringLiteral("trigger_event"));
    response.tacticalEvent.playerRatingRange = requiredString(eventObject, QStringLiteral("player_rating_range"));
    response.tacticalEvent.mappingStatus = requiredString(eventObject, QStringLiteral("mapping_status"));
    response.tacticalEvent.mappedStartPly = eventObject.value(QStringLiteral("mapped_start_ply")).toInt();
    response.tacticalEvent.mappingMethod = requiredString(eventObject, QStringLiteral("mapping_method"));
    response.tacticalEvent.mappingConfidence = requiredString(eventObject, QStringLiteral("mapping_confidence"));
    response.tacticalEvent.mappingNotes = requiredString(eventObject, QStringLiteral("mapping_notes"));
    response.tacticalEvent.analysisWindowStartPly = eventObject.value(QStringLiteral("analysis_window_start_ply")).toInt();
    response.tacticalEvent.analysisWindowEndPly = eventObject.value(QStringLiteral("analysis_window_end_ply")).toInt();
    response.tacticalEvent.primaryBreakPly = eventObject.value(QStringLiteral("primary_break_ply")).toInt();
    response.tacticalEvent.primaryBreakReason = requiredString(eventObject, QStringLiteral("primary_break_reason"));
    response.tacticalEvent.retainedBreakFormatVersion = requiredString(eventObject, QStringLiteral("retained_break_format_version"));
    response.tacticalEvent.retainedBreakRole = requiredString(eventObject, QStringLiteral("retained_break_role"));
    response.tacticalEvent.retainedBreakPlayedMove = requiredString(eventObject, QStringLiteral("retained_break_played_move"));
    response.tacticalEvent.retainedBreakBestMove = requiredString(eventObject, QStringLiteral("retained_break_best_move"));
    response.tacticalEvent.retainedBreakCompactSequence = requiredString(eventObject, QStringLiteral("retained_break_compact_sequence"));
    response.tacticalEvent.retainedBreakSummary = requiredString(eventObject, QStringLiteral("retained_break_summary"));
    response.tacticalEvent.bestVsPlayedDivergenceSummary = requiredString(eventObject, QStringLiteral("best_vs_played_divergence_summary"));
    response.tacticalEvent.divergenceType = requiredString(eventObject, QStringLiteral("divergence_type"));
    response.tacticalEvent.divergenceSeverity = requiredString(eventObject, QStringLiteral("divergence_severity"));
    response.tacticalEvent.divergenceCompactSummary = requiredString(eventObject, QStringLiteral("divergence_compact_summary"));
    response.tacticalEvent.divergenceEvidenceNotes = requiredString(eventObject, QStringLiteral("divergence_evidence_notes"));
    response.tacticalEvent.localSequenceConfidence = requiredString(eventObject, QStringLiteral("local_sequence_confidence"));
    response.tacticalEvent.collapseSequenceFormatVersion = requiredString(eventObject, QStringLiteral("collapse_sequence_format_version"));
    response.tacticalEvent.collapseSequenceType = requiredString(eventObject, QStringLiteral("collapse_sequence_type"));
    response.tacticalEvent.collapseSequenceSummary = requiredString(eventObject, QStringLiteral("collapse_sequence_summary"));
    response.tacticalEvent.retainedRoleSummary = requiredString(eventObject, QStringLiteral("retained_role_summary"));
    response.tacticalEvent.omittedAdjacentCandidateCount = eventObject.value(QStringLiteral("omitted_adjacent_candidate_count")).toInt();
    response.tacticalEvent.omissionReasonSummary = requiredString(eventObject, QStringLiteral("omission_reason_summary"));
    response.tacticalEvent.omissionReasonCountsJson = requiredString(eventObject, QStringLiteral("omission_reason_counts_json"));
    response.tacticalEvent.engineLimitSummary = requiredString(eventObject, QStringLiteral("engine_limit_summary"));
    response.tacticalEvent.tacticalCandidatesJson = requiredString(eventObject, QStringLiteral("tactical_candidates_json"));
    response.tacticalEvent.structuralCandidatesJson = requiredString(eventObject, QStringLiteral("structural_candidates_json"));
    response.tacticalEvent.evidencePayloadJson = requiredString(eventObject, QStringLiteral("evidence_payload_json"));
    response.tacticalEvent.assistantInferenceStatus = requiredString(eventObject, QStringLiteral("assistant_inference_status"));
    response.tacticalEvent.assistantLabelsJson = eventObject.value(QStringLiteral("assistant_labels_json")).toString();
    response.tacticalEvent.assistantSummaryMarkdown = eventObject.value(QStringLiteral("assistant_summary_markdown")).toString();

    for (const QJsonValue &value : root.value(QStringLiteral("critical_moves")).toArray()) {
        const QJsonObject moveObject = value.toObject();
        CriticalMove move;
        move.criticalMoveId = requiredString(moveObject, QStringLiteral("critical_move_id"));
        move.eventId = requiredString(moveObject, QStringLiteral("event_id"));
        move.role = requiredString(moveObject, QStringLiteral("role"));
        move.ply = moveObject.value(QStringLiteral("ply")).toInt();
        move.side = requiredString(moveObject, QStringLiteral("side"));
        move.playedMove = requiredString(moveObject, QStringLiteral("played_move"));
        move.bestMove = requiredString(moveObject, QStringLiteral("best_move"));
        move.whyCritical = requiredString(moveObject, QStringLiteral("why_critical"));
        move.hasEvalBeforeCp = !moveObject.value(QStringLiteral("eval_before_cp")).isNull();
        move.evalBeforeCp = moveObject.value(QStringLiteral("eval_before_cp")).toInt();
        move.hasEvalAfterPlayedCp = !moveObject.value(QStringLiteral("eval_after_played_cp")).isNull();
        move.evalAfterPlayedCp = moveObject.value(QStringLiteral("eval_after_played_cp")).toInt();
        move.hasEvalAfterBestCp = !moveObject.value(QStringLiteral("eval_after_best_cp")).isNull();
        move.evalAfterBestCp = moveObject.value(QStringLiteral("eval_after_best_cp")).toInt();
        move.hasEvalDeltaCp = !moveObject.value(QStringLiteral("eval_delta_cp")).isNull();
        move.evalDeltaCp = moveObject.value(QStringLiteral("eval_delta_cp")).toInt();
        move.decisiveSwing = requiredString(moveObject, QStringLiteral("decisive_swing"));
        move.analysisDepth = moveObject.value(QStringLiteral("analysis_depth")).toInt();
        move.mateFlag = requiredString(moveObject, QStringLiteral("mate_flag"));
        move.criticalReasonType = requiredString(moveObject, QStringLiteral("critical_reason_type"));
        move.criticalReasonSeverity = requiredString(moveObject, QStringLiteral("critical_reason_severity"));
        move.criticalReasonCompactSummary = requiredString(moveObject, QStringLiteral("critical_reason_compact_summary"));
        move.continuationFormatVersion = requiredString(moveObject, QStringLiteral("continuation_format_version"));
        move.bestContinuationCompact = requiredString(moveObject, QStringLiteral("best_continuation_compact"));
        move.playedContinuationCompact = requiredString(moveObject, QStringLiteral("played_continuation_compact"));
        move.pvUci = requiredString(moveObject, QStringLiteral("pv_uci"));
        move.pvSan = requiredString(moveObject, QStringLiteral("pv_san"));
        move.bestContinuationSummary = requiredString(moveObject, QStringLiteral("best_continuation_summary"));
        move.playedContinuationSummary = requiredString(moveObject, QStringLiteral("played_continuation_summary"));
        move.onlyMoveStatus = requiredString(moveObject, QStringLiteral("only_move_status"));
        move.hasOnlyMoveMarginCp = !moveObject.value(QStringLiteral("only_move_margin_cp")).isNull();
        move.onlyMoveMarginCp = moveObject.value(QStringLiteral("only_move_margin_cp")).toInt();
        move.onlyMoveReasoning = requiredString(moveObject, QStringLiteral("only_move_reasoning"));
        move.candidateMovesJson = requiredString(moveObject, QStringLiteral("candidate_moves_json"));
        move.candidateRankingType = requiredString(moveObject, QStringLiteral("candidate_ranking_type"));
        move.candidateRankingSeverity = requiredString(moveObject, QStringLiteral("candidate_ranking_severity"));
        move.candidateRankingCompactSummary = requiredString(moveObject, QStringLiteral("candidate_ranking_compact_summary"));
        move.candidateRankingSummary = requiredString(moveObject, QStringLiteral("candidate_ranking_summary"));
        move.evidenceNoteType = requiredString(moveObject, QStringLiteral("evidence_note_type"));
        move.evidenceNoteSeverity = requiredString(moveObject, QStringLiteral("evidence_note_severity"));
        move.evidenceNoteCompact = requiredString(moveObject, QStringLiteral("evidence_note_compact"));
        move.structuralLinkFormatVersion = requiredString(moveObject, QStringLiteral("structural_link_format_version"));
        move.linkedKingZoneTarget = requiredString(moveObject, QStringLiteral("linked_king_zone_target"));
        move.linkedVulnerablePieceTarget = requiredString(moveObject, QStringLiteral("linked_vulnerable_piece_target"));
        move.linkedPressureLaneTarget = requiredString(moveObject, QStringLiteral("linked_pressure_lane_target"));
        move.linkedDecisiveImbalanceTarget = requiredString(moveObject, QStringLiteral("linked_decisive_imbalance_target"));
        move.linkedPinnedCriticalPiece = requiredString(moveObject, QStringLiteral("linked_pinned_critical_piece"));
        move.linkedDefenderRemovalExposure = requiredString(moveObject, QStringLiteral("linked_defender_removal_exposure"));
        move.linkedKingColorComplex = requiredString(moveObject, QStringLiteral("linked_king_color_complex"));
        move.linkedTargetZoneImbalance = requiredString(moveObject, QStringLiteral("linked_target_zone_imbalance"));
        move.linkedAttackerCoordination = requiredString(moveObject, QStringLiteral("linked_attacker_coordination"));
        move.linkedDefensiveNetworkFragility = requiredString(moveObject, QStringLiteral("linked_defensive_network_fragility"));
        move.structuralLinkSummary = requiredString(moveObject, QStringLiteral("structural_link_summary"));
        move.strongerAlternativeCount = moveObject.value(QStringLiteral("stronger_alternative_count")).toInt();
        response.criticalMoves.append(move);
    }

    response.ok = !response.analysisRun.runId.isEmpty() && !response.tacticalEvent.eventId.isEmpty() && !response.criticalMoves.isEmpty();
    if (!response.ok) {
        response.errorMessage = QStringLiteral("worker response was missing required analysis payload");
    }
    return response;
}

} // namespace worker_protocol
