#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "parlawl_config.h"
#include "analysis_repository.h"
#include "database_manager.h"
#include "worker_protocol.h"

namespace {

AnalysisRun makePersistedRun(
    AnalysisRepository &repository,
    const QString &puzzleId,
    const QString &sourceId,
    QString *errorMessage)
{
    PuzzleRound round;
    round.puzzleId = puzzleId;
    round.sideToMove = QStringLiteral("white");
    round.fetchedAtUtc = QDateTime::currentDateTimeUtc();
    round.sourceGameId = sourceId;
    round.initialFen = QStringLiteral("8/8/8/8/8/8/8/8 w - - 0 1");
    round.solutionMovesJson = QStringLiteral("[]");
    round.themesJson = QStringLiteral("[]");
    if (!repository.upsertPuzzleRound(round, errorMessage)) {
        return {};
    }

    SourceGame game;
    game.sourceGameId = sourceId;
    game.pgnText = QStringLiteral("[Event \"Retention Test\"]\n\n1. a3 a6");
    game.openingName = QStringLiteral("Test Opening");
    game.fetchedAtUtc = QDateTime::currentDateTimeUtc();
    if (!repository.upsertSourceGame(game, errorMessage)) {
        return {};
    }

    AnalysisRun run = repository.createAnalysisRun(puzzleId, QStringLiteral("mode"), QStringLiteral("stockfish"), 8, errorMessage);
    if (run.runId.isEmpty()) {
        return {};
    }

    TacticalEvent event;
    event.eventId = puzzleId + QStringLiteral("-event");
    event.runId = run.runId;
    event.puzzleId = puzzleId;
    event.sourceGameId = sourceId;
    CriticalMove move;
    move.eventId = event.eventId;
    move.criticalMoveId = puzzleId + QStringLiteral("-move");
    move.role = QStringLiteral("preventative_resource");
    move.ply = 1;
    move.side = QStringLiteral("white");
    move.playedMove = QStringLiteral("a2a3");
    move.bestMove = QStringLiteral("a2a4");
    move.whyCritical = QStringLiteral("stronger alternative missed");
    move.criticalReasonType = QStringLiteral("stronger_alternative_missed");
    move.criticalReasonSeverity = QStringLiteral("moderate");
    move.criticalReasonCompactSummary = QStringLiteral("stronger alternative missed");
    move.continuationFormatVersion = QStringLiteral("cmv1");
    move.bestContinuationCompact = QStringLiteral("better space gain");
    move.playedContinuationCompact = QStringLiteral("slower plan");
    move.onlyMoveStatus = QStringLiteral("multiple_viable");
    move.onlyMoveReasoning = QStringLiteral("multiple local candidates remain viable");
    move.candidateMovesJson = QStringLiteral("[\"a2a4\"]");
    move.candidateRankingType = QStringLiteral("several_strong_alternatives");
    move.candidateRankingSeverity = QStringLiteral("moderate");
    move.candidateRankingCompactSummary = QStringLiteral("1 stronger alternative");
    move.candidateRankingSummary = QStringLiteral("several_strong_alternatives | 1 stronger alternative");
    move.evidenceNoteType = QStringLiteral("stronger_alternatives_missed");
    move.evidenceNoteSeverity = QStringLiteral("moderate");
    move.evidenceNoteCompact = QStringLiteral("1 stronger alternative missed");
    move.structuralLinkFormatVersion = QStringLiteral("mtlv3");
    move.linkedKingZoneTarget = QStringLiteral("none");
    move.linkedVulnerablePieceTarget = QStringLiteral("none");
    move.linkedPressureLaneTarget = QStringLiteral("none");
    move.linkedDecisiveImbalanceTarget = QStringLiteral("none");
    move.linkedPinnedCriticalPiece = QStringLiteral("none");
    move.linkedDefenderRemovalExposure = QStringLiteral("none");
    move.linkedKingColorComplex = QStringLiteral("none");
    move.linkedTargetZoneImbalance = QStringLiteral("none");
    move.linkedAttackerCoordination = QStringLiteral("none");
    move.linkedDefensiveNetworkFragility = QStringLiteral("none");
    move.structuralLinkSummary = QStringLiteral("none");
    if (!repository.saveAnalysisResult(event, {move}, errorMessage)) {
        return {};
    }
    return run;
}

} // namespace

