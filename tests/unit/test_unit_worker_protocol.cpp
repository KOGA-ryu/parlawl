#include <QFile>
#include <QtTest>

#include "parlawl_config.h"
#include "worker_protocol.h"

class TestUnitWorkerProtocol : public QObject
{
    Q_OBJECT

private slots:
    void requestIncludesEngineConfig();
    void parsesStructuredWorkerResponse();
};

void TestUnitWorkerProtocol::requestIncludesEngineConfig()
{
    AnalysisRun run;
    run.runId = QStringLiteral("run-1");
    run.puzzleId = QStringLiteral("puzzle-1");
    run.status = QStringLiteral("running");
    run.engineMode = QStringLiteral("stockfish_window_v0_1");
    run.engineName = QStringLiteral("stockfish");
    run.engineDepth = 10;
    run.createdAtUtc = QDateTime::fromString(QStringLiteral("2026-03-28T12:00:00.000Z"), Qt::ISODate);

    PuzzleRound puzzle;
    puzzle.puzzleId = QStringLiteral("puzzle-1");
    puzzle.sourceGameId = QStringLiteral("game-1");
    puzzle.sideToMove = QStringLiteral("white");
    puzzle.fetchedAtUtc = run.createdAtUtc;

    SourceGame game;
    game.sourceGameId = QStringLiteral("game-1");
    game.fetchedAtUtc = run.createdAtUtc;

    const QJsonObject root = worker_protocol::makeAnalyzeLatestRequest(
        run,
        puzzle,
        game,
        QStringLiteral("/opt/homebrew/bin/stockfish")
    ).object();
    QCOMPARE(root.value(QStringLiteral("engine")).toObject().value(QStringLiteral("stockfish_path")).toString(), QStringLiteral("/opt/homebrew/bin/stockfish"));
    QCOMPARE(root.value(QStringLiteral("engine")).toObject().value(QStringLiteral("depth")).toInt(), 10);
    QCOMPARE(root.value(QStringLiteral("engine")).toObject().value(QStringLiteral("multipv")).toInt(), 3);
}

