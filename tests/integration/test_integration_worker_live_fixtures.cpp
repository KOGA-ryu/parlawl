#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QtTest>

#include "parlawl_config.h"
#include "worker_protocol.h"

class TestIntegrationWorkerLiveFixtures : public QObject
{
    Q_OBJECT

private slots:
    void workerProcessesCapturedLiveFixtures();
};

void TestIntegrationWorkerLiveFixtures::workerProcessesCapturedLiveFixtures()
{
    struct ExpectedFixture
    {
        QString puzzleId;
        QString sourceGameId;
        QString mappingMethod;
        QString mappingConfidence;
        int primaryBreakPly;
        QString primaryBreakReason;
        QString keyWeakness;
        QString kingSafetyState;
        QString pieceActivity;
        QString structuralV2Summary;
        QString structuralV2Confidence;
        QString criticalRole;
        QString criticalReasonType;
        QString firstPlayedMove;
        QString firstBestMove;
    };

    const QMap<QString, ExpectedFixture> expectedByFixture = {
        {QStringLiteral("worker_request_live_fixture_001.json"),
         {QStringLiteral("dSgis"),
          QStringLiteral("zUbyC5ps"),
          QStringLiteral("exact_fen_search_match"),
          QStringLiteral("medium"),
          43,
          QStringLiteral("collapse_trigger"),
          QStringLiteral("king_exposure"),
          QStringLiteral("king_exposure"),
          QStringLiteral("mixed_local_activity"),
          QStringLiteral("defender removal exposure | defender_fragile"),
          QStringLiteral("medium"),
          QStringLiteral("decisive_blunder"),
          QStringLiteral("mating_line_allowed"),
          QStringLiteral("h4h3"),
          QStringLiteral("h4h3")}},
        {QStringLiteral("worker_request_live_fixture_002.json"),
         {QStringLiteral("tsFGf"),
          QStringLiteral("ehMIX51A"),
          QStringLiteral("exact_fen_search_match"),
          QStringLiteral("medium"),
          86,
          QStringLiteral("narrow_missed_defense"),
          QStringLiteral("king_exposure"),
          QStringLiteral("king_exposure"),
          QStringLiteral("active_tactical_piece"),
          QStringLiteral("defender removal exposure"),
          QStringLiteral("medium"),
          QStringLiteral("last_holding_defense"),
          QStringLiteral("defensible_line_lost"),
          QStringLiteral("b8a7"),
          QStringLiteral("b8e5")}},
        {QStringLiteral("worker_request_live_fixture_003.json"),
         {QStringLiteral("MakVJ"),
          QStringLiteral("Fmk3OTwn"),
          QStringLiteral("exact_fen_search_match"),
          QStringLiteral("medium"),
          35,
          QStringLiteral("collapse_trigger"),
          QStringLiteral("mixed_local_weakness"),
          QStringLiteral("pressured_king_zone"),
          QStringLiteral("mixed_local_activity"),
          QStringLiteral("defender removal exposure"),
          QStringLiteral("medium"),
          QStringLiteral("decisive_blunder"),
          QStringLiteral("defensible_line_lost"),
          QStringLiteral("d5c6"),
          QStringLiteral("c2c3")}},
    };

    const QString fixtureDir = QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures");
    QDir dir(fixtureDir);
    const QStringList fixtures = dir.entryList(
        {QStringLiteral("worker_request_live_fixture_*.json")},
        QDir::Files,
        QDir::Name
    );

    QVERIFY2(fixtures.size() >= 3, qPrintable(QStringLiteral("expected at least 3 live fixtures in %1").arg(fixtureDir)));
    QCOMPARE(fixtures.size(), expectedByFixture.size());

    for (const QString &fixtureName : fixtures) {
        QVERIFY2(expectedByFixture.contains(fixtureName), qPrintable(QStringLiteral("missing expected fixture metadata for %1").arg(fixtureName)));
        QFile requestFile(dir.filePath(fixtureName));
        QVERIFY2(requestFile.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(fixtureName));
        const QByteArray requestPayload = requestFile.readAll();
        QJsonParseError parseError;
        const QJsonDocument requestDocument = QJsonDocument::fromJson(requestPayload, &parseError);
        QVERIFY2(
            requestDocument.isObject(),
            qPrintable(QStringLiteral("%1: fixture json parse failed: %2").arg(fixtureName, parseError.errorString()))
        );
        const QJsonObject requestObject = requestDocument.object();
        QVERIFY2(
            requestObject.contains(QStringLiteral("puzzle_round")) && requestObject.value(QStringLiteral("puzzle_round")).isObject(),
            qPrintable(QStringLiteral("%1: missing puzzle_round object").arg(fixtureName))
        );
        QVERIFY2(
            requestObject.contains(QStringLiteral("source_game")) && requestObject.value(QStringLiteral("source_game")).isObject(),
            qPrintable(QStringLiteral("%1: missing source_game object").arg(fixtureName))
        );

        const ExpectedFixture expected = expectedByFixture.value(fixtureName);
        const QString program = QString::fromUtf8(PARLAWL_DEFAULT_WORKER_PYTHON);
        const QStringList arguments = {QString::fromUtf8(PARLAWL_DEFAULT_WORKER_SCRIPT)};

        QProcess process;
        process.start(program, arguments);
        QVERIFY2(
            process.waitForStarted(),
            qPrintable(QStringLiteral("%1: failed to start %2 %3: %4")
                           .arg(fixtureName, program, arguments.join(QStringLiteral(" ")), process.errorString()))
        );
        process.write(requestPayload);
        process.closeWriteChannel();
        QVERIFY2(
            process.waitForFinished(),
            qPrintable(QStringLiteral("%1: worker did not finish: %2")
                           .arg(fixtureName, process.errorString()))
        );
        const QByteArray stdoutPayload = process.readAllStandardOutput();
        const QByteArray stderrPayload = process.readAllStandardError();
        QCOMPARE_NE(process.exitStatus(), QProcess::CrashExit);
        QVERIFY2(
            process.exitCode() == 0,
            qPrintable(QStringLiteral("%1: worker exit=%2 stderr=%3")
                           .arg(fixtureName)
                           .arg(process.exitCode())
                           .arg(QString::fromUtf8(stderrPayload).trimmed()))
        );

        const WorkerAnalysisResponse response = worker_protocol::parseAnalysisResponse(stdoutPayload);
        QVERIFY2(
            response.ok,
            qPrintable(QStringLiteral("%1: parse failed: %2 stderr=%3 stdout=%4")
                           .arg(fixtureName,
                                response.errorMessage,
                                QString::fromUtf8(stderrPayload).trimmed(),
                                QString::fromUtf8(stdoutPayload.left(600)).trimmed()))
        );
        QCOMPARE(response.tacticalEvent.puzzleId, expected.puzzleId);
        QCOMPARE(response.tacticalEvent.sourceGameId, expected.sourceGameId);
        QCOMPARE(response.tacticalEvent.mappingMethod, expected.mappingMethod);
        QCOMPARE(response.tacticalEvent.mappingConfidence, expected.mappingConfidence);
        QCOMPARE(response.tacticalEvent.primaryBreakPly, expected.primaryBreakPly);
        QCOMPARE(response.tacticalEvent.primaryBreakReason, expected.primaryBreakReason);
        QCOMPARE(response.tacticalEvent.keyWeakness, expected.keyWeakness);
        QCOMPARE(response.tacticalEvent.kingSafetyState, expected.kingSafetyState);
        QCOMPARE(response.tacticalEvent.pieceActivity, expected.pieceActivity);
        QCOMPARE(response.tacticalEvent.structuralV2Summary, expected.structuralV2Summary);
        QCOMPARE(response.tacticalEvent.structuralV2Confidence, expected.structuralV2Confidence);
        QVERIFY(!response.tacticalEvent.structuralFeatureSummary.isEmpty());
        QVERIFY(!response.tacticalEvent.localTargetSummary.isEmpty());
        QVERIFY(!response.criticalMoves.isEmpty());
        QCOMPARE(response.criticalMoves.first().role, expected.criticalRole);
        QCOMPARE(response.criticalMoves.first().criticalReasonType, expected.criticalReasonType);
        QCOMPARE(response.criticalMoves.first().playedMove, expected.firstPlayedMove);
        QCOMPARE(response.criticalMoves.first().bestMove, expected.firstBestMove);
    }
}

QTEST_GUILESS_MAIN(TestIntegrationWorkerLiveFixtures)

#include "test_integration_worker_live_fixtures.moc"
