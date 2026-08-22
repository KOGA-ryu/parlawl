// The integration seam: Arc's real compiled packs driven through ParlAWL's
// real market workspace, offscreen.
//
// Everything the unit tests exercise against the three-record golden fixture is
// re-exercised here against `attention_v1` (475 pattern-call reps) and
// `anomaly_v1` (199 anomaly-flag reps) as `python/dojo/` actually compiled them.
// The packs live outside this repository, so the pack directory arrives by
// environment variable and the whole case skips when it is absent — a checkout
// without the corpus still builds and still runs the suite green.
//
//   PARLAWL_REAL_PACK_DIR=/path/to/Arc/data/puzzle_packs ctest -R real_packs

#include <QtTest>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QStringList>

#include "market_attempt_repository.h"
#include "market_puzzle_pack.h"
#include "market_rush_panel.h"
#include "market_session_controller.h"
#include "market_study_panel.h"
#include "market_test_support.h"
#include "market_workspace_window.h"

using namespace parlawl::market;
using namespace parlawl::market_test;

namespace {

QString packRoot()
{
    return qEnvironmentVariable("PARLAWL_REAL_PACK_DIR");
}

QString visiblePath(const QString &pack)
{
    return packRoot() + QLatin1Char('/') + pack + QLatin1Char('/') + pack
        + QStringLiteral(".visible.jsonl");
}

QString sealedPath(const QString &pack)
{
    return packRoot() + QLatin1Char('/') + pack + QLatin1Char('/') + pack
        + QStringLiteral(".sealed.jsonl");
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

//! Flip one hex digit of the first record's content id. The id is recomputed
//! from the record's own canonical bytes, so a one-character edit must not
//! survive the import.
QByteArray withFlippedContentId(QByteArray bytes, const char *idPrefix)
{
    const int at = bytes.indexOf(idPrefix);
    if (at < 0) {
        return {};
    }
    const int digit = at + static_cast<int>(qstrlen(idPrefix));
    bytes[digit] = bytes.at(digit) == '0' ? '1' : '0';
    return bytes;
}

//! Drop the last sealed record, so the two halves no longer agree on how many
//! continuations exist.
QByteArray withLastLineTruncated(const QByteArray &bytes)
{
    QByteArray trimmed = bytes;
    while (trimmed.endsWith('\n')) {
        trimmed.chop(1);
    }
    const int lastBreak = trimmed.lastIndexOf('\n');
    if (lastBreak < 0) {
        return {};
    }
    return trimmed.left(lastBreak + 1);
}

//! The label/verdict/class the pack's declared rule actually keyed for a given
//! rep, learned the only way anything can learn it: solve the rep in a throwaway
//! journal and read the reveal a committed terminal produced.
struct DiscoveredKey
{
    bool ok = false;
    QString puzzleId;
    QStringList categoricalKeys; // by ply index, empty where the ply has no key
};

DiscoveredKey discoverKey(const QByteArray &visible, const QByteArray &sealed, int puzzleIndex)
{
    DiscoveredKey found;
    JournalHarness harness;
    if (!harness.isOpen()) {
        return found;
    }
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    if (!controller.loadPack(visible, sealed, &error)) {
        return found;
    }
    if (!controller.goToPuzzle(puzzleIndex, &error)) {
        return found;
    }
    const MarketTaskSpec spec = controller.header().taskSpec;
    if (!completeRepWithFirstLegalAnswers(&controller, spec, &error)) {
        return found;
    }
    if (!controller.openReveal(&error)) {
        return found;
    }
    const MarketReveal *reveal = controller.reveal();
    if (reveal == nullptr) {
        return found;
    }
    found.puzzleId = reveal->puzzleId;
    for (const PlyScore &score : reveal->scoreCard.plyScores) {
        while (found.categoricalKeys.size() <= score.plyIndex) {
            found.categoricalKeys.append(QString());
        }
        found.categoricalKeys[score.plyIndex] = score.keyText;
    }
    found.ok = true;
    return found;
}

int countRows(const QSqlDatabase &database, const QString &table)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM ") + table) || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

} // namespace

class TestIntegrationMarketRealPacks : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void bothRealPacksImportThroughTheLoader();
    void aFlippedVisibleContentIdIsRefused();
    void aFlippedSealedContentIdIsRefused();
    void aTruncatedSealedHalfIsRefused();
    void aSealedHalfFromTheOtherPackIsRefused();

