#include <QtTest>

#include <cmath>

#include "market_hud.h"
#include "market_rating.h"
#include "market_scoring.h"

using namespace parlawl::market;

namespace {

MarketBar bar(int index, double open, double high, double low, double close)
{
    MarketBar value;
    value.barIndex = index;
    value.open = open;
    value.high = high;
    value.low = low;
    value.close = close;
    value.volume = 1.0;
    value.tradeCount = 1.0;
    return value;
}

//! A flat-ish 25-bar window with a known geometry, so every derivation below
//! can be hand-worked rather than compared against itself.
QVector<MarketBar> flatWindow(int count, double close)
{
    QVector<MarketBar> bars;
    for (int index = 0; index < count; ++index) {
        bars.append(bar(index, close, close + 1.0, close - 1.0, close));
    }
    return bars;
}

} // namespace

class TestUnitMarketScoring : public QObject
{
    Q_OBJECT

private slots:
    void brierScoresAConfidentHitAndMiss();
    void winklerScoresWidthPlusMissPenalty();
    void coverageIsInclusiveAtTheBounds();
    void categoricalMatchDistinguishesMissFromUnanswered();
    void bracketMatchIsExactOnTheDeclaredGrid();
    void shortfallIsAbsentWhenEitherSideIsAbsent();
    void passIsScoredAsARealAnswerAtZeroR();
    void longTargetFillPaysTheDeclaredTargetMultiple();
    void stopIsCheckedBeforeTargetWithinOneBar();
    void followUpExitClosesAtThatBarsClose();
    void haltedBarCannotFillAnything();
    void sizeBandScalesTheRealizedMultiple();
    void wilderAtrNeedsTwentyOneBarsAndIsScaleFree();
    void returnAndRangePositionAreHandWorked();
    void nullCloseInsideTheLookbackRefusesToDerive();
    void ratingMovesTowardTheSeedAndDampensWithReps();
    void repScoreIgnoresConfidencePlies();
};

void TestUnitMarketScoring::brierScoresAConfidentHitAndMiss()
{
    QCOMPARE(brierScore(1.0, true), 0.0);
    QCOMPARE(brierScore(0.0, true), 1.0);
    QCOMPARE(brierScore(0.5, true), 0.25);
    QCOMPARE(brierScore(0.5, false), 0.25);
    QVERIFY(std::abs(brierScore(0.8, true) - 0.04) < 1e-12);
}

void TestUnitMarketScoring::winklerScoresWidthPlusMissPenalty()
{
    // An 80% interval: alpha = 0.2, so the miss penalty is 2/0.2 = 10 per unit.
    QCOMPARE(winklerIntervalScore(-4.0, 6.5, 3.8421, 0.8), 10.5);
    QVERIFY(std::abs(winklerIntervalScore(-4.0, 6.5, 8.5, 0.8) - (10.5 + 10.0 * 2.0)) < 1e-9);
    QVERIFY(std::abs(winklerIntervalScore(-4.0, 6.5, -5.0, 0.8) - (10.5 + 10.0 * 1.0)) < 1e-9);
    // A narrower band that still covers beats a wide one, which is the whole
    // point of scoring width alongside coverage.
    QVERIFY(winklerIntervalScore(3.0, 4.0, 3.8421, 0.8) < winklerIntervalScore(-4.0, 6.5, 3.8421, 0.8));
}

void TestUnitMarketScoring::coverageIsInclusiveAtTheBounds()
{
    QVERIFY(intervalCovers(-1.0, 1.0, -1.0));
    QVERIFY(intervalCovers(-1.0, 1.0, 1.0));
    QVERIFY(!intervalCovers(-1.0, 1.0, 1.0001));
}

void TestUnitMarketScoring::categoricalMatchDistinguishesMissFromUnanswered()
{
    QCOMPARE(matchCategorical(QStringLiteral("short"), QStringLiteral("short"), true), PlyMatch::Exact);
    QCOMPARE(matchCategorical(QStringLiteral("long"), QStringLiteral("short"), true), PlyMatch::Miss);
    QCOMPARE(matchCategorical(QString(), QStringLiteral("short"), false), PlyMatch::Unanswered);
    QCOMPARE(scoreForMatch(PlyMatch::Exact), 1.0);
    QCOMPARE(scoreForMatch(PlyMatch::Miss), 0.0);
    QCOMPARE(scoreForMatch(PlyMatch::Unanswered), 0.0);
    QCOMPARE(plyMatchText(PlyMatch::Unanswered), QStringLiteral("unanswered"));
}

void TestUnitMarketScoring::bracketMatchIsExactOnTheDeclaredGrid()
{
    const BracketChoice key{1.5, 3.0};
    QCOMPARE(matchBracket(BracketChoice{1.5, 3.0}, key, true), PlyMatch::Exact);
    QCOMPARE(matchBracket(BracketChoice{1.5, 4.0}, key, true), PlyMatch::Miss);
    QCOMPARE(matchBracket(std::nullopt, key, true), PlyMatch::Unanswered);
}

