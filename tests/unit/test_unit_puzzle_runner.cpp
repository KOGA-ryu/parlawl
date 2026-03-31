#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "fixture_puzzle_source.h"
#include "puzzle_engine.h"
#include "puzzle_round.h"
#include "session_controller.h"
#include "source_game.h"

using namespace parlawl::puzzle_runner;

class PuzzleRunnerTest : public QObject
{
    Q_OBJECT

private slots:
    void fixtureSourceLoadsBuiltInPuzzles();
    void fixtureSourceParsesMetadataDefaults();
    void fixtureSourceKeepsAnalysisSeedConsistent();
    void sessionControllerBuildsAnalysisInputFromFixtureData();
    void sessionControllerBuildsAnalysisInputAfterFailure();
    void puzzleEngineAutoRepliesAndSolves();
    void puzzleEngineFailsOnWrongMove();
    void sessionControllerReviewAndRetryFlow();
    void sessionControllerRevealSolutionResetsFailedLine();
    void sessionControllerAutoAdvanceMovesToNextPuzzle();
    void sessionControllerIgnoresSubmitOutsideActiveLatestView();
    void sessionControllerRevealSolutionFromActiveState();
    void sessionControllerRevealSolutionFromSolvedStateIsStable();
    void sessionControllerRetryIsIdempotent();
    void sessionControllerRefillsPuzzleBatchWindow();
};

void PuzzleRunnerTest::fixtureSourceLoadsBuiltInPuzzles()
{
    FixturePuzzleSource source;
    QString errorMessage;
    const QVector<PuzzleDefinition> puzzles = source.loadPuzzles(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY(puzzles.size() >= 3);
    const PuzzleDefinition &first = puzzles.first();
    QCOMPARE(first.solutionMoves.first(), QStringLiteral("f4f3"));
    QCOMPARE(first.metadata.sourceLabel, QStringLiteral("From game 10+0 • Rapid"));
    QCOMPARE(first.metadata.playedCount, 1420);
    QCOMPARE(first.metadata.whiteName, QStringLiteral("gdwojacki"));
    QCOMPARE(first.metadata.whiteRating, 1932);
    QCOMPARE(first.metadata.blackName, QStringLiteral("aircraft135"));
    QCOMPARE(first.metadata.blackRating, 1944);
    QCOMPARE(first.metadata.ratingHidden, false);
    QCOMPARE(first.analysisSeed.sourceGameId, QStringLiteral("zUbyC5ps"));
    QCOMPARE(first.analysisSeed.timeControl, QStringLiteral("10+0"));
    QCOMPARE(first.analysisSeed.sideToMove, QStringLiteral("black"));
    QCOMPARE(first.analysisSeed.lastMove, QStringLiteral("h4h3"));
    QVERIFY(!first.analysisSeed.rawPuzzleJson.isEmpty());
    QVERIFY(!first.analysisSeed.rawActivityJson.isEmpty());
    QVERIFY(first.analysisSeed.sourceGamePgn.contains(QStringLiteral("[Event \"rated rapid game\"]")));
}

void PuzzleRunnerTest::fixtureSourceParsesMetadataDefaults()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString fixturePath = temporaryDir.filePath(QStringLiteral("puzzles.json"));
    QFile fixtureFile(fixturePath);
    QVERIFY(fixtureFile.open(QIODevice::WriteOnly | QIODevice::Text));
    fixtureFile.write(R"JSON({
  "puzzles": [
    {
      "id": "minimal",
      "fen_start": "8/8/8/8/8/8/8/K6k w - - 0 1",
      "solution_moves": ["a1a2"]
    }
  ]
})JSON");
    fixtureFile.close();

    FixturePuzzleSource source(fixturePath);
    QString errorMessage;
    const QVector<PuzzleDefinition> puzzles = source.loadPuzzles(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(puzzles.size(), 1);

    const PuzzleDefinition &puzzle = puzzles.first();
    QCOMPARE(puzzle.id, QStringLiteral("minimal"));
    QCOMPARE(puzzle.metadata.difficulty, QStringLiteral("all"));
    QCOMPARE(puzzle.metadata.source, QStringLiteral("local_fixture"));
    QCOMPARE(puzzle.metadata.sourceLabel, QString());
    QCOMPARE(puzzle.metadata.rating, 0);
    QCOMPARE(puzzle.metadata.ratingHidden, false);
    QCOMPARE(puzzle.metadata.playedCount, 0);
    QCOMPARE(puzzle.metadata.whiteName, QString());
    QCOMPARE(puzzle.metadata.whiteRating, 0);
    QCOMPARE(puzzle.metadata.blackName, QString());
    QCOMPARE(puzzle.metadata.blackRating, 0);
    QCOMPARE(puzzle.analysisSeed.sourceGameId, QString());
    QCOMPARE(puzzle.analysisSeed.timeControl, QString());
    QCOMPARE(puzzle.analysisSeed.sideToMove, QString());
    QCOMPARE(puzzle.analysisSeed.lastMove, QString());
    QCOMPARE(puzzle.analysisSeed.rawPuzzleJson, QString());
    QCOMPARE(puzzle.analysisSeed.rawActivityJson, QString());
    QCOMPARE(puzzle.analysisSeed.sourceGamePgn, QString());
}

