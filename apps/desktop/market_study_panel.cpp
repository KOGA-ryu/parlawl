#include "market_study_panel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

using namespace parlawl::market;

MarketStudyPanel::MarketStudyPanel(QWidget *parent)
    : QWidget(parent)
    , m_planForm(new QFormLayout())
    , m_planContainer(new QWidget(this))
    , m_promptLabel(new QLabel(this))
    , m_errorLabel(new QLabel(this))
    , m_commitButton(new QPushButton(QStringLiteral("Commit plan"), this))
    , m_lowerSpin(new QDoubleSpinBox(this))
    , m_upperSpin(new QDoubleSpinBox(this))
    , m_submitIntervalButton(new QPushButton(QStringLiteral("Submit interval"), this))
    , m_revealButton(new QPushButton(QStringLiteral("Reveal"), this))
    , m_stepButton(new QPushButton(QStringLiteral("Step next bar"), this))
    , m_revealView(new QTextEdit(this))
{
    setObjectName(QStringLiteral("marketStudyPanel"));
    m_planContainer->setObjectName(QStringLiteral("marketStudyPlanEditor"));
    m_promptLabel->setObjectName(QStringLiteral("marketStudyPrompt"));
    m_errorLabel->setObjectName(QStringLiteral("marketStudyError"));
    m_commitButton->setObjectName(QStringLiteral("marketStudyCommitPlan"));
    m_lowerSpin->setObjectName(QStringLiteral("marketStudyIntervalLower"));
    m_upperSpin->setObjectName(QStringLiteral("marketStudyIntervalUpper"));
    m_submitIntervalButton->setObjectName(QStringLiteral("marketStudySubmitInterval"));
    m_revealButton->setObjectName(QStringLiteral("marketStudyReveal"));
    m_stepButton->setObjectName(QStringLiteral("marketStudyStepContinuation"));
    m_revealView->setObjectName(QStringLiteral("marketRevealSummary"));
    m_revealView->setReadOnly(true);
    m_revealView->setAcceptRichText(false);
    for (QLabel *label : {m_promptLabel, m_errorLabel}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
    }
    for (QDoubleSpinBox *spin : {m_lowerSpin, m_upperSpin}) {
        spin->setDecimals(2);
        spin->setRange(-95.0, 400.0);
        spin->setSingleStep(0.5);
    }
    m_planContainer->setLayout(m_planForm);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_promptLabel);
    layout->addWidget(m_planContainer);
    layout->addWidget(m_commitButton);
    auto *calibration = new QFormLayout();
    calibration->addRow(QStringLiteral("Interval lower"), m_lowerSpin);
    calibration->addRow(QStringLiteral("Interval upper"), m_upperSpin);
    layout->addLayout(calibration);
    layout->addWidget(m_submitIntervalButton);
    layout->addWidget(m_revealButton);
    layout->addWidget(m_stepButton);
    layout->addWidget(m_revealView, 1);
    layout->addWidget(m_errorLabel);

    connect(m_commitButton, &QPushButton::clicked, this, &MarketStudyPanel::commitPlan);
    connect(m_submitIntervalButton, &QPushButton::clicked, this, &MarketStudyPanel::submitCalibration);
    connect(m_revealButton, &QPushButton::clicked, this, &MarketStudyPanel::openReveal);
    connect(m_stepButton, &QPushButton::clicked, this, &MarketStudyPanel::stepContinuation);
    refresh();
}

void MarketStudyPanel::setController(MarketSessionController *controller)
{
    m_controller = controller;
    if (m_controller != nullptr) {
        connect(
            m_controller,
            &MarketSessionController::sessionChanged,
            this,
            &MarketStudyPanel::refresh,
            Qt::UniqueConnection);
    }
    rebuildEditor();
    refresh();
}

QString MarketStudyPanel::promptText() const
{
    return m_promptLabel->text();
}

QString MarketStudyPanel::revealSummaryText() const
{
    return m_revealView->toPlainText();
}

bool MarketStudyPanel::canCommitPlan() const
{
    return m_commitButton->isEnabled();
}

bool MarketStudyPanel::canStepContinuation() const
{
    return m_stepButton->isEnabled();
}

