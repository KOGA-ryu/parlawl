#include <QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFile>

#include "market_grader.h"
#include "market_puzzle_pack.h"
#include "market_test_support.h"

using namespace parlawl::market;
using namespace parlawl::market_test;

namespace {

//! A verifier that always says no, standing in for a journal that has been
//! rolled back, truncated or replaced under the running session.
class RefusingVerifier final : public TerminalEventVerifier
{
public:
    bool verifyTerminalEvent(const QString &, const QString &, const QString &, QString *errorMessage)
        const override
    {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("stub verifier refuses everything");
        }
        return false;
    }
};

} // namespace

class TestUnitMarketContinuationSegregation : public QObject
{
    Q_OBJECT

private slots:
    void noDesktopTranslationUnitCanNameTheContinuation();
    void theVisibleTypeHasNoContinuationMember();
    void anUnmintedTicketOpensNothing();
    void aTicketForAnotherPuzzleOpensNothing();
    void aVaultWithNoJournalOpensNothing();
    void aRefusingJournalOpensNothing();
    void revealIsUnreachableUntilTheTerminalIsCommitted();
    void revealDisclosesIdentityAndLabelsTheKey();
};

void TestUnitMarketContinuationSegregation::noDesktopTranslationUnitCanNameTheContinuation()
{
    // Layer one of D1, checked structurally: the desktop cannot include the
    // sealed headers, so it cannot bind a widget to a future it must not see.
    static const QStringList forbidden{
        QStringLiteral("market_continuation.h"),
        QStringLiteral("market_grader.h"),
        QStringLiteral("sealed_continuation_vault_p.h"),
    };
    const QString desktopRoot = QStringLiteral(PARLAWL_TEST_SOURCE_DIR "/apps/desktop");
    QVERIFY2(QDir(desktopRoot).exists(), qPrintable(desktopRoot));

    int scanned = 0;
    QDirIterator iterator(
        desktopRoot,
        {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        const QString source = QString::fromUtf8(file.readAll());
        ++scanned;
        for (const QString &header : forbidden) {
            const QString include = QStringLiteral("#include \"%1\"").arg(header);
            QVERIFY2(
                !source.contains(include),
                qPrintable(QStringLiteral("%1 includes %2, which would put the continuation in reach "
                                          "of a widget").arg(path, header)));
        }
    }
    QVERIFY2(scanned > 0, "the desktop source scan found no translation units");
}

void TestUnitMarketContinuationSegregation::theVisibleTypeHasNoContinuationMember()
{
    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(visibleMarketFixture(), sealedMarketFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));

    // Layer two: the sealed half lives only in the vault. Once it is taken, the
    // pack has nothing left to leak.
    auto vault = pack->takeVault();
    QCOMPARE(vault->size(), 3);
    QVERIFY(pack->takeVault() == nullptr);

    // And the visible record's own bytes never mention a ticker or a date.
    for (const MarketPuzzleVisible &puzzle : pack->puzzles()) {
        const QString line = QString::fromUtf8(puzzle.canonicalLine);
        QVERIFY(!line.contains(QStringLiteral("ticker")));
        QVERIFY(!line.contains(QStringLiteral("decision_time_utc")));
        QVERIFY(!line.contains(QStringLiteral("2021-")));
        QVERIFY(!line.contains(QStringLiteral("source_window_digest")));
        QVERIFY(!line.contains(QStringLiteral("outcome_theme")));
    }
}

void TestUnitMarketContinuationSegregation::anUnmintedTicketOpensNothing()
{
    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(visibleMarketFixture(), sealedMarketFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    auto vault = pack->takeVault();
    RefusingVerifier verifier;
    vault->setTerminalEventVerifier(&verifier);

    const RevealTicket inert;
    QVERIFY(!inert.isValid());
    const auto opened = vault->open(pack->puzzles().first(), {}, inert, &error);
    QVERIFY(!opened.has_value());
    QVERIFY2(error.contains(QStringLiteral("committed terminal event")), qPrintable(error));
}

void TestUnitMarketContinuationSegregation::aTicketForAnotherPuzzleOpensNothing()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());

    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));
    QVERIFY(controller.isTerminal());
    QVERIFY2(controller.openReveal(&error), qPrintable(error));

    // The ticket that opened puzzle 0 is real. Point it at puzzle 1 and it is
    // a forgery.
    auto pack = MarketPuzzlePack::fromJsonLines(visibleMarketFixture(), sealedMarketFixture(), &error);
    QVERIFY(pack.has_value());
    auto vault = pack->takeVault();
    vault->setTerminalEventVerifier(&repository);

    QSqlQuery instanceQuery(harness.database());
    QVERIFY(instanceQuery.exec(QStringLiteral(
        "SELECT attempt_instance_id FROM market_solve_attempt_instances LIMIT 1")));
    QVERIFY(instanceQuery.next());
    const RevealTicket ticket =
        repository.ticketForCommittedTerminal(instanceQuery.value(0).toString(), &error);
    QVERIFY2(ticket.isValid(), qPrintable(error));
    QCOMPARE(ticket.puzzleId(), pack->puzzles().at(0).puzzleId);

    const auto opened = vault->open(pack->puzzles().at(1), {}, ticket, &error);
    QVERIFY(!opened.has_value());
    QVERIFY2(error.contains(QStringLiteral("different puzzle")), qPrintable(error));
}