void PuzzleRunnerTest::fixtureSourceKeepsAnalysisSeedConsistent()
{
    FixturePuzzleSource source;
    QString errorMessage;
    const QVector<PuzzleDefinition> puzzles = source.loadPuzzles(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY(!puzzles.isEmpty());

    for (const PuzzleDefinition &puzzle : puzzles) {
        if (puzzle.analysisSeed.rawPuzzleJson.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument rawPuzzle = QJsonDocument::fromJson(puzzle.analysisSeed.rawPuzzleJson.toUtf8(), &parseError);
        QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
        QVERIFY(rawPuzzle.isObject());

        const QJsonObject root = rawPuzzle.object();
        const QJsonObject puzzleObject = root.value(QStringLiteral("puzzle")).toObject();
        const QJsonObject gameObject = root.value(QStringLiteral("game")).toObject();
        QCOMPARE(puzzleObject.value(QStringLiteral("id")).toString(), puzzle.id);
        QCOMPARE(gameObject.value(QStringLiteral("id")).toString(), puzzle.analysisSeed.sourceGameId);
        QCOMPARE(gameObject.value(QStringLiteral("clock")).toString(), puzzle.analysisSeed.timeControl);

        const QJsonArray solutionArray = puzzleObject.value(QStringLiteral("solution")).toArray();
        QCOMPARE(solutionArray.size(), puzzle.solutionMoves.size());
        for (int index = 0; index < solutionArray.size(); ++index) {
            QCOMPARE(solutionArray.at(index).toString(), puzzle.solutionMoves.at(index));
        }
    }
}

void PuzzleRunnerTest::sessionControllerBuildsAnalysisInputFromFixtureData()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));

    controller.submitUserMove(QStringLiteral("f4f3"));
    controller.submitUserMove(QStringLiteral("e7g5"));
    controller.submitUserMove(QStringLiteral("f3f4"));
    controller.submitUserMove(QStringLiteral("g7g6"));
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Solved);
    QVERIFY(controller.canAnalyzeCurrentPuzzle());

    PuzzleRound puzzleRound;
    SourceGame sourceGame;
    QVERIFY2(controller.buildAnalysisInput(&puzzleRound, &sourceGame, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(puzzleRound.puzzleId, QStringLiteral("dSgis"));
    QCOMPARE(puzzleRound.sourceGameId, QStringLiteral("zUbyC5ps"));
    QCOMPARE(puzzleRound.timeControl, QStringLiteral("10+0"));
    QCOMPARE(puzzleRound.sideToMove, QStringLiteral("black"));
    QCOMPARE(puzzleRound.lastMove, QStringLiteral("h4h3"));
    QCOMPARE(puzzleRound.solved, true);
    QCOMPARE(sourceGame.sourceGameId, QStringLiteral("zUbyC5ps"));
    QVERIFY(sourceGame.pgnText.contains(QStringLiteral("[GameId \"zUbyC5ps\"]")));
    QVERIFY(!puzzleRound.solutionMovesJson.isEmpty());
    QVERIFY(!puzzleRound.themesJson.isEmpty());
}

void PuzzleRunnerTest::sessionControllerBuildsAnalysisInputAfterFailure()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));

    const auto initialPosition = ChessPosition::fromFen(controller.gameStateStore()->currentPuzzle().fenStart, &errorMessage);
    QVERIFY2(initialPosition.has_value(), qPrintable(errorMessage));

    QString legalWrongMove;
    for (const Move &move : initialPosition->legalMoves()) {
        if (move.uci() != QStringLiteral("f4f3")) {
            legalWrongMove = move.uci();
            break;
        }
    }
    QVERIFY(!legalWrongMove.isEmpty());

    controller.submitUserMove(legalWrongMove);
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Failed);
    QVERIFY(controller.canAnalyzeCurrentPuzzle());

    PuzzleRound puzzleRound;
    SourceGame sourceGame;
    QVERIFY2(controller.buildAnalysisInput(&puzzleRound, &sourceGame, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(puzzleRound.puzzleId, QStringLiteral("dSgis"));
    QCOMPARE(sourceGame.sourceGameId, QStringLiteral("zUbyC5ps"));
    QVERIFY(sourceGame.pgnText.contains(QStringLiteral("[GameId \"zUbyC5ps\"]")));
}

void PuzzleRunnerTest::puzzleEngineAutoRepliesAndSolves()
{
    FixturePuzzleSource source;
    QString errorMessage;
    const QVector<PuzzleDefinition> puzzles = source.loadPuzzles(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY(!puzzles.isEmpty());

    PuzzleEngine engine;
    engine.loadPuzzle(puzzles.first());

    const auto initialPosition = ChessPosition::fromFen(puzzles.first().fenStart, &errorMessage);
    QVERIFY2(initialPosition.has_value(), qPrintable(errorMessage));

    SubmissionResult result = engine.submitUserMove(*initialPosition, QStringLiteral("f4f3"));
    QVERIFY(result.accepted);
    QCOMPARE(result.status, SessionStatus::Active);
    QCOMPARE(result.appliedMoves.size(), 2);
    QCOMPARE(result.appliedMoves.at(0).uci, QStringLiteral("f4f3"));
    QCOMPARE(result.appliedMoves.at(1).uci, QStringLiteral("h3h4"));

    ChessPosition position = *initialPosition;
    for (const AppliedMove &move : result.appliedMoves) {
        const auto parsedMove = Move::fromUci(move.uci);
        QVERIFY(parsedMove.has_value());
        QVERIFY(position.applyMove(*parsedMove));
    }

    result = engine.submitUserMove(position, QStringLiteral("e7g5"));
    QVERIFY(result.accepted);
    QCOMPARE(result.appliedMoves.size(), 2);
    for (const AppliedMove &move : result.appliedMoves) {
        const auto parsedMove = Move::fromUci(move.uci);
        QVERIFY(parsedMove.has_value());
        QVERIFY(position.applyMove(*parsedMove));
    }

    result = engine.submitUserMove(position, QStringLiteral("f3f4"));
    QVERIFY(result.accepted);
    QCOMPARE(result.status, SessionStatus::Active);
    QCOMPARE(result.appliedMoves.size(), 2);
    for (const AppliedMove &move : result.appliedMoves) {
        const auto parsedMove = Move::fromUci(move.uci);
        QVERIFY(parsedMove.has_value());
        QVERIFY(position.applyMove(*parsedMove));
    }

    result = engine.submitUserMove(position, QStringLiteral("g7g6"));
    QVERIFY(result.accepted);
    QCOMPARE(result.status, SessionStatus::Solved);
    QCOMPARE(result.appliedMoves.size(), 1);
    QCOMPARE(result.appliedMoves.at(0).uci, QStringLiteral("g7g6"));
}

void PuzzleRunnerTest::puzzleEngineFailsOnWrongMove()
{
    FixturePuzzleSource source;
    QString errorMessage;
    const QVector<PuzzleDefinition> puzzles = source.loadPuzzles(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QVERIFY(!puzzles.isEmpty());

    PuzzleEngine engine;
    engine.loadPuzzle(puzzles.first());

    const auto initialPosition = ChessPosition::fromFen(puzzles.first().fenStart, &errorMessage);
    QVERIFY2(initialPosition.has_value(), qPrintable(errorMessage));

    QString legalWrongMove;
    for (const Move &move : initialPosition->legalMoves()) {
        if (move.uci() != QStringLiteral("f4f3")) {
            legalWrongMove = move.uci();
            break;
        }
    }
    QVERIFY(!legalWrongMove.isEmpty());

    SubmissionResult result = engine.submitUserMove(*initialPosition, legalWrongMove);
    QVERIFY(result.accepted);
    QCOMPARE(result.status, SessionStatus::Failed);
    QCOMPARE(result.appliedMoves.size(), 1);
    QCOMPARE(result.appliedMoves.first().uci, legalWrongMove);
}

void PuzzleRunnerTest::sessionControllerReviewAndRetryFlow()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Active);
    QVERIFY(controller.canSubmitMoves());
    QCOMPARE(controller.promptText(), QStringLiteral("your turn"));
    const int puzzleStartViewIndex = controller.gameStateStore()->puzzleStartViewIndex();
    QVERIFY(puzzleStartViewIndex > 0);
    QCOMPARE(controller.gameStateStore()->currentViewIndex(), puzzleStartViewIndex);

    controller.stepBackward();
    QCOMPARE(controller.gameStateStore()->currentViewIndex(), puzzleStartViewIndex - 1);
    QVERIFY(!controller.gameStateStore()->isViewingLatest());
    QVERIFY(!controller.canSubmitMoves());
    QCOMPARE(controller.promptText(), QStringLiteral("reviewing previous position"));

    controller.stepForward();
    QCOMPARE(controller.gameStateStore()->currentViewIndex(), puzzleStartViewIndex);
    QVERIFY(controller.gameStateStore()->isViewingLatest());
    QVERIFY(controller.canSubmitMoves());

    controller.submitUserMove(QStringLiteral("f4f3"));
    QCOMPARE(controller.gameStateStore()->moves().size(), 2);
    QVERIFY(controller.canSubmitMoves());

    controller.stepBackward();
    QCOMPARE(controller.gameStateStore()->currentViewIndex(), puzzleStartViewIndex + 1);
    QVERIFY(!controller.gameStateStore()->isViewingLatest());
    QVERIFY(!controller.canSubmitMoves());
    QCOMPARE(controller.promptText(), QStringLiteral("reviewing previous position"));

    controller.stepForward();
    QCOMPARE(controller.gameStateStore()->currentViewIndex(), puzzleStartViewIndex + 2);
    QVERIFY(controller.gameStateStore()->isViewingLatest());
    QVERIFY(controller.canSubmitMoves());

    controller.retryPuzzle();
    QCOMPARE(controller.gameStateStore()->moves().size(), 0);
    QCOMPARE(controller.gameStateStore()->currentViewIndex(), puzzleStartViewIndex);
    QVERIFY(controller.gameStateStore()->isViewingLatest());
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Active);
    QCOMPARE(controller.promptText(), QStringLiteral("your turn"));
}

