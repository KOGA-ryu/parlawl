#include "market_rush_panel.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

using namespace parlawl::market;

namespace {

QString keyHintFor(const MarketPlySpec *ply, const MarketTaskSpec &spec)
{
    if (ply == nullptr) {
        return QStringLiteral("space  next rep");
    }
    switch (ply->kind) {
    case PlyKind::Entry:
        return QStringLiteral("B long    S short    H pass");
    case PlyKind::SizeBand: {
        QStringList parts;
        for (int index = 0; index < spec.sizeBands.size(); ++index) {
            parts.append(QStringLiteral("%1 %2").arg(index + 1).arg(spec.sizeBands.at(index)));
        }
        return parts.join(QStringLiteral("    "));
    }
    case PlyKind::Bracket: {
        QStringList stops;
        for (int index = 0; index < spec.stopAtrMultiples.size(); ++index) {
            stops.append(QStringLiteral("%1 %2R").arg(index + 1).arg(spec.stopAtrMultiples.at(index)));
        }
        return QStringLiteral("stop: ") + stops.join(QStringLiteral("  "))
            + QStringLiteral("   then target");
    }
    case PlyKind::FollowUp:
        return QStringLiteral("A add    X exit    H hold    T tighten");
    case PlyKind::Label: {
        QStringList parts;
        for (int index = 0; index < spec.patternLabels.size(); ++index) {
            parts.append(QStringLiteral("%1 %2").arg(index + 1).arg(spec.patternLabels.at(index)));
        }
        return parts.join(QStringLiteral("    "));
    }
    case PlyKind::Verdict:
        return QStringLiteral("P planted    C clean");
    case PlyKind::ArtifactClass: {
        QStringList parts;
        for (int index = 0; index < spec.artifactClasses.size(); ++index) {
            parts.append(QStringLiteral("%1 %2").arg(index + 1).arg(spec.artifactClasses.at(index)));
        }
        return parts.join(QStringLiteral("  "));
    }
    case PlyKind::Confidence:
        return QStringLiteral("0..9  decile midpoint, key d means (d + 0.5) / 10");
    }
    return {};
}

} // namespace

MarketRushPanel::MarketRushPanel(QWidget *parent)
    : QWidget(parent)
    , m_promptLabel(new QLabel(this))
    , m_keyHintLabel(new QLabel(this))
    , m_timerLabel(new QLabel(this))
    , m_streakLabel(new QLabel(this))
    , m_errorLabel(new QLabel(this))
    , m_lowerSpin(new QDoubleSpinBox(this))
    , m_upperSpin(new QDoubleSpinBox(this))
    , m_submitIntervalButton(new QPushButton(QStringLiteral("Submit interval"), this))
    , m_tick(new QTimer(this))
{
    setObjectName(QStringLiteral("marketRushPanel"));
    setFocusPolicy(Qt::StrongFocus);
    m_promptLabel->setObjectName(QStringLiteral("marketRushPrompt"));
    m_keyHintLabel->setObjectName(QStringLiteral("marketRushKeys"));
    m_timerLabel->setObjectName(QStringLiteral("marketRushTimer"));
    m_streakLabel->setObjectName(QStringLiteral("marketRushStreak"));
    m_errorLabel->setObjectName(QStringLiteral("marketRushError"));
    m_lowerSpin->setObjectName(QStringLiteral("marketRushIntervalLower"));
    m_upperSpin->setObjectName(QStringLiteral("marketRushIntervalUpper"));
    m_submitIntervalButton->setObjectName(QStringLiteral("marketRushSubmitInterval"));
    for (QLabel *label : {m_promptLabel, m_keyHintLabel, m_timerLabel, m_streakLabel, m_errorLabel}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
    }
    for (QDoubleSpinBox *spin : {m_lowerSpin, m_upperSpin}) {
        spin->setDecimals(2);
        spin->setRange(-95.0, 400.0);
        spin->setSingleStep(0.5);
    }

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_promptLabel);
    layout->addWidget(m_keyHintLabel);
    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("Interval lower"), m_lowerSpin);
    form->addRow(QStringLiteral("Interval upper"), m_upperSpin);
    layout->addLayout(form);
    layout->addWidget(m_submitIntervalButton);
    layout->addWidget(m_timerLabel);
    layout->addWidget(m_streakLabel);
    layout->addWidget(m_errorLabel);
    layout->addStretch(1);

    connect(m_submitIntervalButton, &QPushButton::clicked, this, &MarketRushPanel::submitCalibration);
    connect(m_tick, &QTimer::timeout, this, &MarketRushPanel::onTick);
    m_tick->setInterval(100);
    refresh();
}