    void threeAttentionRepsThroughTheRushWorkspace();
    void twoAnomalyRepsIncludingAStudyReveal();

    void theJournalHoldsCompletedAttemptsOnlyAndExportsThem();
    void theContinuationIsAbsentFromLiveAppStateAtDecisionTime();

private:
    void skipWithoutPacks();
};

void TestIntegrationMarketRealPacks::skipWithoutPacks()
{
    if (packRoot().isEmpty()) {
        QSKIP("PARLAWL_REAL_PACK_DIR is not set; the compiled Arc packs are not available here");
    }
}

void TestIntegrationMarketRealPacks::initTestCase()
{
    skipWithoutPacks();
    for (const QString &pack : {QStringLiteral("attention_v1"), QStringLiteral("anomaly_v1")}) {
        QVERIFY2(QFileInfo::exists(visiblePath(pack)), qPrintable(visiblePath(pack)));
        QVERIFY2(QFileInfo::exists(sealedPath(pack)), qPrintable(sealedPath(pack)));
    }
}

void TestIntegrationMarketRealPacks::bothRealPacksImportThroughTheLoader()
{
    skipWithoutPacks();

    struct Expected
    {
        QString pack;
        int records;
        TaskKind kind;
        int plies;
    };
    const QVector<Expected> expectations{
        {QStringLiteral("attention_v1"), 475, TaskKind::PatternCall, 2},
        {QStringLiteral("anomaly_v1"), 199, TaskKind::AnomalyFlag, 3},
    };

    for (const Expected &expected : expectations) {
        QString error;
        auto pack = MarketPuzzlePack::fromJsonLines(
            readAll(visiblePath(expected.pack)), readAll(sealedPath(expected.pack)), &error);
        QVERIFY2(pack.has_value(), qPrintable(expected.pack + QStringLiteral(": ") + error));

        const MarketPackHeader &header = pack->header();
        QCOMPARE(header.recordCount, expected.records);
        QCOMPARE(pack->puzzles().size(), expected.records);
        QCOMPARE(header.grain, QStringLiteral("1d"));
        QCOMPARE(header.visibleBarCount, 63);
        QCOMPARE(header.continuationBarCount, 20);
        QCOMPARE(header.responseHorizonBars, 5);
        QVERIFY(header.packId.startsWith(QStringLiteral("market-puzzle-pack-v1:")));
        QVERIFY(!header.evidenceGrade.isEmpty());

        // The charter sentence survives the trip from `dojo/__init__.py`.
        QCOMPARE(header.evidenceGrade, marketPackEvidenceGrade());

        for (const MarketPuzzleVisible &puzzle : pack->puzzles()) {
            QCOMPARE(puzzle.taskKind, expected.kind);
            QCOMPARE(puzzle.plies.size(), expected.plies);
            QCOMPARE(puzzle.window.bars.size(), 63);
            QVERIFY(!puzzle.hud.isEmpty());
            // Every retained line re-verifies standing alone, which is the check
            // a journal row will later have to pass.
            QString lineError;
            QVERIFY2(
                verifyRetainedMarketPuzzleLine(
                    puzzle.canonicalLine, puzzle.recordId, puzzle.puzzleId, &lineError),
                qPrintable(lineError));
        }

        auto vault = pack->takeVault();
        QVERIFY(vault != nullptr);
        QCOMPARE(vault->size(), expected.records);
        QCOMPARE(vault->continuationPackId(), header.continuationPackId);
    }
}

void TestIntegrationMarketRealPacks::aFlippedVisibleContentIdIsRefused()
{
    skipWithoutPacks();
    const QByteArray visible = readAll(visiblePath(QStringLiteral("attention_v1")));
    const QByteArray sealed = readAll(sealedPath(QStringLiteral("attention_v1")));

    const QByteArray corrupted = withFlippedContentId(visible, "market-puzzle-record-v1:");
    QVERIFY(!corrupted.isEmpty());
    QVERIFY(corrupted != visible);
    QCOMPARE(corrupted.size(), visible.size());

    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(corrupted, sealed, &error);
    QVERIFY2(!pack.has_value(), "a flipped visible record id imported anyway");
    QVERIFY2(!error.isEmpty(), "the refusal carried no message");
}