void PuzzleRunnerTest::sessionControllerRevealSolutionResetsFailedLine()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));

    const auto initialPosition = ChessPosition::fromFen(controller.gameStateStore()->currentPuzzle().fenStart, &errorMessage);
    QVERIFY2(initialPosition.has_value(), qPrintable(errorMessage));

    QString legalWrongMove;
    for (const Move &move : initialPosition->legalMoves()) {
        if (move.uci() != QStringLiteral("f4f3")) {
            legalWrongMove = move.uci();
            break;
        }
    }
    QVERIFY(!legalWrongMove.isEmpty());

    controller.submitUserMove(legalWrongMove);
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Failed);
    QCOMPARE(controller.gameStateStore()->moves().size(), 1);
    QCOMPARE(controller.promptText(), QStringLiteral("puzzle failed; review the position or retry"));

    controller.revealSolution();
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Solved);
    QCOMPARE(controller.gameStateStore()->moves().size(), controller.gameStateStore()->currentPuzzle().solutionMoves.size());
    QCOMPARE(controller.gameStateStore()->moves().first().uci, QStringLiteral("f4f3"));
    QCOMPARE(controller.gameStateStore()->moves().last().uci, QStringLiteral("g7g6"));
    QCOMPARE(controller.promptText(), QStringLiteral("puzzle solved"));
}

