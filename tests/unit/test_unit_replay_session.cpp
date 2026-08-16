#include <QtTest>

#include "annotated_replay_pack.h"
#include "replay_session.h"

using namespace parlawl::puzzle_runner;

namespace parlawl::test_support {
QByteArray syntheticAnnotatedReplayJson();
}

class ReplaySessionTest : public QObject
{
    Q_OBJECT

private slots:
    void navigatesImmutableMainline();
    void variationIsTemporaryAndExitRestoresPlayedMove();
    void refusesMissingVariationAndSeekDuringBranch();
    void failedVariationExitIsAtomic();
};

void ReplaySessionTest::navigatesImmutableMainline()
{
    QString error;
    const auto pack = AnnotatedReplayPack::fromJson(
        parlawl::test_support::syntheticAnnotatedReplayJson(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));

    ReplaySession session;
    session.load(*pack);
    QVERIFY(session.hasReplay());
    QCOMPARE(session.currentMainlinePly(), 0);
    QCOMPARE(session.currentPosition().toFen(), pack->mainlinePositions().at(0).toFen());
    QVERIFY(session.stepForward());
    QCOMPARE(session.currentMainlinePly(), 1);
    QVERIFY(session.seekMainlinePly(2));
    QCOMPARE(session.currentPosition().toFen(), pack->mainlinePositions().at(2).toFen());
    QVERIFY(session.stepBackward());
    QCOMPARE(session.currentMainlinePly(), 1);
    QVERIFY(!session.seekMainlinePly(3));
}

void ReplaySessionTest::variationIsTemporaryAndExitRestoresPlayedMove()
{
    QString error;
    const auto pack = AnnotatedReplayPack::fromJson(
        parlawl::test_support::syntheticAnnotatedReplayJson(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    const QVector<QString> immutableMainline {
        pack->mainlinePositions().at(0).toFen(),
        pack->mainlinePositions().at(1).toFen(),
        pack->mainlinePositions().at(2).toFen()};

    ReplaySession session;
    session.load(*pack);
    QVERIFY2(session.enterPreferredVariation(1, &error), qPrintable(error));
    QVERIFY(session.inVariation());
    QCOMPARE(session.currentMainlinePly(), 0);
    QCOMPARE(session.currentVariationPly(), 0);
    QVERIFY(session.stepForward());
    QCOMPARE(session.currentVariationPly(), 1);
    QCOMPARE(
        session.currentPosition().toFen(),
        pack->preferredVariation(1)->displayedSteps.at(0).afterFen);
    QVERIFY(session.stepForward());
    QCOMPARE(session.currentVariationPly(), 2);
    QVERIFY(!session.stepForward());
    QVERIFY(session.stepBackward());
    QCOMPARE(session.currentVariationPly(), 1);

    QVERIFY2(session.exitVariation(&error), qPrintable(error));
    QVERIFY(!session.inVariation());
    QCOMPARE(session.currentMainlinePly(), 1);
    QCOMPARE(session.currentPosition().toFen(), pack->mainlinePositions().at(1).toFen());
    QCOMPARE(pack->mainlinePositions().at(0).toFen(), immutableMainline.at(0));
    QCOMPARE(pack->mainlinePositions().at(1).toFen(), immutableMainline.at(1));
    QCOMPARE(pack->mainlinePositions().at(2).toFen(), immutableMainline.at(2));
}

void ReplaySessionTest::refusesMissingVariationAndSeekDuringBranch()
{
    QString error;
    const auto pack = AnnotatedReplayPack::fromJson(
        parlawl::test_support::syntheticAnnotatedReplayJson(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));

    ReplaySession session;
    session.load(*pack);
    QVERIFY(!session.enterPreferredVariation(2, &error));
    QVERIFY2(session.enterPreferredVariation(1, &error), qPrintable(error));
    QVERIFY(!session.seekMainlinePly(2));
    QVERIFY2(session.exitVariation(&error), qPrintable(error));
    QVERIFY(!session.exitVariation(&error));
}

void ReplaySessionTest::failedVariationExitIsAtomic()
{
    QString error;
    auto pack = AnnotatedReplayPack::fromJson(
        parlawl::test_support::syntheticAnnotatedReplayJson(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));

    auto &moves = const_cast<QVector<ReplayMove> &>(pack->moves());
    QVERIFY(moves.at(0).preferredVariation.has_value());
    moves[0].preferredVariation->restorePlayedUci = QStringLiteral("g1g3");

    ReplaySession session;
    session.load(*pack);
    QVERIFY2(session.enterPreferredVariation(1, &error), qPrintable(error));
    QVERIFY(session.stepForward());
    const int mainlinePly = session.currentMainlinePly();
    const int variationPly = session.currentVariationPly();
    const QString position = session.currentPosition().toFen();

    QVERIFY(!session.exitVariation(&error));
    QVERIFY(error.contains(QStringLiteral("restore")));
    QVERIFY(session.inVariation());
    QCOMPARE(session.currentMainlinePly(), mainlinePly);
    QCOMPARE(session.currentVariationPly(), variationPly);
    QCOMPARE(session.currentPosition().toFen(), position);
}

QTEST_MAIN(ReplaySessionTest)
#include "test_unit_replay_session.moc"
