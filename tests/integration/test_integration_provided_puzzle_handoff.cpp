#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "analysis_orchestrator.h"
#include "analysis_repository.h"
#include "database_manager.h"
#include "fixture_puzzle_source.h"
#include "parlawl_config.h"
#include "puzzle_round.h"
#include "report_formatter.h"
#include "session_controller.h"
#include "source_game.h"

using namespace parlawl::puzzle_runner;

class TestIntegrationProvidedPuzzleHandoff : public QObject
{
    Q_OBJECT

private slots:
    void orchestratorAnalyzesProvidedPuzzle();
};

void TestIntegrationProvidedPuzzleHandoff::orchestratorAnalyzesProvidedPuzzle()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));

    controller.submitUserMove(QStringLiteral("f4f3"));
    controller.submitUserMove(QStringLiteral("e7g5"));
    controller.submitUserMove(QStringLiteral("f3f4"));
    controller.submitUserMove(QStringLiteral("g7g6"));
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Solved);

    PuzzleRound puzzleRound;
    SourceGame sourceGame;
    QVERIFY2(controller.buildAnalysisInput(&puzzleRound, &sourceGame, &errorMessage), qPrintable(errorMessage));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("provided-puzzle.sqlite3"));

    AnalysisOrchestrator orchestrator;
    QSignalSpy completedSpy(&orchestrator, &AnalysisOrchestrator::analysisCompleted);
    QSignalSpy failedSpy(&orchestrator, &AnalysisOrchestrator::analysisFailed);

    orchestrator.analyzeProvidedPuzzle(
        puzzleRound,
        sourceGame,
        QString::fromUtf8(PARLAWL_DEFAULT_WORKER_PYTHON),
        QString::fromUtf8(PARLAWL_DEFAULT_STOCKFISH_PATH),
        databasePath);

    QCOMPARE(failedSpy.size(), 0);
    QCOMPARE(completedSpy.size(), 1);

    const QList<QVariant> completedArgs = completedSpy.takeFirst();
    const QString runId = completedArgs.at(0).toString();
    const QString reportText = completedArgs.at(1).toString();
    QVERIFY(!runId.isEmpty());
    QVERIFY(!reportText.trimmed().isEmpty());
    QVERIFY(reportText.contains(QStringLiteral("dSgis")));

    DatabaseManager manager;
    QVERIFY(manager.initialize(databasePath).ok);
    AnalysisRepository repository(manager.database());
    PersistedAnalysisReport report;
    QVERIFY2(repository.loadReport(runId, &report, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(report.puzzleRound.puzzleId, QStringLiteral("dSgis"));
    QCOMPARE(report.puzzleRound.sourceGameId, QStringLiteral("zUbyC5ps"));
    QCOMPARE(report.sourceGame.sourceGameId, QStringLiteral("zUbyC5ps"));
    QCOMPARE(report.analysisRun.status, QStringLiteral("completed"));
    QCOMPARE(report.tacticalEvent.puzzleId, QStringLiteral("dSgis"));
    QVERIFY(!report.criticalMoves.isEmpty());
}

QTEST_GUILESS_MAIN(TestIntegrationProvidedPuzzleHandoff)

#include "test_integration_provided_puzzle_handoff.moc"
