#pragma once

// OHLCV rendering for a market rep. `apps/desktop/` draws a board and nothing
// else, so this is new. It uses the Qt Charts module (`QCandlestickSeries` plus
// a volume bar series), which the owner approved and which this Qt ships; no
// third-party charting library is involved.
//
// The widget is handed `MarketBar` values and never anything sealed. It cannot
// draw a bar after T until `setRevealedContinuation` is called with bars that
// only a committed terminal could have produced.

#include <QVector>
#include <QWidget>

#include "market_types.h"

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

namespace QtCharts {
}

class QChart;
class QChartView;
class QCandlestickSeries;
class QBarSeries;
class QBarCategoryAxis;
class QValueAxis;

class MarketChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MarketChartWidget(QWidget *parent = nullptr);

    void setWindow(
        const QString &displaySymbol,
        const QString &grain,
        const QVector<parlawl::market::MarketBar> &bars,
        const QVector<bool> &sessionBreakAfter);
    void setRevealedContinuation(const QVector<parlawl::market::MarketBar> &bars, int revealedCount);
    void clearRevealedContinuation();
    void clear();

    [[nodiscard]] int drawnVisibleBarCount() const { return m_drawnVisibleBars; }
    [[nodiscard]] int drawnContinuationBarCount() const { return m_drawnContinuationBars; }
    [[nodiscard]] QString titleText() const;

private:
    void rebuild();

    QLabel *m_titleLabel;
    QChartView *m_priceView;
    QChartView *m_volumeView;
    QString m_displaySymbol;
    QString m_grain;
    QVector<parlawl::market::MarketBar> m_visibleBars;
    QVector<bool> m_sessionBreakAfter;
    QVector<parlawl::market::MarketBar> m_continuationBars;
    int m_revealedContinuation = 0;
    int m_drawnVisibleBars = 0;
    int m_drawnContinuationBars = 0;
};