void TestUnitMarketScoring::shortfallIsAbsentWhenEitherSideIsAbsent()
{
    QCOMPARE(*shortfall(1.82, 1.11), 0.71);
    QVERIFY(!shortfall(std::nullopt, 1.11).has_value());
    QVERIFY(!shortfall(1.82, std::nullopt).has_value());
}

void TestUnitMarketScoring::passIsScoredAsARealAnswerAtZeroR()
{
    TradeLinePlan plan;
    plan.entry = QStringLiteral("pass");
    plan.sizeBand = QStringLiteral("0");
    const auto result = simulateTradeLine(flatWindow(5, 100.0), 2.0, 100.0, plan, 5);
    QCOMPARE(*result.rMultiple, 0.0);
    QCOMPARE(result.exitReason, QStringLiteral("no_position"));
}

void TestUnitMarketScoring::longTargetFillPaysTheDeclaredTargetMultiple()
{
    QVector<MarketBar> bars;
    bars.append(bar(0, 100.0, 101.0, 99.5, 100.5));
    bars.append(bar(1, 100.5, 106.5, 100.0, 106.0));
    TradeLinePlan plan;
    plan.entry = QStringLiteral("long");
    plan.sizeBand = QStringLiteral("1R");
    plan.bracket = {1.0, 3.0};
    plan.followUps = {QStringLiteral("hold"), QStringLiteral("hold")};
    // 1 ATR = 2.0, stop at 98.0, target at 106.0. The target fills on bar 2.
    const auto result = simulateTradeLine(bars, 2.0, 100.0, plan, 5);
    QCOMPARE(result.exitReason, QStringLiteral("target"));
    QCOMPARE(result.exitBarOffset, 2);
    QVERIFY(std::abs(*result.rMultiple - 3.0) < 1e-9);
}

void TestUnitMarketScoring::stopIsCheckedBeforeTargetWithinOneBar()
{
    QVector<MarketBar> bars;
    // A bar that touches both. The intra-bar path is unknown, so the pessimistic
    // reading is the only honest one.
    bars.append(bar(0, 100.0, 106.5, 97.5, 100.0));
    TradeLinePlan plan;
    plan.entry = QStringLiteral("long");
    plan.sizeBand = QStringLiteral("1R");
    plan.bracket = {1.0, 3.0};
    plan.followUps = {QStringLiteral("hold")};
    const auto result = simulateTradeLine(bars, 2.0, 100.0, plan, 5);
    QCOMPARE(result.exitReason, QStringLiteral("stop"));
    QVERIFY(std::abs(*result.rMultiple + 1.0) < 1e-9);
}

void TestUnitMarketScoring::followUpExitClosesAtThatBarsClose()
{
    QVector<MarketBar> bars;
    bars.append(bar(0, 100.0, 101.0, 99.5, 101.0));
    bars.append(bar(1, 101.0, 102.0, 100.5, 102.0));
    TradeLinePlan plan;
    plan.entry = QStringLiteral("long");
    plan.sizeBand = QStringLiteral("1R");
    plan.bracket = {1.0, 6.0};
    plan.followUps = {QStringLiteral("exit"), QStringLiteral("hold")};
    const auto result = simulateTradeLine(bars, 2.0, 100.0, plan, 5);
    QCOMPARE(result.exitReason, QStringLiteral("follow_up_exit"));
    QCOMPARE(result.exitBarOffset, 1);
    QVERIFY(std::abs(*result.rMultiple - 0.5) < 1e-9);
}

void TestUnitMarketScoring::haltedBarCannotFillAnything()
{
    QVector<MarketBar> bars;
    MarketBar halted;
    halted.barIndex = 0;
    bars.append(halted);
    bars.append(bar(1, 100.0, 101.0, 99.0, 101.0));
    TradeLinePlan plan;
    plan.entry = QStringLiteral("long");
    plan.sizeBand = QStringLiteral("1R");
    plan.bracket = {1.0, 6.0};
    plan.followUps = {QStringLiteral("exit"), QStringLiteral("exit")};
    const auto result = simulateTradeLine(bars, 2.0, 100.0, plan, 5);
    // The exit on the halted bar filled nothing, because a halt has no price to
    // fill at. The next bar, which does, is where the position leaves.
    QCOMPARE(result.exitBarOffset, 2);
    QCOMPARE(result.exitReason, QStringLiteral("follow_up_exit"));
    QVERIFY(std::abs(*result.rMultiple - 0.5) < 1e-9);
}