void MarketStudyPanel::rebuildEditor()
{
    while (m_planForm->rowCount() > 0) {
        m_planForm->removeRow(0);
    }
    m_editors.clear();
    m_editorPuzzleId.clear();
    if (m_controller == nullptr || m_controller->currentPuzzle() == nullptr) {
        return;
    }
    const MarketPuzzleVisible *puzzle = m_controller->currentPuzzle();
    const MarketTaskSpec &spec = m_controller->header().taskSpec;
    m_editorPuzzleId = puzzle->puzzleId;

    for (const MarketPlySpec &ply : puzzle->plies) {
        PlyEditor editor;
        editor.spec = ply;
        QString label = plyKindText(ply.kind);
        if (ply.barOffset.has_value()) {
            label += QStringLiteral(" T+%1").arg(*ply.barOffset);
        }
        switch (ply.kind) {
        case PlyKind::Entry:
        case PlyKind::SizeBand:
        case PlyKind::FollowUp:
        case PlyKind::Label:
        case PlyKind::ArtifactClass:
        case PlyKind::Verdict: {
            editor.primary = new QComboBox(m_planContainer);
            editor.primary->setObjectName(
                QStringLiteral("marketStudyPly_%1").arg(ply.plyIndex));
            QStringList options;
            switch (ply.kind) {
            case PlyKind::Entry:
                options = spec.entries;
                break;
            case PlyKind::SizeBand:
                options = spec.sizeBands;
                break;
            case PlyKind::FollowUp:
                options = spec.followUpActions;
                break;
            case PlyKind::Label:
                options = spec.patternLabels;
                break;
            case PlyKind::ArtifactClass:
                options = spec.artifactClasses;
                break;
            default:
                options = {QStringLiteral("planted"), QStringLiteral("clean")};
                break;
            }
            editor.primary->addItems(options);
            m_planForm->addRow(label, editor.primary);
            break;
        }
        case PlyKind::Bracket: {
            editor.primary = new QComboBox(m_planContainer);
            editor.secondary = new QComboBox(m_planContainer);
            editor.primary->setObjectName(
                QStringLiteral("marketStudyPly_%1_stop").arg(ply.plyIndex));
            editor.secondary->setObjectName(
                QStringLiteral("marketStudyPly_%1_target").arg(ply.plyIndex));
            for (double stop : spec.stopAtrMultiples) {
                editor.primary->addItem(QStringLiteral("%1 ATR").arg(stop), stop);
            }
            for (double target : spec.targetAtrMultiples) {
                editor.secondary->addItem(QStringLiteral("%1 ATR").arg(target), target);
            }
            m_planForm->addRow(label + QStringLiteral(" stop"), editor.primary);
            m_planForm->addRow(label + QStringLiteral(" target"), editor.secondary);
            break;
        }
        case PlyKind::Confidence:
            editor.probability = new QDoubleSpinBox(m_planContainer);
            editor.probability->setObjectName(
                QStringLiteral("marketStudyPly_%1").arg(ply.plyIndex));
            editor.probability->setDecimals(2);
            editor.probability->setRange(0.0, 1.0);
            editor.probability->setSingleStep(0.05);
            editor.probability->setValue(0.5);
            m_planForm->addRow(label, editor.probability);
            break;
        }
        m_editors.append(editor);
    }
}

void MarketStudyPanel::commitPlan()
{
    if (m_controller == nullptr) {
        return;
    }
    m_errorLabel->clear();
    for (const PlyEditor &editor : m_editors) {
        const MarketPlySpec *current = m_controller->currentPly();
        if (current == nullptr) {
            break;
        }
        QString error;
        bool ok = false;
        switch (editor.spec.kind) {
        case PlyKind::Bracket:
            ok = m_controller->answerBracket(
                BracketChoice{
                    editor.primary->currentData().toDouble(),
                    editor.secondary->currentData().toDouble()},
                &error);
            break;
        case PlyKind::Confidence:
            ok = m_controller->answerConfidence(editor.probability->value(), &error);
            break;
        default:
            ok = m_controller->answerCategorical(editor.primary->currentText(), &error);
            break;
        }
        if (!ok) {
            m_errorLabel->setText(error);
            return;
        }
    }
    refresh();
}

void MarketStudyPanel::submitCalibration()
{
    if (m_controller == nullptr || !m_controller->awaitingCalibration()) {
        return;
    }
    QString error;
    if (!m_controller->answerCalibration(m_lowerSpin->value(), m_upperSpin->value(), &error)) {
        m_errorLabel->setText(error);
        return;
    }
    m_errorLabel->clear();
    refresh();
}

void MarketStudyPanel::openReveal()
{
    if (m_controller == nullptr) {
        return;
    }
    QString error;
    if (!m_controller->openReveal(&error)) {
        m_errorLabel->setText(error);
        return;
    }
    m_errorLabel->clear();
    renderReveal();
    emit revealChanged();
    refresh();
}

void MarketStudyPanel::stepContinuation()
{
    if (m_controller == nullptr) {
        return;
    }
    m_controller->stepContinuation();
    renderReveal();
    emit revealChanged();
    refresh();
}

