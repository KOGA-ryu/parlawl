#include "evaluation_bar_widget.h"

#include <QPainter>

EvaluationBarWidget::EvaluationBarWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void EvaluationBarWidget::setExpectation(double whiteExpectation, bool available)
{
    m_whiteExpectation = qBound(0.0, whiteExpectation, 1.0);
    m_available = available;
    update();
}

void EvaluationBarWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QRect frameRect = rect().adjusted(0, 0, -1, -1);
    painter.fillRect(frameRect, QColor(30, 30, 30));

    if (!m_available) {
        painter.fillRect(frameRect.adjusted(2, 2, -2, -2), QColor(80, 80, 80));
    } else {
        const QRect innerRect = frameRect.adjusted(2, 2, -2, -2);
        const int whiteHeight = static_cast<int>(innerRect.height() * m_whiteExpectation);
        const QRect blackRect(innerRect.left(), innerRect.top(), innerRect.width(), innerRect.height() - whiteHeight);
        const QRect whiteRect(innerRect.left(), innerRect.bottom() - whiteHeight + 1, innerRect.width(), whiteHeight);
        painter.fillRect(blackRect, QColor(32, 32, 32));
        painter.fillRect(whiteRect, QColor(245, 245, 245));
    }

    painter.setPen(QPen(QColor(120, 120, 120), 1));
    painter.drawRect(frameRect);
}

QSize EvaluationBarWidget::sizeHint() const
{
    return {18, 520};
}
