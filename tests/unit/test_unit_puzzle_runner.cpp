#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "fixture_puzzle_source.h"
#include "chess_position.h"
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
    void sessionControllerAtomicallyAcceptsUniversalDifficultyPack();
    void chessPositionValidatesEnPassantState();
    void chessPositionEmitsOnlyCapturableEnPassant();
    void lichessHydrationRequiresExplicitProvider();
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
    QCOMPARE(puzzle.analysisSeed.allowLichessPgnHydration, false);
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

void PuzzleRunnerTest::sessionControllerAtomicallyAcceptsUniversalDifficultyPack()
{
    SessionController controller;
    QString errorMessage;
    QVERIFY2(controller.initialize(&errorMessage), qPrintable(errorMessage));
    const QString originalId = controller.gameStateStore()->currentPuzzle().id;

    PuzzleDefinition imported;
    imported.id = QStringLiteral("validated-pack-puzzle");
    imported.fenStart = QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    imported.solutionMoves = {QStringLiteral("e2e4")};
    imported.metadata.difficulty = QStringLiteral("all");
    QVERIFY2(controller.replacePuzzles({imported}, &errorMessage), qPrintable(errorMessage));
    QCOMPARE(controller.puzzleCount(), 1);
    QCOMPARE(controller.gameStateStore()->currentPuzzle().id, imported.id);

    PuzzleDefinition invalid = imported;
    invalid.id = QStringLiteral("invalid-replacement");
    invalid.fenStart = QStringLiteral("not a fen");
    QVERIFY(!controller.replacePuzzles({invalid}, &errorMessage));
    QCOMPARE(controller.puzzleCount(), 1);
    QCOMPARE(controller.gameStateStore()->currentPuzzle().id, imported.id);
    QVERIFY(controller.gameStateStore()->currentPuzzle().id != originalId);

    QVERIFY(!controller.replacePuzzles({imported, imported}, &errorMessage));
    QVERIFY2(errorMessage.contains(QStringLiteral("repeats puzzle id")), qPrintable(errorMessage));
    QCOMPARE(controller.puzzleCount(), 1);
    QCOMPARE(controller.gameStateStore()->currentPuzzle().id, imported.id);

    PuzzleDefinition illegalLine = imported;
    illegalLine.id = QStringLiteral("illegal-later-solution-ply");
    illegalLine.solutionMoves = {
        QStringLiteral("e2e4"),
        QStringLiteral("e7e5"),
        QStringLiteral("e1e3"),
    };
    QVERIFY(!controller.replacePuzzles({illegalLine}, &errorMessage));
    QVERIFY2(errorMessage.contains(QStringLiteral("solution move 3")), qPrintable(errorMessage));
    QCOMPARE(controller.puzzleCount(), 1);
    QCOMPARE(controller.gameStateStore()->currentPuzzle().id, imported.id);

    PuzzleDefinition forgedImported = imported;
    forgedImported.id = QStringLiteral("forged-imported-style");
    forgedImported.metadata.title = importedEngineRecordTitle();
    forgedImported.metadata.source = importedEngineRecordSource();
    forgedImported.metadata.sourceLabel = importedEngineRecordSourceLabel(QStringLiteral("forged"));
    forgedImported.analysisSeed.sourceProvider = QStringLiteral("forged");
    forgedImported.analysisSeed.sourceRecordSchema = importedEngineRecordSchema();
    forgedImported.analysisSeed.sourceRecordId = QStringLiteral(
        "puzzle-record-v1:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    forgedImported.analysisSeed.rawSourceRecordJson = QStringLiteral("{}");
    QVERIFY(!controller.replacePuzzles({forgedImported}, &errorMessage));
    QVERIFY2(errorMessage.contains(QStringLiteral("provenance")), qPrintable(errorMessage));
    QCOMPARE(controller.puzzleCount(), 1);
    QCOMPARE(controller.gameStateStore()->currentPuzzle().id, imported.id);

    QVERIFY(!controller.appendPuzzles({illegalLine}, &errorMessage));
    QVERIFY2(errorMessage.contains(QStringLiteral("solution move 3")), qPrintable(errorMessage));
    QCOMPARE(controller.puzzleCount(), 1);

    PuzzleDefinition appendable = imported;
    appendable.id = QStringLiteral("duplicate-append-input");
    QVERIFY(!controller.appendPuzzles({appendable, appendable}, &errorMessage));
    QVERIFY2(errorMessage.contains(QStringLiteral("append input repeats")), qPrintable(errorMessage));
    QCOMPARE(controller.puzzleCount(), 1);
}

void PuzzleRunnerTest::chessPositionValidatesEnPassantState()
{
    QString errorMessage;
    auto whiteCapture = ChessPosition::fromFen(
        QStringLiteral("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2"),
        &errorMessage);
    QVERIFY2(whiteCapture.has_value(), qPrintable(errorMessage));
    const auto whiteMove = Move::fromUci(QStringLiteral("e5d6"));
    QVERIFY(whiteMove.has_value());
    QVERIFY(whiteCapture->isLegalMove(*whiteMove));
    QVERIFY(whiteCapture->applyMove(*whiteMove));
    QCOMPARE(
        whiteCapture->toFen(),
        QStringLiteral("4k3/8/3P4/8/8/8/8/4K3 b - - 0 2"));

    auto blackCapture = ChessPosition::fromFen(
        QStringLiteral("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1"),
        &errorMessage);
    QVERIFY2(blackCapture.has_value(), qPrintable(errorMessage));
    const auto blackMove = Move::fromUci(QStringLiteral("d4e3"));
    QVERIFY(blackMove.has_value());
    QVERIFY(blackCapture->isLegalMove(*blackMove));
    QVERIFY(blackCapture->applyMove(*blackMove));
    QCOMPARE(
        blackCapture->toFen(),
        QStringLiteral("4k3/8/8/8/8/4p3/8/4K3 w - - 0 2"));

    const QStringList impossibleFens{
        QStringLiteral("4k3/8/8/3pP3/8/8/8/4K3 w - d3 0 2"),
        QStringLiteral("4k3/8/3N4/3pP3/8/8/8/4K3 w - d6 0 2"),
        QStringLiteral("4k3/8/8/4P3/8/8/8/4K3 w - d6 0 2"),
        QStringLiteral("4k3/8/8/3PP3/8/8/8/4K3 w - d6 0 2"),
        QStringLiteral("4k3/3p4/8/3pP3/8/8/8/4K3 w - d6 0 2"),
        QStringLiteral("4k3/8/8/3p4/8/8/8/4K3 w - d6 0 2"),
        QStringLiteral("4k3/8/8/3pP3/8/8/8/4K3 w - d6 1 2"),
        QStringLiteral("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"),
    };
    for (const QString &fen : impossibleFens) {
        errorMessage.clear();
        QVERIFY2(!ChessPosition::fromFen(fen, &errorMessage).has_value(), qPrintable(fen));
        QVERIFY2(!errorMessage.isEmpty(), qPrintable(fen));
    }
}

void PuzzleRunnerTest::chessPositionEmitsOnlyCapturableEnPassant()
{
    QString errorMessage;
    auto noCapturer = ChessPosition::fromFen(
        QStringLiteral("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"),
        &errorMessage);
    QVERIFY2(noCapturer.has_value(), qPrintable(errorMessage));
    const auto e2e4 = Move::fromUci(QStringLiteral("e2e4"));
    QVERIFY(e2e4.has_value());
    QVERIFY(noCapturer->applyMove(*e2e4));
    QCOMPARE(noCapturer->toFen(), QStringLiteral("4k3/8/8/8/4P3/8/8/4K3 b - - 0 1"));

    auto adjacentCapturer = ChessPosition::fromFen(
        QStringLiteral("4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1"),
        &errorMessage);
    QVERIFY2(adjacentCapturer.has_value(), qPrintable(errorMessage));
    QVERIFY(adjacentCapturer->applyMove(*e2e4));
    QCOMPARE(
        adjacentCapturer->toFen(),
        QStringLiteral("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1"));
    const auto d4e3 = Move::fromUci(QStringLiteral("d4e3"));
    QVERIFY(d4e3.has_value());
    QVERIFY(adjacentCapturer->applyMove(*d4e3));
    QCOMPARE(
        adjacentCapturer->toFen(),
        QStringLiteral("4k3/8/8/8/8/4p3/8/4K3 w - - 0 2"));
}

void PuzzleRunnerTest::lichessHydrationRequiresExplicitProvider()
{
    PuzzleAnalysisSeed seed;
    QVERIFY(!allowsLichessPgnHydration(seed));
    seed.allowLichessPgnHydration = true;
    QVERIFY(!allowsLichessPgnHydration(seed));
    seed.sourceProvider = QStringLiteral("unknown");
    QVERIFY(!allowsLichessPgnHydration(seed));
    seed.sourceProvider = QStringLiteral("Lichess");
    QVERIFY(!allowsLichessPgnHydration(seed));
    seed.sourceProvider = QStringLiteral("lichess");
    QVERIFY(allowsLichessPgnHydration(seed));
}

QTEST_MAIN(PuzzleRunnerTest)

#include "test_unit_puzzle_runner.moc"