void TestIntegrationMarketRealPacks::aFlippedSealedContentIdIsRefused()
{
    skipWithoutPacks();
    const QByteArray visible = readAll(visiblePath(QStringLiteral("attention_v1")));
    const QByteArray sealed = readAll(sealedPath(QStringLiteral("attention_v1")));

    const QByteArray corrupted = withFlippedContentId(sealed, "market-puzzle-v1:");
    QVERIFY(!corrupted.isEmpty());
    QVERIFY(corrupted != sealed);

    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(visible, corrupted, &error);
    QVERIFY2(!pack.has_value(), "a flipped sealed puzzle id imported anyway");
    QVERIFY2(!error.isEmpty(), "the refusal carried no message");
}

void TestIntegrationMarketRealPacks::aTruncatedSealedHalfIsRefused()
{
    skipWithoutPacks();
    const QByteArray visible = readAll(visiblePath(QStringLiteral("anomaly_v1")));
    const QByteArray sealed = readAll(sealedPath(QStringLiteral("anomaly_v1")));

    const QByteArray truncated = withLastLineTruncated(sealed);
    QVERIFY(!truncated.isEmpty());
    QVERIFY(truncated.size() < sealed.size());

    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(visible, truncated, &error);
    QVERIFY2(!pack.has_value(), "a pack short one continuation imported anyway");
    QVERIFY2(!error.isEmpty(), "the refusal carried no message");
}

void TestIntegrationMarketRealPacks::aSealedHalfFromTheOtherPackIsRefused()
{
    skipWithoutPacks();
    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(
        readAll(visiblePath(QStringLiteral("attention_v1"))),
        readAll(sealedPath(QStringLiteral("anomaly_v1"))),
        &error);
    QVERIFY2(!pack.has_value(), "two halves from different packs imported as a pair");
    QVERIFY2(!error.isEmpty(), "the refusal carried no message");
}

