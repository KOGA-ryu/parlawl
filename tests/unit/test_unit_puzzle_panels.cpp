#include <QtTest>

#include "puzzle_panels.h"

class TestUnitPuzzlePanels : public QObject
{
    Q_OBJECT

private slots:
    void moveListPanelShowsFullSourceGameStatus();
    void moveListPanelShowsPartialSourceHistoryStatus();
    void moveListPanelShowsUnavailableSourceHistoryStatus();
    void settingsCardShowsSupplyStatusText();
};

void TestUnitPuzzlePanels::moveListPanelShowsFullSourceGameStatus()
{
    MoveListPanel panel;
    parlawl::puzzle_runner::PuzzleDefinition puzzle;
    puzzle.fenStart = QStringLiteral("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
    puzzle.analysisSeed.sourceGamePgn = QStringLiteral("[Event \"Test\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bb5");
    puzzle.analysisSeed.rawPuzzleJson = QStringLiteral(R"JSON({"puzzle":{"initialPly":4}})JSON");

    panel.setMoves(puzzle, {}, 5);
    QCOMPARE(panel.truthStatusText(), QStringLiteral("Full source game shown; board review starts at the puzzle position."));
}

void TestUnitPuzzlePanels::moveListPanelShowsPartialSourceHistoryStatus()
{
    MoveListPanel panel;
    parlawl::puzzle_runner::PuzzleDefinition puzzle;
    puzzle.fenStart = QStringLiteral("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
    puzzle.analysisSeed.rawPuzzleJson = QStringLiteral(R"JSON({"puzzle":{"initialPly":4},"game":{"pgn":"e4 e5 Nf3 Nc6 Bb5"}})JSON");

    panel.setMoves(puzzle, {}, 5);
    QCOMPARE(panel.truthStatusText(), QStringLiteral("Partial source history shown up to the puzzle start; board review starts at the puzzle position."));
}

void TestUnitPuzzlePanels::moveListPanelShowsUnavailableSourceHistoryStatus()
{
    MoveListPanel panel;
    parlawl::puzzle_runner::PuzzleDefinition puzzle;
    puzzle.fenStart = QStringLiteral("8/8/8/8/8/8/8/K6k w - - 0 1");

    panel.setMoves(puzzle, {}, 0);
    QCOMPARE(panel.truthStatusText(), QStringLiteral("Source history unavailable; showing puzzle-local history only."));
}

void TestUnitPuzzlePanels::settingsCardShowsSupplyStatusText()
{
    SettingsCard card;
    card.setSupplyStatusText(QStringLiteral("Using restored cached live puzzles from a previous session. Add a token to reload from Lichess."));
    QCOMPARE(
        card.supplyStatusText(),
        QStringLiteral("Using restored cached live puzzles from a previous session. Add a token to reload from Lichess."));
}

QTEST_MAIN(TestUnitPuzzlePanels)

#include "test_unit_puzzle_panels.moc"
