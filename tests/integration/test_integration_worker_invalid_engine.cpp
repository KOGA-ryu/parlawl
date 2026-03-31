#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QtTest>

#include "parlawl_config.h"

class TestIntegrationWorkerInvalidEngine : public QObject
{
    Q_OBJECT

private slots:
    void failsClearlyForMissingStockfish();
};

void TestIntegrationWorkerInvalidEngine::failsClearlyForMissingStockfish()
{
    QFile requestFile(QString::fromUtf8(PARLAWL_SOURCE_DIR) + QStringLiteral("/tests/fixtures/worker_request_fixture.json"));
    QVERIFY(requestFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QJsonObject request = QJsonDocument::fromJson(requestFile.readAll()).object();
    request[QStringLiteral("engine")].toObject();
    QJsonObject engine = request.value(QStringLiteral("engine")).toObject();
    engine.insert(QStringLiteral("stockfish_path"), QStringLiteral("/tmp/does-not-exist-stockfish"));
    request.insert(QStringLiteral("engine"), engine);

    QProcess process;
    process.start(QString::fromUtf8(PARLAWL_DEFAULT_WORKER_PYTHON), {QString::fromUtf8(PARLAWL_DEFAULT_WORKER_SCRIPT)});
    QVERIFY(process.waitForStarted());
    process.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
    process.closeWriteChannel();
    QVERIFY(process.waitForFinished());
    QVERIFY(process.exitCode() != 0);
    QVERIFY(QString::fromUtf8(process.readAllStandardError()).contains(QStringLiteral("engine launch failure")));
}

QTEST_GUILESS_MAIN(TestIntegrationWorkerInvalidEngine)

#include "test_integration_worker_invalid_engine.moc"