void TestUnitMarketContinuationSegregation::aVaultWithNoJournalOpensNothing()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());

    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));

    QSqlQuery instanceQuery(harness.database());
    QVERIFY(instanceQuery.exec(QStringLiteral(
        "SELECT attempt_instance_id FROM market_solve_attempt_instances LIMIT 1")));
    QVERIFY(instanceQuery.next());
    const RevealTicket ticket =
        repository.ticketForCommittedTerminal(instanceQuery.value(0).toString(), &error);
    QVERIFY(ticket.isValid());

    auto pack = MarketPuzzlePack::fromJsonLines(visibleMarketFixture(), sealedMarketFixture(), &error);
    QVERIFY(pack.has_value());
    auto vault = pack->takeVault();
    // No verifier configured at all: the vault has nothing to ask, so it says no.
    const auto opened = vault->open(pack->puzzles().at(0), {}, ticket, &error);
    QVERIFY(!opened.has_value());
    QVERIFY2(error.contains(QStringLiteral("no journal")), qPrintable(error));
}

void TestUnitMarketContinuationSegregation::aRefusingJournalOpensNothing()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());

    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));

    QSqlQuery instanceQuery(harness.database());
    QVERIFY(instanceQuery.exec(QStringLiteral(
        "SELECT attempt_instance_id FROM market_solve_attempt_instances LIMIT 1")));
    QVERIFY(instanceQuery.next());
    const RevealTicket ticket =
        repository.ticketForCommittedTerminal(instanceQuery.value(0).toString(), &error);
    QVERIFY(ticket.isValid());

    auto pack = MarketPuzzlePack::fromJsonLines(visibleMarketFixture(), sealedMarketFixture(), &error);
    QVERIFY(pack.has_value());
    auto vault = pack->takeVault();
    RefusingVerifier refusing;
    vault->setTerminalEventVerifier(&refusing);
    const auto opened = vault->open(pack->puzzles().at(0), {}, ticket, &error);
    QVERIFY(!opened.has_value());
    QVERIFY2(error.contains(QStringLiteral("journal verification")), qPrintable(error));
}

void TestUnitMarketContinuationSegregation::revealIsUnreachableUntilTheTerminalIsCommitted()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());

    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));

    // Before a single answer.
    QVERIFY(controller.reveal() == nullptr);
    QVERIFY(!controller.openReveal(&error));
    QVERIFY2(error.contains(QStringLiteral("committed terminal event")), qPrintable(error));

    // Mid-rep, with plies answered but no terminal.
    QVERIFY2(controller.answerCategorical(QStringLiteral("planted"), &error), qPrintable(error));
    QVERIFY(!controller.isTerminal());
    QVERIFY(controller.reveal() == nullptr);
    QVERIFY(!controller.openReveal(&error));
    QVERIFY(controller.revealedContinuationBars() == 0);

    // The journal has no exportable terminal yet, so no ticket exists either.
    QSqlQuery instanceQuery(harness.database());
    QVERIFY(instanceQuery.exec(QStringLiteral(
        "SELECT attempt_instance_id FROM market_solve_attempt_instances LIMIT 1")));
    QVERIFY(instanceQuery.next());
    const RevealTicket premature =
        repository.ticketForCommittedTerminal(instanceQuery.value(0).toString(), &error);
    QVERIFY(!premature.isValid());
    QVERIFY2(error.contains(QStringLiteral("no committed terminal")), qPrintable(error));
}

void TestUnitMarketContinuationSegregation::revealDisclosesIdentityAndLabelsTheKey()
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
    // Walk to the trade_line rep, which is the one with a rule line to label.
    int tradeLineIndex = -1;
    for (int index = 0; index < controller.puzzleCount(); ++index) {
        QVERIFY(controller.goToPuzzle(index, &error));
        if (controller.currentPuzzle()->taskKind == TaskKind::TradeLine) {
            tradeLineIndex = index;
            break;
        }
    }
    QVERIFY(tradeLineIndex >= 0);
    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));
    QVERIFY2(controller.openReveal(&error), qPrintable(error));

    const MarketReveal *reveal = controller.reveal();
    QVERIFY(reveal != nullptr);
    QVERIFY(!reveal->ticker.isEmpty());
    QCOMPARE(reveal->decisionTimeUtc, QStringLiteral("2021-02-08T21:00:00Z"));
    QCOMPARE(reveal->exchange, QStringLiteral("XNAS"));
    QCOMPARE(reveal->continuationBars.size(), 8);
    QCOMPARE(reveal->ruleId, QStringLiteral("u2.fade_v1"));
    QVERIFY(!reveal->keyLine.isEmpty());
    // Every surface that shows the key shows it beside this sentence.
    QCOMPARE(reveal->keyDisclaimer, scoringKeyDisclaimer());
    QVERIFY(reveal->keyDisclaimer.contains(QStringLiteral("not the right answer")));
    QVERIFY(!reveal->sourceWindowDigest.isEmpty());
    QVERIFY(!reveal->partitionPaths.isEmpty());
    QCOMPARE(reveal->scoreCard.scoringPolicyId, marketScoringPolicyId());
    QCOMPARE(reveal->scoreCard.lineScore.pliesTotal, 8);
    QVERIFY(reveal->scoreCard.lineScore.humanRMultiple.has_value());
    QCOMPARE(*reveal->scoreCard.lineScore.ruleRMultiple, 1.82);
    QCOMPARE(*reveal->scoreCard.lineScore.perfectRMultiple, 2.94);
    QVERIFY(reveal->scoreCard.calibration.answered);

    // Study mode steps the disclosed bars one at a time; rush mode does not.
    QCOMPARE(controller.revealedContinuationBars(), 0);
    QVERIFY(controller.stepContinuation());
    QCOMPARE(controller.revealedContinuationBars(), 1);
}

QTEST_MAIN(TestUnitMarketContinuationSegregation)

#include "test_unit_market_continuation_segregation.moc"
