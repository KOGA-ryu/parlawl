#include <QtTest>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextEdit>

#include "market_chart_widget.h"
#include "market_hud_widget.h"
#include "market_rush_panel.h"
#include "market_study_panel.h"
#include "market_test_support.h"
#include "market_workspace_window.h"

using namespace parlawl::market;
using namespace parlawl::market_test;

namespace {

QString labelText(const QWidget *root, const QString &objectName)
{
    const auto *label = root->findChild<const QLabel *>(objectName);
    return label == nullptr ? QString() : label->text();
}

} // namespace

class TestUnitMarketPanels : public QObject
{
    Q_OBJECT

private slots:
    void chartDrawsOnlyTheWindowUntilTheRevealDisclosesMore();
    void chartLeavesAHaltedBarAsAGapRatherThanZero();
    void hudDistinguishesRecomputedStatsFromSuppliedOnes();
    void hudLabelsTheSeedAsASeedAndNotADifficulty();
    void rushKeyMapAnswersEveryPlyKind();
    void rushRefusesAChoiceOutsideTheDeclaredEnumeration();
    void rushDeadlineExpiryClosesTheRepAsTimedOut();
    void rushSpaceOpensTheRevealOnlyOnceTheRepIsTerminal();
    void studyBuildsOnePlanRowPerPly();
    void studyCommitsTheWholeLineThenAsksForTheInterval();
    void studyRevealCarriesTheDisclaimerAndCitationChain();
    void studyStepsTheContinuationOneBarAtATime();
    void workspaceRefusesAPackWithoutItsSealedPartner();
    void workspaceLoadsBothHalvesAndPopulatesEverySurface();
};

void TestUnitMarketPanels::chartDrawsOnlyTheWindowUntilTheRevealDisclosesMore()
{
    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(visibleMarketFixture(), sealedMarketFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    const MarketPuzzleVisible &puzzle = pack->puzzles().first();

    MarketChartWidget chart;
    chart.setWindow(
        puzzle.displaySymbol, puzzle.window.grain, puzzle.window.bars, puzzle.window.sessionBreakAfter);
    QCOMPARE(chart.drawnVisibleBarCount(), 24);
    QCOMPARE(chart.drawnContinuationBarCount(), 0);
    QVERIFY(chart.titleText().contains(puzzle.displaySymbol));
    QVERIFY(chart.titleText().contains(QStringLiteral("close at T = 100.0")));
    QVERIFY(chart.findChild<QWidget *>(QStringLiteral("marketChartPriceView")) != nullptr);
    QVERIFY(chart.findChild<QWidget *>(QStringLiteral("marketChartVolumeView")) != nullptr);

    QVector<MarketBar> continuation;
    for (int index = 0; index < 4; ++index) {
        MarketBar bar;
        bar.barIndex = index;
        bar.open = 100.0;
        bar.high = 101.0;
        bar.low = 99.0;
        bar.close = 100.5;
        bar.volume = 1.0;
        continuation.append(bar);
    }
    chart.setRevealedContinuation(continuation, 2);
    QCOMPARE(chart.drawnVisibleBarCount(), 24);
    QCOMPARE(chart.drawnContinuationBarCount(), 2);
    QVERIFY(chart.titleText().contains(QStringLiteral("2 bars after T revealed")));

    chart.clearRevealedContinuation();
    QCOMPARE(chart.drawnContinuationBarCount(), 0);
}

void TestUnitMarketPanels::chartLeavesAHaltedBarAsAGapRatherThanZero()
{
    QVector<MarketBar> bars;
    for (int index = 0; index < 3; ++index) {
        MarketBar bar;
        bar.barIndex = index;
        if (index != 1) {
            bar.open = 100.0;
            bar.high = 101.0;
            bar.low = 99.0;
            bar.close = 100.0;
            bar.volume = 1.0;
        }
        bars.append(bar);
    }
    MarketChartWidget chart;
    chart.setWindow(QStringLiteral("SYM-0001"), QStringLiteral("1d"), bars, {});
    // Three bars in, two candles out: the halt is a gap, not a zero.
    QCOMPARE(chart.drawnVisibleBarCount(), 2);
}

void TestUnitMarketPanels::hudDistinguishesRecomputedStatsFromSuppliedOnes()
{
    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(visibleMarketFixture(), sealedMarketFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));

    MarketHudWidget hud;
    hud.setPuzzle(pack->puzzles().first(), pack->header().verifiedHudStats);
    QCOMPARE(hud.statCount(), 6);
    QVERIFY(hud.statText(QStringLiteral("atr_pct_20")).contains(QStringLiteral("recomputed by ParlAWL")));
    QVERIFY(hud.statText(QStringLiteral("return_5")).contains(QStringLiteral("recomputed by ParlAWL")));
    QVERIFY(
        hud.statText(QStringLiteral("volume_ratio_20")).contains(QStringLiteral("producer-supplied")));
    QVERIFY(hud.findChild<QLabel *>(QStringLiteral("marketHudStat_atr_pct_20")) != nullptr);

    hud.clear();
    QCOMPARE(hud.statCount(), 0);
}