void PuzzleRunnerTest::sessionControllerAutoAdvanceMovesToNextPuzzle()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));
    controller.setDifficulty(QStringLiteral("medium"));
    controller.setAutoAdvance(true);
    QCOMPARE(controller.currentPuzzleIndex(), 1);

    controller.submitUserMove(QStringLiteral("f4d4"));
    QCOMPARE(controller.currentPuzzleIndex(), 1);
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Active);
    QCOMPARE(controller.gameStateStore()->moves().size(), 2);

    controller.submitUserMove(QStringLiteral("f6d4"));

    QCOMPARE(controller.currentPuzzleIndex(), 2);
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Active);
    QCOMPARE(controller.gameStateStore()->moves().size(), 0);
    QCOMPARE(controller.promptText(), QStringLiteral("your turn"));
}

void PuzzleRunnerTest::sessionControllerIgnoresSubmitOutsideActiveLatestView()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));

    controller.submitUserMove(QStringLiteral("f4f3"));
    QCOMPARE(controller.gameStateStore()->moves().size(), 2);

    controller.stepBackward();
    QCOMPARE(controller.promptText(), QStringLiteral("reviewing previous position"));
    const int reviewedIndex = controller.gameStateStore()->currentViewIndex();
    const int reviewedMoveCount = controller.gameStateStore()->moves().size();
    controller.submitUserMove(QStringLiteral("e7g5"));
    QCOMPARE(controller.gameStateStore()->currentViewIndex(), reviewedIndex);
    QCOMPARE(controller.gameStateStore()->moves().size(), reviewedMoveCount);
    QCOMPARE(controller.promptText(), QStringLiteral("reviewing previous position"));

    controller.stepForward();
    controller.submitUserMove(QStringLiteral("e7g5"));
    controller.submitUserMove(QStringLiteral("f3f4"));
    controller.submitUserMove(QStringLiteral("g7g6"));
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Solved);
    const int solvedMoveCount = controller.gameStateStore()->moves().size();
    controller.submitUserMove(QStringLiteral("a1a2"));
    QCOMPARE(controller.gameStateStore()->moves().size(), solvedMoveCount);
    QCOMPARE(controller.promptText(), QStringLiteral("puzzle solved"));
}

