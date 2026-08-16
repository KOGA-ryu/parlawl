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
#include <QTextEdit>
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

QString percentageText(int millionths)
{
    QString text = QString::number(static_cast<double>(millionths) / 10000.0, 'f', 4);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    return text + QLatin1Char('%');
}

QString replayOpeningText(const parlawl::puzzle_runner::AnnotatedReplayPack &pack)
{
    if (pack.openingStatus() != QStringLiteral("classified")) {
        return QStringLiteral("Supplied opening annotation (not verified): %1").arg(pack.openingStatus());
    }
    const QString eco = pack.openingEco().value_or(QString());
    const QString name = pack.openingName().value_or(QStringLiteral("unnamed exact-position match"));
    return eco.isEmpty()
        ? QStringLiteral("Supplied opening annotation (not verified): %1").arg(name)
        : QStringLiteral("Supplied opening annotation (not verified): %1 %2").arg(eco, name);
}

} // namespace

MoveListPanel::MoveListPanel(QWidget *parent)
    : QGroupBox(QStringLiteral("move list"), parent)
    , m_truthStatusLabel(new QLabel(this))
    , m_table(new QTableWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    m_truthStatusLabel->setTextFormat(Qt::PlainText);
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
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (!m_showingAnnotatedReplay || column < 1 || column > 2) {
            return;
        }
        const int ply = row * 2 + (column == 1 ? 1 : 2);
        if (ply >= 1 && ply <= m_replayMoveCount) {
            emit replayPlyRequested(ply);
        }
    });
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
    m_showingAnnotatedReplay = false;
    m_replayMoveCount = 0;
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

void MoveListPanel::setAnnotatedReplay(
    const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
    int currentMainlinePly,
    bool variationActive,
    int variationAnchorPly)
{
    m_showingAnnotatedReplay = true;
    m_replayMoveCount = pack.moves().size();
    m_table->clearContents();
    const int rowCount = std::max(1, (m_replayMoveCount + 1) / 2);
    m_table->setRowCount(rowCount);
    m_truthStatusLabel->setText(
        variationActive
            ? QStringLiteral("Supplied engine line active: moves are legally checked, but engine claims are not verified. It was not played; Return restores the real game.")
            : QStringLiteral("Legal move/FEN replay verified. Severity labels and derived annotations are supplied and not verified by ParlAWL."));

    auto makeItem = [&](const QString &text, bool isCurrent, bool isVariationAnchor) {
        auto *item = new QTableWidgetItem(text);
        item->setFlags((item->flags() | Qt::ItemIsSelectable) & ~Qt::ItemIsEditable);
        if (isCurrent) {
            item->setBackground(QColor(222, 235, 255));
            item->setForeground(QColor(24, 54, 90));
        } else if (isVariationAnchor) {
            item->setBackground(QColor(255, 241, 194));
            item->setForeground(QColor(90, 62, 12));
        }
        return item;
    };

    for (int row = 0; row < rowCount; ++row) {
        m_table->setItem(row, 0, makeItem(QString::number(row + 1), false, false));
    }
    for (const auto &move : pack.moves()) {
        const int row = (move.ply - 1) / 2;
        const int column = move.ply % 2 == 1 ? 1 : 2;
        QString text = move.notation.san;
        if (move.severity != QStringLiteral("none")) {
            text += QStringLiteral("  [%1]").arg(move.severity);
        }
        const bool current = !variationActive && currentMainlinePly == move.ply;
        const bool anchor = variationActive && variationAnchorPly == move.ply;
        m_table->setItem(row, column, makeItem(text, current, anchor));
    }
}

ReplayEvidencePanel::ReplayEvidencePanel(QWidget *parent)
    : QGroupBox(QStringLiteral("analysis replay"), parent)
    , m_gameLabel(new QLabel(this))
    , m_openingLabel(new QLabel(this))
    , m_engineLabel(new QLabel(this))
    , m_summaryView(new QTextEdit(this))
    , m_openButton(new QPushButton(QStringLiteral("Open Analysis Replay"), this))
    , m_backButton(new QPushButton(QStringLiteral("Back to Puzzles"), this))
    , m_showEngineLineButton(new QPushButton(QStringLiteral("Show Supplied Engine Line"), this))
    , m_returnToGameButton(new QPushButton(QStringLiteral("Return to Game"), this))
{
    auto *layout = new QVBoxLayout(this);
    auto *actions = new QHBoxLayout();
    actions->addWidget(m_openButton);
    actions->addWidget(m_backButton);
    actions->addStretch(1);
    layout->addLayout(actions);
    for (QLabel *label : {m_gameLabel, m_openingLabel, m_engineLabel}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
    }
    layout->addWidget(m_gameLabel);
    layout->addWidget(m_openingLabel);
    layout->addWidget(m_engineLabel);
    m_summaryView->setReadOnly(true);
    layout->addWidget(m_summaryView, 1);
    auto *variationActions = new QHBoxLayout();
    variationActions->addWidget(m_showEngineLineButton);
    variationActions->addWidget(m_returnToGameButton);
    variationActions->addStretch(1);
    layout->addLayout(variationActions);

    connect(m_openButton, &QPushButton::clicked, this, &ReplayEvidencePanel::openReplayRequested);
    connect(m_backButton, &QPushButton::clicked, this, &ReplayEvidencePanel::backToPuzzlesRequested);
    connect(m_showEngineLineButton, &QPushButton::clicked, this, &ReplayEvidencePanel::showEngineLineRequested);
    connect(m_returnToGameButton, &QPushButton::clicked, this, &ReplayEvidencePanel::returnToGameRequested);
    setEmptyState();
}

