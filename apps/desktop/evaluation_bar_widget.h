#pragma once

#include <QWidget>

class EvaluationBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EvaluationBarWidget(QWidget *parent = nullptr);

    void setExpectation(double whiteExpectation, bool available);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;

private:
    double m_whiteExpectation = 0.5;
    bool m_available = false;
};
