#include <QtTest>
#include <algorithm>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>

#include "puzzle_panels.h"

namespace parlawl::test_support {
QByteArray syntheticAnnotatedReplayJson();
}

namespace {

parlawl::puzzle_runner::MechanicalReplayGame mechanicalGameFixture()
{
    parlawl::puzzle_runner::MechanicalReplayGame game;
    game.sourceGameId = QStringLiteral(
        "chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111");
    game.canonicalGameUrl = QStringLiteral("https://www.chess.com/game/live/1");
    game.eventStartUtc = QStringLiteral("2026-08-01T12:00:00Z");
    game.whiteUsername = QStringLiteral("Alpha");
    game.blackUsername = QStringLiteral("Beta");
    game.whiteRating = 2100;
    game.blackRating = 2050;
    game.result = QStringLiteral("1-0");
    game.openingStatus = QStringLiteral("classified");
    game.openingEco = QStringLiteral("C20");
    game.openingName = QStringLiteral("King's Pawn Game");
    game.openingLastBookPly = 2;
    game.viewedPlayerColor = QStringLiteral("white");
    const auto addMove = [&game](int ply, const QString &san, const QString &uci, int legal, qint64 elapsed) {
        parlawl::puzzle_runner::MechanicalReplayMove move;
        move.ply = ply;
        move.san = san;
        move.uci = uci;
        move.positionPhase = QStringLiteral("opening");
        move.forcednessStatus = QStringLiteral("nonforced");
        move.legalMoveCount = legal;
        move.decisionStartClockMs = 180'000;
        move.clockRemainingAfterMoveMs = 180'000 - elapsed;
        move.elapsedMoveMs = elapsed;
        move.elapsedStatus = QStringLiteral("derived_clock_difference");
        game.moves.append(move);
    };
    addMove(1, QStringLiteral("e4"), QStringLiteral("e2e4"), 20, 1'000);
    addMove(2, QStringLiteral("e5"), QStringLiteral("e7e5"), 20, 2'000);
    addMove(3, QStringLiteral("Nf3"), QStringLiteral("g1f3"), 29, 3'000);
    addMove(4, QStringLiteral("Nc6"), QStringLiteral("b8c6"), 29, 4'000);
    return game;
}

parlawl::puzzle_runner::MechanicalReplayGame mechanicalGameWithEngineFixture()
{
    using namespace parlawl::puzzle_runner;
    MechanicalReplayGame game = mechanicalGameFixture();
    PersistedEngineGameEvidence engineGame;
    engineGame.evidenceId = QStringLiteral("player-game-engine-view-v1:")
        + QString(64, QLatin1Char('a'));
    engineGame.representativeRunId = QStringLiteral("performance-analysis-run-v2:")
        + QString(64, QLatin1Char('b'));
    engineGame.analysisRecordedAtUtc = QStringLiteral("2026-08-03T12:00:00Z");
    engineGame.lineageCount = 1;
    engineGame.engineConfigId = QStringLiteral("performance-engine-config-v1:")
        + QString(64, QLatin1Char('c'));
    engineGame.engineName = QStringLiteral("Stockfish 18");
    engineGame.engineAuthor = QStringLiteral("Stockfish developers");
    engineGame.engineBinarySha256 = QString(64, QLatin1Char('d'));
    engineGame.engineAdapterVersion = QStringLiteral("stockfish-complete-position-v1");
    engineGame.nodeLimit = 1'000;
    engineGame.hashMebibytes = 16;
    engineGame.threads = 1;
    engineGame.wdlLossThresholds = {25'000, 50'000, 100'000};
    engineGame.winningExpectationMillionths = 750'000;
    game.engineEvidence = engineGame;
    for (int index = 0; index < game.moves.size(); ++index) {
        PersistedEngineMoveEvidence move;
        const bool severe = index == 2;
        move.expectedBeforeMillionths = 750'000;
        move.expectedAfterMillionths = severe ? 584'000 : 740'000;
        move.wdlLossMillionths = severe ? 166'000 : 10'000;
        move.centipawnLoss = severe ? 40 : 5;
        move.missedWinningAdvantage = severe;
        move.severity = severe ? QStringLiteral("severe") : QStringLiteral("none");
        move.beforeScoreKind = QStringLiteral("cp");
        move.beforeCentipawnsWhite = 100;
        move.beforeWdlWhite = {500, 500, 0};
        move.beforeBestMoveUci = QStringLiteral("d2d4");
        move.beforeDepth = 7;
        move.beforeSelectiveDepth = 9;
        move.beforeNodes = 1'000;
        move.beforePvUci = QStringLiteral("d2d4 d7d5");
        move.afterScoreKind = QStringLiteral("cp");
        move.afterCentipawnsWhite = severe ? 60 : 90;
        move.afterWdlWhite = {450, 550, 0};
        game.moves[index].engineEvidence = move;
    }
    return game;
}

} // namespace

class TestUnitPuzzlePanels : public QObject
{
    Q_OBJECT

private slots:
    void moveListPanelShowsFullSourceGameStatus();
    void moveListPanelShowsPartialSourceHistoryStatus();
    void moveListPanelShowsUnavailableSourceHistoryStatus();
    void moveListPanelShowsAnnotatedReplaySeverity();
    void replayEvidencePanelShowsRecordedVariationBoundary();
    void replayEvidencePanelMarksForgedSuppliedTextUnverified();
    void gameBreakdownShowsClockAndOpeningBoundaryWithoutEngineClaims();
    void gameBreakdownShowsPersistedEngineEvidenceWithoutStartingEngine();
    void gameReviewPanelKeepsMovesAndEvidenceTogether();
    void settingsCardShowsSupplyStatusText();
    void enginePanelTreatsDynamicMarkupAsPlainText();
    void settingsCardOffersValidatedPackAction();
    void settingsCardOffersTerminalAttemptExportAction();
    void metadataCardLabelsImportedLineWithoutProofClaim();
    void metadataCardTreatsDynamicMarkupAsPlainText();
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

void TestUnitPuzzlePanels::moveListPanelShowsAnnotatedReplaySeverity()
{
    QString error;
    const auto pack = parlawl::puzzle_runner::AnnotatedReplayPack::fromJson(
        parlawl::test_support::syntheticAnnotatedReplayJson(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));

    MoveListPanel panel;
    panel.setAnnotatedReplay(*pack, 1, false, 0);
    QCOMPARE(
        panel.truthStatusText(),
        QStringLiteral("Legal move/FEN replay verified. Severity labels and derived annotations are supplied and not verified by ParlAWL."));
    const auto *table = panel.findChild<QTableWidget *>();
    QVERIFY(table != nullptr);
    QVERIFY(table->item(0, 1) != nullptr);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("e4  [severe]"));
}

void TestUnitPuzzlePanels::replayEvidencePanelShowsRecordedVariationBoundary()
{
    QString error;
    const auto pack = parlawl::puzzle_runner::AnnotatedReplayPack::fromJson(
        parlawl::test_support::syntheticAnnotatedReplayJson(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    parlawl::puzzle_runner::ReplaySession session;
    session.load(*pack);
    QVERIFY(session.seekMainlinePly(1));

    ReplayEvidencePanel panel;
    panel.setReplayState(*pack, session, 0);
    QVERIFY(panel.summaryText().contains(QStringLiteral("Supplied derived mover expectation loss (not verified): 10%")));
    QVERIFY(panel.summaryText().contains(QStringLiteral("Supplied explanation (not verified)")));
    QVERIFY(panel.summaryText().contains(QStringLiteral("Supplied derived annotations (not verified)")));
    QVERIFY(panel.canShowEngineLine());
    QVERIFY(!panel.canReturnToGame());

    QVERIFY2(session.enterPreferredVariation(1, &error), qPrintable(error));
    QVERIFY(session.stepForward());
    panel.setReplayState(*pack, session, 1);
    QVERIFY(panel.summaryText().contains(QStringLiteral("ENGINE LINE, NOT PLAYED")));
    QVERIFY(!panel.canShowEngineLine());
    QVERIFY(panel.canReturnToGame());
}

void TestUnitPuzzlePanels::replayEvidencePanelMarksForgedSuppliedTextUnverified()
{
    QByteArray supplied = parlawl::test_support::syntheticAnnotatedReplayJson();
    supplied.replace(
        QByteArrayLiteral("1.e4 moves the pawn from e2 to e4."),
        QByteArrayLiteral("<b>A supplied claim with an unrecomputed identity.</b>"));
    QString error;
    const auto pack = parlawl::puzzle_runner::AnnotatedReplayPack::fromJson(supplied, &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    QVERIFY(!pack->suppliedAnnotationsAreVerified());

    parlawl::puzzle_runner::ReplaySession session;
    session.load(*pack);
    QVERIFY(session.seekMainlinePly(1));
    ReplayEvidencePanel panel;
    panel.setReplayState(*pack, session, 0);
    QVERIFY(panel.summaryText().contains(QStringLiteral("<b>A supplied claim with an unrecomputed identity.</b>")));
    QVERIFY(panel.summaryText().contains(QStringLiteral("Supplied explanation (not verified)")));
    QVERIFY(panel.summaryText().contains(QStringLiteral("Supplied derived annotations (not verified)")));
    for (const QLabel *label : panel.findChildren<QLabel *>()) {
        QCOMPARE(label->textFormat(), Qt::PlainText);
    }
}

void TestUnitPuzzlePanels::gameBreakdownShowsClockAndOpeningBoundaryWithoutEngineClaims()
{
    const auto game = mechanicalGameFixture();

    QString error;
    const auto pack = parlawl::puzzle_runner::AnnotatedReplayPack::fromMechanicalGame(game, &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    parlawl::puzzle_runner::ReplaySession session;
    session.load(*pack);

    ReplayEvidencePanel evidence;
    evidence.setReplayState(*pack, session, 0);
    QVERIFY(evidence.summaryText().contains(QStringLiteral("Longest server-recorded decision")));
    QVERIFY(evidence.summaryText().contains(QStringLiteral("No best-move, blunder")));
    QVERIFY(!evidence.canShowEngineLine());
    QVERIFY(session.seekMainlinePly(3));
    evidence.setReplayState(*pack, session, 0);
    QVERIFY(evidence.summaryText().contains(QStringLiteral("first recorded departure")));
    QVERIFY(evidence.summaryText().contains(QStringLiteral("Server-accounted move time: 3.0s")));

    MoveListPanel moves;
    moves.setAnnotatedReplay(*pack, 3, false, 0);
    QVERIFY(moves.truthStatusText().contains(QStringLiteral("server-accounted clock evidence")));
    const QTableWidget *table = moves.findChild<QTableWidget *>();
    QVERIFY(table != nullptr);
    QVERIFY(table->item(1, 1)->text().contains(QStringLiteral("first departure")));
}

void TestUnitPuzzlePanels::gameBreakdownShowsPersistedEngineEvidenceWithoutStartingEngine()
{
    QString error;
    const auto pack = parlawl::puzzle_runner::AnnotatedReplayPack::fromMechanicalGame(
        mechanicalGameWithEngineFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    parlawl::puzzle_runner::ReplaySession session;
    session.load(*pack);

    ReplayEvidencePanel evidence;
    evidence.setReplayState(*pack, session, 0);
    QVERIFY(evidence.summaryText().contains(QStringLiteral("GAME REPORT V1")));
    QVERIFY(evidence.summaryText().contains(QStringLiteral("Coverage: persisted fixed-node evidence for 4/4 recorded moves")));
    QVERIFY(evidence.summaryText().contains(QStringLiteral("Threshold labels: Severe 1 · Mistake 0 · Inaccuracy 0")));
    QVERIFY(evidence.summaryText().contains(QStringLiteral("1. Ply 3 Nf3 — Alpha — Severe — 16.6% mover expectation loss")));
    QVERIFY(evidence.summaryText().contains(QStringLiteral("best d2d4")));
    QVERIFY(evidence.summaryText().contains(QStringLiteral("do not prove cause, intent, or a unique best move")));

    QVERIFY(session.seekMainlinePly(3));

    evidence.setReplayState(*pack, session, 0);
    QVERIFY(evidence.summaryText().contains(QStringLiteral("PERSISTED FIXED-NODE REPORT")));
    QVERIFY(evidence.summaryText().contains(QStringLiteral("Frozen threshold label: Severe")));
    QVERIFY(evidence.summaryText().contains(QStringLiteral("Engine-reported best move before play: d2d4")));
    QVERIFY(evidence.summaryText().contains(QStringLiteral("Reported PV, not played: d2d4 d7d5")));
    QVERIFY(evidence.summaryText().contains(QStringLiteral("not an objective verdict")));

    MoveListPanel moves;
    moves.setAnnotatedReplay(*pack, 3, false, 0);
    QVERIFY(moves.truthStatusText().contains(QStringLiteral("starts no engine")));
    const QTableWidget *table = moves.findChild<QTableWidget *>();
    QVERIFY(table != nullptr);
    QVERIFY(table->item(1, 1)->text().contains(QStringLiteral("severe")));
    QVERIFY(table->item(1, 1)->text().contains(QStringLiteral("+0.60")));
}

void TestUnitPuzzlePanels::gameReviewPanelKeepsMovesAndEvidenceTogether()
{
    QString error;
    const auto pack = parlawl::puzzle_runner::AnnotatedReplayPack::fromMechanicalGame(
        mechanicalGameFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    parlawl::puzzle_runner::ReplaySession session;
    session.load(*pack);
    QVERIFY(session.seekMainlinePly(3));

    GameReviewPanel panel;
    panel.resize(620, 760);
    panel.setReplayState(*pack, session, 0);
    panel.show();
    QCoreApplication::processEvents();

    auto *moves = panel.findChild<MoveListPanel *>(QStringLiteral("gameReviewMoveList"));
    auto *evidence = panel.findChild<ReplayEvidencePanel *>(QStringLiteral("gameReviewInspector"));
    QVERIFY(moves != nullptr);
    QVERIFY(evidence != nullptr);
    QVERIFY(moves->isVisible());
    QVERIFY(evidence->isVisible());
    QVERIFY(evidence->summaryText().contains(QStringLiteral("first recorded departure")));

    QSignalSpy seekSpy(&panel, &GameReviewPanel::replayPlyRequested);
    auto *table = moves->findChild<QTableWidget *>();
    QVERIFY(table != nullptr);
    QVERIFY(QMetaObject::invokeMethod(
        table,
        "cellClicked",
        Qt::DirectConnection,
        Q_ARG(int, 1),
        Q_ARG(int, 2)));
    QCOMPARE(seekSpy.count(), 1);
    QCOMPARE(seekSpy.at(0).at(0).toInt(), 4);
}

void TestUnitPuzzlePanels::settingsCardShowsSupplyStatusText()
{
    SettingsCard card;
    card.setSupplyStatusText(QStringLiteral(
        "<b>Using restored cached live puzzles from a previous session.</b> Add a token to reload from Lichess."));
    QCOMPARE(
        card.supplyStatusText(),
        QStringLiteral("<b>Using restored cached live puzzles from a previous session.</b> Add a token to reload from Lichess."));
    bool foundStatusLabel = false;
    for (const QLabel *label : card.findChildren<QLabel *>()) {
        if (label->text() == card.supplyStatusText()) {
            foundStatusLabel = true;
            QCOMPARE(label->textFormat(), Qt::PlainText);
        }
    }
    QVERIFY(foundStatusLabel);
}

void TestUnitPuzzlePanels::enginePanelTreatsDynamicMarkupAsPlainText()
{
    EnginePanel panel;
    panel.setReviewState(
        QStringLiteral("<b>status</b>"),
        QStringLiteral("<i>+1.0</i>"),
        QStringLiteral("<u>e2e4</u>"),
        QStringLiteral("<script>e2e4 e7e5</script>"),
        true,
        false,
        false);
    const QList<QLabel *> labels = panel.findChildren<QLabel *>();
    QCOMPARE(labels.size(), 4);
    for (const QLabel *label : labels) {
        QCOMPARE(label->textFormat(), Qt::PlainText);
        QVERIFY(label->text().contains(QLatin1Char('<')));
    }
}

void TestUnitPuzzlePanels::settingsCardOffersValidatedPackAction()
{
    SettingsCard card;
    QPushButton *openButton = nullptr;
    for (QPushButton *button : card.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Import Engine-Line Pack")) {
            openButton = button;
            break;
        }
    }
    QVERIFY(openButton != nullptr);
    QSignalSpy spy(&card, &SettingsCard::openValidatedPuzzlePackRequested);
    openButton->click();
    QCOMPARE(spy.count(), 1);
}

void TestUnitPuzzlePanels::settingsCardOffersTerminalAttemptExportAction()
{
    SettingsCard card;
    QPushButton *exportButton = nullptr;
    for (QPushButton *button : card.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Export Solve History")) {
            exportButton = button;
            break;
        }
    }
    QVERIFY(exportButton != nullptr);

    QSignalSpy spy(&card, &SettingsCard::exportSolveHistoryRequested);
    exportButton->click();
    QCOMPARE(spy.count(), 1);

    bool foundBoundaryNote = false;
    for (const QLabel *label : card.findChildren<QLabel *>()) {
        if (label->text().contains(QStringLiteral("only completed solved or failed attempts"))) {
            foundBoundaryNote = true;
            QCOMPARE(label->textFormat(), Qt::PlainText);
            QVERIFY(label->text().contains(QStringLiteral("exact retained imported engine-line records")));
            QVERIFY(label->text().contains(QStringLiteral("Local, open, abandoned, invalid, and non-imported attempts are not exported")));
            QVERIFY(label->text().contains(QStringLiteral("internal consistency, not authenticity")));
            QVERIFY(label->text().contains(QStringLiteral("not used to train anything automatically")));
        }
    }
    QVERIFY(foundBoundaryNote);
}

void TestUnitPuzzlePanels::metadataCardLabelsImportedLineWithoutProofClaim()
{
    MetadataCard card;
    parlawl::puzzle_runner::PuzzleDefinition puzzle;
    puzzle.id = QStringLiteral("puzzle-v1:test");
    puzzle.metadata.title = parlawl::puzzle_runner::importedEngineRecordTitle();
    puzzle.metadata.source = parlawl::puzzle_runner::importedEngineRecordSource();
    puzzle.analysisSeed.sourceProvider = QStringLiteral("chesscom");
    puzzle.metadata.themes = {QStringLiteral("fork")};
    puzzle.analysisSeed.rawSourceRecordJson = QStringLiteral("{\"status\":\"engine_validated\"}");
    card.setPuzzle(puzzle, 0, 1, QStringLiteral("active"));

    QStringList labelTexts;
    for (const QLabel *label : card.findChildren<QLabel *>()) {
        labelTexts.append(label->text());
    }
    QVERIFY(std::any_of(labelTexts.cbegin(), labelTexts.cend(), [](const QString &text) {
        return text.contains(QStringLiteral("declares engine_validated"))
            && text.contains(QStringLiteral("not independently verified by ParlAWL"))
            && text.contains(QStringLiteral("did not rerun the engine"))
            && text.contains(QStringLiteral("did not authenticate the producer"))
            && text.contains(QStringLiteral("do not prove engine optimality, uniqueness, or forced play"));
    }));
    QVERIFY(labelTexts.contains(
        QStringLiteral("Supplied by imported record (not independently verified): fork")));
}

void TestUnitPuzzlePanels::metadataCardTreatsDynamicMarkupAsPlainText()
{
    MetadataCard card;
    parlawl::puzzle_runner::PuzzleDefinition puzzle;
    puzzle.id = QStringLiteral("puzzle-v1:markup");
    puzzle.metadata.title = QStringLiteral("<b>Imported title</b>");
    puzzle.metadata.source = parlawl::puzzle_runner::importedEngineRecordSource();
    puzzle.metadata.themes = {QStringLiteral("<i>fork</i>")};
    puzzle.analysisSeed.sourceProvider = QStringLiteral("<u>provider</u>");
    puzzle.analysisSeed.rawSourceRecordJson = QStringLiteral("{\"claim\":\"<script>not markup</script>\"}");
    card.setPuzzle(puzzle, 0, 1, QStringLiteral("active"));

    int plainTextLabelCount = 0;
    int injectedMarkupLabelCount = 0;
    for (const QLabel *label : card.findChildren<QLabel *>()) {
        if (label->textFormat() == Qt::PlainText) {
            ++plainTextLabelCount;
        }
        if (label->text().contains(QLatin1Char('<'))) {
            ++injectedMarkupLabelCount;
            QCOMPARE(label->textFormat(), Qt::PlainText);
        }
    }
    QCOMPARE(plainTextLabelCount, 10);
    QVERIFY(injectedMarkupLabelCount >= 4);
}

QTEST_MAIN(TestUnitPuzzlePanels)

#include "test_unit_puzzle_panels.moc"