void TestIntegrationMarketRealPacks::threeAttentionRepsThroughTheRushWorkspace()
{
    skipWithoutPacks();
    const QByteArray visible = readAll(visiblePath(QStringLiteral("attention_v1")));
    const QByteArray sealed = readAll(sealedPath(QStringLiteral("attention_v1")));

    // Learn what the declared rule keyed for reps 0 and 1, in throwaway journals
    // that never touch the journal under audit.
    const DiscoveredKey keyForZero = discoverKey(visible, sealed, 0);
    const DiscoveredKey keyForOne = discoverKey(visible, sealed, 1);
    QVERIFY2(keyForZero.ok, "could not learn the key for attention rep 0");
    QVERIFY2(keyForOne.ok, "could not learn the key for attention rep 1");

    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketWorkspaceWindow window(&repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(window.loadPackFromFiles(visiblePath(QStringLiteral("attention_v1")), &error),
             qPrintable(error));
    QCOMPARE(window.controller()->puzzleCount(), 475);

    window.setStudyMode(false);
    MarketRushPanel *rush = window.rushPanel();
    MarketSessionController *controller = window.controller();
    const QStringList labels = controller->header().taskSpec.patternLabels;
    QVERIFY(labels.size() >= 2);

    const auto digitForLabel = [&labels](const QString &label) {
        return Qt::Key_1 + labels.indexOf(label);
    };
    const auto answerInterval = [&](QWidget *panel, double lower, double upper) {
        auto *low = panel->findChild<QDoubleSpinBox *>(QStringLiteral("marketRushIntervalLower"));
        auto *high = panel->findChild<QDoubleSpinBox *>(QStringLiteral("marketRushIntervalUpper"));
        auto *submit = panel->findChild<QPushButton *>(QStringLiteral("marketRushSubmitInterval"));
        QVERIFY(low != nullptr && high != nullptr && submit != nullptr);
        QVERIFY2(submit->isEnabled(), "the interval prompt never opened");
        low->setValue(lower);
        high->setValue(upper);
        submit->click();
    };

    // Rep 0 — answered with the rule's own key.
    QVERIFY2(controller->goToPuzzle(0, &error), qPrintable(error));
    QCOMPARE(controller->currentPuzzle()->puzzleId, keyForZero.puzzleId);
    const QString key0 = keyForZero.categoricalKeys.value(0);
    QVERIFY(labels.contains(key0));
    QVERIFY(rush->handleKey(digitForLabel(key0)));
    QVERIFY(rush->handleKey(Qt::Key_7)); // confidence 0.75
    answerInterval(rush, -4.0, 6.5);
    QVERIFY(controller->isTerminal());
    QVERIFY(!controller->clockInvalidated());
    QVERIFY2(controller->openReveal(&error), qPrintable(error));
    QCOMPARE(controller->reveal()->scoreCard.plyScores.at(0).match, PlyMatch::Exact);
    QCOMPARE(controller->streak(), 1);

    // Rep 1 — answered with a label the rule did not key.
    QVERIFY2(controller->goToPuzzle(1, &error), qPrintable(error));
    const QString key1 = keyForOne.categoricalKeys.value(0);
    QString wrong;
    for (const QString &candidate : labels) {
        if (candidate != key1) {
            wrong = candidate;
            break;
        }
    }
    QVERIFY(!wrong.isEmpty());
    QVERIFY(rush->handleKey(digitForLabel(wrong)));
    QVERIFY(rush->handleKey(Qt::Key_3));
    answerInterval(rush, -10.0, 2.0);
    QVERIFY(controller->isTerminal());
    QVERIFY2(controller->openReveal(&error), qPrintable(error));
    QCOMPARE(controller->reveal()->scoreCard.plyScores.at(0).match, PlyMatch::Miss);
    QCOMPARE(controller->streak(), 0);

    // Rep 2 — the operator froze on the open question.
    QVERIFY2(controller->goToPuzzle(2, &error), qPrintable(error));
    controller->setPlyDeadlineMilliseconds(1);
    QVERIFY(controller->currentPly() != nullptr);
    QVERIFY2(controller->expireCurrentPly(&error), qPrintable(error));
    QVERIFY(controller->isTerminal());
    QCOMPARE(controller->streak(), 0);
    QVERIFY(!controller->answers().calibration.answered);
    QVERIFY2(controller->openReveal(&error), qPrintable(error));
    QCOMPARE(controller->reveal()->scoreCard.plyScores.at(0).match, PlyMatch::Unanswered);

    // Three reps in, three terminal records, and nothing extra.
    QCOMPARE(countRows(harness.database(), QStringLiteral("market_solve_attempt_instances")), 3);
    QCOMPARE(
        countRows(harness.database(), QStringLiteral("market_solve_attempt_terminal_records")), 3);
}

void TestIntegrationMarketRealPacks::twoAnomalyRepsIncludingAStudyReveal()
{
    skipWithoutPacks();
    const QString visible = visiblePath(QStringLiteral("anomaly_v1"));

    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketWorkspaceWindow window(&repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(window.loadPackFromFiles(visible, &error), qPrintable(error));
    QCOMPARE(window.controller()->puzzleCount(), 199);
    MarketSessionController *controller = window.controller();
    const QStringList classes = controller->header().taskSpec.artifactClasses;
    QCOMPARE(classes.size(), 6);

    // Rep A — rush, verdict + artifact class + confidence + interval.
    window.setStudyMode(false);
    MarketRushPanel *rush = window.rushPanel();
    QVERIFY2(controller->goToPuzzle(0, &error), qPrintable(error));
    QCOMPARE(controller->currentPly()->kind, PlyKind::Verdict);
    QVERIFY(rush->handleKey(Qt::Key_P));
    QCOMPARE(controller->currentPly()->kind, PlyKind::ArtifactClass);
    QVERIFY(rush->handleKey(Qt::Key_1));
    QCOMPARE(controller->currentPly()->kind, PlyKind::Confidence);
    QVERIFY(rush->handleKey(Qt::Key_6));
    QVERIFY(controller->awaitingCalibration());
    auto *rushSubmit = rush->findChild<QPushButton *>(QStringLiteral("marketRushSubmitInterval"));
    QVERIFY(rushSubmit != nullptr && rushSubmit->isEnabled());
    rush->findChild<QDoubleSpinBox *>(QStringLiteral("marketRushIntervalLower"))->setValue(-8.0);
    rush->findChild<QDoubleSpinBox *>(QStringLiteral("marketRushIntervalUpper"))->setValue(9.0);
    rushSubmit->click();
    QVERIFY(controller->isTerminal());

    // Rep B — study: the whole line first, then the reveal stepped bar by bar.
    window.setStudyMode(true);
    MarketStudyPanel *study = window.studyPanel();
    QVERIFY2(controller->goToPuzzle(1, &error), qPrintable(error));
    study->refresh();
    QCOMPARE(study->planEditorRowCount(), 3);

    auto *verdict = study->findChild<QComboBox *>(QStringLiteral("marketStudyPly_0"));
    auto *artifact = study->findChild<QComboBox *>(QStringLiteral("marketStudyPly_1"));
    auto *confidence = study->findChild<QDoubleSpinBox *>(QStringLiteral("marketStudyPly_2"));
    QVERIFY(verdict != nullptr && artifact != nullptr && confidence != nullptr);
    verdict->setCurrentText(QStringLiteral("clean"));
    artifact->setCurrentText(classes.at(2));
    confidence->setValue(0.55);

    auto *commit = study->findChild<QPushButton *>(QStringLiteral("marketStudyCommitPlan"));
    QVERIFY(commit != nullptr && commit->isEnabled());
    commit->click();
    QVERIFY2(controller->awaitingCalibration(), "the interval prompt did not open after the plan");

    auto *low = study->findChild<QDoubleSpinBox *>(QStringLiteral("marketStudyIntervalLower"));
    auto *high = study->findChild<QDoubleSpinBox *>(QStringLiteral("marketStudyIntervalUpper"));
    auto *submit = study->findChild<QPushButton *>(QStringLiteral("marketStudySubmitInterval"));
    QVERIFY(low != nullptr && high != nullptr && submit != nullptr);
    QCOMPARE(low->minimum(), -95.0);
    QCOMPARE(high->maximum(), 400.0);
    low->setValue(-12.5);
    high->setValue(18.0);
    submit->click();
    QVERIFY(controller->isTerminal());
    QVERIFY(controller->answers().calibration.answered);
    QCOMPARE(controller->answers().calibration.lower, -12.5);
    QCOMPARE(controller->answers().calibration.upper, 18.0);

    // Nothing after T is drawn until the reveal is opened.
    QCOMPARE(controller->revealedContinuationBars(), 0);
    auto *revealButton = study->findChild<QPushButton *>(QStringLiteral("marketStudyReveal"));
    QVERIFY(revealButton != nullptr && revealButton->isEnabled());
    revealButton->click();
    QVERIFY(controller->isRevealed());
    QCOMPARE(controller->revealedContinuationBars(), 0);

    auto *step = study->findChild<QPushButton *>(QStringLiteral("marketStudyStepContinuation"));
    QVERIFY(step != nullptr);
    const int continuationBars = controller->reveal()->continuationBars.size();
    QCOMPARE(continuationBars, 20);
    for (int bar = 1; bar <= continuationBars; ++bar) {
        QVERIFY2(step->isEnabled(), qPrintable(QStringLiteral("step disabled at bar %1").arg(bar)));
        step->click();
        QCOMPARE(controller->revealedContinuationBars(), bar);
    }
    // The last bar is the last bar. There is no twenty-first.
    QVERIFY(!controller->stepContinuation());
    QCOMPARE(controller->revealedContinuationBars(), continuationBars);

    // The reveal carries the identity and the sentence that says what the key is not.
    const MarketReveal *reveal = controller->reveal();
    QVERIFY(!reveal->ticker.isEmpty());
    QVERIFY(!reveal->decisionTimeUtc.isEmpty());
    QCOMPARE(reveal->keyDisclaimer, scoringKeyDisclaimer());
    QVERIFY(study->revealSummaryText().contains(scoringKeyDisclaimer()));

    QCOMPARE(
        countRows(harness.database(), QStringLiteral("market_solve_attempt_terminal_records")), 2);
}

void TestIntegrationMarketRealPacks::theJournalHoldsCompletedAttemptsOnlyAndExportsThem()
{
    skipWithoutPacks();
    const QByteArray visible = readAll(visiblePath(QStringLiteral("anomaly_v1")));
    const QByteArray sealed = readAll(sealedPath(QStringLiteral("anomaly_v1")));

    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    const QString solverId = opaqueSolverId();
    const QString sessionId = opaqueSessionId();
    controller.configureJournal(&repository, &repository, &repository, solverId, sessionId);
    QString error;
    QVERIFY2(controller.loadPack(visible, sealed, &error), qPrintable(error));
    const MarketTaskSpec spec = controller.header().taskSpec;

    QStringList completedPuzzleIds;

    // Two completed reps.
    for (int index : {3, 4}) {
        QVERIFY2(controller.goToPuzzle(index, &error), qPrintable(error));
        completedPuzzleIds.append(controller.currentPuzzle()->puzzleId);
        QVERIFY2(completeRepWithFirstLegalAnswers(&controller, spec, &error), qPrintable(error));
        QVERIFY(controller.isTerminal());
    }

    // One timed-out rep: also terminal, and the contract says it exports.
    QVERIFY2(controller.goToPuzzle(5, &error), qPrintable(error));
    completedPuzzleIds.append(controller.currentPuzzle()->puzzleId);
    QVERIFY2(controller.expireCurrentPly(&error), qPrintable(error));

    // One abandoned rep: answered part-way and walked away from.
    QVERIFY2(controller.goToPuzzle(6, &error), qPrintable(error));
    const QString abandonedPuzzleId = controller.currentPuzzle()->puzzleId;
    QVERIFY2(controller.answerCategorical(QStringLiteral("planted"), &error), qPrintable(error));
    QVERIFY2(controller.abandonAttempt(QStringLiteral("navigation"), &error), qPrintable(error));

    // The runtime flag says "this attempt is over", which is not the same claim
    // as "an exportable terminal was committed" — so the reveal must still
    // refuse. Layer three of D1 is what makes the difference, not the flag.
    QVERIFY(controller.isTerminal());
    QVERIFY2(!controller.openReveal(&error), "an abandoned rep opened the reveal");
    QVERIFY(!controller.isRevealed());
    QVERIFY(controller.reveal() == nullptr);

    QCOMPARE(countRows(harness.database(), QStringLiteral("market_solve_attempt_instances")), 4);
    QCOMPARE(
        countRows(harness.database(), QStringLiteral("market_solve_attempt_terminal_records")), 3);

    // Every attempt's event chain verifies, terminal or not.
    QSqlQuery instances(harness.database());
    QVERIFY(instances.exec(QStringLiteral("SELECT attempt_instance_id FROM market_solve_attempt_instances")));
    int verified = 0;
    while (instances.next()) {
        QString chainError;
        QVERIFY2(
            repository.verifyAttempt(instances.value(0).toString(), &chainError),
            qPrintable(chainError));
        ++verified;
    }
    QCOMPARE(verified, 4);

    // Every event is hash-linked. The chain is anchored at the attempt's
    // genesis hash rather than at an empty string, so event 0 has a real
    // predecessor: the instance's own identity.
    QHash<QString, QString> genesisByAttempt;
    QSqlQuery genesis(harness.database());
    QVERIFY(genesis.exec(QStringLiteral(
        "SELECT attempt_instance_id, genesis_hash FROM market_solve_attempt_instances")));
    while (genesis.next()) {
        genesisByAttempt.insert(genesis.value(0).toString(), genesis.value(1).toString());
    }
    QCOMPARE(genesisByAttempt.size(), 4);

    QSqlQuery events(harness.database());
    QVERIFY(events.exec(QStringLiteral(
        "SELECT attempt_instance_id, event_index, event_hash, previous_hash "
        "FROM market_solve_attempt_events ORDER BY attempt_instance_id, event_index")));
    QHash<QString, QString> lastHashByAttempt;
    int eventCount = 0;
    while (events.next()) {
        const QString attempt = events.value(0).toString();
        const int index = events.value(1).toInt();
        const QString hash = events.value(2).toString();
        const QString previous = events.value(3).toString();
        QVERIFY2(
            QRegularExpression(QStringLiteral("^[a-z0-9-]+:[0-9a-f]{64}$")).match(hash).hasMatch(),
            qPrintable(hash));
        if (index == 0) {
            QCOMPARE(previous, genesisByAttempt.value(attempt));
            QVERIFY(!previous.isEmpty());
        } else {
            QCOMPARE(previous, lastHashByAttempt.value(attempt));
        }
        lastHashByAttempt.insert(attempt, hash);
        ++eventCount;
    }
    QVERIFY(eventCount > 0);

    // Export, then read the export back as bytes and check the contract's shape.
    const QString path = harness.path(QStringLiteral("market_results.jsonl"));
    int exported = 0;
    QVERIFY2(repository.exportMarketSolveResults(path, &exported, &error), qPrintable(error));
    QCOMPARE(exported, 3);

    const QFileInfo info(path);
    QVERIFY(info.exists() && info.isFile() && !info.isSymLink());
    QCOMPARE(
        QFile::permissions(path)
            & (QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::WriteGroup
               | QFile::ReadOther | QFile::WriteOther),
        QFile::ReadOwner | QFile::WriteOwner);

    const QByteArray exportBytes = readAll(path);
    QVERIFY(!exportBytes.isEmpty());
    QVERIFY(!exportBytes.contains('\0'));
    QVERIFY(!exportBytes.startsWith("\xEF\xBB\xBF"));
    QVERIFY(exportBytes.endsWith('\n'));

    QList<QByteArray> lines = exportBytes.split('\n');
    lines.removeAll(QByteArray());
    QCOMPARE(lines.size(), 4); // header plus three results

    const QJsonObject header = QJsonDocument::fromJson(lines.at(0)).object();
    QCOMPARE(header.value(QStringLiteral("schema")).toString(),
             QStringLiteral("arc/market-solve-results/v1"));
    QCOMPARE(header.value(QStringLiteral("record_type")).toString(),
             QStringLiteral("market_solve_results_header"));
    QCOMPARE(header.value(QStringLiteral("counts")).toObject().value(QStringLiteral("records")).toInt(), 3);
    const QJsonArray packs = header.value(QStringLiteral("packs_referenced")).toArray();
    QCOMPARE(packs.size(), 1);
    QCOMPARE(packs.at(0).toString(), controller.header().packId);

    QStringList exportedPuzzleIds;
    QStringList resultIds;
    for (int i = 1; i < lines.size(); ++i) {
        // Every line must be exactly its own canonical re-spelling.
        const QJsonObject record = QJsonDocument::fromJson(lines.at(i)).object();
        QVERIFY(!record.isEmpty());
        QCOMPARE(record.value(QStringLiteral("schema")).toString(),
                 QStringLiteral("arc/market-solve-result/v1"));
        QCOMPARE(record.value(QStringLiteral("pack_id")).toString(), controller.header().packId);
        QCOMPARE(record.value(QStringLiteral("solver_id")).toString(), solverId);
        QVERIFY(record.value(QStringLiteral("result_id")).toString().startsWith(
            QStringLiteral("market-solve-result-v1:")));
        exportedPuzzleIds.append(record.value(QStringLiteral("puzzle_id")).toString());
        resultIds.append(record.value(QStringLiteral("result_id")).toString());
    }

    // The abandoned rep is nowhere in it; the three terminal ones all are.
    QVERIFY2(!exportedPuzzleIds.contains(abandonedPuzzleId), "an abandoned rep was exported");
    for (const QString &puzzleId : completedPuzzleIds) {
        QVERIFY2(exportedPuzzleIds.contains(puzzleId), qPrintable(puzzleId));
    }

    // Sorted by `result_id`, per §2.
    QStringList sorted = resultIds;
    sorted.sort();
    QCOMPARE(resultIds, sorted);

    // And the export is never overwritten.
    QVERIFY(!repository.exportMarketSolveResults(path, nullptr, &error));
}

void TestIntegrationMarketRealPacks::theContinuationIsAbsentFromLiveAppStateAtDecisionTime()
{
    skipWithoutPacks();
    const QString visible = visiblePath(QStringLiteral("attention_v1"));

    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketWorkspaceWindow window(&repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(window.loadPackFromFiles(visible, &error), qPrintable(error));
    MarketSessionController *controller = window.controller();
    QVERIFY2(controller->goToPuzzle(9, &error), qPrintable(error));

    // Decision time: a question is open and nothing is terminal.
    QVERIFY(controller->currentPly() != nullptr);
    QVERIFY(!controller->isTerminal());
    QVERIFY(!controller->isRevealed());
    QVERIFY(controller->reveal() == nullptr);
    QCOMPARE(controller->revealedContinuationBars(), 0);

    const MarketPuzzleVisible *puzzle = controller->currentPuzzle();
    QVERIFY(puzzle != nullptr);

    // The object the panels hold carries the visible window and nothing else.
    QCOMPARE(puzzle->window.bars.size(), 63);
    QCOMPARE(puzzle->window.bars.size(), controller->header().visibleBarCount);

    // Its retained bytes are the whole of what it holds, and they mention no
    // future, no identity and no digest of either.
    const QString line = QString::fromUtf8(puzzle->canonicalLine);
    for (const QString &forbidden : {QStringLiteral("continuation_bars"),
                                     QStringLiteral("scoring_key"),
                                     QStringLiteral("ticker"),
                                     QStringLiteral("decision_time_utc"),
                                     QStringLiteral("outcome_theme"),
                                     QStringLiteral("source_window_digest"),
                                     QStringLiteral("source_continuation_digest"),
                                     QStringLiteral("realized")}) {
        QVERIFY2(!line.contains(forbidden), qPrintable(forbidden));
    }
    // Only the commitment to a future it cannot read.
    QVERIFY(line.contains(QStringLiteral("continuation_commitment")));
    const QString commitmentPrefix = QStringLiteral("market-continuation-v1:");
    QVERIFY(puzzle->continuationCommitment.startsWith(commitmentPrefix));
    QCOMPARE(puzzle->continuationCommitment.size(), commitmentPrefix.size() + 64);

    // Byte-level: no bar the pack sealed for this puzzle is anywhere in the
    // whole visible half. Take the first continuation close from the sealed
    // file for this puzzle id and look for its spelling in the visible bytes.
    const QByteArray sealedBytes = readAll(sealedPath(QStringLiteral("attention_v1")));
    const QByteArray visibleBytes = readAll(visible);
    QByteArray sealedLineForPuzzle;
    for (const QByteArray &candidate : sealedBytes.split('\n')) {
        if (!candidate.isEmpty() && candidate.contains(puzzle->puzzleId.toUtf8())) {
            sealedLineForPuzzle = candidate;
            break;
        }
    }
    QVERIFY2(!sealedLineForPuzzle.isEmpty(), "the sealed half has no record for this puzzle");
    QVERIFY(!visibleBytes.contains(sealedLineForPuzzle));

    const QJsonObject sealedRecord = QJsonDocument::fromJson(sealedLineForPuzzle).object();
    const QString ticker = sealedRecord.value(QStringLiteral("content"))
                               .toObject()
                               .value(QStringLiteral("reveal_identity"))
                               .toObject()
                               .value(QStringLiteral("ticker"))
                               .toString();
    QVERIFY2(!ticker.isEmpty(), "the sealed record disclosed no ticker to look for");
    QVERIFY2(!visibleBytes.contains(ticker.toUtf8()), "the visible half spells a sealed ticker");

    // Answering opens the reveal, and only then does the future exist in the app.
    const QStringList labels = controller->header().taskSpec.patternLabels;
    QVERIFY2(completeRepWithFirstLegalAnswers(controller, controller->header().taskSpec, &error),
             qPrintable(error));
    QVERIFY(!controller->isRevealed());
    QVERIFY2(controller->openReveal(&error), qPrintable(error));
    QCOMPARE(controller->reveal()->continuationBars.size(), 20);
    QCOMPARE(controller->reveal()->ticker, ticker);
    Q_UNUSED(labels);
}

QTEST_MAIN(TestIntegrationMarketRealPacks)

#include "test_integration_market_real_packs.moc"