void TestUnitMarketPanels::hudLabelsTheSeedAsASeedAndNotADifficulty()
{
    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(visibleMarketFixture(), sealedMarketFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    MarketHudWidget hud;
    hud.setPuzzle(pack->puzzles().first(), pack->header().verifiedHudStats);
    const QString seed = hud.seedText();
    QVERIFY(seed.contains(QStringLiteral("atr_percentile_v1")));
    QVERIFY(seed.contains(ratingSeedDisclaimer()));
    // The word "difficulty" appears only inside the denial.
    QVERIFY(seed.contains(QStringLiteral("not a measured difficulty and not a solver rating")));
    QVERIFY(!seed.contains(QStringLiteral("Difficulty:")));
}

void TestUnitMarketPanels::rushKeyMapAnswersEveryPlyKind()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));

    // Walk to the trade_line rep, which exercises entry, size, bracket and
    // follow-up in one line.
    int tradeLine = -1;
    for (int index = 0; index < controller.puzzleCount(); ++index) {
        QVERIFY(controller.goToPuzzle(index, &error));
        if (controller.currentPuzzle()->taskKind == TaskKind::TradeLine) {
            tradeLine = index;
            break;
        }
    }
    QVERIFY(tradeLine >= 0);

    MarketRushPanel panel;
    panel.setController(&controller);
    QVERIFY(panel.keyHintText().contains(QStringLiteral("B long")));

    QVERIFY(panel.handleKey(Qt::Key_S));
    QCOMPARE(controller.answers().responses.first().choice, QStringLiteral("short"));
    QVERIFY(panel.keyHintText().contains(QStringLiteral("0.5R")));

    QVERIFY(panel.handleKey(Qt::Key_3));
    QCOMPARE(controller.answers().responses.at(1).choice, QStringLiteral("0.5R"));

    // The bracket is two keystrokes: stop, then target.
    QVERIFY(panel.handleKey(Qt::Key_3));
    QVERIFY(panel.keyHintText().contains(QStringLiteral("stop 1.5R chosen")));
    QVERIFY(panel.handleKey(Qt::Key_3));
    QVERIFY(controller.answers().responses.at(2).bracket.has_value());
    QCOMPARE(controller.answers().responses.at(2).bracket->stopAtr, 1.5);
    QCOMPARE(controller.answers().responses.at(2).bracket->targetAtr, 3.0);

    QVERIFY(panel.keyHintText().contains(QStringLiteral("H hold")));
    QVERIFY(panel.handleKey(Qt::Key_H));
    QCOMPARE(controller.answers().responses.at(3).choice, QStringLiteral("hold"));
    QVERIFY(panel.handleKey(Qt::Key_X));
    QCOMPARE(controller.answers().responses.at(4).choice, QStringLiteral("exit"));
    QVERIFY(panel.handleKey(Qt::Key_A));
    QCOMPARE(controller.answers().responses.at(5).choice, QStringLiteral("add"));
    QVERIFY(panel.handleKey(Qt::Key_T));
    QCOMPARE(controller.answers().responses.at(6).choice, QStringLiteral("tighten"));
    QVERIFY(panel.handleKey(Qt::Key_H));
    QVERIFY(controller.awaitingCalibration());
    QVERIFY(panel.calibrationEnabled());
    QVERIFY(panel.streakText().contains(QStringLiteral("streak")));
}

