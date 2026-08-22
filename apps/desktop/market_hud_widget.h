#pragma once

// The fixed-position stat HUD. A plain widget either way — it is a column of
// labels, and the chart module has nothing to offer it.
//
// Each stat is marked as recomputed by ParlAWL or as producer-supplied, because
// the HUD is what the operator decides on and an unmarked number invites the
// reflex the import boundary exists to prevent.

#include <QHash>
#include <QVector>
#include <QWidget>

#include "market_types.h"

QT_BEGIN_NAMESPACE
class QFormLayout;
class QLabel;
QT_END_NAMESPACE

class MarketHudWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MarketHudWidget(QWidget *parent = nullptr);

    void setPuzzle(
        const parlawl::market::MarketPuzzleVisible &puzzle,
        const QStringList &verifiedStatIds);
    void clear();

    [[nodiscard]] QString statText(const QString &statId) const;
    [[nodiscard]] QString seedText() const;
    [[nodiscard]] QString themeText() const;
    [[nodiscard]] int statCount() const { return m_statLabels.size(); }

private:
    QFormLayout *m_form;
    QLabel *m_symbolLabel;
    QLabel *m_themeLabel;
    QLabel *m_seedLabel;
    QLabel *m_taskLabel;
    QHash<QString, QLabel *> m_statLabels;
};
