#include "market_chart_widget.h"

#include <algorithm>
#include <limits>

#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QCandlestickSeries>
#include <QCandlestickSet>
#include <QChart>
#include <QChartView>
#include <QLabel>
#include <QValueAxis>
#include <QVBoxLayout>

using namespace parlawl::market;

namespace {

constexpr int kMaximumAxisLabels = 12;

QColor bullishColor()
{
    return QColor(0x2e, 0x7d, 0x32);
}

QColor bearishColor()
{
    return QColor(0xc6, 0x28, 0x28);
}

QColor continuationColor()
{
    return QColor(0x37, 0x47, 0x4f);
}

} // namespace

MarketChartWidget::MarketChartWidget(QWidget *parent)
    : QWidget(parent)
    , m_titleLabel(new QLabel(this))
    , m_priceView(new QChartView(new QChart(), this))
    , m_volumeView(new QChartView(new QChart(), this))
{
    setObjectName(QStringLiteral("marketChartWidget"));
    m_titleLabel->setObjectName(QStringLiteral("marketChartTitle"));
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_priceView->setObjectName(QStringLiteral("marketChartPriceView"));
    m_volumeView->setObjectName(QStringLiteral("marketChartVolumeView"));
    m_priceView->setRenderHint(QPainter::Antialiasing);
    m_volumeView->setRenderHint(QPainter::Antialiasing);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_priceView, 3);
    layout->addWidget(m_volumeView, 1);
    clear();
}

QString MarketChartWidget::titleText() const
{
    return m_titleLabel->text();
}

void MarketChartWidget::setWindow(
    const QString &displaySymbol,
    const QString &grain,
    const QVector<MarketBar> &bars,
    const QVector<bool> &sessionBreakAfter)
{
    m_displaySymbol = displaySymbol;
    m_grain = grain;
    m_visibleBars = bars;
    m_sessionBreakAfter = sessionBreakAfter;
    m_continuationBars.clear();
    m_revealedContinuation = 0;
    rebuild();
}

void MarketChartWidget::setRevealedContinuation(const QVector<MarketBar> &bars, int revealedCount)
{
    m_continuationBars = bars;
    m_revealedContinuation = std::clamp(revealedCount, 0, static_cast<int>(bars.size()));
    rebuild();
}

void MarketChartWidget::clearRevealedContinuation()
{
    m_continuationBars.clear();
    m_revealedContinuation = 0;
    rebuild();
}

void MarketChartWidget::clear()
{
    m_displaySymbol.clear();
    m_grain.clear();
    m_visibleBars.clear();
    m_sessionBreakAfter.clear();
    m_continuationBars.clear();
    m_revealedContinuation = 0;
    rebuild();
}

