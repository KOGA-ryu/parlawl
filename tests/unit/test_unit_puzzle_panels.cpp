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
    void settingsCardShowsSupplyStatusText();
    void enginePanelTreatsDynamicMarkupAsPlainText();
    void settingsCardOffersValidatedPackAction();
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