void TestUnitMarketPanels::rushRefusesAChoiceOutsideTheDeclaredEnumeration()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));

    // The first rep is anomaly_flag; a verdict ply has no 'B' key.
    QCOMPARE(controller.currentPuzzle()->taskKind, TaskKind::AnomalyFlag);
    MarketRushPanel panel;
    panel.setController(&controller);
    QVERIFY(panel.keyHintText().contains(QStringLiteral("P planted")));
    QVERIFY(!panel.handleKey(Qt::Key_B));
    QVERIFY(controller.answers().responses.isEmpty());

    QVERIFY(panel.handleKey(Qt::Key_P));
    QCOMPARE(controller.answers().responses.first().choice, QStringLiteral("planted"));
    // A digit beyond the declared artifact classes answers nothing.
    QVERIFY(!panel.handleKey(Qt::Key_9));
    QCOMPARE(controller.answers().responses.size(), 1);
    QVERIFY(panel.handleKey(Qt::Key_3));
    QCOMPARE(controller.answers().responses.at(1).choice, QStringLiteral("stale_print_repeat"));

    // Confidence is a decile midpoint, so no key claims certainty.
    QVERIFY(panel.handleKey(Qt::Key_7));
    QVERIFY(controller.answers().responses.at(2).confidence.has_value());
    QCOMPARE(*controller.answers().responses.at(2).confidence, 0.75);
}

void TestUnitMarketPanels::rushDeadlineExpiryClosesTheRepAsTimedOut()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.setPlyDeadlineMilliseconds(0);
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));

    MarketRushPanel panel;
    panel.setController(&controller);
    QTRY_VERIFY_WITH_TIMEOUT(controller.isTerminal(), 3000);
    // A closed rep has no deadline left to run.
    QCOMPARE(controller.remainingDeadlineMilliseconds(), -1);
    QVERIFY(panel.timerText().contains(QStringLiteral("rep closed")));

    QSqlQuery outcome(harness.database());
    QVERIFY(outcome.exec(QStringLiteral("SELECT outcome FROM market_solve_attempt_terminal_records")));
    QVERIFY(outcome.next());
    QCOMPARE(outcome.value(0).toString(), QStringLiteral("timed_out"));
}

void TestUnitMarketPanels::rushSpaceOpensTheRevealOnlyOnceTheRepIsTerminal()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));

    MarketRushPanel panel;
    panel.setController(&controller);
    QSignalSpy revealSpy(&panel, &MarketRushPanel::revealRequested);
    QSignalSpy nextSpy(&panel, &MarketRushPanel::nextRepRequested);

    // Mid-rep, space asks for nothing.
    QVERIFY(panel.handleKey(Qt::Key_Space));
    QCOMPARE(revealSpy.size(), 0);
    QCOMPARE(nextSpy.size(), 0);

    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));
    QVERIFY(controller.isTerminal());
    QVERIFY(panel.handleKey(Qt::Key_Space));
    QCOMPARE(revealSpy.size(), 1);
    QCOMPARE(nextSpy.size(), 0);

    QVERIFY2(controller.openReveal(&error), qPrintable(error));
    QVERIFY(panel.handleKey(Qt::Key_Space));
    QCOMPARE(nextSpy.size(), 1);
}

