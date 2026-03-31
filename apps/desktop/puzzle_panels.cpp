#include "puzzle_panels.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFontDatabase>
#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>

#include "review_engine_adapter.h"
#include "pgn_utils.h"

namespace {

QString ratingText(int rating, bool hidden)
{
    if (hidden) {
        return QStringLiteral("hidden");
    }
    return rating > 0 ? QString::number(rating) : QStringLiteral("unknown");
}

QString countText(int count)
{
    return count > 0 ? QString::number(count) : QStringLiteral("unknown");
}

QStringList sourceMoveList(const parlawl::puzzle_runner::PuzzleDefinition &puzzle)
{
    if (!puzzle.analysisSeed.sourceGamePgn.trimmed().isEmpty()) {
        return parlawl::puzzle_runner::pgnMoveList(puzzle.analysisSeed.sourceGamePgn);
    }

    if (puzzle.analysisSeed.rawPuzzleJson.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(puzzle.analysisSeed.rawPuzzleJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    const QJsonObject root = document.object();
    const QString pgnMoves = root.value(QStringLiteral("game")).toObject().value(QStringLiteral("pgn")).toString().trimmed();
    if (pgnMoves.isEmpty()) {
        return {};
    }

    return pgnMoves.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

int puzzleInitialPly(const parlawl::puzzle_runner::PuzzleDefinition &puzzle)
{
    if (puzzle.analysisSeed.rawPuzzleJson.isEmpty()) {
        return 0;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(puzzle.analysisSeed.rawPuzzleJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return 0;
    }

    const QJsonObject puzzleObject = document.object().value(QStringLiteral("puzzle")).toObject();
    if (!puzzleObject.contains(QStringLiteral("initialPly"))) {
        return 0;
    }
    return puzzleObject.value(QStringLiteral("initialPly")).toInt() + 1;
}

bool fenSideToMoveIsWhite(const QString &fen)
{
    const QStringList parts = fen.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return parts.size() > 1 ? parts.at(1) == QStringLiteral("w") : true;
}

int fenFullmoveNumber(const QString &fen)
{
    const QStringList parts = fen.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() > 5) {
        return std::max(parts.at(5).toInt(), 1);
    }
    return 1;
}

} // namespace

MoveListPanel::MoveListPanel(QWidget *parent)
    : QGroupBox(QStringLiteral("move list"), parent)
    , m_truthStatusLabel(new QLabel(this))
    , m_table(new QTableWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    m_truthStatusLabel->setWordWrap(true);
    QPalette hintPalette = m_truthStatusLabel->palette();
    hintPalette.setColor(QPalette::WindowText, QColor(92, 92, 92));
    m_truthStatusLabel->setPalette(hintPalette);
    layout->addWidget(m_truthStatusLabel);
    layout->addWidget(m_table);
    QFont listFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_table->setFont(listFont);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("White"), QStringLiteral("Black")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setAlternatingRowColors(true);
}

QString MoveListPanel::truthStatusText() const
{
    return m_truthStatusLabel->text();
}

void MoveListPanel::setMoves(
    const parlawl::puzzle_runner::PuzzleDefinition &puzzle,
    const QVector<parlawl::puzzle_runner::AppliedMove> &moves,
    int currentViewIndex)
{
    m_table->clearContents();
    const QStringList sourceMoves = sourceMoveList(puzzle);
    const int sourcePlyCount = std::min(puzzleInitialPly(puzzle), static_cast<int>(sourceMoves.size()));
    const bool hasFullSourceGame = !puzzle.analysisSeed.sourceGamePgn.trimmed().isEmpty() && !sourceMoves.isEmpty();
    const int displayedSourcePlyCount = hasFullSourceGame ? sourceMoves.size() : (moves.isEmpty() ? sourceMoves.size() : sourcePlyCount);
    const bool hasSourceHistory = sourcePlyCount > 0;
    const bool sideToMoveWhite = fenSideToMoveIsWhite(puzzle.fenStart);
    const int fullmoveNumber = fenFullmoveNumber(puzzle.fenStart);
    const bool hasLastMoveContext = !hasSourceHistory && !puzzle.analysisSeed.lastMove.isEmpty();
    const int fallbackStartOffset = sideToMoveWhite ? 0 : 1;
    const int sourceLikePlyCount = hasSourceHistory ? sourcePlyCount : (hasLastMoveContext ? 1 : 0);
    const int totalPlyCount = (hasSourceHistory ? displayedSourcePlyCount : sourceLikePlyCount) + moves.size() + (hasSourceHistory ? 0 : fallbackStartOffset);
    const int rowCount = std::max(1, (totalPlyCount + 1) / 2);
    m_table->setRowCount(rowCount);
    if (hasFullSourceGame) {
        m_truthStatusLabel->setText(QStringLiteral("Full source game shown; board review starts at the puzzle position."));
    } else if (hasSourceHistory) {
        m_truthStatusLabel->setText(QStringLiteral("Partial source history shown up to the puzzle start; board review starts at the puzzle position."));
    } else if (hasLastMoveContext) {
        m_truthStatusLabel->setText(QStringLiteral("Source history unavailable; showing last-move context and puzzle-local history only."));
    } else {
        m_truthStatusLabel->setText(QStringLiteral("Source history unavailable; showing puzzle-local history only."));
    }

    auto makeItem = [&](const QString &text, bool isCurrent) {
        auto *item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        if (isCurrent) {
            item->setBackground(QColor(222, 235, 255));
            item->setForeground(QColor(24, 54, 90));
        }
        return item;
    };

    for (int row = 0; row < rowCount; ++row) {
        const int moveNumber = hasSourceHistory ? row + 1 : fullmoveNumber + row;
        m_table->setItem(row, 0, makeItem(QString::number(std::max(moveNumber, 1)), false));
    }

    if (hasSourceHistory) {
        for (int plyIndex = 0; plyIndex < displayedSourcePlyCount; ++plyIndex) {
            const int row = plyIndex / 2;
            const int column = (plyIndex % 2 == 0) ? 1 : 2;
            const bool isCurrent = currentViewIndex == plyIndex + 1 && plyIndex < sourcePlyCount;
            m_table->setItem(row, column, makeItem(sourceMoves.at(plyIndex), isCurrent));
        }
    } else if (hasLastMoveContext) {
        const int row = 0;
        const int column = sideToMoveWhite ? 2 : 1;
        m_table->setItem(row, column, makeItem(puzzle.analysisSeed.lastMove, false));
    }

    for (int index = 0; index < moves.size(); ++index) {
        const auto &move = moves.at(index);
        const int absolutePly = hasSourceHistory ? sourcePlyCount + index : fallbackStartOffset + index;
        const bool isCurrent = currentViewIndex == sourceLikePlyCount + index + 1;
        const int row = absolutePly / 2;
        const int column = (absolutePly % 2 == 0) ? 1 : 2;
        const QString displayText = move.userMove
            ? QStringLiteral("[%1]").arg(move.uci)
            : move.uci;
        m_table->setItem(row, column, makeItem(displayText, isCurrent));
    }

    if (!hasSourceHistory && moves.isEmpty()) {
        const int row = fallbackStartOffset / 2;
        const int column = (fallbackStartOffset % 2 == 0) ? 1 : 2;
        if (m_table->item(row, column) == nullptr) {
            m_table->setItem(row, column, makeItem(QStringLiteral("start"), currentViewIndex == 0));
        }
    }
}

MetadataCard::MetadataCard(QWidget *parent)
    : QGroupBox(parent)
    , m_titleLabel(new QLabel(this))
    , m_availabilityLabel(new QLabel(this))
    , m_warningLabel(new QLabel(this))
    , m_openingLabel(new QLabel(this))
    , m_strategicErrorLabel(new QLabel(this))
    , m_planLabel(new QLabel(this))
    , m_criticalMistakeLabel(new QLabel(this))
    , m_lastPracticalMistakeLabel(new QLabel(this))
    , m_tacticalThemeLabel(new QLabel(this))
    , m_rawEvidenceToggle(new QPushButton(QStringLiteral("Raw Evidence"), this))
    , m_rawEvidenceLabel(new QLabel(this))
{
    m_titleLabel->setWordWrap(true);
    m_availabilityLabel->setWordWrap(true);
    m_warningLabel->setWordWrap(true);
    m_openingLabel->setWordWrap(true);
    m_strategicErrorLabel->setWordWrap(true);
    m_planLabel->setWordWrap(true);
    m_criticalMistakeLabel->setWordWrap(true);
    m_lastPracticalMistakeLabel->setWordWrap(true);
    m_tacticalThemeLabel->setWordWrap(true);
    m_rawEvidenceLabel->setWordWrap(true);
    for (QLabel *label : {m_warningLabel, m_openingLabel, m_strategicErrorLabel, m_planLabel, m_criticalMistakeLabel, m_lastPracticalMistakeLabel, m_tacticalThemeLabel, m_rawEvidenceLabel}) {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1.0);
    m_titleLabel->setFont(titleFont);
    QPalette warningPalette = m_warningLabel->palette();
    warningPalette.setColor(QPalette::WindowText, QColor(140, 92, 16));
    m_warningLabel->setPalette(warningPalette);
    m_rawEvidenceToggle->setCheckable(true);
    m_rawEvidenceToggle->setChecked(false);
    m_rawEvidenceLabel->setVisible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_availabilityLabel);
    layout->addWidget(m_warningLabel);
    auto addSection = [layout, this](const QString &title, QLabel *label) {
        auto *heading = new QLabel(title, this);
        QFont font = heading->font();
        font.setBold(true);
        heading->setFont(font);
        layout->addWidget(heading);
        layout->addWidget(label);
    };
    addSection(QStringLiteral("Opening"), m_openingLabel);
    addSection(QStringLiteral("Strategic error"), m_strategicErrorLabel);
    addSection(QStringLiteral("Plan"), m_planLabel);
    addSection(QStringLiteral("Critical mistake"), m_criticalMistakeLabel);
    addSection(QStringLiteral("Last practical mistake"), m_lastPracticalMistakeLabel);
    addSection(QStringLiteral("Tactical theme"), m_tacticalThemeLabel);
    layout->addWidget(m_rawEvidenceToggle);
    layout->addWidget(m_rawEvidenceLabel);

    connect(m_rawEvidenceToggle, &QPushButton::toggled, this, [this](bool checked) {
        m_rawEvidenceLabel->setVisible(checked);
    });
}

void MetadataCard::setPuzzle(const parlawl::puzzle_runner::PuzzleDefinition &puzzle, int currentIndex, int puzzleCount, const QString &status)
{
    Q_UNUSED(currentIndex)
    m_titleLabel->setText(puzzle.metadata.title.isEmpty() ? puzzle.id : puzzle.metadata.title);
    m_availabilityLabel->setText(QStringLiteral("%1 puzzles available to solve").arg(QString::number(std::max(puzzleCount, 0))));
    m_warningLabel->setText(QStringLiteral("Run Analyze to generate the coach summary for this position."));
    m_openingLabel->setText(QStringLiteral("Awaiting analysis."));
    m_strategicErrorLabel->setText(QStringLiteral("Awaiting analysis."));
    m_planLabel->setText(QStringLiteral("Awaiting analysis."));
    m_criticalMistakeLabel->setText(QStringLiteral("Awaiting analysis."));
    m_lastPracticalMistakeLabel->setText(QStringLiteral("Awaiting analysis."));
    m_tacticalThemeLabel->setText(QStringLiteral("Awaiting analysis."));
    m_rawEvidenceToggle->setChecked(false);
    m_rawEvidenceLabel->clear();
    Q_UNUSED(status)
}

void MetadataCard::setAwaitingAnalysis(const parlawl::puzzle_runner::PuzzleDefinition &puzzle, int currentIndex, int puzzleCount)
{
    setPuzzle(puzzle, currentIndex, puzzleCount, QString());
}

void MetadataCard::setAnalysisSummary(const PuzzleInfoSummary &summary)
{
    m_warningLabel->setVisible(!summary.warnings.isEmpty());
    m_warningLabel->setText(summary.warnings.join(QStringLiteral("  ")));
    m_openingLabel->setText(summary.opening);
    m_strategicErrorLabel->setText(summary.strategicError);
    m_planLabel->setText(summary.plan);
    m_criticalMistakeLabel->setText(summary.criticalMistake);
    m_lastPracticalMistakeLabel->setText(summary.lastPracticalMistake);
    m_tacticalThemeLabel->setText(summary.tacticalTheme);
    m_rawEvidenceToggle->setChecked(false);
    m_rawEvidenceLabel->setText(summary.rawEvidenceText);
}

SettingsCard::SettingsCard(QWidget *parent)
    : QGroupBox(parent)
    , m_autoAdvanceCheck(new QCheckBox(QStringLiteral("auto next"), this))
    , m_difficultyCombo(new QComboBox(this))
    , m_queueSizeCombo(new QComboBox(this))
    , m_refillWhenLowCheck(new QCheckBox(QStringLiteral("refill when low"), this))
    , m_refillThresholdCombo(new QComboBox(this))
    , m_availableToSolveLabel(new QLabel(this))
    , m_supplyStatusLabel(new QLabel(this))
    , m_reloadPuzzlesButton(new QPushButton(QStringLiteral("Reload puzzles"), this))
    , m_keepRecentRunsCombo(new QComboBox(this))
    , m_preserveAnalyzedCheck(new QCheckBox(QStringLiteral("preserve analyzed"), this))
    , m_cleanupButton(new QPushButton(QStringLiteral("Cleanup now"), this))
{
    m_difficultyCombo->addItems({QStringLiteral("medium"), QStringLiteral("hard")});
    m_queueSizeCombo->addItems({QStringLiteral("10"), QStringLiteral("20")});
    m_refillThresholdCombo->addItems({QStringLiteral("2"), QStringLiteral("5"), QStringLiteral("10")});
    m_keepRecentRunsCombo->addItems({QStringLiteral("25"), QStringLiteral("50"), QStringLiteral("100"), QStringLiteral("250")});

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    auto *trainerLabel = new QLabel(QStringLiteral("trainer"), this);
    QFont sectionFont = trainerLabel->font();
    sectionFont.setBold(true);
    trainerLabel->setFont(sectionFont);
    layout->addWidget(trainerLabel);
    layout->addWidget(m_autoAdvanceCheck);
    layout->addWidget(new QLabel(QStringLiteral("difficulty"), this));
    layout->addWidget(m_difficultyCombo);
    auto *note = new QLabel(QStringLiteral("These settings apply when you click Reload puzzles."), this);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto *supplyLabel = new QLabel(QStringLiteral("puzzle supply"), this);
    supplyLabel->setFont(sectionFont);
    layout->addWidget(supplyLabel);
    layout->addWidget(new QLabel(QStringLiteral("queue size"), this));
    layout->addWidget(m_queueSizeCombo);
    layout->addWidget(m_refillWhenLowCheck);
    layout->addWidget(new QLabel(QStringLiteral("refill threshold"), this));
    layout->addWidget(m_refillThresholdCombo);
    m_availableToSolveLabel->setWordWrap(true);
    m_supplyStatusLabel->setWordWrap(true);
    QPalette supplyPalette = m_supplyStatusLabel->palette();
    supplyPalette.setColor(QPalette::WindowText, QColor(92, 92, 92));
    m_supplyStatusLabel->setPalette(supplyPalette);
    layout->addWidget(m_availableToSolveLabel);
    layout->addWidget(m_supplyStatusLabel);
    layout->addWidget(m_reloadPuzzlesButton);
    auto *supplyNote = new QLabel(QStringLiteral("Reload uses the live Lichess API. Difficulty and queue size do not auto-fetch."), this);
    supplyNote->setWordWrap(true);
    layout->addWidget(supplyNote);

    auto *retentionLabel = new QLabel(QStringLiteral("cleanup"), this);
    retentionLabel->setFont(sectionFont);
    layout->addWidget(retentionLabel);
    layout->addWidget(new QLabel(QStringLiteral("keep recent runs"), this));
    layout->addWidget(m_keepRecentRunsCombo);
    layout->addWidget(m_preserveAnalyzedCheck);
    layout->addWidget(m_cleanupButton);

    connect(m_autoAdvanceCheck, &QCheckBox::toggled, this, &SettingsCard::autoAdvanceChanged);
    connect(m_difficultyCombo, &QComboBox::currentTextChanged, this, &SettingsCard::difficultyChanged);
    connect(m_queueSizeCombo, &QComboBox::currentTextChanged, this, &SettingsCard::queueSizeChanged);
    connect(m_refillWhenLowCheck, &QCheckBox::toggled, this, &SettingsCard::refillWhenLowChanged);
    connect(m_refillThresholdCombo, &QComboBox::currentTextChanged, this, &SettingsCard::refillThresholdChanged);
    connect(m_reloadPuzzlesButton, &QPushButton::clicked, this, &SettingsCard::reloadPuzzlesRequested);
    connect(m_keepRecentRunsCombo, &QComboBox::currentTextChanged, this, &SettingsCard::keepRecentRunsChanged);
    connect(m_preserveAnalyzedCheck, &QCheckBox::toggled, this, &SettingsCard::preserveAnalyzedChanged);
    connect(m_cleanupButton, &QPushButton::clicked, this, &SettingsCard::cleanupRequested);
}

void SettingsCard::setSettings(bool autoAdvance, const QString &difficulty)
{
    const QSignalBlocker autoAdvanceBlocker(m_autoAdvanceCheck);
    const QSignalBlocker difficultyBlocker(m_difficultyCombo);
    m_autoAdvanceCheck->setChecked(autoAdvance);
    const int difficultyIndex = m_difficultyCombo->findText(difficulty);
    if (difficultyIndex >= 0) {
        m_difficultyCombo->setCurrentIndex(difficultyIndex);
    } else {
        m_difficultyCombo->setCurrentText(QStringLiteral("hard"));
    }
}

void SettingsCard::setSupplySettings(const QString &queueSize, bool refillWhenLow, const QString &refillThreshold)
{
    const QSignalBlocker queueBlocker(m_queueSizeCombo);
    const QSignalBlocker refillBlocker(m_refillWhenLowCheck);
    const QSignalBlocker thresholdBlocker(m_refillThresholdCombo);
    const int queueIndex = m_queueSizeCombo->findText(queueSize);
    if (queueIndex >= 0) {
        m_queueSizeCombo->setCurrentIndex(queueIndex);
    }
    m_refillWhenLowCheck->setChecked(refillWhenLow);
    const int thresholdIndex = m_refillThresholdCombo->findText(refillThreshold);
    if (thresholdIndex >= 0) {
        m_refillThresholdCombo->setCurrentIndex(thresholdIndex);
    }
}

void SettingsCard::setRetentionSettings(const QString &keepRecentRuns, bool preserveAnalyzed)
{
    const QSignalBlocker keepBlocker(m_keepRecentRunsCombo);
    const QSignalBlocker analyzedBlocker(m_preserveAnalyzedCheck);
    const int keepIndex = m_keepRecentRunsCombo->findText(keepRecentRuns);
    if (keepIndex >= 0) {
        m_keepRecentRunsCombo->setCurrentIndex(keepIndex);
    }
    m_preserveAnalyzedCheck->setChecked(preserveAnalyzed);
}

void SettingsCard::setAvailablePuzzleCount(int availableCount)
{
    m_availableToSolveLabel->setText(
        QStringLiteral("%1 puzzles available to solve").arg(QString::number(std::max(availableCount, 0))));
}

void SettingsCard::setSupplyStatusText(const QString &statusText)
{
    m_supplyStatusLabel->setText(statusText);
}

QString SettingsCard::supplyStatusText() const
{
    return m_supplyStatusLabel->text();
}

TransportControls::TransportControls(QWidget *parent)
    : QWidget(parent)
    , m_previousButton(new QPushButton(QStringLiteral("Prev"), this))
    , m_nextButton(new QPushButton(QStringLiteral("Next"), this))
    , m_retryButton(new QPushButton(QStringLiteral("Retry"), this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_previousButton);
    layout->addWidget(m_nextButton);
    layout->addWidget(m_retryButton);
    m_retryButton->setToolTip(QStringLiteral("reset the current puzzle attempt to the starting position"));

    connect(m_previousButton, &QPushButton::clicked, this, &TransportControls::previousRequested);
    connect(m_nextButton, &QPushButton::clicked, this, &TransportControls::nextRequested);
    connect(m_retryButton, &QPushButton::clicked, this, &TransportControls::retryRequested);
}

void TransportControls::setEnabledState(
    bool reviewMode,
    bool canStepBackward,
    bool canStepForward,
    bool canGoToPreviousPuzzle,
    bool canGoToNextPuzzle)
{
    if (reviewMode) {
        m_previousButton->setEnabled(canStepBackward);
        m_nextButton->setEnabled(canStepForward);
        m_previousButton->setToolTip(QStringLiteral("step backward through move history"));
        m_nextButton->setToolTip(QStringLiteral("step forward through move history"));
        return;
    }

    m_previousButton->setEnabled(canGoToPreviousPuzzle);
    m_nextButton->setEnabled(canGoToNextPuzzle);
    m_previousButton->setToolTip(QStringLiteral("load the previous puzzle"));
    m_nextButton->setToolTip(QStringLiteral("load the next puzzle"));
}

EnginePanel::EnginePanel(QWidget *parent)
    : QGroupBox(parent)
    , m_statusLabel(new QLabel(this))
    , m_evaluationLabel(new QLabel(this))
    , m_bestMoveLabel(new QLabel(this))
    , m_pvLabel(new QLabel(this))
    , m_refreshButton(new QPushButton(QStringLiteral("Refresh"), this))
    , m_autoRefreshCheck(new QCheckBox(QStringLiteral("auto refresh"), this))
{
    m_statusLabel->setWordWrap(true);
    m_evaluationLabel->setWordWrap(true);
    m_bestMoveLabel->setWordWrap(true);
    m_pvLabel->setWordWrap(true);
    auto *layout = new QVBoxLayout(this);
    auto *controlsRow = new QHBoxLayout();
    controlsRow->setContentsMargins(0, 0, 0, 0);
    controlsRow->setSpacing(6);
    m_refreshButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_refreshButton->setMaximumHeight(24);
    m_refreshButton->setStyleSheet(QStringLiteral(
        "QPushButton { padding: 1px 8px; border-radius: 10px; min-height: 18px; }"
    ));
    controlsRow->addWidget(m_refreshButton);
    controlsRow->addWidget(m_autoRefreshCheck);
    controlsRow->addStretch(1);
    layout->addLayout(controlsRow);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_evaluationLabel);
    layout->addWidget(m_bestMoveLabel);
    layout->addWidget(m_pvLabel);

    connect(m_refreshButton, &QPushButton::clicked, this, &EnginePanel::refreshRequested);
    connect(m_autoRefreshCheck, &QCheckBox::toggled, this, &EnginePanel::autoRefreshChanged);
}

void EnginePanel::setReviewState(
    const QString &statusText,
    const QString &evaluationText,
    const QString &bestMoveText,
    const QString &pvText,
    bool canRefresh,
    bool autoRefresh,
    bool reviewInProgress)
{
    m_statusLabel->setText(statusText);
    m_evaluationLabel->setText(QStringLiteral("eval: %1").arg(evaluationText));
    m_bestMoveLabel->setText(QStringLiteral("best move: %1").arg(bestMoveText));
    m_pvLabel->setText(QStringLiteral("pv: %1").arg(pvText));
    m_refreshButton->setEnabled(canRefresh && !reviewInProgress);
    m_autoRefreshCheck->blockSignals(true);
    m_autoRefreshCheck->setChecked(autoRefresh);
    m_autoRefreshCheck->blockSignals(false);
}