class TestIntegrationPersistence : public QObject
{
    Q_OBJECT

private slots:
    void persistsCompletedAnalysisResult();
    void prunesOldHistoryAndKeepsProtectedRuns();
};

void TestIntegrationPersistence::persistsCompletedAnalysisResult()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DatabaseManager manager;
    QVERIFY(manager.initialize(dir.path() + QStringLiteral("/test.sqlite3")).ok);

    QFile requestFile(QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_request_fixture.json"));
    QVERIFY(requestFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QJsonObject request = QJsonDocument::fromJson(requestFile.readAll()).object();

    PuzzleRound puzzleRound;
    const QJsonObject puzzleObject = request.value(QStringLiteral("puzzle_round")).toObject();
    puzzleRound.puzzleId = puzzleObject.value(QStringLiteral("puzzle_id")).toString();
    puzzleRound.puzzleRating = puzzleObject.value(QStringLiteral("puzzle_rating")).toInt();
    puzzleRound.timeControl = puzzleObject.value(QStringLiteral("time_control")).toString();
    puzzleRound.whitePlayer = puzzleObject.value(QStringLiteral("white_player")).toString();
    puzzleRound.whiteRating = puzzleObject.value(QStringLiteral("white_rating")).toInt();
    puzzleRound.blackPlayer = puzzleObject.value(QStringLiteral("black_player")).toString();
    puzzleRound.blackRating = puzzleObject.value(QStringLiteral("black_rating")).toInt();
    puzzleRound.sideToMove = puzzleObject.value(QStringLiteral("side_to_move")).toString();
    puzzleRound.sourceGameId = puzzleObject.value(QStringLiteral("source_game_id")).toString();
    puzzleRound.fetchedAtUtc = QDateTime::fromString(puzzleObject.value(QStringLiteral("fetched_at")).toString(), Qt::ISODate);
    puzzleRound.initialFen = puzzleObject.value(QStringLiteral("initial_fen")).toString();
    puzzleRound.lastMove = puzzleObject.value(QStringLiteral("last_move")).toString();
    puzzleRound.solved = puzzleObject.value(QStringLiteral("solved")).toBool();
    puzzleRound.rawPuzzleJson = puzzleObject.value(QStringLiteral("raw_puzzle_json")).toString();
    puzzleRound.rawActivityJson = puzzleObject.value(QStringLiteral("raw_activity_json")).toString();
    puzzleRound.solutionMovesJson = puzzleObject.value(QStringLiteral("solution_moves_json")).toString();
    puzzleRound.themesJson = puzzleObject.value(QStringLiteral("themes_json")).toString();

    SourceGame sourceGame;
    const QJsonObject sourceObject = request.value(QStringLiteral("source_game")).toObject();
    sourceGame.sourceGameId = sourceObject.value(QStringLiteral("source_game_id")).toString();
    sourceGame.pgnText = sourceObject.value(QStringLiteral("pgn_text")).toString();
    sourceGame.openingName = sourceObject.value(QStringLiteral("opening_name")).toString();
    sourceGame.fetchedAtUtc = QDateTime::fromString(sourceObject.value(QStringLiteral("fetched_at")).toString(), Qt::ISODate);

    AnalysisRepository repository(manager.database());
    QString errorMessage;
    QVERIFY2(repository.upsertPuzzleRound(puzzleRound, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(repository.upsertSourceGame(sourceGame, &errorMessage), qPrintable(errorMessage));

    AnalysisRun run = repository.createAnalysisRun(
        puzzleRound.puzzleId,
        QStringLiteral("stockfish_window_v0_1"),
        QStringLiteral("stockfish"),
        10,
        &errorMessage
    );
    QVERIFY2(!run.runId.isEmpty(), qPrintable(errorMessage));

    QFile responseFile(QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_response_fixture.json"));
    QVERIFY(responseFile.open(QIODevice::ReadOnly | QIODevice::Text));
    WorkerAnalysisResponse response = worker_protocol::parseAnalysisResponse(responseFile.readAll());
    QVERIFY(response.ok);
    response.analysisRun.engineName = QStringLiteral("stockfish");
    response.analysisRun.engineDepth = 10;
    response.tacticalEvent.runId = run.runId;
    response.tacticalEvent.eventId = QStringLiteral("persisted-event-001");
    for (int i = 0; i < response.criticalMoves.size(); ++i) {
        response.criticalMoves[i].eventId = response.tacticalEvent.eventId;
    }

    QVERIFY2(repository.saveAnalysisResult(response.tacticalEvent, response.criticalMoves, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(repository.completeAnalysisRun(run.runId, QStringLiteral("completed"), QString(), &errorMessage), qPrintable(errorMessage));

    PersistedAnalysisReport report;
    QVERIFY2(repository.loadReport(run.runId, &report, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(report.puzzleRound.puzzleId, QStringLiteral("fixture-puzzle-001"));
    QCOMPARE(report.sourceGame.sourceGameId, QStringLiteral("fixture-game-001"));
    QCOMPARE(report.tacticalEvent.eventId, QStringLiteral("persisted-event-001"));
    QCOMPARE(report.analysisRun.engineName, QStringLiteral("stockfish"));
    QCOMPARE(report.analysisRun.engineDepth, 10);
    QCOMPARE(report.tacticalEvent.assistantInferenceStatus, QStringLiteral("pending"));
    QCOMPARE(report.tacticalEvent.mappingMethod, QStringLiteral("exact_initial_ply_match"));
    QCOMPARE(report.tacticalEvent.mappingConfidence, QStringLiteral("high"));
    QCOMPARE(report.tacticalEvent.kingSafetyState, QStringLiteral("pressured_king_zone"));
    QCOMPARE(report.tacticalEvent.kingExposureType, QStringLiteral("no_clear_exposure"));
    QCOMPARE(report.tacticalEvent.kingExposureSeverity, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.loosePieceCount, 0);
    QCOMPARE(report.tacticalEvent.loosePieceSummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.overloadedDefenderCount, 0);
    QCOMPARE(report.tacticalEvent.overloadedDefenderSummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.backRankState, QStringLiteral("back_rank_stable"));
    QCOMPARE(report.tacticalEvent.luftState, QStringLiteral("limited_luft"));
    QCOMPARE(report.tacticalEvent.kingLinePressureType, QStringLiteral("diagonal_pressure"));
    QCOMPARE(report.tacticalEvent.kingSquarePressureType, QStringLiteral("balanced_square_control"));
    QCOMPARE(report.tacticalEvent.criticalPieceImbalanceSummary, QStringLiteral("balanced_local_targets"));
    QCOMPARE(report.tacticalEvent.structuralFeatureSummary, QStringLiteral("limited_luft | diagonal_pressure"));
    QCOMPARE(report.tacticalEvent.structuralFeatureConfidence, QStringLiteral("medium"));
    QCOMPARE(report.tacticalEvent.kingZoneTargetType, QStringLiteral("no_clear_king_zone_target"));
    QCOMPARE(report.tacticalEvent.kingZoneTargetSummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.vulnerablePieceTargetType, QStringLiteral("no_clear_vulnerable_piece_target"));
    QCOMPARE(report.tacticalEvent.vulnerablePieceTargetSummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.pressureLaneTargetType, QStringLiteral("diagonal_lane_target"));
    QCOMPARE(report.tacticalEvent.pressureLaneTargetSummary, QStringLiteral("diag b3-f7"));
    QCOMPARE(report.tacticalEvent.decisiveImbalanceTarget, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.pinnedCriticalPieceType, QStringLiteral("no_clear_pinned_critical_piece"));
    QCOMPARE(report.tacticalEvent.pinnedCriticalPieceSummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.defenderRemovalExposureType, QStringLiteral("no_clear_defender_removal_exposure"));
    QCOMPARE(report.tacticalEvent.defenderRemovalExposureSummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.kingColorComplexState, QStringLiteral("no_clear_color_complex_weakness"));
    QCOMPARE(report.tacticalEvent.kingColorComplexSummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.targetZoneImbalanceType, QStringLiteral("balanced_target_zone"));
    QCOMPARE(report.tacticalEvent.targetZoneImbalanceSummary, QStringLiteral("balanced"));
    QCOMPARE(report.tacticalEvent.structuralV2Summary, QStringLiteral("no_clear_structural_v2"));
    QCOMPARE(report.tacticalEvent.structuralV2Confidence, QStringLiteral("low"));
    QCOMPARE(report.tacticalEvent.escapeGeometryState, QStringLiteral("no_clear_escape_geometry"));
    QCOMPARE(report.tacticalEvent.escapeGeometrySummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.flightControlType, QStringLiteral("no_clear_flight_control"));
    QCOMPARE(report.tacticalEvent.flightControlSummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.defensiveEscapeFragilityType, QStringLiteral("no_clear_escape_fragility"));
    QCOMPARE(report.tacticalEvent.defensiveEscapeFragilitySummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.structuralV3Summary, QStringLiteral("no_clear_structural_v3"));
    QCOMPARE(report.tacticalEvent.structuralV3Confidence, QStringLiteral("low"));
    QCOMPARE(report.tacticalEvent.attackerCoordinationType, QStringLiteral("no_clear_attacker_coordination"));
    QCOMPARE(report.tacticalEvent.attackerCoordinationSummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.defensiveNetworkFragilityType, QStringLiteral("no_clear_defensive_network_fragility"));
    QCOMPARE(report.tacticalEvent.defensiveNetworkFragilitySummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.structuralV4Summary, QStringLiteral("no_clear_structural_v4"));
    QCOMPARE(report.tacticalEvent.structuralV4Confidence, QStringLiteral("low"));
    QCOMPARE(report.tacticalEvent.localTargetSummary, QStringLiteral("diag b3-f7"));
    QCOMPARE(report.tacticalEvent.localTargetConfidence, QStringLiteral("medium"));
    QCOMPARE(report.tacticalEvent.keyWeakness, QStringLiteral("pressure_lane"));
    QCOMPARE(report.tacticalEvent.pieceActivity, QStringLiteral("active_pressure_lane"));
    QCOMPARE(report.tacticalEvent.primaryBreakPly, 18);
    QCOMPARE(report.tacticalEvent.primaryBreakReason, QStringLiteral("narrow_missed_defense"));
    QCOMPARE(report.tacticalEvent.retainedBreakFormatVersion, QStringLiteral("rbsv1"));
    QCOMPARE(report.tacticalEvent.retainedBreakRole, QStringLiteral("last_holding_defense"));
    QCOMPARE(report.tacticalEvent.retainedBreakPlayedMove, QStringLiteral("d6d5"));
    QCOMPARE(report.tacticalEvent.retainedBreakBestMove, QStringLiteral("h7h6"));
    QCOMPARE(report.tacticalEvent.retainedBreakCompactSequence, QStringLiteral("stronger alternatives available"));
    QCOMPARE(
        report.tacticalEvent.retainedBreakSummary,
        QStringLiteral("last_holding_defense@18 | played d6d5 | best h7h6 | stronger alternatives available")
    );
    QVERIFY(report.tacticalEvent.bestVsPlayedDivergenceSummary.contains(QStringLiteral("114 cp")));
    QCOMPARE(report.tacticalEvent.divergenceType, QStringLiteral("stronger_alternatives_available"));
    QCOMPARE(report.tacticalEvent.divergenceSeverity, QStringLiteral("moderate"));
    QCOMPARE(report.tacticalEvent.divergenceCompactSummary, QStringLiteral("multiple stronger alternatives existed"));
    QCOMPARE(report.tacticalEvent.localSequenceConfidence, QStringLiteral("medium"));
    QCOMPARE(report.tacticalEvent.collapseSequenceFormatVersion, QStringLiteral("csv1"));
    QCOMPARE(report.tacticalEvent.collapseSequenceType, QStringLiteral("single_break"));
    QCOMPARE(
        report.tacticalEvent.collapseSequenceSummary,
        QStringLiteral("single_break | break@18 | roles last_holding_defense@18 | omitted 0")
    );
    QCOMPARE(report.tacticalEvent.retainedRoleSummary, QStringLiteral("last_holding_defense@18"));
    QCOMPARE(report.tacticalEvent.omittedAdjacentCandidateCount, 0);
    QCOMPARE(report.tacticalEvent.omissionReasonSummary, QStringLiteral("none"));
    QCOMPARE(report.tacticalEvent.omissionReasonCountsJson, QStringLiteral("{}"));
    QCOMPARE(report.tacticalEvent.engineLimitSummary, QStringLiteral("depth=10, multipv=3, window_before=4, window_after=4"));
    QCOMPARE(report.tacticalEvent.structuralCandidatesJson, QStringLiteral("[\"pressure_lane\"]"));
    QVERIFY(report.criticalMoves.first().hasEvalDeltaCp);
    QVERIFY(!report.criticalMoves.first().pvSan.isEmpty());
    QCOMPARE(report.criticalMoves.first().onlyMoveStatus, QStringLiteral("multiple_viable"));
    QCOMPARE(
        report.criticalMoves.first().onlyMoveReasoning,
        QStringLiteral("multiple local candidates remain within the viable evaluation band")
    );
    QCOMPARE(report.criticalMoves.first().whyCritical, QStringLiteral("stronger_alternative_missed | stronger alternative missed"));
    QCOMPARE(report.criticalMoves.first().criticalReasonType, QStringLiteral("stronger_alternative_missed"));
    QCOMPARE(report.criticalMoves.first().criticalReasonSeverity, QStringLiteral("moderate"));
    QCOMPARE(report.criticalMoves.first().criticalReasonCompactSummary, QStringLiteral("stronger alternative missed"));
    QCOMPARE(report.criticalMoves.first().continuationFormatVersion, QStringLiteral("cmv1"));
    QCOMPARE(report.criticalMoves.first().bestContinuationCompact, QStringLiteral("stronger line retained"));
    QCOMPARE(report.criticalMoves.first().playedContinuationCompact, QStringLiteral("stronger line missed"));
    QCOMPARE(report.criticalMoves.first().candidateRankingType, QStringLiteral("several_strong_alternatives"));
    QCOMPARE(report.criticalMoves.first().candidateRankingSeverity, QStringLiteral("moderate"));
    QCOMPARE(report.criticalMoves.first().candidateRankingCompactSummary, QStringLiteral("3 stronger alternatives"));
    QCOMPARE(
        report.criticalMoves.first().candidateRankingSummary,
        QStringLiteral("several_strong_alternatives | 3 stronger alternatives")
    );
    QCOMPARE(report.criticalMoves.first().evidenceNoteType, QStringLiteral("stronger_alternatives_missed"));
    QCOMPARE(report.criticalMoves.first().evidenceNoteSeverity, QStringLiteral("moderate"));
    QCOMPARE(report.criticalMoves.first().evidenceNoteCompact, QStringLiteral("3 stronger alternatives missed"));
    QCOMPARE(report.criticalMoves.first().structuralLinkFormatVersion, QStringLiteral("mtlv3"));
    QCOMPARE(report.criticalMoves.first().linkedKingZoneTarget, QStringLiteral("none"));
    QCOMPARE(report.criticalMoves.first().linkedVulnerablePieceTarget, QStringLiteral("none"));
    QCOMPARE(report.criticalMoves.first().linkedPressureLaneTarget, QStringLiteral("diag b3-f7"));
    QCOMPARE(report.criticalMoves.first().linkedDecisiveImbalanceTarget, QStringLiteral("none"));
    QCOMPARE(report.criticalMoves.first().linkedPinnedCriticalPiece, QStringLiteral("none"));
    QCOMPARE(report.criticalMoves.first().linkedDefenderRemovalExposure, QStringLiteral("none"));
    QCOMPARE(report.criticalMoves.first().linkedKingColorComplex, QStringLiteral("none"));
    QCOMPARE(report.criticalMoves.first().linkedTargetZoneImbalance, QStringLiteral("none"));
    QCOMPARE(report.criticalMoves.first().linkedAttackerCoordination, QStringLiteral("none"));
    QCOMPARE(report.criticalMoves.first().linkedDefensiveNetworkFragility, QStringLiteral("none"));
    QCOMPARE(report.criticalMoves.first().structuralLinkSummary, QStringLiteral("lane diag b3-f7"));
    QCOMPARE(report.criticalMoves.first().strongerAlternativeCount, 3);
    QVERIFY(report.criticalMoves.first().candidateMovesJson.contains(QStringLiteral("\"move_uci\":\"h7h6\"")));
    QVERIFY(
        report.criticalMoves.first().candidateMovesJson.contains(
            QStringLiteral("\"candidate_display_format_version\":\"cdv1\"")
        )
    );
    QVERIFY(
        report.criticalMoves.first().candidateMovesJson.contains(
            QStringLiteral("\"candidate_display_compact\":\"1. h6 | cp -21 | best\"")
        )
    );
    QCOMPARE(report.criticalMoves.size(), 1);

    QVERIFY2(repository.saveAssistantInference(
                  run.runId,
                  QStringLiteral("provided"),
                  QStringLiteral("[\"mate_net\",\"king_hunt\"]"),
                  QStringLiteral("## external inference\n- black's king was boxed in."),
                  &errorMessage),
              qPrintable(errorMessage));

    PersistedAnalysisReport withAssistant;
    QVERIFY2(repository.loadReport(run.runId, &withAssistant, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(withAssistant.tacticalEvent.assistantInferenceStatus, QStringLiteral("provided"));
    QCOMPARE(withAssistant.tacticalEvent.assistantLabelsJson, QStringLiteral("[\"mate_net\",\"king_hunt\"]"));
    QCOMPARE(withAssistant.tacticalEvent.assistantSummaryMarkdown, QStringLiteral("## external inference\n- black's king was boxed in."));
}

void TestIntegrationPersistence::prunesOldHistoryAndKeepsProtectedRuns()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    DatabaseManager manager;
    QVERIFY(manager.initialize(dir.path() + QStringLiteral("/test.sqlite3")).ok);

    AnalysisRepository repository(manager.database());
    QString errorMessage;

    AnalysisRun completedOld = makePersistedRun(repository, QStringLiteral("p-old-complete"), QStringLiteral("g-old-complete"), &errorMessage);
    QVERIFY2(!completedOld.runId.isEmpty(), qPrintable(errorMessage));
    QVERIFY2(repository.completeAnalysisRun(completedOld.runId, QStringLiteral("completed"), QString(), &errorMessage), qPrintable(errorMessage));
    QTest::qSleep(5);
    AnalysisRun failedOlder = makePersistedRun(repository, QStringLiteral("p-failed-older"), QStringLiteral("g-failed-older"), &errorMessage);
    QVERIFY2(!failedOlder.runId.isEmpty(), qPrintable(errorMessage));
    QVERIFY2(repository.completeAnalysisRun(failedOlder.runId, QStringLiteral("failed"), QStringLiteral("boom"), &errorMessage), qPrintable(errorMessage));
    QTest::qSleep(5);
    AnalysisRun failedNewest = makePersistedRun(repository, QStringLiteral("p-failed-newest"), QStringLiteral("g-failed-newest"), &errorMessage);
    QVERIFY2(!failedNewest.runId.isEmpty(), qPrintable(errorMessage));
    QVERIFY2(repository.completeAnalysisRun(failedNewest.runId, QStringLiteral("failed"), QStringLiteral("boom"), &errorMessage), qPrintable(errorMessage));

    int deletedCount = 0;
    QVERIFY2(repository.pruneAnalysisHistory(1, true, &deletedCount, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(deletedCount, 1);

    const QList<AnalysisRun> remainingRuns = repository.listRecentRuns(10, &errorMessage);
    QCOMPARE(remainingRuns.size(), 2);
    QStringList remainingIds;
    for (const AnalysisRun &run : remainingRuns) {
        remainingIds.append(run.runId);
    }
    QVERIFY(remainingIds.contains(completedOld.runId));
    QVERIFY(!remainingIds.contains(failedOlder.runId));
    QVERIFY(remainingIds.contains(failedNewest.runId));
}

QTEST_GUILESS_MAIN(TestIntegrationPersistence)

#include "test_integration_persistence.moc"
