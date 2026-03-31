#include <QtTest>

#include "critical_move.h"
#include "puzzle_round.h"
#include "report_formatter.h"
#include "source_game.h"
#include "tactical_event.h"

class TestUnitReportFormatter : public QObject
{
    Q_OBJECT

private slots:
    void rendersEngineEvidence();
    void humanizesFallbackOpeningCandidate();
};

void TestUnitReportFormatter::rendersEngineEvidence()
{
    PuzzleRound puzzle;
    puzzle.puzzleId = QStringLiteral("p1");
    puzzle.whitePlayer = QStringLiteral("w");
    puzzle.blackPlayer = QStringLiteral("b");
    puzzle.whiteRating = 1700;
    puzzle.blackRating = 1750;
    puzzle.timeControl = QStringLiteral("300+0");

    SourceGame game;
    game.sourceGameId = QStringLiteral("g1");
    game.pgnText = QStringLiteral("[Event \"Test\"]\n[ECO \"C50\"]\n[Opening \"Italian Game\"]\n\n1. e4 e5");

    TacticalEvent event;
    event.mappingStatus = QStringLiteral("mapped");
    event.mappedStartPly = 18;
    event.mappingMethod = QStringLiteral("exact_initial_ply_match");
    event.mappingConfidence = QStringLiteral("high");
    event.mappingNotes = QStringLiteral("initialPly and initial_fen identity agreed during PGN replay");
    event.openingFamily = QStringLiteral("italian_game");
    event.gamePhase = QStringLiteral("opening");
    event.analysisWindowStartPly = 15;
    event.analysisWindowEndPly = 23;
    event.primaryBreakPly = 18;
    event.primaryBreakReason = QStringLiteral("collapse_trigger");
    event.retainedBreakFormatVersion = QStringLiteral("rbsv1");
    event.retainedBreakRole = QStringLiteral("decisive_blunder");
    event.retainedBreakPlayedMove = QStringLiteral("d7d5");
    event.retainedBreakBestMove = QStringLiteral("e5d4");
    event.retainedBreakCompactSequence = QStringLiteral("328 cp collapse");
    event.retainedBreakSummary = QStringLiteral("decisive_blunder@18 | played d7d5 | best e5d4 | 328 cp collapse");
    event.bestVsPlayedDivergenceSummary = QStringLiteral("the played line underperformed multiple stronger local continuations by about 328 cp");
    event.divergenceType = QStringLiteral("centipawn_collapse");
    event.divergenceSeverity = QStringLiteral("major");
    event.divergenceCompactSummary = QStringLiteral("328 cp collapse");
    event.divergenceEvidenceNotes = QStringLiteral("the best local continuation preserved a materially stronger evaluation than the played continuation");
    event.localSequenceConfidence = QStringLiteral("high");
    event.collapseSequenceFormatVersion = QStringLiteral("csv1");
    event.collapseSequenceType = QStringLiteral("single_break");
    event.collapseSequenceSummary = QStringLiteral("single_break | break@18 | roles decisive_blunder@18 | omitted 1");
    event.retainedRoleSummary = QStringLiteral("decisive_blunder@18");
    event.omittedAdjacentCandidateCount = 1;
    event.omissionReasonSummary = QStringLiteral("adjacent_duplicate=1");
    event.omissionReasonCountsJson = QStringLiteral("{\"adjacent_duplicate\":1}");
    event.engineLimitSummary = QStringLiteral("depth=10, multipv=3, window_before=4, window_after=4");
    event.tacticalCandidatesJson = QStringLiteral("[\"fork\"]");
    event.structuralCandidatesJson = QStringLiteral("[\"king_exposure\",\"back_rank_weakness\",\"pressure_lane\",\"vulnerable_piece\",\"overloaded_defender\",\"decisive_imbalance\",\"king_zone_target\"]");
    event.materialBalance = QStringLiteral("equal");
    event.kingSafetyState = QStringLiteral("back_rank_danger");
    event.pieceActivity = QStringLiteral("mixed_local_activity");
    event.keyWeakness = QStringLiteral("king_exposure");
    event.kingExposureType = QStringLiteral("exposed_open_file");
    event.kingExposureSeverity = QStringLiteral("high");
    event.loosePieceCount = 1;
    event.loosePieceSummary = QStringLiteral("n@e5");
    event.overloadedDefenderCount = 1;
    event.overloadedDefenderSummary = QStringLiteral("n@f6 duties=2");
    event.backRankState = QStringLiteral("back_rank_vulnerable");
    event.luftState = QStringLiteral("no_luft");
    event.kingLinePressureType = QStringLiteral("file_pressure");
    event.kingSquarePressureType = QStringLiteral("localized_square_pressure");
    event.criticalPieceImbalanceSummary = QStringLiteral("p@d5 2v1");
    event.structuralFeatureSummary = QStringLiteral("exposed_open_file | back_rank_vulnerable | file_pressure | loose 1 | overloaded 1 | piece_imbalance");
    event.structuralFeatureConfidence = QStringLiteral("high");
    event.kingZoneTargetType = QStringLiteral("king_square_target");
    event.kingZoneTargetSummary = QStringLiteral("g8 3v1");
    event.vulnerablePieceTargetType = QStringLiteral("underdefended_piece_target");
    event.vulnerablePieceTargetSummary = QStringLiteral("p@d5 2v1");
    event.pressureLaneTargetType = QStringLiteral("file_lane_target");
    event.pressureLaneTargetSummary = QStringLiteral("file g");
    event.decisiveImbalanceTarget = QStringLiteral("p@d5 2v1");
    event.pinnedCriticalPieceType = QStringLiteral("pinned_overloaded_defender");
    event.pinnedCriticalPieceSummary = QStringLiteral("n@f6");
    event.defenderRemovalExposureType = QStringLiteral("deflection_sensitive_defense");
    event.defenderRemovalExposureSummary = QStringLiteral("n@f6 -> g8 3v1");
    event.kingColorComplexState = QStringLiteral("weak_dark_complex");
    event.kingColorComplexSummary = QStringLiteral("dark 2 weak squares");
    event.targetZoneImbalanceType = QStringLiteral("attacker_heavy");
    event.targetZoneImbalanceSummary = QStringLiteral("g8 3v1");
    event.structuralV2Summary = QStringLiteral("pinned n@f6 | defender removal exposure | weak_dark_complex | attacker_heavy");
    event.structuralV2Confidence = QStringLiteral("high");
    event.escapeGeometryState = QStringLiteral("sealed_king_box");
    event.escapeGeometrySummary = QStringLiteral("escapes 0 | attacked 3 | blocked 2");
    event.flightControlType = QStringLiteral("mixed_flight_control");
    event.flightControlSummary = QStringLiteral("g8 3v1 + 3 attacked flights");
    event.defensiveEscapeFragilityType = QStringLiteral("overloaded_escape_defender");
    event.defensiveEscapeFragilitySummary = QStringLiteral("n@f6");
    event.structuralV3Summary = QStringLiteral("sealed_king_box | mixed_flight_control | overloaded_escape_defender");
    event.structuralV3Confidence = QStringLiteral("high");
    event.attackerCoordinationType = QStringLiteral("file_battery");
    event.attackerCoordinationSummary = QStringLiteral("file g via q@g3+r@g1");
    event.defensiveNetworkFragilityType = QStringLiteral("collapsing_defender_cluster");
    event.defensiveNetworkFragilitySummary = QStringLiteral("g8 3v1 via n@f6");
    event.structuralV4Summary = QStringLiteral("file_battery | collapsing_defender_cluster");
    event.structuralV4Confidence = QStringLiteral("high");
    event.localTargetSummary = QStringLiteral("king-zone g8 3v1 | piece p@d5 2v1 | file g | imbalance p@d5 2v1");
    event.localTargetConfidence = QStringLiteral("high");
    event.assistantInferenceStatus = QStringLiteral("provided");
    event.assistantLabelsJson = QStringLiteral("[\"mate_net\",\"king_hunt\"]");
    event.assistantSummaryMarkdown = QStringLiteral("## external inference\n- black's king was boxed in.");

    CriticalMove move;
    move.role = QStringLiteral("decisive_blunder");
    move.ply = 19;
    move.playedMove = QStringLiteral("d7d5");
    move.bestMove = QStringLiteral("e5d4");
    move.hasEvalBeforeCp = true;
    move.evalBeforeCp = 22;
    move.hasEvalAfterPlayedCp = true;
    move.evalAfterPlayedCp = -310;
    move.hasEvalAfterBestCp = true;
    move.evalAfterBestCp = 18;
    move.hasEvalDeltaCp = true;
    move.evalDeltaCp = 328;
    move.decisiveSwing = QStringLiteral("decisive");
    move.analysisDepth = 10;
    move.mateFlag = QStringLiteral("none");
    move.criticalReasonType = QStringLiteral("eval_collapse_trigger");
    move.criticalReasonSeverity = QStringLiteral("major");
    move.criticalReasonCompactSummary = QStringLiteral("328 cp collapse");
    move.continuationFormatVersion = QStringLiteral("cmv1");
    move.bestContinuationCompact = QStringLiteral("cp collapse avoided");
    move.playedContinuationCompact = QStringLiteral("328 cp collapse");
    move.pvSan = QStringLiteral("9...h6 10. Nbd2 Re8 11. Nf1");
    move.bestContinuationSummary = QStringLiteral("best h7h6: 9...h6 10. Nbd2 Re8 11. Nf1");
    move.playedContinuationSummary = QStringLiteral("played d6d5: 10. exd5 Nxd5 11. Nxe5 Nxe5");
    move.onlyMoveStatus = QStringLiteral("multiple_viable");
    move.hasOnlyMoveMarginCp = true;
    move.onlyMoveMarginCp = 12;
    move.onlyMoveReasoning = QStringLiteral("multiple local candidates remain within the viable evaluation band");
    move.candidateRankingType = QStringLiteral("played_collapse");
    move.candidateRankingSeverity = QStringLiteral("major");
    move.candidateRankingCompactSummary = QStringLiteral("328 cp collapse");
    move.candidateRankingSummary = QStringLiteral("played_collapse | 328 cp collapse");
    move.evidenceNoteType = QStringLiteral("collapse_triggered");
    move.evidenceNoteSeverity = QStringLiteral("major");
    move.evidenceNoteCompact = QStringLiteral("328 cp collapse");
    move.structuralLinkFormatVersion = QStringLiteral("mtlv3");
    move.linkedKingZoneTarget = QStringLiteral("g8 3v1");
    move.linkedVulnerablePieceTarget = QStringLiteral("p@d5 2v1");
    move.linkedPressureLaneTarget = QStringLiteral("file g");
    move.linkedDecisiveImbalanceTarget = QStringLiteral("p@d5 2v1");
    move.linkedPinnedCriticalPiece = QStringLiteral("n@f6");
    move.linkedDefenderRemovalExposure = QStringLiteral("n@f6 -> g8 3v1");
    move.linkedKingColorComplex = QStringLiteral("dark 2 weak squares");
    move.linkedTargetZoneImbalance = QStringLiteral("g8 3v1");
    move.linkedAttackerCoordination = QStringLiteral("file g via q@g3+r@g1");
    move.linkedDefensiveNetworkFragility = QStringLiteral("g8 2v1 via n@f6");
    move.structuralLinkSummary = QStringLiteral(
        "king-zone g8 3v1 | piece p@d5 2v1 | lane file g | imbalance p@d5 2v1 | "
        "pinned n@f6 | defender-removal n@f6 -> g8 3v1 | color-complex dark 2 weak squares | target-zone g8 3v1 | "
        "coordination file g via q@g3+r@g1 | network-fragility g8 2v1 via n@f6"
    );
    move.strongerAlternativeCount = 2;
    move.candidateMovesJson = QStringLiteral(
        "["
        "{\"move_uci\":\"e5d4\",\"move_san\":\"exd4\",\"eval_cp\":18,\"mate_flag\":\"none\",\"mate_distance\":null,"
        "\"score_kind\":\"cp\",\"rank\":1,\"is_played_move\":false,\"is_best_move\":true,"
        "\"candidate_display_format_version\":\"cdv1\",\"candidate_display_compact\":\"1. exd4 | cp +18 | best\"},"
        "{\"move_uci\":\"c7c6\",\"move_san\":\"c6\",\"eval_cp\":-12,\"mate_flag\":\"none\",\"mate_distance\":null,"
        "\"score_kind\":\"cp\",\"rank\":2,\"is_played_move\":false,\"is_best_move\":false,"
        "\"candidate_display_format_version\":\"cdv1\",\"candidate_display_compact\":\"2. c6 | cp -12\"},"
        "{\"move_uci\":\"d7d5\",\"move_san\":\"d5\",\"eval_cp\":-310,\"mate_flag\":\"none\",\"mate_distance\":null,"
        "\"score_kind\":\"cp\",\"rank\":3,\"is_played_move\":true,\"is_best_move\":false,"
        "\"candidate_display_format_version\":\"cdv1\",\"candidate_display_compact\":\"3. d5 | cp -310 | played\"}"
        "]"
    );

    const QString report = ReportFormatter::formatAnalysisReport(puzzle, game, event, {move});
    QVERIFY(report.contains(QStringLiteral("opening candidate: Italian Game (C50)")));
    QVERIFY(report.contains(QStringLiteral("mapped start ply: 18")));
    QVERIFY(report.contains(QStringLiteral("mapping method: exact_initial_ply_match")));
    QVERIFY(report.contains(QStringLiteral("analysis window: ply 15 to 23")));
    QVERIFY(report.contains(QStringLiteral("primary break ply: 18")));
    QVERIFY(report.contains(QStringLiteral("primary break reason: collapse_trigger")));
    QVERIFY(report.contains(QStringLiteral("retained break summary: decisive_blunder@18 | played d7d5 | best e5d4 | 328 cp collapse")));
    QVERIFY(report.contains(QStringLiteral("divergence type: centipawn_collapse")));
    QVERIFY(report.contains(QStringLiteral("divergence severity: major")));
    QVERIFY(report.contains(QStringLiteral("compact divergence: 328 cp collapse")));
    QVERIFY(report.contains(QStringLiteral("best-vs-played divergence: the played line underperformed multiple stronger local continuations by about 328 cp")));
    QVERIFY(report.contains(QStringLiteral("local sequence confidence: high")));
    QVERIFY(report.contains(QStringLiteral("collapse sequence type: single_break")));
    QVERIFY(report.contains(QStringLiteral("retained roles: decisive_blunder@18")));
    QVERIFY(report.contains(QStringLiteral("collapse sequence: single_break | break@18 | roles decisive_blunder@18 | omitted 1")));
    QVERIFY(report.contains(QStringLiteral("omitted adjacent candidates: 1")));
    QVERIFY(report.contains(QStringLiteral("omission reasons: adjacent_duplicate=1")));
    QVERIFY(report.contains(QStringLiteral("engine limits: depth=10, multipv=3, window_before=4, window_after=4")));
    QVERIFY(report.contains(QStringLiteral("structural candidates: [\"king_exposure\",\"back_rank_weakness\",\"pressure_lane\",\"vulnerable_piece\",\"overloaded_defender\",\"decisive_imbalance\",\"king_zone_target\"]")));
    QVERIFY(report.contains(QStringLiteral("king safety state: back_rank_danger")));
    QVERIFY(report.contains(QStringLiteral("piece activity facts: mixed_local_activity")));
    QVERIFY(report.contains(QStringLiteral("key weakness candidates: king_exposure")));
    QVERIFY(report.contains(QStringLiteral("structural feature summary: exposed_open_file | back_rank_vulnerable | file_pressure | loose 1 | overloaded 1 | piece_imbalance")));
    QVERIFY(report.contains(QStringLiteral("structural feature confidence: high")));
    QVERIFY(report.contains(QStringLiteral("king exposure: exposed_open_file (high)")));
    QVERIFY(report.contains(QStringLiteral("loose pieces: 1 | n@e5")));
    QVERIFY(report.contains(QStringLiteral("overloaded defenders: 1 | n@f6 duties=2")));
    QVERIFY(report.contains(QStringLiteral("back rank state: back_rank_vulnerable")));
    QVERIFY(report.contains(QStringLiteral("luft state: no_luft")));
    QVERIFY(report.contains(QStringLiteral("king line pressure: file_pressure")));
    QVERIFY(report.contains(QStringLiteral("king square pressure: localized_square_pressure")));
    QVERIFY(report.contains(QStringLiteral("critical piece imbalance: p@d5 2v1")));
    QVERIFY(report.contains(QStringLiteral("local target summary: king-zone g8 3v1 | piece p@d5 2v1 | file g | imbalance p@d5 2v1")));
    QVERIFY(report.contains(QStringLiteral("local target confidence: high")));
    QVERIFY(report.contains(QStringLiteral("king-zone target: king_square_target | g8 3v1")));
    QVERIFY(report.contains(QStringLiteral("vulnerable-piece target: underdefended_piece_target | p@d5 2v1")));
    QVERIFY(report.contains(QStringLiteral("pressure lane target: file_lane_target | file g")));
    QVERIFY(report.contains(QStringLiteral("decisive imbalance target: p@d5 2v1")));
    QVERIFY(report.contains(QStringLiteral("structural v2 summary: pinned n@f6 | defender removal exposure | weak_dark_complex | attacker_heavy")));
    QVERIFY(report.contains(QStringLiteral("structural v2 confidence: high")));
    QVERIFY(report.contains(QStringLiteral("pinned critical piece: pinned_overloaded_defender | n@f6")));
    QVERIFY(report.contains(QStringLiteral("defender-removal exposure: deflection_sensitive_defense | n@f6 -> g8 3v1")));
    QVERIFY(report.contains(QStringLiteral("king color-complex: weak_dark_complex | dark 2 weak squares")));
    QVERIFY(report.contains(QStringLiteral("target-zone imbalance: attacker_heavy | g8 3v1")));
    QVERIFY(report.contains(QStringLiteral("structural v3 summary: sealed_king_box | mixed_flight_control | overloaded_escape_defender")));
    QVERIFY(report.contains(QStringLiteral("structural v3 confidence: high")));
    QVERIFY(report.contains(QStringLiteral("escape geometry: sealed_king_box | escapes 0 | attacked 3 | blocked 2")));
    QVERIFY(report.contains(QStringLiteral("flight control: mixed_flight_control | g8 3v1 + 3 attacked flights")));
    QVERIFY(report.contains(QStringLiteral("defensive escape fragility: overloaded_escape_defender | n@f6")));
    QVERIFY(report.contains(QStringLiteral("structural v4 summary: file_battery | collapsing_defender_cluster")));
    QVERIFY(report.contains(QStringLiteral("structural v4 confidence: high")));
    QVERIFY(report.contains(QStringLiteral("attacker coordination: file_battery | file g via q@g3+r@g1")));
    QVERIFY(report.contains(QStringLiteral("defensive network fragility: collapsing_defender_cluster | g8 3v1 via n@f6")));
    QVERIFY(report.contains(QStringLiteral("eval_delta=328")));
    QVERIFY(report.contains(QStringLiteral("critical reason type: eval_collapse_trigger")));
    QVERIFY(report.contains(QStringLiteral("critical reason severity: major")));
    QVERIFY(report.contains(QStringLiteral("critical reason: 328 cp collapse")));
    QVERIFY(report.contains(QStringLiteral("candidate ranking type: played_collapse")));
    QVERIFY(report.contains(QStringLiteral("candidate ranking severity: major")));
    QVERIFY(report.contains(QStringLiteral("candidate ranking: 328 cp collapse")));
    QVERIFY(report.contains(QStringLiteral("evidence note type: collapse_triggered")));
    QVERIFY(report.contains(QStringLiteral("evidence note severity: major")));
    QVERIFY(report.contains(QStringLiteral("evidence note: 328 cp collapse")));
    QVERIFY(report.contains(QStringLiteral("structural link: king-zone g8 3v1 | piece p@d5 2v1 | lane file g | imbalance p@d5 2v1 | pinned n@f6 | defender-removal n@f6 -> g8 3v1 | color-complex dark 2 weak squares | target-zone g8 3v1 | coordination file g via q@g3+r@g1 | network-fragility g8 2v1 via n@f6")));
    QVERIFY(report.contains(QStringLiteral("linked king-zone target: g8 3v1")));
    QVERIFY(report.contains(QStringLiteral("linked vulnerable-piece target: p@d5 2v1")));
    QVERIFY(report.contains(QStringLiteral("linked pressure-lane target: file g")));
    QVERIFY(report.contains(QStringLiteral("linked decisive-imbalance target: p@d5 2v1")));
    QVERIFY(report.contains(QStringLiteral("linked pinned critical piece: n@f6")));
    QVERIFY(report.contains(QStringLiteral("linked defender-removal exposure: n@f6 -> g8 3v1")));
    QVERIFY(report.contains(QStringLiteral("linked king color-complex: dark 2 weak squares")));
    QVERIFY(report.contains(QStringLiteral("linked target-zone imbalance: g8 3v1")));
    QVERIFY(report.contains(QStringLiteral("linked attacker coordination: file g via q@g3+r@g1")));
    QVERIFY(report.contains(QStringLiteral("linked defensive network fragility: g8 2v1 via n@f6")));
    QVERIFY(report.contains(QStringLiteral("best continuation compact: cp collapse avoided")));
    QVERIFY(report.contains(QStringLiteral("played continuation compact: 328 cp collapse")));
    QVERIFY(report.contains(QStringLiteral("pv_san: 9...h6 10. Nbd2 Re8 11. Nf1")));
    QVERIFY(report.contains(QStringLiteral("only-move reasoning: multiple local candidates remain within the viable evaluation band")));
    QVERIFY(report.contains(QStringLiteral("ranking summary: played_collapse | 328 cp collapse")));
    QVERIFY(report.contains(QStringLiteral("candidates:")));
    QVERIFY(report.contains(QStringLiteral("1. exd4 | cp +18 | best")));
    QVERIFY(report.contains(QStringLiteral("2. c6 | cp -12")));
    QVERIFY(report.contains(QStringLiteral("3. d5 | cp -310 | played")));
    QVERIFY(report.contains(QStringLiteral("Assistant Inference")));
    QVERIFY(report.contains(QStringLiteral("status: provided")));
    QVERIFY(report.contains(QStringLiteral("labels json: [\"mate_net\",\"king_hunt\"]")));
    QVERIFY(report.contains(QStringLiteral("summary markdown: ## external inference")));
    QVERIFY(report.contains(QStringLiteral("- black's king was boxed in.")));
}

void TestUnitReportFormatter::humanizesFallbackOpeningCandidate()
{
    PuzzleRound puzzle;
    puzzle.puzzleId = QStringLiteral("p-fallback");

    SourceGame game;
    game.sourceGameId = QStringLiteral("g-fallback");
    game.pgnText = QStringLiteral("1. e4 e5 2. Nf3 Nc6");

    TacticalEvent event;
    event.openingFamily = QStringLiteral("italian_game");

    const QString report = ReportFormatter::formatAnalysisReport(puzzle, game, event, {});
    QVERIFY(report.contains(QStringLiteral("opening candidate: Italian Game")));
    QVERIFY(report.contains(QStringLiteral("opening family candidate: italian_game")));
}

QTEST_GUILESS_MAIN(TestUnitReportFormatter)

#include "test_unit_report_formatter.moc"