void ReplayEvidencePanel::setEmptyState()
{
    m_gameLabel->setText(QStringLiteral("No annotated replay loaded."));
    m_openingLabel->clear();
    m_engineLabel->setText(QStringLiteral("Import is read-only and does not run Stockfish or use the network."));
    m_summaryView->setPlainText(
        QStringLiteral("Open an annotated-game-replay-v1 JSON file produced by the esports evidence pipeline."));
    m_backButton->setEnabled(false);
    m_showEngineLineButton->setEnabled(false);
    m_returnToGameButton->setEnabled(false);
}

void ReplayEvidencePanel::setReplayState(
    const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
    const parlawl::puzzle_runner::ReplaySession &session,
    int variationAnchorPly)
{
    m_gameLabel->setText(
        QStringLiteral("%1 vs %2 — %3").arg(pack.whiteUsername(), pack.blackUsername(), pack.result()));
    m_openingLabel->setText(replayOpeningText(pack));
    m_engineLabel->setText(
        QStringLiteral("Supplied engine metadata (not verified): %1 — %2 nodes — config %3")
            .arg(pack.sourceEngineName(), QString::number(pack.sourceEngineNodeLimit()), pack.sourceEngineConfigId()));

    QStringList lines;
    if (session.inVariation()) {
        const auto *variation = pack.preferredVariation(variationAnchorPly);
        lines << QStringLiteral("SUPPLIED ENGINE LINE, NOT PLAYED")
              << QStringLiteral("Move legality and exact return are checked; engine provenance and optimality are not verified by ParlAWL.");
        if (variation != nullptr) {
            lines << QStringLiteral("Alternative to ply %1. Supplied preferred move: %2.")
                         .arg(variation->anchorPly)
                         .arg(variation->reportedBestMoveUci);
            const int localPly = session.currentVariationPly();
            if (localPly == 0) {
                lines << QStringLiteral("At the exact pre-move checkpoint.");
            } else if (localPly <= variation->displayedSteps.size()) {
                const auto &step = variation->displayedSteps.at(localPly - 1);
                lines << QStringLiteral("Variation step %1: %2 (%3)")
                             .arg(step.localPly)
                             .arg(step.san, step.uci);
            }
            lines << QStringLiteral("Return to Game discards this branch and restores the recorded move exactly.");
        }
    } else if (session.currentMainlinePly() == 0) {
        lines << QStringLiteral("Start position")
              << QStringLiteral("Use Next, the mouse wheel, or the move list to inspect the recorded game.");
    } else {
        const auto &move = pack.moves().at(session.currentMainlinePly() - 1);
        lines << QStringLiteral("Ply %1: %2 (%3)").arg(move.ply).arg(move.notation.san, move.notation.uci)
              << QStringLiteral("Supplied severity (not verified): %1").arg(move.severity)
              << QStringLiteral("Supplied derived mover expectation loss (not verified): %1").arg(percentageText(move.wdlLossMillionths));
        if (!move.narration.isEmpty()) {
            lines << QString() << QStringLiteral("Supplied explanation (not verified)");
            for (const auto &item : move.narration) {
                lines << QStringLiteral("• %1").arg(item.text);
            }
        }
        if (!move.facts.isEmpty()) {
            lines << QString() << QStringLiteral("Supplied derived annotations (not verified)");
            for (const auto &fact : move.facts) {
                lines << QStringLiteral("• [%1] %2").arg(fact.authority, fact.summary);
            }
        }
        if (move.preferredVariation.has_value()) {
            lines << QString() << QStringLiteral("A supplied engine line is available. Its moves and return checkpoint are legally checked; its engine claim is not verified. It was not played.");
        } else if (!move.alternativeUnavailableReason.value_or(QString()).isEmpty()) {
            lines << QString() << QStringLiteral("Engine line unavailable: %1")
                                      .arg(move.alternativeUnavailableReason.value());
        }
    }

    m_summaryView->setPlainText(lines.join(QLatin1Char('\n')));
    m_backButton->setEnabled(true);
    const bool canShow = !session.inVariation()
        && session.currentMainlinePly() > 0
        && pack.preferredVariation(session.currentMainlinePly()) != nullptr;
    m_showEngineLineButton->setEnabled(canShow);
    m_returnToGameButton->setEnabled(session.inVariation());
}