void TestUnitMarketPanels::studyBuildsOnePlanRowPerPly()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.setMode(MarketMode::Study);
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));

    MarketStudyPanel panel;
    panel.setController(&controller);
    // anomaly_flag: verdict, artifact class, confidence.
    QCOMPARE(panel.planEditorRowCount(), 3);
    QVERIFY(panel.findChild<QComboBox *>(QStringLiteral("marketStudyPly_0")) != nullptr);
    QVERIFY(panel.findChild<QDoubleSpinBox *>(QStringLiteral("marketStudyPly_2")) != nullptr);

    for (int index = 0; index < controller.puzzleCount(); ++index) {
        QVERIFY(controller.goToPuzzle(index, &error));
        if (controller.currentPuzzle()->taskKind == TaskKind::TradeLine) {
            break;
        }
    }
    panel.refresh();
    QCOMPARE(panel.planEditorRowCount(), 8);
    QVERIFY(panel.findChild<QComboBox *>(QStringLiteral("marketStudyPly_2_stop")) != nullptr);
    QVERIFY(panel.findChild<QComboBox *>(QStringLiteral("marketStudyPly_2_target")) != nullptr);
}

void TestUnitMarketPanels::studyCommitsTheWholeLineThenAsksForTheInterval()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.setMode(MarketMode::Study);
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));

    MarketStudyPanel panel;
    panel.setController(&controller);
    QVERIFY(panel.canCommitPlan());
    auto *commit = panel.findChild<QPushButton *>(QStringLiteral("marketStudyCommitPlan"));
    QVERIFY(commit != nullptr);
    commit->click();

    QCOMPARE(controller.answers().responses.size(), 3);
    QVERIFY(controller.awaitingCalibration());
    QVERIFY(!panel.canCommitPlan());

    auto *lower = panel.findChild<QDoubleSpinBox *>(QStringLiteral("marketStudyIntervalLower"));
    auto *upper = panel.findChild<QDoubleSpinBox *>(QStringLiteral("marketStudyIntervalUpper"));
    auto *submit = panel.findChild<QPushButton *>(QStringLiteral("marketStudySubmitInterval"));
    QVERIFY(lower != nullptr && upper != nullptr && submit != nullptr);
    QVERIFY(submit->isEnabled());
    lower->setValue(-4.0);
    upper->setValue(6.5);
    submit->click();
    QVERIFY(controller.isTerminal());
    QVERIFY(!submit->isEnabled());

    // Study mode has no deadline, and the rep is closed by answering, not by a
    // clock.
    QCOMPARE(controller.remainingDeadlineMilliseconds(), -1);
}

void TestUnitMarketPanels::studyRevealCarriesTheDisclaimerAndCitationChain()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.setMode(MarketMode::Study);
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));

    MarketStudyPanel panel;
    panel.setController(&controller);
    auto *reveal = panel.findChild<QPushButton *>(QStringLiteral("marketStudyReveal"));
    QVERIFY(reveal != nullptr);
    // The reveal control is dead until a terminal is committed.
    QVERIFY(!reveal->isEnabled());
    QVERIFY(panel.revealSummaryText().isEmpty());

    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));
    panel.refresh();
    QVERIFY(reveal->isEnabled());
    reveal->click();

    const QString summary = panel.revealSummaryText();
    QVERIFY(!summary.isEmpty());
    QVERIFY(summary.contains(scoringKeyDisclaimer()));
    QVERIFY(summary.contains(marketPackEvidenceGrade()));
    QVERIFY(summary.contains(QStringLiteral("2021-02-08T21:00:00Z")));
    QVERIFY(summary.contains(QStringLiteral("Citation chain")));
    QVERIFY(summary.contains(QStringLiteral("20260821T041500Z-attention_v1-42e15520")));
    QVERIFY(summary.contains(QStringLiteral("source window digest")));
    QVERIFY(summary.contains(QStringLiteral("Arc's regrade from the pack is authoritative")));
    QVERIFY(!reveal->isEnabled());
}