void MarketRushPanel::setController(MarketSessionController *controller)
{
    m_controller = controller;
    if (m_controller != nullptr) {
        connect(
            m_controller,
            &MarketSessionController::sessionChanged,
            this,
            &MarketRushPanel::refresh,
            Qt::UniqueConnection);
    }
    refresh();
}

QString MarketRushPanel::promptText() const
{
    return m_promptLabel->text();
}

QString MarketRushPanel::keyHintText() const
{
    return m_keyHintLabel->text();
}

QString MarketRushPanel::timerText() const
{
    return m_timerLabel->text();
}

QString MarketRushPanel::streakText() const
{
    return m_streakLabel->text();
}

QString MarketRushPanel::lastErrorText() const
{
    return m_errorLabel->text();
}

bool MarketRushPanel::calibrationEnabled() const
{
    return m_submitIntervalButton->isEnabled();
}

void MarketRushPanel::refresh()
{
    if (m_controller == nullptr || m_controller->currentPuzzle() == nullptr) {
        m_promptLabel->setText(QStringLiteral("No market pack loaded."));
        m_keyHintLabel->clear();
        m_timerLabel->setText(QStringLiteral("—"));
        m_streakLabel->setText(QStringLiteral("streak 0"));
        m_submitIntervalButton->setEnabled(false);
        m_tick->stop();
        return;
    }
    const MarketPuzzleVisible *puzzle = m_controller->currentPuzzle();
    m_promptLabel->setText(m_controller->promptText());
    const MarketPlySpec *ply = m_controller->currentPly();
    if (ply != nullptr && ply->kind == PlyKind::Bracket && m_pendingStopAtr.has_value()) {
        QStringList targets;
        const MarketTaskSpec &spec = m_controller->header().taskSpec;
        for (int index = 0; index < spec.targetAtrMultiples.size(); ++index) {
            targets.append(
                QStringLiteral("%1 %2R").arg(index + 1).arg(spec.targetAtrMultiples.at(index)));
        }
        m_keyHintLabel->setText(
            QStringLiteral("stop %1R chosen — target: ").arg(*m_pendingStopAtr)
            + targets.join(QStringLiteral("  ")));
    } else {
        m_keyHintLabel->setText(keyHintFor(ply, m_controller->header().taskSpec));
    }

    m_lowerSpin->setRange(
        puzzle->calibrationQuestion.lowerBound, puzzle->calibrationQuestion.upperBound);
    m_upperSpin->setRange(
        puzzle->calibrationQuestion.lowerBound, puzzle->calibrationQuestion.upperBound);
    m_submitIntervalButton->setEnabled(m_controller->awaitingCalibration());
    m_lowerSpin->setEnabled(m_controller->awaitingCalibration());
    m_upperSpin->setEnabled(m_controller->awaitingCalibration());
    m_streakLabel->setText(
        QStringLiteral("streak %1 · rating %2 after %3 reps")
            .arg(m_controller->streak())
            .arg(QString::number(m_controller->solverRating().rating, 'f', 0))
            .arg(m_controller->solverRating().reps));

    if (m_controller->isTerminal()) {
        m_tick->stop();
        m_timerLabel->setText(QStringLiteral("rep closed"));
    } else {
        m_tick->start();
        onTick();
    }
}

void MarketRushPanel::onTick()
{
    if (m_controller == nullptr || m_controller->isTerminal()) {
        m_tick->stop();
        return;
    }
    const qint64 remaining = m_controller->remainingDeadlineMilliseconds();
    if (remaining < 0) {
        m_timerLabel->setText(QStringLiteral("no deadline"));
        return;
    }
    m_timerLabel->setText(QStringLiteral("%1 ms").arg(remaining));
    if (remaining == 0) {
        m_tick->stop();
        QString error;
        // A timeout is a terminal with the plies that were entered and nulls
        // after. It is recorded, not discarded.
        if (!m_controller->expireCurrentPly(&error)) {
            reportError(error);
        }
    }
}

void MarketRushPanel::reportError(const QString &message)
{
    m_errorLabel->setText(message);
}