QString ReplayEvidencePanel::summaryText() const
{
    return m_summaryView->toPlainText();
}

bool ReplayEvidencePanel::canShowEngineLine() const
{
    return m_showEngineLineButton->isEnabled();
}

bool ReplayEvidencePanel::canReturnToGame() const
{
    return m_returnToGameButton->isEnabled();
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
    const QList<QLabel *> dynamicLabels{
        m_titleLabel,
        m_availabilityLabel,
        m_warningLabel,
        m_openingLabel,
        m_strategicErrorLabel,
        m_planLabel,
        m_criticalMistakeLabel,
        m_lastPracticalMistakeLabel,
        m_tacticalThemeLabel,
        m_rawEvidenceLabel,
    };
    for (QLabel *label : dynamicLabels) {
        label->setWordWrap(true);
        label->setTextFormat(Qt::PlainText);
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
    const bool importedValidatedLine = parlawl::puzzle_runner::isImportedEngineRecord(puzzle);
    m_titleLabel->setText(importedValidatedLine
            ? parlawl::puzzle_runner::importedEngineRecordTitle()
            : (puzzle.metadata.title.isEmpty() ? puzzle.id : puzzle.metadata.title));
    const QString provider = puzzle.analysisSeed.sourceProvider.trimmed().isEmpty()
        ? QStringLiteral("unknown provider")
        : puzzle.analysisSeed.sourceProvider.trimmed();
    const QString importedTrustLabel = parlawl::puzzle_runner::importedEngineRecordSourceLabel(provider);
    m_availabilityLabel->setText(importedValidatedLine
            ? QStringLiteral("%1 puzzles available to solve\n%2")
                  .arg(QString::number(std::max(puzzleCount, 0)), importedTrustLabel)
            : QStringLiteral("%1 puzzles available to solve").arg(QString::number(std::max(puzzleCount, 0))));
    m_warningLabel->setVisible(true);
    m_warningLabel->setText(importedValidatedLine
            ? importedTrustLabel
            : QStringLiteral("Run Analyze to generate the coach summary for this position."));
    m_openingLabel->setText(QStringLiteral("Awaiting analysis."));
    m_strategicErrorLabel->setText(QStringLiteral("Awaiting analysis."));
    m_planLabel->setText(QStringLiteral("Awaiting analysis."));
    m_criticalMistakeLabel->setText(QStringLiteral("Awaiting analysis."));
    m_lastPracticalMistakeLabel->setText(QStringLiteral("Awaiting analysis."));
    m_tacticalThemeLabel->setText(importedValidatedLine && !puzzle.metadata.themes.isEmpty()
            ? QStringLiteral("Supplied by imported record (not independently verified): %1")
                  .arg(puzzle.metadata.themes.join(QStringLiteral(", ")))
            : QStringLiteral("Awaiting analysis."));
    m_rawEvidenceToggle->setChecked(false);
    m_rawEvidenceLabel->setText(importedValidatedLine
            ? puzzle.analysisSeed.rawSourceRecordJson
            : QString());
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
    , m_openValidatedPuzzlePackButton(new QPushButton(QStringLiteral("Import Engine-Line Pack"), this))
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
    m_availableToSolveLabel->setTextFormat(Qt::PlainText);
    m_supplyStatusLabel->setTextFormat(Qt::PlainText);
    QPalette supplyPalette = m_supplyStatusLabel->palette();
    supplyPalette.setColor(QPalette::WindowText, QColor(92, 92, 92));
    m_supplyStatusLabel->setPalette(supplyPalette);
    layout->addWidget(m_availableToSolveLabel);
    layout->addWidget(m_supplyStatusLabel);
    layout->addWidget(m_openValidatedPuzzlePackButton);
    layout->addWidget(m_reloadPuzzlesButton);
    auto *supplyNote = new QLabel(QStringLiteral(
        "Import loads offline JSONL records that declare engine_validated. ParlAWL checks the records and "
        "legal move lines without authenticating the producer or rerunning the engine. Remote top-up turns off; "
        "Reload uses the live Lichess API."), this);
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
    connect(m_openValidatedPuzzlePackButton, &QPushButton::clicked, this, &SettingsCard::openValidatedPuzzlePackRequested);
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

void TransportControls::setReplayMode(bool enabled)
{
    m_retryButton->setText(enabled ? QStringLiteral("Start") : QStringLiteral("Retry"));
    m_retryButton->setToolTip(
        enabled
            ? QStringLiteral("return to the annotated game's start position")
            : QStringLiteral("reset the current puzzle attempt to the starting position"));
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
    for (QLabel *label : {m_statusLabel, m_evaluationLabel, m_bestMoveLabel, m_pvLabel}) {
        label->setWordWrap(true);
        label->setTextFormat(Qt::PlainText);
    }
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