void MarketStudyPanel::renderReveal()
{
    if (m_controller == nullptr || m_controller->reveal() == nullptr) {
        m_revealView->clear();
        return;
    }
    const MarketReveal *reveal = m_controller->reveal();
    QStringList lines;
    lines.append(
        QStringLiteral("%1 · %2 · %3 · %4")
            .arg(reveal->ticker, reveal->decisionTimeUtc, reveal->exchange, reveal->instrumentClass));
    lines.append(QStringLiteral("outcome theme: %1").arg(reveal->outcomeTheme));
    if (reveal->difficultyNoteValue.has_value()) {
        lines.append(
            QStringLiteral("difficulty note (%1): %2")
                .arg(reveal->difficultyNoteBasis)
                .arg(*reveal->difficultyNoteValue));
    }
    lines.append(QString());

    if (!reveal->keyLine.isEmpty()) {
        lines.append(QStringLiteral("Declared rule %1").arg(reveal->ruleId));
        // The disclaimer travels with the key, never in a tooltip somewhere else.
        lines.append(reveal->keyDisclaimer);
        for (const MarketRevealKeyLine &line : reveal->keyLine) {
            lines.append(
                QStringLiteral("  ply %1 %2 → %3")
                    .arg(line.plyIndex)
                    .arg(line.plyKindText, line.keyText));
        }
        lines.append(QString());
    }

    const MarketScoreCard &card = reveal->scoreCard;
    lines.append(
        QStringLiteral("ParlAWL local grade (%1) — Arc's regrade from the pack is authoritative")
            .arg(card.scoringPolicyId));
    for (const PlyScore &score : card.plyScores) {
        lines.append(
            QStringLiteral("  ply %1 %2: %3 (key %4)")
                .arg(score.plyIndex)
                .arg(plyKindText(score.kind), plyMatchText(score.match), score.keyText));
    }
    lines.append(
        QStringLiteral("  plies exact %1 of %2")
            .arg(card.lineScore.pliesExact)
            .arg(card.lineScore.pliesTotal));
    if (card.lineScore.humanRMultiple.has_value()) {
        lines.append(
            QStringLiteral("  R: you %1 · rule %2 · perfect %3")
                .arg(*card.lineScore.humanRMultiple)
                .arg(card.lineScore.ruleRMultiple.value_or(0.0))
                .arg(card.lineScore.perfectRMultiple.value_or(0.0)));
        if (card.lineScore.shortfallVsRule.has_value()) {
            lines.append(
                QStringLiteral("  shortfall vs rule %1 · vs perfect %2")
                    .arg(*card.lineScore.shortfallVsRule)
                    .arg(card.lineScore.shortfallVsPerfect.value_or(0.0)));
        }
    }
    if (card.calibration.answered) {
        lines.append(
            QStringLiteral("  interval [%1, %2] realized %3 — %4, Winkler %5")
                .arg(card.calibration.lower)
                .arg(card.calibration.upper)
                .arg(card.calibration.realizedValue)
                .arg(card.calibration.covered ? QStringLiteral("covered") : QStringLiteral("missed"))
                .arg(card.calibration.winklerScore));
    }
    if (card.brierScore.has_value()) {
        lines.append(QStringLiteral("  Brier %1").arg(*card.brierScore));
    }
    lines.append(QString());

    lines.append(QStringLiteral("Citation chain"));
    lines.append(QStringLiteral("  scan manifest: %1").arg(reveal->scanManifestId));
    if (!reveal->scanCitation.isEmpty()) {
        lines.append(QStringLiteral("  %1").arg(reveal->scanCitation));
    }
    lines.append(QStringLiteral("  dataset: %1").arg(reveal->corpusDatasetVersion));
    lines.append(QStringLiteral("  adjustment table: %1").arg(reveal->adjustmentTableSha256));
    lines.append(QStringLiteral("  source window digest: %1").arg(reveal->sourceWindowDigest));
    lines.append(
        QStringLiteral("  source continuation digest: %1").arg(reveal->sourceContinuationDigest));
    lines.append(QStringLiteral("  bars root: %1").arg(reveal->barsRootId));
    for (const QString &path : reveal->partitionPaths) {
        lines.append(QStringLiteral("  partition: %1").arg(path));
    }
    lines.append(QString());
    lines.append(marketPackEvidenceGrade());

    m_revealView->setPlainText(lines.join(QLatin1Char('\n')));
}

void MarketStudyPanel::refresh()
{
    if (m_controller == nullptr || m_controller->currentPuzzle() == nullptr) {
        m_promptLabel->setText(QStringLiteral("No market pack loaded."));
        m_commitButton->setEnabled(false);
        m_submitIntervalButton->setEnabled(false);
        m_revealButton->setEnabled(false);
        m_stepButton->setEnabled(false);
        return;
    }
    if (m_editorPuzzleId != m_controller->currentPuzzle()->puzzleId) {
        rebuildEditor();
    }
    m_promptLabel->setText(m_controller->promptText());
    m_commitButton->setEnabled(m_controller->currentPly() != nullptr);
    m_submitIntervalButton->setEnabled(m_controller->awaitingCalibration());
    m_lowerSpin->setEnabled(m_controller->awaitingCalibration());
    m_upperSpin->setEnabled(m_controller->awaitingCalibration());
    m_revealButton->setEnabled(m_controller->isTerminal() && !m_controller->isRevealed());
    const MarketReveal *reveal = m_controller->reveal();
    m_stepButton->setEnabled(
        reveal != nullptr
        && m_controller->revealedContinuationBars() < reveal->continuationBars.size());
    if (reveal == nullptr) {
        m_revealView->clear();
    }
}
