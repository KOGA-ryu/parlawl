#include <QFile>
#include <QProcess>
#include <QtTest>

#include "parlawl_config.h"
#include "worker_protocol.h"

class TestIntegrationWorkerMultiRole : public QObject
{
    Q_OBJECT

private slots:
    void emitsMultipleCriticalRolesWhenSupported();
};

void TestIntegrationWorkerMultiRole::emitsMultipleCriticalRolesWhenSupported()
{
    QFile requestFile(
        QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_request_multi_role_fixture.json")
    );
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
    QCOMPARE(response.tacticalEvent.mappingMethod, QStringLiteral("exact_initial_ply_match"));
    QVERIFY(response.criticalMoves.size() >= 2);

    QStringList roles;
    for (const CriticalMove &move : response.criticalMoves) {
        roles.append(move.role);
        QVERIFY(!move.candidateRankingSummary.isEmpty());
        QVERIFY(move.candidateMovesJson.contains(QStringLiteral("\"rank\"")));
    }
    QVERIFY(roles.contains(QStringLiteral("decisive_blunder")));
    QVERIFY(roles.contains(QStringLiteral("preventative_resource")));
    QVERIFY(response.tacticalEvent.primaryBreakPly > 0);
    QVERIFY(!response.tacticalEvent.primaryBreakReason.isEmpty());
    QVERIFY(!response.tacticalEvent.retainedBreakFormatVersion.isEmpty());
    QVERIFY(!response.tacticalEvent.retainedBreakCompactSequence.isEmpty());
    QVERIFY(!response.tacticalEvent.retainedBreakSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.bestVsPlayedDivergenceSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.divergenceType.isEmpty());
    QVERIFY(!response.tacticalEvent.divergenceSeverity.isEmpty());
    QVERIFY(!response.tacticalEvent.collapseSequenceFormatVersion.isEmpty());
    QVERIFY(!response.tacticalEvent.collapseSequenceType.isEmpty());
    QVERIFY(!response.tacticalEvent.collapseSequenceSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.omissionReasonSummary.isEmpty());
    QVERIFY(!response.tacticalEvent.omissionReasonCountsJson.isEmpty());

    bool foundDecisive = false;
    for (const CriticalMove &move : response.criticalMoves) {
        QVERIFY(!move.criticalReasonType.isEmpty());
        QVERIFY(!move.criticalReasonSeverity.isEmpty());
        QVERIFY(!move.criticalReasonCompactSummary.isEmpty());
        QVERIFY(!move.continuationFormatVersion.isEmpty());
        QVERIFY(!move.bestContinuationCompact.isEmpty());
        QVERIFY(!move.playedContinuationCompact.isEmpty());
        QVERIFY(!move.candidateRankingType.isEmpty());
        QVERIFY(!move.candidateRankingSeverity.isEmpty());
        QVERIFY(!move.candidateRankingCompactSummary.isEmpty());
        QVERIFY(!move.candidateRankingSummary.isEmpty());
        QVERIFY(!move.evidenceNoteType.isEmpty());
        QVERIFY(!move.evidenceNoteSeverity.isEmpty());
        QVERIFY(!move.evidenceNoteCompact.isEmpty());
        if (move.role != QStringLiteral("decisive_blunder")) {
            continue;
        }
        foundDecisive = true;
        QVERIFY(move.candidateMovesJson.contains(QStringLiteral("\"score_kind\"")));
        QVERIFY(!move.candidateRankingType.isEmpty());
        QVERIFY(!move.candidateRankingCompactSummary.isEmpty());
    }
    QVERIFY(foundDecisive);
}

QTEST_GUILESS_MAIN(TestIntegrationWorkerMultiRole)

#include "test_integration_worker_multi_role.moc"
