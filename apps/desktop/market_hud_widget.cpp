#include "market_hud_widget.h"

#include <QFormLayout>
#include <QLabel>

using namespace parlawl::market;

MarketHudWidget::MarketHudWidget(QWidget *parent)
    : QWidget(parent)
    , m_form(new QFormLayout(this))
    , m_symbolLabel(new QLabel(this))
    , m_themeLabel(new QLabel(this))
    , m_seedLabel(new QLabel(this))
    , m_taskLabel(new QLabel(this))
{
    setObjectName(QStringLiteral("marketHudWidget"));
    m_symbolLabel->setObjectName(QStringLiteral("marketHudSymbol"));
    m_themeLabel->setObjectName(QStringLiteral("marketHudTheme"));
    m_seedLabel->setObjectName(QStringLiteral("marketHudSeed"));
    m_taskLabel->setObjectName(QStringLiteral("marketHudTask"));
    for (QLabel *label : {m_symbolLabel, m_themeLabel, m_seedLabel, m_taskLabel}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
    }
    m_seedLabel->setToolTip(ratingSeedDisclaimer());

    m_form->setContentsMargins(8, 8, 8, 8);
    m_form->addRow(QStringLiteral("Symbol"), m_symbolLabel);
    m_form->addRow(QStringLiteral("Task"), m_taskLabel);
    m_form->addRow(QStringLiteral("Theme"), m_themeLabel);
    m_form->addRow(QStringLiteral("Seed"), m_seedLabel);
    clear();
}

void MarketHudWidget::clear()
{
    for (auto it = m_statLabels.constBegin(); it != m_statLabels.constEnd(); ++it) {
        m_form->removeRow(it.value());
    }
    m_statLabels.clear();
    m_symbolLabel->setText(QStringLiteral("—"));
    m_themeLabel->setText(QStringLiteral("—"));
    m_taskLabel->setText(QStringLiteral("—"));
    m_seedLabel->setText(QStringLiteral("—"));
}

void MarketHudWidget::setPuzzle(
    const MarketPuzzleVisible &puzzle,
    const QStringList &verifiedStatIds)
{
    clear();
    m_symbolLabel->setText(puzzle.displaySymbol);
    m_taskLabel->setText(taskKindText(puzzle.taskKind));
    m_themeLabel->setText(puzzle.theme);
    // A seed, never a difficulty and never a rating.
    m_seedLabel->setText(
        QStringLiteral("%1 (%2) — %3")
            .arg(QString::number(puzzle.ratingSeed), puzzle.ratingSeedBasis, ratingSeedDisclaimer()));

    for (const MarketHudStat &stat : puzzle.hud) {
        auto *label = new QLabel(this);
        label->setObjectName(QStringLiteral("marketHudStat_") + stat.statId);
        label->setTextFormat(Qt::PlainText);
        const bool verified = verifiedStatIds.contains(stat.statId);
        label->setText(
            QStringLiteral("%1 %2 · %3")
                .arg(
                    QString::number(stat.value, 'f', 4),
                    stat.unit,
                    verified
                        ? QStringLiteral("recomputed by ParlAWL from the visible bars")
                        : QStringLiteral("producer-supplied, not recomputed")));
        m_form->addRow(stat.statId, label);
        m_statLabels.insert(stat.statId, label);
    }
}

QString MarketHudWidget::statText(const QString &statId) const
{
    QLabel *label = m_statLabels.value(statId, nullptr);
    return label == nullptr ? QString() : label->text();
}

QString MarketHudWidget::seedText() const
{
    return m_seedLabel->text();
}

QString MarketHudWidget::themeText() const
{
    return m_themeLabel->text();
}
