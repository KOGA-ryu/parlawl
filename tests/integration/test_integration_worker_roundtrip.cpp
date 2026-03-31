#include <QFile>
#include <QProcess>
#include <QtTest>

#include "parlawl_config.h"
#include "worker_protocol.h"

class TestIntegrationWorkerRoundtrip : public QObject
{
    Q_OBJECT

private slots:
    void workerProducesSchemaAlignedResponse();
};

void TestIntegrationWorkerRoundtrip::workerProducesSchemaAlignedResponse()
{
    QFile requestFile(QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_request_fixture.json"));
    QVERIFY(requestFile.open(QIODevice::ReadOnly | QIODevice::Text));

    QProcess process;
    process.start(QString::fromUtf8(PARLAWL_DEFAULT_WORKER_PYTHON), {QString::fromUtf8(PARLAWL_DEFAULT_WORKER_SCRIPT)});
    QVERIFY(process.waitForStarted());
    process.write(requestFile.readAll());
    process.closeWriteChannel();
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 0);

    const WorkerAnalysisResponse response = worker_protocol::parseAnalysisResponse(process.readAllStandardOutput());
    QVERIFY2(response.ok, qPrintable(response.errorMessage));
    QCOMPARE(response.analysisRun.runId, QStringLiteral("fixture-run-001"));
    QCOMPARE(response.tacticalEvent.puzzleId, QStringLiteral("fixture-puzzle-001"));
    QCOMPARE(response.tacticalEvent.mappingMethod, QStringLiteral("exact_initial_ply_match"));
    QCOMPARE(response.tacticalEvent.mappingConfidence, QStringLiteral("high"));
    QVERIFY(!response.tacticalEvent.kingExposureType.isEmpty());
    QVERIFY(!response.tacticalEvent.kingExposureSeverity.isEmpty());
    QVERIFY(!response.tacticalEvent.loosePieceSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.overloadedDefenderSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.backRankState.isEmpty());
    QVERIFY(!response.tacticalEvent.luftState.isEmpty());
    QVERIFY(!response.tacticalEvent.kingLinePressureType.isEmpty());
    QVERIFY(!response.tacticalEvent.kingSquarePressureType.isEmpty());
    QVERIFY(!response.tacticalEvent.criticalPieceImbalanceSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.structuralFeatureSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.structuralFeatureConfidence.isEmpty());
    QVERIFY(!response.tacticalEvent.kingZoneTargetType.isEmpty());
    QVERIFY(!response.tacticalEvent.kingZoneTargetSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.vulnerablePieceTargetType.isEmpty());
    QVERIFY(!response.tacticalEvent.vulnerablePieceTargetSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.pressureLaneTargetType.isEmpty());
    QVERIFY(!response.tacticalEvent.pressureLaneTargetSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.decisiveImbalanceTarget.isEmpty());
    QVERIFY(!response.tacticalEvent.pinnedCriticalPieceType.isEmpty());
    QVERIFY(!response.tacticalEvent.pinnedCriticalPieceSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.defenderRemovalExposureType.isEmpty());
    QVERIFY(!response.tacticalEvent.defenderRemovalExposureSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.kingColorComplexState.isEmpty());
    QVERIFY(!response.tacticalEvent.kingColorComplexSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.targetZoneImbalanceType.isEmpty());
    QVERIFY(!response.tacticalEvent.targetZoneImbalanceSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.structuralV2Summary.isEmpty());
    QVERIFY(!response.tacticalEvent.structuralV2Confidence.isEmpty());
    QVERIFY(!response.tacticalEvent.escapeGeometryState.isEmpty());
    QVERIFY(!response.tacticalEvent.escapeGeometrySummary.isEmpty());
    QVERIFY(!response.tacticalEvent.flightControlType.isEmpty());
    QVERIFY(!response.tacticalEvent.flightControlSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.defensiveEscapeFragilityType.isEmpty());
    QVERIFY(!response.tacticalEvent.defensiveEscapeFragilitySummary.isEmpty());
    QVERIFY(!response.tacticalEvent.structuralV3Summary.isEmpty());
    QVERIFY(!response.tacticalEvent.structuralV3Confidence.isEmpty());
    QVERIFY(!response.tacticalEvent.attackerCoordinationType.isEmpty());
    QVERIFY(!response.tacticalEvent.attackerCoordinationSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.defensiveNetworkFragilityType.isEmpty());
    QVERIFY(!response.tacticalEvent.defensiveNetworkFragilitySummary.isEmpty());
    QVERIFY(!response.tacticalEvent.structuralV4Summary.isEmpty());
    QVERIFY(!response.tacticalEvent.structuralV4Confidence.isEmpty());
    QVERIFY(!response.tacticalEvent.localTargetSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.localTargetConfidence.isEmpty());
    QVERIFY(response.tacticalEvent.primaryBreakPly > 0);
    QVERIFY(!response.tacticalEvent.primaryBreakReason.isEmpty());
    QVERIFY(!response.tacticalEvent.retainedBreakFormatVersion.isEmpty());
    QVERIFY(!response.tacticalEvent.retainedBreakRole.isEmpty());
    QVERIFY(!response.tacticalEvent.retainedBreakPlayedMove.isEmpty());
    QVERIFY(!response.tacticalEvent.retainedBreakBestMove.isEmpty());
    QVERIFY(!response.tacticalEvent.retainedBreakCompactSequence.isEmpty());
    QVERIFY(!response.tacticalEvent.retainedBreakSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.bestVsPlayedDivergenceSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.divergenceType.isEmpty());
    QVERIFY(!response.tacticalEvent.divergenceSeverity.isEmpty());
    QVERIFY(!response.tacticalEvent.divergenceCompactSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.collapseSequenceFormatVersion.isEmpty());
    QVERIFY(!response.tacticalEvent.collapseSequenceType.isEmpty());
    QVERIFY(!response.tacticalEvent.collapseSequenceSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.retainedRoleSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.omissionReasonSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.omissionReasonCountsJson.isEmpty());
    QCOMPARE(response.tacticalEvent.engineLimitSummary, QStringLiteral("depth=10, multipv=3, window_before=4, window_after=4"));
    QVERIFY(response.criticalMoves.first().hasEvalDeltaCp);
    QVERIFY(!response.criticalMoves.first().criticalReasonType.isEmpty());
    QVERIFY(!response.criticalMoves.first().criticalReasonSeverity.isEmpty());
    QVERIFY(!response.criticalMoves.first().criticalReasonCompactSummary.isEmpty());
    QVERIFY(!response.criticalMoves.first().continuationFormatVersion.isEmpty());
    QVERIFY(!response.criticalMoves.first().bestContinuationCompact.isEmpty());
    QVERIFY(!response.criticalMoves.first().playedContinuationCompact.isEmpty());
    QVERIFY(!response.criticalMoves.first().candidateRankingType.isEmpty());
    QVERIFY(!response.criticalMoves.first().candidateRankingSeverity.isEmpty());
    QVERIFY(!response.criticalMoves.first().candidateRankingCompactSummary.isEmpty());
    QVERIFY(!response.criticalMoves.first().candidateRankingSummary.isEmpty());
    QVERIFY(!response.criticalMoves.first().evidenceNoteType.isEmpty());
    QVERIFY(!response.criticalMoves.first().evidenceNoteSeverity.isEmpty());
    QVERIFY(!response.criticalMoves.first().evidenceNoteCompact.isEmpty());
    QVERIFY(!response.criticalMoves.first().structuralLinkFormatVersion.isEmpty());
    QVERIFY(!response.criticalMoves.first().linkedKingZoneTarget.isEmpty());
    QVERIFY(!response.criticalMoves.first().linkedVulnerablePieceTarget.isEmpty());
    QVERIFY(!response.criticalMoves.first().linkedPressureLaneTarget.isEmpty());
    QVERIFY(!response.criticalMoves.first().linkedDecisiveImbalanceTarget.isEmpty());
    QVERIFY(!response.criticalMoves.first().linkedPinnedCriticalPiece.isEmpty());
    QVERIFY(!response.criticalMoves.first().linkedDefenderRemovalExposure.isEmpty());
    QVERIFY(!response.criticalMoves.first().linkedKingColorComplex.isEmpty());
    QVERIFY(!response.criticalMoves.first().linkedTargetZoneImbalance.isEmpty());
    QVERIFY(!response.criticalMoves.first().linkedAttackerCoordination.isEmpty());
    QVERIFY(!response.criticalMoves.first().linkedDefensiveNetworkFragility.isEmpty());
    QVERIFY(!response.criticalMoves.first().structuralLinkSummary.isEmpty());
    QVERIFY(!response.criticalMoves.first().bestContinuationSummary.isEmpty());
    QVERIFY(!response.criticalMoves.first().onlyMoveReasoning.isEmpty());
    QVERIFY(response.criticalMoves.first().candidateMovesJson.contains(QStringLiteral("\"is_best_move\"")));
    QVERIFY(response.criticalMoves.first().candidateMovesJson.contains(QStringLiteral("\"candidate_display_format_version\":\"cdv1\"")));
    QVERIFY(response.criticalMoves.first().candidateMovesJson.contains(QStringLiteral("\"candidate_display_compact\"")));
    QVERIFY(
        response.criticalMoves.first().candidateRankingSummary.startsWith(
            QStringLiteral("several_strong_alternatives | ")
        )
    );
    QVERIFY(!response.criticalMoves.isEmpty());
}

QTEST_GUILESS_MAIN(TestIntegrationWorkerRoundtrip)

#include "test_integration_worker_roundtrip.moc"
