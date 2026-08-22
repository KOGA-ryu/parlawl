#pragma once

// RUSH: keyboard-first, one key per answer, a per-ply deadline and a streak.
//
// The key map is fixed and shown on screen rather than discovered:
//
//   entry          B long        S short      H pass
//   size band      1..4          (the pack's declared bands, in order)
//   bracket        1..5 stop, then 1..5 target (the declared ATR grids)
//   follow up      A add   X exit   H hold   T tighten
//   label          1..N          (the pack's declared labels, in order)
//   verdict        P planted     C clean
//   artifact class 1..N          (the pack's declared classes, in order)
//   confidence     0..9          decile midpoint: key d means (d + 0.5) / 10
//   space          next rep, or open the reveal once the rep is terminal
//
// Identity stays anonymized for the whole rep. There is no peek key, and there
// is no setting that adds one.

#include <QWidget>

#include "market_session_controller.h"

QT_BEGIN_NAMESPACE
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTimer;
QT_END_NAMESPACE

class MarketRushPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MarketRushPanel(QWidget *parent = nullptr);

    void setController(parlawl::market::MarketSessionController *controller);
    void refresh();

    //! Exposed so a test can drive the map without fighting focus policy.
    bool handleKey(int key);

    [[nodiscard]] QString promptText() const;
    [[nodiscard]] QString keyHintText() const;
    [[nodiscard]] QString timerText() const;
    [[nodiscard]] QString streakText() const;
    [[nodiscard]] QString lastErrorText() const;
    [[nodiscard]] bool calibrationEnabled() const;

signals:
    void nextRepRequested();
    void revealRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void onTick();
    void submitCalibration();
    bool answerDigit(int digit);
    void reportError(const QString &message);

    parlawl::market::MarketSessionController *m_controller = nullptr;
    QLabel *m_promptLabel;
    QLabel *m_keyHintLabel;
    QLabel *m_timerLabel;
    QLabel *m_streakLabel;
    QLabel *m_errorLabel;
    QDoubleSpinBox *m_lowerSpin;
    QDoubleSpinBox *m_upperSpin;
    QPushButton *m_submitIntervalButton;
    QTimer *m_tick;
    //! Half-entered bracket: the stop is chosen, the target is not.
    std::optional<double> m_pendingStopAtr;
};
