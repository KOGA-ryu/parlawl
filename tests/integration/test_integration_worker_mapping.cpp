#include <QFile>
#include <QProcess>
#include <QtTest>

#include "parlawl_config.h"
#include "worker_protocol.h"

class TestIntegrationWorkerMapping : public QObject
{
    Q_OBJECT

private slots:
    void mapsWithFenSearchFallback();
    void failsClearlyForAmbiguousMapping();
    void failsClearlyForMappingFailure();
};

void TestIntegrationWorkerMapping::mapsWithFenSearchFallback()
{
    QFile requestFile(
        QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_request_fen_fallback_fixture.json")
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
    QCOMPARE(response.tacticalEvent.mappingMethod, QStringLiteral("exact_fen_search_match"));
    QCOMPARE(response.tacticalEvent.mappingConfidence, QStringLiteral("medium"));
    QCOMPARE(response.tacticalEvent.mappedStartPly, 18);
}

void TestIntegrationWorkerMapping::failsClearlyForAmbiguousMapping()
{
    QFile requestFile(
        QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_request_ambiguous_mapping_fixture.json")
    );
    QVERIFY(requestFile.open(QIODevice::ReadOnly | QIODevice::Text));

    QProcess process;
    process.start(QString::fromUtf8(PARLAWL_DEFAULT_WORKER_PYTHON), {QString::fromUtf8(PARLAWL_DEFAULT_WORKER_SCRIPT)});
    QVERIFY(process.waitForStarted());
    process.write(requestFile.readAll());
    process.closeWriteChannel();
    QVERIFY(process.waitForFinished());
    QVERIFY(process.exitCode() != 0);
    QVERIFY(QString::fromUtf8(process.readAllStandardError()).contains(QStringLiteral("ambiguous mapping")));
}

void TestIntegrationWorkerMapping::failsClearlyForMappingFailure()
{
    QFile requestFile(
        QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_request_mapping_failure_fixture.json")
    );
    QVERIFY(requestFile.open(QIODevice::ReadOnly | QIODevice::Text));

    QProcess process;
    process.start(QString::fromUtf8(PARLAWL_DEFAULT_WORKER_PYTHON), {QString::fromUtf8(PARLAWL_DEFAULT_WORKER_SCRIPT)});
    QVERIFY(process.waitForStarted());
    process.write(requestFile.readAll());
    process.closeWriteChannel();
    QVERIFY(process.waitForFinished());
    QVERIFY(process.exitCode() != 0);
    QVERIFY(QString::fromUtf8(process.readAllStandardError()).contains(QStringLiteral("mapping failure")));
}

QTEST_GUILESS_MAIN(TestIntegrationWorkerMapping)

#include "test_integration_worker_mapping.moc"