void MarketRushPanel::submitCalibration()
{
    if (m_controller == nullptr || !m_controller->awaitingCalibration()) {
        return;
    }
    QString error;
    if (!m_controller->answerCalibration(m_lowerSpin->value(), m_upperSpin->value(), &error)) {
        reportError(error);
        return;
    }
    m_errorLabel->clear();
}

bool MarketRushPanel::answerDigit(int digit)
{
    const MarketPlySpec *ply = m_controller->currentPly();
    if (ply == nullptr) {
        return false;
    }
    const MarketTaskSpec &spec = m_controller->header().taskSpec;
    QString error;
    switch (ply->kind) {
    case PlyKind::Confidence: {
        // Decile midpoint. A key that meant "exactly 0.0" would let a rep claim
        // certainty it did not type.
        const double probability = (static_cast<double>(digit) + 0.5) / 10.0;
        if (!m_controller->answerConfidence(probability, &error)) {
            reportError(error);
            return false;
        }
        return true;
    }
    case PlyKind::SizeBand:
        if (digit < 1 || digit > spec.sizeBands.size()) {
            return false;
        }
        if (!m_controller->answerCategorical(spec.sizeBands.at(digit - 1), &error)) {
            reportError(error);
            return false;
        }
        return true;
    case PlyKind::Label:
        if (digit < 1 || digit > spec.patternLabels.size()) {
            return false;
        }
        if (!m_controller->answerCategorical(spec.patternLabels.at(digit - 1), &error)) {
            reportError(error);
            return false;
        }
        return true;
    case PlyKind::ArtifactClass:
        if (digit < 1 || digit > spec.artifactClasses.size()) {
            return false;
        }
        if (!m_controller->answerCategorical(spec.artifactClasses.at(digit - 1), &error)) {
            reportError(error);
            return false;
        }
        return true;
    case PlyKind::Bracket:
        if (!m_pendingStopAtr.has_value()) {
            if (digit < 1 || digit > spec.stopAtrMultiples.size()) {
                return false;
            }
            m_pendingStopAtr = spec.stopAtrMultiples.at(digit - 1);
            refresh();
            return true;
        }
        if (digit < 1 || digit > spec.targetAtrMultiples.size()) {
            return false;
        }
        {
            const BracketChoice bracket{*m_pendingStopAtr, spec.targetAtrMultiples.at(digit - 1)};
            m_pendingStopAtr.reset();
            if (!m_controller->answerBracket(bracket, &error)) {
                reportError(error);
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}

bool MarketRushPanel::handleKey(int key)
{
    if (m_controller == nullptr || m_controller->currentPuzzle() == nullptr) {
        return false;
    }
    if (key == Qt::Key_Space) {
        if (m_controller->isTerminal() && !m_controller->isRevealed()) {
            emit revealRequested();
        } else if (m_controller->isRevealed()) {
            emit nextRepRequested();
        }
        return true;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return answerDigit(key - Qt::Key_0);
    }

    const MarketPlySpec *ply = m_controller->currentPly();
    if (ply == nullptr) {
        return false;
    }
    QString choice;
    switch (ply->kind) {
    case PlyKind::Entry:
        if (key == Qt::Key_B) {
            choice = QStringLiteral("long");
        } else if (key == Qt::Key_S) {
            choice = QStringLiteral("short");
        } else if (key == Qt::Key_H) {
            // Declining a fight is a real answer and is scored as one.
            choice = QStringLiteral("pass");
        }
        break;
    case PlyKind::FollowUp:
        if (key == Qt::Key_A) {
            choice = QStringLiteral("add");
        } else if (key == Qt::Key_X) {
            choice = QStringLiteral("exit");
        } else if (key == Qt::Key_H) {
            choice = QStringLiteral("hold");
        } else if (key == Qt::Key_T) {
            choice = QStringLiteral("tighten");
        }
        break;
    case PlyKind::Verdict:
        if (key == Qt::Key_P) {
            choice = QStringLiteral("planted");
        } else if (key == Qt::Key_C) {
            choice = QStringLiteral("clean");
        }
        break;
    default:
        break;
    }
    if (choice.isEmpty()) {
        return false;
    }
    QString error;
    if (!m_controller->answerCategorical(choice, &error)) {
        reportError(error);
        return false;
    }
    m_errorLabel->clear();
    return true;
}

void MarketRushPanel::keyPressEvent(QKeyEvent *event)
{
    if (!handleKey(event->key())) {
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}
