#include <QtTest>

#include "critical_move.h"
#include "puzzle_info_summary_builder.h"

class TestUnitPuzzleInfoSummaryBuilder : public QObject
{
    Q_OBJECT

private slots:
    void buildsCoachSummary();
    void fallsBackToOpeningFamilyWhenPgnHeadersAreAbsent();
    void warnsWhenStoredOpeningDisagreesWithPgn();
    void flagsConflictingBreakMoves();
};

void TestUnitPuzzleInfoSummaryBuilder::buildsCoachSummary()
{
    PuzzleRound puzzle;
    puzzle.puzzleId = QStringLiteral("p1");
    puzzle.themesJson = QStringLiteral("[\"exposedKing\",\"mateIn2\"]");

    SourceGame game;
    game.sourceGameId = QStringLiteral("g1");
    game.pgnText = QStringLiteral("[Event \"Test\"]\n[ECO \"C50\"]\n[Opening \"Italian Game\"]\n\n1. e4 e5");

    TacticalEvent event;
    event.openingFamily = QStringLiteral("italian_game");
    event.keyWeakness = QStringLiteral("king_exposure");
    event.kingSafetyState = QStringLiteral("back_rank_danger");
    event.structuralFeatureSummary = QStringLiteral("exposed_open_file | back_rank_vulnerable");
    event.solutionSummary = QStringLiteral("Double on the g-file and remove the main defender.");
    event.attackerCoordinationSummary = QStringLiteral("file g via queen and rook battery");
    event.retainedBreakSummary = QStringLiteral("decisive_blunder@18 | played d7d5 | best e5d4");
    event.primaryBreakPly = 18;
    event.tacticalCandidatesJson = QStringLiteral("[\"mate_net\",\"attraction\"]");

    CriticalMove move;
    move.role = QStringLiteral("decisive_blunder");
    move.playedMove = QStringLiteral("d7d5");
    move.bestMove = QStringLiteral("e5d4");
    move.criticalReasonCompactSummary = QStringLiteral("it opened the king and lost control of the attack");
    move.candidateMovesJson = QStringLiteral(
        "["
        "{\"move_uci\":\"e5d4\",\"move_san\":\"exd4\"},"
        "{\"move_uci\":\"d7d5\",\"move_san\":\"d5\"}"
        "]");
    CriticalMove practical;
    practical.role = QStringLiteral("preventative_resource");
    practical.playedMove = QStringLiteral("c7c6");
    practical.bestMove = QStringLiteral("h7h6");
    practical.whyCritical = QStringLiteral("stronger_alternative_missed");
    practical.hasEvalDeltaCp = true;
    practical.evalDeltaCp = 84;
    practical.candidateMovesJson = QStringLiteral(
        "["
        "{\"move_uci\":\"h7h6\",\"move_san\":\"h6\"},"
        "{\"move_uci\":\"c7c6\",\"move_san\":\"c6\"}"
        "]");

    const PuzzleInfoSummary summary = PuzzleInfoSummaryBuilder::build(puzzle, game, event, {practical, move});
    QVERIFY(summary.available);
    QCOMPARE(summary.opening, QStringLiteral("Italian Game (C50)"));
    QVERIFY(summary.strategicError.contains(QStringLiteral("king exposure")));
    QVERIFY(summary.plan.contains(QStringLiteral("Double on the g-file")));
    QVERIFY(summary.criticalMistake.contains(QStringLiteral("d5")));
    QVERIFY(summary.criticalMistake.contains(QStringLiteral("exd4")));
    QVERIFY(summary.lastPracticalMistake.contains(QStringLiteral("c6")));
    QVERIFY(summary.lastPracticalMistake.contains(QStringLiteral("h6")));
    QVERIFY(summary.tacticalTheme.contains(QStringLiteral("Exposed King")));
    QVERIFY(summary.tacticalTheme.contains(QStringLiteral("Mate In2")));
    QVERIFY(summary.warnings.isEmpty());
}

void TestUnitPuzzleInfoSummaryBuilder::fallsBackToOpeningFamilyWhenPgnHeadersAreAbsent()
{
    PuzzleRound puzzle;
    puzzle.puzzleId = QStringLiteral("p-fallback");

    SourceGame game;
    game.sourceGameId = QStringLiteral("g-fallback");
    game.pgnText = QStringLiteral("1. e4 e5 2. Nf3 Nc6");

    TacticalEvent event;
    event.openingFamily = QStringLiteral("italian_game");

    const PuzzleInfoSummary summary = PuzzleInfoSummaryBuilder::build(puzzle, game, event, {});
    QCOMPARE(summary.opening, QStringLiteral("Italian Game"));
    QVERIFY(summary.warnings.contains(QStringLiteral("Opening inferred from fallback field.")));
}

void TestUnitPuzzleInfoSummaryBuilder::warnsWhenStoredOpeningDisagreesWithPgn()
{
    PuzzleRound puzzle;
    puzzle.puzzleId = QStringLiteral("p-opening-conflict");

    SourceGame game;
    game.sourceGameId = QStringLiteral("g-opening-conflict");
    game.pgnText = QStringLiteral("[Event \"Test\"]\n[ECO \"C50\"]\n[Opening \"Italian Game\"]\n\n1. e4 e5");
    game.openingName = QStringLiteral("Scotch Game");

    TacticalEvent event;
    event.openingFamily = QStringLiteral("italian_game");

    const PuzzleInfoSummary summary = PuzzleInfoSummaryBuilder::build(puzzle, game, event, {});
    QCOMPARE(summary.opening, QStringLiteral("Italian Game (C50)"));
    QVERIFY(summary.warnings.contains(QStringLiteral("Stored opening disagrees with PGN header.")));
}

void TestUnitPuzzleInfoSummaryBuilder::flagsConflictingBreakMoves()
{
    PuzzleRound puzzle;
    puzzle.puzzleId = QStringLiteral("p2");

    SourceGame game;
    TacticalEvent event;
    event.primaryBreakPly = 23;
    event.retainedBreakPlayedMove = QStringLiteral("h4h3");
    event.retainedBreakBestMove = QStringLiteral("h4h3");

    CriticalMove move;
    move.playedMove = QStringLiteral("h4h3");
    move.bestMove = QStringLiteral("h4h3");
    move.playedContinuationCompact = QStringLiteral("mate appears");
    move.bestContinuationCompact = QStringLiteral("mate avoided");

    const PuzzleInfoSummary summary = PuzzleInfoSummaryBuilder::build(puzzle, game, event, {move});
    QVERIFY(summary.criticalMistake.contains(QStringLiteral("conflicting move labels")));
    QVERIFY(!summary.warnings.isEmpty());
}

QTEST_MAIN(TestUnitPuzzleInfoSummaryBuilder)
#include "test_unit_puzzle_info_summary_builder.moc"