void TestUnitMarketPanels::studyStepsTheContinuationOneBarAtATime()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.setMode(MarketMode::Study);
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));

    MarketStudyPanel panel;
    panel.setController(&controller);
    auto *step = panel.findChild<QPushButton *>(QStringLiteral("marketStudyStepContinuation"));
    QVERIFY(step != nullptr);
    QVERIFY(!step->isEnabled());

    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));
    panel.refresh();
    panel.findChild<QPushButton *>(QStringLiteral("marketStudyReveal"))->click();
    QCOMPARE(controller.revealedContinuationBars(), 0);
    QVERIFY(step->isEnabled());

    for (int expected = 1; expected <= 8; ++expected) {
        step->click();
        QCOMPARE(controller.revealedContinuationBars(), expected);
    }
    // Eight bars in the continuation, and no ninth to step to.
    QVERIFY(!step->isEnabled());
}

void TestUnitMarketPanels::workspaceRefusesAPackWithoutItsSealedPartner()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketWorkspaceWindow window(&repository, opaqueSolverId(), opaqueSessionId());

    QTemporaryDir directory;
    const QString visiblePath = directory.filePath(QStringLiteral("orphan.visible.jsonl"));
    QFile visible(visiblePath);
    QVERIFY(visible.open(QIODevice::WriteOnly));
    visible.write(visibleMarketFixture());
    visible.close();

    QString error;
    QVERIFY(!window.loadPackFromFiles(visiblePath, &error));
    QVERIFY2(error.contains(QStringLiteral("sealed partner")), qPrintable(error));
    QCOMPARE(window.controller()->puzzleCount(), 0);

    // A path that is not the visible half at all is refused too.
    QVERIFY(!window.loadPackFromFiles(directory.filePath(QStringLiteral("pack.jsonl")), &error));
    QVERIFY2(error.contains(QStringLiteral("visible.jsonl")), qPrintable(error));
}

void TestUnitMarketPanels::workspaceLoadsBothHalvesAndPopulatesEverySurface()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketWorkspaceWindow window(&repository, opaqueSolverId(), opaqueSessionId());

    QTemporaryDir directory;
    const QString visiblePath = directory.filePath(QStringLiteral("pack.visible.jsonl"));
    const QString sealedPath = directory.filePath(QStringLiteral("pack.sealed.jsonl"));
    QFile visible(visiblePath);
    QVERIFY(visible.open(QIODevice::WriteOnly));
    visible.write(visibleMarketFixture());
    visible.close();
    QFile sealed(sealedPath);
    QVERIFY(sealed.open(QIODevice::WriteOnly));
    sealed.write(sealedMarketFixture());
    sealed.close();

    QString error;
    QVERIFY2(window.loadPackFromFiles(visiblePath, &error), qPrintable(error));
    QCOMPARE(window.controller()->puzzleCount(), 3);
    QCOMPARE(window.chart()->drawnVisibleBarCount(), 24);
    QCOMPARE(window.chart()->drawnContinuationBarCount(), 0);
    QVERIFY(!window.hud()->statText(QStringLiteral("atr_pct_20")).isEmpty());
    QVERIFY(window.statusText().contains(QStringLiteral("rush")));
    QCOMPARE(
        labelText(&window, QStringLiteral("marketWorkspaceEvidenceGrade")),
        marketPackEvidenceGrade());

    // Solve one rep and reveal it: only then does anything after T reach the
    // chart.
    QVERIFY2(
        completeRepWithFirstLegalAnswers(
            window.controller(), window.controller()->header().taskSpec, &error),
        qPrintable(error));
    QCOMPARE(window.chart()->drawnContinuationBarCount(), 0);
    window.onRevealRequested();
    QVERIFY(window.controller()->isRevealed());
    // Eight continuation bars, one of them halted: the halt stays a gap on the
    // chart rather than being drawn at a price it never had.
    QCOMPARE(window.controller()->reveal()->continuationBars.size(), 8);
    QCOMPARE(window.chart()->drawnContinuationBarCount(), 7);

    // The queue prefers a rep this solver has not seen.
    window.onNextRepRequested();
    QCOMPARE(window.controller()->priorExposureCount(), 0);
    QCOMPARE(window.chart()->drawnContinuationBarCount(), 0);
}

QTEST_MAIN(TestUnitMarketPanels)

#include "test_unit_market_panels.moc"