void MarketChartWidget::rebuild()
{
    auto *priceChart = new QChart();
    auto *volumeChart = new QChart();
    priceChart->legend()->hide();
    volumeChart->legend()->hide();
    priceChart->setMargins(QMargins(2, 2, 2, 2));
    volumeChart->setMargins(QMargins(2, 2, 2, 2));

    m_drawnVisibleBars = 0;
    m_drawnContinuationBars = 0;

    if (m_visibleBars.isEmpty()) {
        m_titleLabel->setText(QStringLiteral("No market rep loaded."));
        m_priceView->setChart(priceChart);
        m_volumeView->setChart(volumeChart);
        return;
    }

    // Nothing after T is drawn unless the reveal disclosed it.
    QVector<MarketBar> drawn = m_visibleBars;
    for (int index = 0; index < m_revealedContinuation && index < m_continuationBars.size(); ++index) {
        drawn.append(m_continuationBars.at(index));
    }

    auto *visibleSeries = new QCandlestickSeries();
    visibleSeries->setName(QStringLiteral("window"));
    visibleSeries->setIncreasingColor(bullishColor());
    visibleSeries->setDecreasingColor(bearishColor());
    auto *continuationSeries = new QCandlestickSeries();
    continuationSeries->setName(QStringLiteral("after T"));
    continuationSeries->setIncreasingColor(continuationColor());
    continuationSeries->setDecreasingColor(continuationColor());

    auto *volumeSet = new QBarSet(QStringLiteral("volume"));
    auto *volumeSeries = new QBarSeries();

    QStringList categories;
    double lowest = std::numeric_limits<double>::max();
    double highest = std::numeric_limits<double>::lowest();
    double highestVolume = 0.0;

    for (int index = 0; index < drawn.size(); ++index) {
        const MarketBar &bar = drawn.at(index);
        const bool afterT = index >= m_visibleBars.size();
        // T is 0. Bars before it count down, bars after it count up.
        const int offset = index - static_cast<int>(m_visibleBars.size()) + 1;
        categories.append(afterT ? QStringLiteral("+%1").arg(offset) : QString::number(offset));

        // A halted bar has no prices. It is left as a gap rather than drawn at
        // zero or carried forward from its neighbour.
        if (bar.hasOhlc()) {
            auto *candle = new QCandlestickSet(
                *bar.open, *bar.high, *bar.low, *bar.close, static_cast<qreal>(index));
            if (afterT) {
                continuationSeries->append(candle);
                ++m_drawnContinuationBars;
            } else {
                visibleSeries->append(candle);
                ++m_drawnVisibleBars;
            }
            lowest = std::min(lowest, *bar.low);
            highest = std::max(highest, *bar.high);
        }
        const double volume = bar.volume.value_or(0.0);
        *volumeSet << volume;
        highestVolume = std::max(highestVolume, volume);
    }

    priceChart->addSeries(visibleSeries);
    if (m_drawnContinuationBars > 0) {
        priceChart->addSeries(continuationSeries);
    } else {
        delete continuationSeries;
    }
    volumeSeries->append(volumeSet);
    volumeChart->addSeries(volumeSeries);

    auto *priceCategoryAxis = new QBarCategoryAxis();
    priceCategoryAxis->append(categories);
    // A dense axis is unreadable at 120 bars; thin the labels, never the bars.
    priceCategoryAxis->setLabelsVisible(categories.size() <= kMaximumAxisLabels);
    priceChart->addAxis(priceCategoryAxis, Qt::AlignBottom);
    visibleSeries->attachAxis(priceCategoryAxis);
    if (m_drawnContinuationBars > 0) {
        continuationSeries->attachAxis(priceCategoryAxis);
    }

    auto *priceValueAxis = new QValueAxis();
    if (highest > lowest) {
        const double pad = (highest - lowest) * 0.04;
        priceValueAxis->setRange(lowest - pad, highest + pad);
    }
    priceValueAxis->setLabelFormat(QStringLiteral("%.2f"));
    priceChart->addAxis(priceValueAxis, Qt::AlignLeft);
    visibleSeries->attachAxis(priceValueAxis);
    if (m_drawnContinuationBars > 0) {
        continuationSeries->attachAxis(priceValueAxis);
    }

    auto *volumeCategoryAxis = new QBarCategoryAxis();
    volumeCategoryAxis->append(categories);
    volumeCategoryAxis->setLabelsVisible(false);
    volumeChart->addAxis(volumeCategoryAxis, Qt::AlignBottom);
    volumeSeries->attachAxis(volumeCategoryAxis);
    auto *volumeValueAxis = new QValueAxis();
    volumeValueAxis->setRange(0.0, highestVolume > 0.0 ? highestVolume * 1.1 : 1.0);
    volumeValueAxis->setLabelFormat(QStringLiteral("%.1f"));
    volumeChart->addAxis(volumeValueAxis, Qt::AlignLeft);
    volumeSeries->attachAxis(volumeValueAxis);

    int sessionBreaks = 0;
    for (bool flag : m_sessionBreakAfter) {
        if (flag) {
            ++sessionBreaks;
        }
    }
    m_titleLabel->setText(
        QStringLiteral("%1 · %2 · %3 bars to T · %4 session breaks · prices normalized, close at T = 100.0%5")
            .arg(m_displaySymbol, m_grain)
            .arg(m_visibleBars.size())
            .arg(sessionBreaks)
            .arg(
                m_drawnContinuationBars > 0
                    ? QStringLiteral(" · %1 bars after T revealed").arg(m_drawnContinuationBars)
                    : QString()));

    m_priceView->setChart(priceChart);
    m_volumeView->setChart(volumeChart);
}