void TestUnitWorkerProtocol::parsesStructuredWorkerResponse()
{
    QFile file(QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_response_fixture.json"));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));

    const WorkerAnalysisResponse response = worker_protocol::parseAnalysisResponse(file.readAll());
    QVERIFY2(response.ok, qPrintable(response.errorMessage));
    QCOMPARE(response.analysisRun.runId, QStringLiteral("fixture-run-001"));
    QCOMPARE(response.analysisRun.engineName, QStringLiteral("stockfish"));
    QCOMPARE(response.analysisRun.engineDepth, 10);
    QCOMPARE(response.tacticalEvent.openingFamily, QStringLiteral("italian_game"));
    QCOMPARE(response.tacticalEvent.assistantInferenceStatus, QStringLiteral("pending"));
    QCOMPARE(response.tacticalEvent.kingExposureType, QStringLiteral("no_clear_exposure"));
    QCOMPARE(response.tacticalEvent.kingExposureSeverity, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.loosePieceCount, 0);
    QCOMPARE(response.tacticalEvent.loosePieceSummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.overloadedDefenderCount, 0);
    QCOMPARE(response.tacticalEvent.overloadedDefenderSummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.backRankState, QStringLiteral("back_rank_stable"));
    QCOMPARE(response.tacticalEvent.luftState, QStringLiteral("limited_luft"));
    QCOMPARE(response.tacticalEvent.kingLinePressureType, QStringLiteral("diagonal_pressure"));
    QCOMPARE(response.tacticalEvent.kingSquarePressureType, QStringLiteral("balanced_square_control"));
    QCOMPARE(response.tacticalEvent.criticalPieceImbalanceSummary, QStringLiteral("balanced_local_targets"));
    QCOMPARE(response.tacticalEvent.structuralFeatureSummary, QStringLiteral("limited_luft | diagonal_pressure"));
    QCOMPARE(response.tacticalEvent.structuralFeatureConfidence, QStringLiteral("medium"));
    QCOMPARE(response.tacticalEvent.kingZoneTargetType, QStringLiteral("no_clear_king_zone_target"));
    QCOMPARE(response.tacticalEvent.kingZoneTargetSummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.vulnerablePieceTargetType, QStringLiteral("no_clear_vulnerable_piece_target"));
    QCOMPARE(response.tacticalEvent.vulnerablePieceTargetSummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.pressureLaneTargetType, QStringLiteral("diagonal_lane_target"));
    QCOMPARE(response.tacticalEvent.pressureLaneTargetSummary, QStringLiteral("diag b3-f7"));
    QCOMPARE(response.tacticalEvent.decisiveImbalanceTarget, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.pinnedCriticalPieceType, QStringLiteral("no_clear_pinned_critical_piece"));
    QCOMPARE(response.tacticalEvent.pinnedCriticalPieceSummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.defenderRemovalExposureType, QStringLiteral("no_clear_defender_removal_exposure"));
    QCOMPARE(response.tacticalEvent.defenderRemovalExposureSummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.kingColorComplexState, QStringLiteral("no_clear_color_complex_weakness"));
    QCOMPARE(response.tacticalEvent.kingColorComplexSummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.targetZoneImbalanceType, QStringLiteral("balanced_target_zone"));
    QCOMPARE(response.tacticalEvent.targetZoneImbalanceSummary, QStringLiteral("balanced"));
    QCOMPARE(response.tacticalEvent.structuralV2Summary, QStringLiteral("no_clear_structural_v2"));
    QCOMPARE(response.tacticalEvent.structuralV2Confidence, QStringLiteral("low"));
    QCOMPARE(response.tacticalEvent.escapeGeometryState, QStringLiteral("no_clear_escape_geometry"));
    QCOMPARE(response.tacticalEvent.escapeGeometrySummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.flightControlType, QStringLiteral("no_clear_flight_control"));
    QCOMPARE(response.tacticalEvent.flightControlSummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.defensiveEscapeFragilityType, QStringLiteral("no_clear_escape_fragility"));
    QCOMPARE(response.tacticalEvent.defensiveEscapeFragilitySummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.structuralV3Summary, QStringLiteral("no_clear_structural_v3"));
    QCOMPARE(response.tacticalEvent.structuralV3Confidence, QStringLiteral("low"));
    QCOMPARE(response.tacticalEvent.attackerCoordinationType, QStringLiteral("no_clear_attacker_coordination"));
    QCOMPARE(response.tacticalEvent.attackerCoordinationSummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.defensiveNetworkFragilityType, QStringLiteral("no_clear_defensive_network_fragility"));
    QCOMPARE(response.tacticalEvent.defensiveNetworkFragilitySummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.structuralV4Summary, QStringLiteral("no_clear_structural_v4"));
    QCOMPARE(response.tacticalEvent.structuralV4Confidence, QStringLiteral("low"));
    QCOMPARE(response.tacticalEvent.localTargetSummary, QStringLiteral("diag b3-f7"));
    QCOMPARE(response.tacticalEvent.localTargetConfidence, QStringLiteral("medium"));
    QCOMPARE(response.tacticalEvent.mappingMethod, QStringLiteral("exact_initial_ply_match"));
    QCOMPARE(response.tacticalEvent.mappedStartPly, 18);
    QCOMPARE(response.tacticalEvent.analysisWindowStartPly, 14);
    QCOMPARE(response.tacticalEvent.primaryBreakPly, 18);
    QCOMPARE(response.tacticalEvent.primaryBreakReason, QStringLiteral("narrow_missed_defense"));
    QCOMPARE(response.tacticalEvent.retainedBreakFormatVersion, QStringLiteral("rbsv1"));
    QCOMPARE(response.tacticalEvent.retainedBreakRole, QStringLiteral("last_holding_defense"));
    QCOMPARE(response.tacticalEvent.retainedBreakPlayedMove, QStringLiteral("d6d5"));
    QCOMPARE(response.tacticalEvent.retainedBreakBestMove, QStringLiteral("h7h6"));
    QCOMPARE(response.tacticalEvent.retainedBreakCompactSequence, QStringLiteral("stronger alternatives available"));
    QCOMPARE(
        response.tacticalEvent.retainedBreakSummary,
        QStringLiteral("last_holding_defense@18 | played d6d5 | best h7h6 | stronger alternatives available")
    );
    QVERIFY(response.tacticalEvent.bestVsPlayedDivergenceSummary.contains(QStringLiteral("114 cp")));
    QCOMPARE(response.tacticalEvent.divergenceType, QStringLiteral("stronger_alternatives_available"));
    QCOMPARE(response.tacticalEvent.divergenceSeverity, QStringLiteral("moderate"));
    QCOMPARE(response.tacticalEvent.divergenceCompactSummary, QStringLiteral("multiple stronger alternatives existed"));
    QVERIFY(response.tacticalEvent.divergenceEvidenceNotes.contains(QStringLiteral("3 stronger analysed local alternatives")));
    QCOMPARE(response.tacticalEvent.localSequenceConfidence, QStringLiteral("medium"));
    QCOMPARE(response.tacticalEvent.collapseSequenceFormatVersion, QStringLiteral("csv1"));
    QCOMPARE(response.tacticalEvent.collapseSequenceType, QStringLiteral("single_break"));
    QCOMPARE(
        response.tacticalEvent.collapseSequenceSummary,
        QStringLiteral("single_break | break@18 | roles last_holding_defense@18 | omitted 0")
    );
    QCOMPARE(response.tacticalEvent.retainedRoleSummary, QStringLiteral("last_holding_defense@18"));
    QCOMPARE(response.tacticalEvent.omittedAdjacentCandidateCount, 0);
    QCOMPARE(response.tacticalEvent.omissionReasonSummary, QStringLiteral("none"));
    QCOMPARE(response.tacticalEvent.omissionReasonCountsJson, QStringLiteral("{}"));
    QCOMPARE(response.tacticalEvent.engineLimitSummary, QStringLiteral("depth=10, multipv=3, window_before=4, window_after=4"));
    QCOMPARE(response.criticalMoves.size(), 1);
    QCOMPARE(response.criticalMoves.first().bestMove, QStringLiteral("h7h6"));
    QVERIFY(response.criticalMoves.first().hasEvalDeltaCp);
    QCOMPARE(response.criticalMoves.first().whyCritical, QStringLiteral("stronger_alternative_missed | stronger alternative missed"));
    QCOMPARE(response.criticalMoves.first().criticalReasonType, QStringLiteral("stronger_alternative_missed"));
    QCOMPARE(response.criticalMoves.first().criticalReasonSeverity, QStringLiteral("moderate"));
    QCOMPARE(response.criticalMoves.first().criticalReasonCompactSummary, QStringLiteral("stronger alternative missed"));
    QCOMPARE(response.criticalMoves.first().continuationFormatVersion, QStringLiteral("cmv1"));
    QCOMPARE(response.criticalMoves.first().bestContinuationCompact, QStringLiteral("stronger line retained"));
    QCOMPARE(response.criticalMoves.first().playedContinuationCompact, QStringLiteral("stronger line missed"));
    QCOMPARE(response.criticalMoves.first().candidateRankingType, QStringLiteral("several_strong_alternatives"));
    QCOMPARE(response.criticalMoves.first().candidateRankingSeverity, QStringLiteral("moderate"));
    QCOMPARE(response.criticalMoves.first().candidateRankingCompactSummary, QStringLiteral("3 stronger alternatives"));
    QCOMPARE(
        response.criticalMoves.first().candidateRankingSummary,
        QStringLiteral("several_strong_alternatives | 3 stronger alternatives")
    );
    QVERIFY(
        response.criticalMoves.first().candidateMovesJson.contains(
            QStringLiteral("\"candidate_display_format_version\":\"cdv1\"")
        )
    );
    QVERIFY(
        response.criticalMoves.first().candidateMovesJson.contains(
            QStringLiteral("\"candidate_display_compact\":\"1. h6 | cp -21 | best\"")
        )
    );
    QCOMPARE(response.criticalMoves.first().evidenceNoteType, QStringLiteral("stronger_alternatives_missed"));
    QCOMPARE(response.criticalMoves.first().evidenceNoteSeverity, QStringLiteral("moderate"));
    QCOMPARE(response.criticalMoves.first().evidenceNoteCompact, QStringLiteral("3 stronger alternatives missed"));
    QCOMPARE(response.criticalMoves.first().structuralLinkFormatVersion, QStringLiteral("mtlv3"));
    QCOMPARE(response.criticalMoves.first().linkedKingZoneTarget, QStringLiteral("none"));
    QCOMPARE(response.criticalMoves.first().linkedVulnerablePieceTarget, QStringLiteral("none"));
    QCOMPARE(response.criticalMoves.first().linkedPressureLaneTarget, QStringLiteral("diag b3-f7"));
    QCOMPARE(response.criticalMoves.first().linkedDecisiveImbalanceTarget, QStringLiteral("none"));
    QCOMPARE(response.criticalMoves.first().linkedPinnedCriticalPiece, QStringLiteral("none"));
    QCOMPARE(response.criticalMoves.first().linkedDefenderRemovalExposure, QStringLiteral("none"));
    QCOMPARE(response.criticalMoves.first().linkedKingColorComplex, QStringLiteral("none"));
    QCOMPARE(response.criticalMoves.first().linkedTargetZoneImbalance, QStringLiteral("none"));
    QCOMPARE(response.criticalMoves.first().linkedAttackerCoordination, QStringLiteral("none"));
    QCOMPARE(response.criticalMoves.first().linkedDefensiveNetworkFragility, QStringLiteral("none"));
    QCOMPARE(response.criticalMoves.first().structuralLinkSummary, QStringLiteral("lane diag b3-f7"));
    QVERIFY(!response.criticalMoves.first().pvSan.isEmpty());
    QCOMPARE(response.criticalMoves.first().onlyMoveStatus, QStringLiteral("multiple_viable"));
    QCOMPARE(
        response.criticalMoves.first().onlyMoveReasoning,
        QStringLiteral("multiple local candidates remain within the viable evaluation band")
    );
    QCOMPARE(response.criticalMoves.first().strongerAlternativeCount, 3);
    QVERIFY(response.criticalMoves.first().candidateMovesJson.contains(QStringLiteral("\"rank\":1")));
    QCOMPARE(
        response.criticalMoves.first().candidateRankingSummary,
        QStringLiteral("several_strong_alternatives | 3 stronger alternatives")
    );
}

QTEST_GUILESS_MAIN(TestUnitWorkerProtocol)

#include "test_unit_worker_protocol.moc"