void TestUnitMarketScoring::sizeBandScalesTheRealizedMultiple()
{
    QCOMPARE(sizeBandMultiplier(QStringLiteral("0")), 0.0);
    QCOMPARE(sizeBandMultiplier(QStringLiteral("0.25R")), 0.25);
    QCOMPARE(sizeBandMultiplier(QStringLiteral("0.5R")), 0.5);
    QCOMPARE(sizeBandMultiplier(QStringLiteral("1R")), 1.0);

    QVector<MarketBar> bars;
    bars.append(bar(0, 100.0, 106.5, 99.0, 106.0));
    TradeLinePlan plan;
    plan.entry = QStringLiteral("long");
    plan.sizeBand = QStringLiteral("0.5R");
    plan.bracket = {1.0, 3.0};
    plan.followUps = {QStringLiteral("hold")};
    const auto result = simulateTradeLine(bars, 2.0, 100.0, plan, 5);
    QVERIFY(std::abs(*result.rMultiple - 1.5) < 1e-9);
}

void TestUnitMarketScoring::wilderAtrNeedsTwentyOneBarsAndIsScaleFree()
{
    QVERIFY(!atrPercent20(flatWindow(20, 100.0)).has_value());
    const auto flat = atrPercent20(flatWindow(21, 100.0));
    QVERIFY(flat.has_value());
    // Every true range is 2.0 on a flat window whose high/low straddle close by
    // 1.0, so ATR is 2.0 and the percentage is 2.0.
    QVERIFY(std::abs(*flat - 2.0) < 1e-9);

    // Scale-free: the same shape at a different price level reports the same
    // percentage, which is what lets it survive normalization.
    QVector<MarketBar> scaled;
    for (int index = 0; index < 21; ++index) {
        scaled.append(bar(index, 50.0, 50.5, 49.5, 50.0));
    }
    QVERIFY(std::abs(*atrPercent20(scaled) - 2.0) < 1e-9);
}

void TestUnitMarketScoring::returnAndRangePositionAreHandWorked()
{
    QVector<MarketBar> bars = flatWindow(25, 100.0);
    bars.last().close = 110.0;
    bars.last().high = 110.0;
    QVERIFY(std::abs(*simpleReturnPercent(bars, 5) - 10.0) < 1e-9);
    QVERIFY(std::abs(*simpleReturnPercent(bars, 20) - 10.0) < 1e-9);
    // Last 20 bars: lows are 99.0, highs are 101.0 except the last at 110.0.
    QVERIFY(std::abs(*rangePosition20(bars) - 1.0) < 1e-9);

    QVERIFY(!simpleReturnPercent(flatWindow(4, 100.0), 5).has_value());
    QVERIFY(!rangePosition20(flatWindow(19, 100.0)).has_value());
}

void TestUnitMarketScoring::nullCloseInsideTheLookbackRefusesToDerive()
{
    QVector<MarketBar> bars = flatWindow(25, 100.0);
    bars[10].close.reset();
    // Absent is masked, never zero: the derivation refuses rather than
    // substituting a number nobody measured.
    QVERIFY(!atrPercent20(bars).has_value());
    QVERIFY(!computeVerifiedHudStat(QStringLiteral("atr_pct_20"), bars).has_value());
    QVERIFY(!computeVerifiedHudStat(QStringLiteral("unknown_stat"), bars).has_value());
}

void TestUnitMarketScoring::ratingMovesTowardTheSeedAndDampensWithReps()
{
    QCOMPARE(expectedScore(1500.0, 1500.0), 0.5);
    QVERIFY(expectedScore(1900.0, 1500.0) > 0.9);
    QCOMPARE(kFactorForReps(0), 40.0);
    QCOMPARE(kFactorForReps(30), 20.0);
    QCOMPARE(kFactorForReps(100), 10.0);

    MarketSolverRating rating;
    const MarketSolverRating won = updateSolverRating(rating, 1500, 1.0);
    QCOMPARE(won.reps, 1);
    QVERIFY(std::abs(won.rating - 1520.0) < 1e-9);
    const MarketSolverRating lost = updateSolverRating(rating, 1500, 0.0);
    QVERIFY(std::abs(lost.rating - 1480.0) < 1e-9);
    // A score outside [0, 1] is a caller bug, not a licence to move further.
    QCOMPARE(updateSolverRating(rating, 1500, 9.0).rating, won.rating);

    MarketSolverRating veteran;
    veteran.reps = 120;
    const MarketSolverRating settled = updateSolverRating(veteran, 1500, 1.0);
    QVERIFY(std::abs(settled.rating - 1505.0) < 1e-9);
}

void TestUnitMarketScoring::repScoreIgnoresConfidencePlies()
{
    MarketScoreCard card;
    card.lineScore.pliesTotal = 4;
    card.lineScore.pliesExact = 3;
    QVERIFY(std::abs(repScoreFromCard(card) - 0.75) < 1e-12);
    MarketScoreCard empty;
    QCOMPARE(repScoreFromCard(empty), 0.0);
}

QTEST_MAIN(TestUnitMarketScoring)

#include "test_unit_market_scoring.moc"