void PuzzleRunnerTest::sessionControllerRevealSolutionFromActiveState()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));

    controller.revealSolution();
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Solved);
    QCOMPARE(controller.gameStateStore()->moves().size(), controller.gameStateStore()->currentPuzzle().solutionMoves.size());
    QCOMPARE(controller.gameStateStore()->moves().first().uci, QStringLiteral("f4f3"));
    QCOMPARE(controller.gameStateStore()->moves().last().uci, QStringLiteral("g7g6"));
    QCOMPARE(controller.promptText(), QStringLiteral("puzzle solved"));
}

void PuzzleRunnerTest::sessionControllerRevealSolutionFromSolvedStateIsStable()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));

    controller.revealSolution();
    const auto solvedMoves = controller.gameStateStore()->moves();
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Solved);

    controller.revealSolution();
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Solved);
    QCOMPARE(controller.gameStateStore()->moves().size(), solvedMoves.size());
    QCOMPARE(controller.gameStateStore()->moves().first().uci, solvedMoves.first().uci);
    QCOMPARE(controller.gameStateStore()->moves().last().uci, solvedMoves.last().uci);
    QCOMPARE(controller.promptText(), QStringLiteral("puzzle solved"));
}

void PuzzleRunnerTest::sessionControllerRetryIsIdempotent()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));

    controller.submitUserMove(QStringLiteral("f4f3"));
    QCOMPARE(controller.gameStateStore()->moves().size(), 2);

    controller.retryPuzzle();
    QCOMPARE(controller.gameStateStore()->moves().size(), 0);
    QCOMPARE(controller.gameStateStore()->currentViewIndex(), controller.gameStateStore()->puzzleStartViewIndex());
    QCOMPARE(controller.promptText(), QStringLiteral("your turn"));

    controller.retryPuzzle();
    QCOMPARE(controller.gameStateStore()->moves().size(), 0);
    QCOMPARE(controller.gameStateStore()->currentViewIndex(), controller.gameStateStore()->puzzleStartViewIndex());
    QCOMPARE(controller.puzzleEngine().status(), SessionStatus::Active);
    QCOMPARE(controller.promptText(), QStringLiteral("your turn"));
}

void PuzzleRunnerTest::sessionControllerRefillsPuzzleBatchWindow()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));

    controller.setDifficulty(QStringLiteral("medium"));
    controller.setQueueSize(1);
    controller.setRefillWhenLow(true);
    controller.setRefillThreshold(0);

    QCOMPARE(controller.puzzleCount(), 1);
    QVERIFY(controller.canGoToNextPuzzle());

    controller.nextPuzzle();

    QCOMPARE(controller.currentPuzzleIndex(), 2);
    QCOMPARE(controller.puzzleCount(), 2);
    QVERIFY(!controller.canGoToNextPuzzle());
}

QTEST_MAIN(PuzzleRunnerTest)

#include "test_unit_puzzle_runner.moc"
