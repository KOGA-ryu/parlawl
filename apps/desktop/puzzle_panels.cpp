#include "puzzle_panels.h"

#include <algorithm>

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
#include <QSplitter>
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

QString engineScoreText(
    const QString &kind,
    const std::optional<qint64> &centipawnsWhite,
    const std::optional<qint64> &mateForWhite)
{
    if (kind == QStringLiteral("cp") && centipawnsWhite.has_value()) {
        const double pawns = static_cast<double>(*centipawnsWhite) / 100.0;
        return QStringLiteral("%1%2 (White)")
            .arg(pawns >= 0.0 ? QStringLiteral("+") : QString())
            .arg(QString::number(pawns, 'f', 2));
    }
    if (kind == QStringLiteral("mate") && mateForWhite.has_value()) {
        return QStringLiteral("mate %1%2 (White)")
            .arg(*mateForWhite > 0 ? QStringLiteral("+") : QString())
            .arg(*mateForWhite);
    }
    if (kind == QStringLiteral("terminal_mate")) {
        return QStringLiteral("checkmate");
    }
    if (kind == QStringLiteral("terminal_draw")) {
        return QStringLiteral("draw");
    }
    return QStringLiteral("N/A");
}

QString engineWdlText(const QVector<int> &wdlWhite)
{
    if (wdlWhite.size() != 3) {
        return QStringLiteral("W/D/L unavailable");
    }
    return QStringLiteral("White W/D/L %1/%2/%3‰")
        .arg(wdlWhite.at(0))
        .arg(wdlWhite.at(1))
        .arg(wdlWhite.at(2));
}

QString moveTimeText(const std::optional<qint64> &milliseconds)
{
    if (!milliseconds.has_value()) {
        return QStringLiteral("time N/A");
    }
    return QStringLiteral("%1s").arg(
        QString::number(static_cast<double>(*milliseconds) / 1000.0, 'f', 1));
}

QString clockText(const std::optional<qint64> &milliseconds)
{
    if (!milliseconds.has_value()) {
        return QStringLiteral("N/A");
    }
    const qint64 minutes = *milliseconds / 60'000;
    const double seconds = static_cast<double>(*milliseconds % 60'000) / 1000.0;
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 4, 'f', 1, QLatin1Char('0'));
}

QString humanizedToken(QString value)
{
    value.replace(QLatin1Char('_'), QLatin1Char(' '));
    value.replace(QLatin1Char('-'), QLatin1Char(' '));
    if (!value.isEmpty()) {
        value[0] = value.at(0).toUpper();
    }
    return value;
}

QString replayOpeningText(const parlawl::puzzle_runner::AnnotatedReplayPack &pack)
{
    if (pack.isMechanicalGameBreakdown()) {
        QString label;
        if (pack.openingStatus() == QStringLiteral("classified")) {
            label = pack.openingEco().value_or(QString()) + QLatin1Char(' ')
                + pack.openingName().value_or(QStringLiteral("unnamed opening"));
        } else {
            label = humanizedToken(pack.openingStatus());
        }
        label = label.trimmed();
        if (!pack.openingLastBookPly().has_value()) {
            return QStringLiteral("Pinned opening label: %1 · exact book boundary unavailable")
                .arg(label);
        }
        const int bookPly = *pack.openingLastBookPly();
        if (bookPly >= pack.moves().size()) {
            return QStringLiteral("Pinned opening label: %1 · recorded game stayed on the known path through ply %2")
                .arg(label)
                .arg(bookPly);
        }
        return QStringLiteral("Pinned opening label: %1 · known path through ply %2 · first departure ply %3")
            .arg(label)
            .arg(bookPly)
            .arg(bookPly + 1);
    }
    if (pack.openingStatus() != QStringLiteral("classified")) {
        return QStringLiteral("Supplied opening annotation (not verified): %1").arg(pack.openingStatus());
    }
    const QString eco = pack.openingEco().value_or(QString());
    const QString name = pack.openingName().value_or(QStringLiteral("unnamed exact-position match"));
    return eco.isEmpty()
        ? QStringLiteral("Supplied opening annotation (not verified): %1").arg(name)
        : QStringLiteral("Supplied opening annotation (not verified): %1 %2").arg(eco, name);
}

QString deepLineSummary(const parlawl::puzzle_runner::SelectiveDeepEngineLine &line)
{
    const QString pv = line.pvUci.isEmpty()
        ? QStringLiteral("PV unavailable") : line.pvUci.join(QLatin1Char(' '));
    return QStringLiteral("%1 · %2 · depth %3/%4 · %5 nodes · PV %6")
        .arg(
            line.rootMoveUci,
            engineScoreText(
                line.scoreKind,
                line.centipawnsWhite,
                line.mateForWhite))
        .arg(line.depth)
        .arg(line.selectiveDepth)
        .arg(line.nodes)
        .arg(pv);
}

QStringList selectiveDeepReportLines(
    const parlawl::puzzle_runner::AnnotatedReplayPack &pack)
{
    if (!pack.selectiveDeepReview().has_value()) {
        return {};
    }
    const auto &review = *pack.selectiveDeepReview();
    QHash<QString, int> statusCounts;
    for (const auto &moment : review.moments) {
        ++statusCounts[moment.status];
    }

    QStringList lines;
    lines << QStringLiteral("DEEP SELECTIVE REVIEW · %1-NODE POLICY")
                 .arg(review.nodeLimit)
          << QStringLiteral("%1 outcome-blind selected moment%2 · %3 other recorded moves were not selected for deep assessment.")
                 .arg(review.moments.size())
                 .arg(review.moments.size() == 1 ? QString() : QStringLiteral("s"))
                 .arg(pack.moves().size() - review.moments.size())
          << QStringLiteral("Deep statuses: confirmed severe %1 · confirmed missed opportunity %2 · ambiguous %3 · below threshold %4")
                 .arg(statusCounts.value(QStringLiteral("confirmed_severe_error")))
                 .arg(statusCounts.value(QStringLiteral("confirmed_missed_opportunity")))
                 .arg(statusCounts.value(QStringLiteral("ambiguous_engine_instability")))
                 .arg(statusCounts.value(QStringLiteral("below_confirmation_threshold")))
          << QStringLiteral("Selected moments");
    for (const auto &moment : review.moments) {
        const QString player = moment.mover == QStringLiteral("white")
            ? pack.whiteUsername() : pack.blackUsername();
        const QString classification = moment.severity.has_value()
            ? humanizedToken(*moment.severity)
            : humanizedToken(moment.status);
        const QString loss = moment.wdlLossMillionths.has_value()
            ? percentageText(*moment.wdlLossMillionths)
                + QStringLiteral(" mover expectation loss")
            : QStringLiteral("no stable loss published");
        lines << QStringLiteral("%1. Ply %2 %3 — %4 — %5 — %6 · played %7 · deep best %8")
                     .arg(moment.presentationOrder)
                     .arg(moment.ply)
                     .arg(moment.san)
                     .arg(player)
                     .arg(classification)
                     .arg(loss)
                     .arg(moment.playedMoveUci)
                     .arg(moment.bestMoveUci.value_or(QStringLiteral("unavailable")));
        if (!moment.alternativeLines.isEmpty()) {
            lines << QStringLiteral("   Rank 1 alternative, not played: %1")
                         .arg(deepLineSummary(moment.alternativeLines.first()));
        }
        if (moment.playedLine.has_value()) {
            lines << QStringLiteral("   Played-move constrained line: %1")
                         .arg(deepLineSummary(*moment.playedLine));
        }
    }
    lines << QStringLiteral(
        "Selection used no result or postgame rating. Unselected moves are not certified accurate. ParlAWL exact-joined this retained Report v2 to the legal mainline; it did not rerun the producer's sources or Stockfish.");
    return lines;
}

QStringList mechanicalGameReportLines(
    const parlawl::puzzle_runner::AnnotatedReplayPack &pack)
{
    if (pack.moves().isEmpty()
        || (!pack.persistedEngineEvidence().has_value()
            && !pack.selectiveDeepReview().has_value())) {
        return {};
    }

    QStringList lines = selectiveDeepReportLines(pack);
    if (!lines.isEmpty()) {
        lines << QString();
    }
    if (!pack.persistedEngineEvidence().has_value()) {
        return lines;
    }

    QVector<const parlawl::puzzle_runner::ReplayMove *> coveredMoves;
    int severeCount = 0;
    int mistakeCount = 0;
    int inaccuracyCount = 0;
    int missedWinCount = 0;
    int missedMateCount = 0;
    for (const auto &move : pack.moves()) {
        if (!move.persistedEngineEvidence.has_value()) {
            continue;
        }
        coveredMoves.append(&move);
        const auto &engine = *move.persistedEngineEvidence;
        if (engine.severity == QStringLiteral("severe")) {
            ++severeCount;
        } else if (engine.severity == QStringLiteral("mistake")) {
            ++mistakeCount;
        } else if (engine.severity == QStringLiteral("inaccuracy")) {
            ++inaccuracyCount;
        }
        if (engine.missedWinningAdvantage) {
            ++missedWinCount;
        }
        if (engine.missedForcedMate) {
            ++missedMateCount;
        }
    }

    const auto *firstCoveredMove = coveredMoves.first();
    const auto *lastCoveredMove = coveredMoves.last();

    std::sort(
        coveredMoves.begin(),
        coveredMoves.end(),
        [](const auto *left, const auto *right) {
            const int leftLoss = left->persistedEngineEvidence->wdlLossMillionths;
            const int rightLoss = right->persistedEngineEvidence->wdlLossMillionths;
            return leftLoss != rightLoss ? leftLoss > rightLoss : left->ply < right->ply;
        });

    lines << QStringLiteral("GAME REPORT V1 · RETROSPECTIVE MECHANICAL SUMMARY")
          << QStringLiteral("Shallow screen coverage: persisted fixed-node evidence for %1/%2 recorded moves.")
                 .arg(coveredMoves.size())
                 .arg(pack.moves().size())
          << QStringLiteral("Shallow candidate labels: Severe %1 · Mistake %2 · Inaccuracy %3")
                 .arg(severeCount)
                 .arg(mistakeCount)
                 .arg(inaccuracyCount)
          << QStringLiteral("Shallow candidate events: lost winning advantage %1 · lost forced mate %2")
                 .arg(missedWinCount)
                 .arg(missedMateCount);

    const auto &firstEngine = *firstCoveredMove->persistedEngineEvidence;
    const auto &lastEngine = *lastCoveredMove->persistedEngineEvidence;
    lines << QStringLiteral("Recorded evaluation span: %1 before the first move · %2 after the final recorded move.")
                 .arg(
                     engineScoreText(
                         firstEngine.beforeScoreKind,
                         firstEngine.beforeCentipawnsWhite,
                         firstEngine.beforeMateForWhite),
                     engineScoreText(
                         lastEngine.afterScoreKind,
                         lastEngine.afterCentipawnsWhite,
                         lastEngine.afterMateForWhite))
          << (pack.selectiveDeepReview().has_value()
                ? QStringLiteral("The deep-selected list above supersedes a shallow-only critical-moment ranking.")
                : QStringLiteral("Largest shallow-screen mover-expectation losses"));

    const qsizetype maximumMomentCount = pack.selectiveDeepReview().has_value()
        ? 0 : std::min<qsizetype>(5, coveredMoves.size());
    int emittedMomentCount = 0;
    for (qsizetype index = 0; index < maximumMomentCount; ++index) {
        const auto &move = *coveredMoves.at(index);
        const auto &engine = *move.persistedEngineEvidence;
        if (engine.wdlLossMillionths <= 0) {
            break;
        }
        const QString mover = move.ply % 2 == 1
            ? pack.whiteUsername() : pack.blackUsername();
        const QString bestMove = engine.beforeBestMoveUci.value_or(
            QStringLiteral("unavailable"));
        lines << QStringLiteral(
                     "%1. Ply %2 %3 — %4 — %5 — %6 mover expectation loss · %7 → %8 · %9 · best %10")
                     .arg(++emittedMomentCount)
                     .arg(move.ply)
                     .arg(move.notation.san)
                     .arg(mover)
                     .arg(humanizedToken(engine.severity))
                     .arg(percentageText(engine.wdlLossMillionths))
                     .arg(engineScoreText(
                         engine.beforeScoreKind,
                         engine.beforeCentipawnsWhite,
                         engine.beforeMateForWhite))
                     .arg(engineScoreText(
                         engine.afterScoreKind,
                         engine.afterCentipawnsWhite,
                         engine.afterMateForWhite))
                     .arg(QStringLiteral("%1 · %2")
                         .arg(humanizedToken(move.positionPhase), moveTimeText(move.elapsedMoveMs)))
                     .arg(bestMove);
    }
    if (emittedMomentCount == 0 && !pack.selectiveDeepReview().has_value()) {
        lines << QStringLiteral("No positive mover-expectation loss was recorded.");
    }
    lines << QStringLiteral(
        "Shallow labels use only the frozen mover WDL-loss measurement. They are screening candidates, not deep verdicts, and do not prove cause, intent, or a unique best move.");
    return lines;
}

QString mechanicalSummaryHtml(const QStringList &lines)
{
    QString html = QStringLiteral(
        "<html><body style='color: palette(text);'>");
    for (const QString &line : lines) {
        const QString safe = line.toHtmlEscaped();
        if (line.isEmpty()) {
            html += QStringLiteral("<div style='height: 8px'></div>");
        } else if (line.startsWith(QStringLiteral("START POSITION"))
                   || line.startsWith(QStringLiteral("PLY "))
                   || line.startsWith(QStringLiteral("RECORDED FINISH"))) {
            html += QStringLiteral(
                        "<div style='font-size: 15px; font-weight: 700; margin: 3px 0 8px 0;'>%1</div>")
                        .arg(safe);
        } else if (line.startsWith(QStringLiteral("No best-move"))) {
            html += QStringLiteral(
                        "<div style='margin-top: 8px; padding: 8px; background: #342f24; color: #e8cf93;'>%1</div>")
                        .arg(safe);
        } else {
            const qsizetype separator = line.indexOf(QStringLiteral(": "));
            if (separator > 0) {
                html += QStringLiteral("<div style='margin: 3px 0;'><b>%1:</b> %2</div>")
                            .arg(line.left(separator).toHtmlEscaped(),
                                 line.mid(separator + 2).toHtmlEscaped());
            } else {
                html += QStringLiteral("<div style='margin: 3px 0;'>%1</div>").arg(safe);
            }
        }
    }
    html += QStringLiteral("</body></html>");
    return html;
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
        pack.isMechanicalGameBreakdown()
            ? pack.selectiveDeepReview().has_value()
                ? QStringLiteral("Recorded legal game replay with a retained outcome-blind selective deep report. Unselected moves are not certified accurate; ParlAWL starts no engine and does not replay producer sources.")
                : pack.persistedEngineEvidence().has_value()
                ? QStringLiteral("Recorded legal game replay with persisted shallow fixed-node screening. ParlAWL starts no engine; labels are candidates, not objective verdicts.")
                : QStringLiteral("Recorded legal game replay. Times are server-accounted clock evidence, not direct thinking time. Engine analysis is not joined.")
            : variationActive
            ? QStringLiteral("Supplied engine line active: moves are legally checked, but engine claims are not verified. It was not played; Return restores the real game.")
            : QStringLiteral("Legal move/FEN replay verified. Severity labels and derived annotations are supplied and not verified by ParlAWL."));

    qint64 longestElapsed = -1;
    if (pack.isMechanicalGameBreakdown()) {
        for (const auto &move : pack.moves()) {
            if (move.elapsedMoveMs.has_value()) {
                longestElapsed = std::max(longestElapsed, *move.elapsedMoveMs);
            }
        }
    }

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
        if (pack.isMechanicalGameBreakdown()) {
            text += QStringLiteral(" · %1").arg(moveTimeText(move.elapsedMoveMs));
            if (pack.openingLastBookPly().has_value()) {
                if (move.ply == *pack.openingLastBookPly()) {
                    text += QStringLiteral(" · book end");
                } else if (move.ply == *pack.openingLastBookPly() + 1) {
                    text += QStringLiteral(" · first departure");
                }
            }
            if (move.elapsedMoveMs.has_value() && *move.elapsedMoveMs == longestElapsed) {
                text += QStringLiteral(" · longest");
            }
            if (!move.selectiveDeepMoments.isEmpty()) {
                const auto &deep = move.selectiveDeepMoments.first();
                text += QStringLiteral(" · deep %1")
                    .arg(deep.severity.has_value()
                        ? *deep.severity : humanizedToken(deep.status));
            }
            if (move.persistedEngineEvidence.has_value()) {
                const auto &engine = *move.persistedEngineEvidence;
                text += QStringLiteral(" · screen %1 · %2")
                    .arg(engine.severity,
                         engineScoreText(
                             engine.afterScoreKind,
                             engine.afterCentipawnsWhite,
                             engine.afterMateForWhite));
            }
        } else if (move.severity != QStringLiteral("none")) {
            text += QStringLiteral("  [%1]").arg(move.severity);
        }
        const bool current = !variationActive && currentMainlinePly == move.ply;
        const bool anchor = variationActive && variationAnchorPly == move.ply;
        m_table->setItem(row, column, makeItem(text, current, anchor));
    }
    if (!variationActive && currentMainlinePly > 0) {
        const int row = (currentMainlinePly - 1) / 2;
        const int column = currentMainlinePly % 2 == 1 ? 1 : 2;
        if (QTableWidgetItem *item = m_table->item(row, column); item != nullptr) {
            m_table->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        }
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
    setTitle(QStringLiteral("analysis replay"));
    m_gameLabel->setText(QStringLiteral("No annotated replay loaded."));
    m_openingLabel->clear();
    m_engineLabel->setText(QStringLiteral("Import is read-only and does not run Stockfish or use the network."));
    m_summaryView->setPlainText(
        QStringLiteral("Open an annotated-game-replay-v1 JSON file produced by the esports evidence pipeline."));
    m_openButton->setVisible(true);
    m_backButton->setText(QStringLiteral("Back to Puzzles"));
    m_backButton->setEnabled(false);
    m_showEngineLineButton->setEnabled(false);
    m_returnToGameButton->setEnabled(false);
    m_showEngineLineButton->setVisible(true);
    m_returnToGameButton->setVisible(true);
}

void ReplayEvidencePanel::setReplayState(
    const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
    const parlawl::puzzle_runner::ReplaySession &session,
    int variationAnchorPly)
{
    if (pack.isMechanicalGameBreakdown()) {
        setTitle(QStringLiteral("game breakdown"));
        m_openButton->setVisible(false);
        m_backButton->setText(QStringLiteral("Back to Player Stats"));
        m_gameLabel->setText(
            QStringLiteral("%1 (%2) vs %3 (%4) — %5\n%6")
                .arg(pack.whiteUsername())
                .arg(pack.whiteRating())
                .arg(pack.blackUsername())
                .arg(pack.blackRating())
                .arg(pack.result(), pack.eventStartUtc()));
        m_openingLabel->setText(replayOpeningText(pack));
        if (pack.selectiveDeepReview().has_value()) {
            const auto &deep = *pack.selectiveDeepReview();
            const QString shallow = pack.persistedEngineEvidence().has_value()
                ? QStringLiteral(" · SHALLOW %1 ALL MOVES")
                      .arg(pack.persistedEngineEvidence()->nodeLimit)
                : QString();
            m_engineLabel->setText(
                QStringLiteral("PERSISTED %1 · DEEP %2 SELECTED MOMENTS%3 · NO PROCESS STARTED")
                    .arg(deep.engineName.toUpper())
                    .arg(deep.nodeLimit)
                    .arg(shallow));
        } else if (pack.persistedEngineEvidence().has_value()) {
            const auto &engine = *pack.persistedEngineEvidence();
            m_engineLabel->setText(
                QStringLiteral("PERSISTED %1 · SHALLOW SCREEN %2 NODES/POSITION · %3 LINEAGE%4 · NO PROCESS STARTED")
                    .arg(engine.engineName.toUpper())
                    .arg(engine.nodeLimit)
                    .arg(engine.lineageCount)
                    .arg(engine.lineageCount == 1 ? QString() : QStringLiteral("S")));
        } else {
            m_engineLabel->setText(
                QStringLiteral("MECHANICAL REPLAY · ENGINE EVIDENCE NOT JOINED · NO PROCESS STARTED"));
        }

        QStringList lines;
        const parlawl::puzzle_runner::ChessPosition &finalPosition = pack.mainlinePositions().last();
        const bool noFinalMoves = finalPosition.legalMoves().isEmpty();
        const bool finalCheck = finalPosition.isInCheck(finalPosition.sideToMove());
        QString finish;
        if (noFinalMoves && finalCheck) {
            finish = QStringLiteral("The final recorded position is checkmate.");
        } else if (noFinalMoves) {
            finish = QStringLiteral("The final recorded position is stalemate.");
        } else {
            finish = QStringLiteral("The explorer records the result but does not distinguish resignation, timeout, or another non-board termination.");
        }

        const parlawl::puzzle_runner::ReplayMove *longest = nullptr;
        for (const auto &move : pack.moves()) {
            if (move.elapsedMoveMs.has_value()
                && (longest == nullptr || *move.elapsedMoveMs > *longest->elapsedMoveMs)) {
                longest = &move;
            }
        }
        if (session.currentMainlinePly() == 0) {
            lines << QStringLiteral("START POSITION")
                  << QStringLiteral("%1 recorded plies. Board orientation follows the selected player (%2).")
                         .arg(pack.moves().size())
                         .arg(humanizedToken(pack.viewedPlayerColor()))
                  << QStringLiteral("Retrospective result: %1. %2").arg(pack.result(), finish)
                  << QStringLiteral("Use Next, the mouse wheel, or the move list to replay the game.");
            if (longest != nullptr) {
                lines << QStringLiteral("Longest server-recorded decision: ply %1, %2, %3.")
                             .arg(longest->ply)
                             .arg(longest->notation.san, moveTimeText(longest->elapsedMoveMs));
            }
            if (pack.persistedEngineEvidence().has_value()
                && !pack.moves().isEmpty()
                && pack.moves().first().persistedEngineEvidence.has_value()) {
                const auto &engine = *pack.moves().first().persistedEngineEvidence;
                lines << QStringLiteral("Persisted start evaluation: %1 · %2")
                             .arg(engineScoreText(
                                      engine.beforeScoreKind,
                                      engine.beforeCentipawnsWhite,
                                      engine.beforeMateForWhite),
                                  engineWdlText(engine.beforeWdlWhite));
            }
            if (pack.persistedEngineEvidence().has_value()
                || pack.selectiveDeepReview().has_value()) {
                lines << QString();
                lines.append(mechanicalGameReportLines(pack));
            }
        } else {
            const auto &move = pack.moves().at(session.currentMainlinePly() - 1);
            const QString mover = move.ply % 2 == 1
                ? pack.whiteUsername() : pack.blackUsername();
            lines << QStringLiteral("PLY %1 · %2").arg(move.ply).arg(move.notation.san)
                  << QStringLiteral("%1 played %2 (%3).")
                         .arg(mover, move.notation.san, move.notation.uci)
                  << QStringLiteral("Phase: %1 · decision context: %2 legal move%3 · %4")
                         .arg(humanizedToken(move.positionPhase))
                         .arg(move.legalMoveCount)
                         .arg(move.legalMoveCount == 1 ? QString() : QStringLiteral("s"))
                         .arg(humanizedToken(move.forcednessStatus))
                  << QStringLiteral("Server-accounted move time: %1 · clock before: %2 · clock after: %3")
                         .arg(moveTimeText(move.elapsedMoveMs),
                              clockText(move.decisionStartClockMs),
                              clockText(move.clockRemainingAfterMoveMs))
                  << QStringLiteral("Timing status: %1").arg(humanizedToken(move.elapsedStatus));
            if (pack.openingLastBookPly().has_value()) {
                if (move.ply <= *pack.openingLastBookPly()) {
                    lines << QStringLiteral("Opening path: inside the pinned known line.");
                } else if (move.ply == *pack.openingLastBookPly() + 1) {
                    lines << QStringLiteral("Opening path: this is the first recorded departure from the pinned known line.");
                } else {
                    lines << QStringLiteral("Opening path: beyond the pinned known line.");
                }
            }
            if (longest == &move) {
                lines << QStringLiteral("This is the longest server-recorded decision in the game.");
            }
            if (!move.selectiveDeepMoments.isEmpty()) {
                for (const auto &moment : move.selectiveDeepMoments) {
                    lines << QStringLiteral("DEEP SELECTIVE ASSESSMENT · %1 NODES")
                                 .arg(pack.selectiveDeepReview()->nodeLimit)
                          << QStringLiteral("Status: %1%2")
                                 .arg(
                                     humanizedToken(moment.status),
                                     moment.severity.has_value()
                                         ? QStringLiteral(" · frozen severity %1")
                                               .arg(humanizedToken(*moment.severity))
                                         : QStringLiteral(" · no stable severity published"));
                    if (moment.bestExpectationMillionths.has_value()
                        && moment.playedExpectationMillionths.has_value()) {
                        lines << QStringLiteral("Mover expectation: deep best %1 · played %2%3")
                                     .arg(
                                         percentageText(*moment.bestExpectationMillionths),
                                         percentageText(*moment.playedExpectationMillionths),
                                         moment.wdlLossMillionths.has_value()
                                             ? QStringLiteral(" · stable loss %1")
                                                   .arg(percentageText(*moment.wdlLossMillionths))
                                             : QStringLiteral(" · no stable loss published"));
                    }
                    lines << QStringLiteral("Deep best move: %1 · recorded move: %2")
                                 .arg(
                                     moment.bestMoveUci.value_or(QStringLiteral("unavailable")),
                                     moment.playedMoveUci)
                          << QStringLiteral("Pair stability: %1 · mate comparison: %2")
                                 .arg(
                                     humanizedToken(moment.pairStability),
                                     humanizedToken(moment.mateComparison));
                    for (int lineIndex = 0; lineIndex < moment.alternativeLines.size(); ++lineIndex) {
                        lines << QStringLiteral("Alternative %1, not played: %2")
                                     .arg(lineIndex + 1)
                                     .arg(deepLineSummary(moment.alternativeLines.at(lineIndex)));
                    }
                    if (moment.playedLine.has_value()) {
                        lines << QStringLiteral("Played-move constrained line: %1")
                                     .arg(deepLineSummary(*moment.playedLine));
                    }
                }
            } else if (pack.selectiveDeepReview().has_value()) {
                lines << QStringLiteral("Deep assessment: not selected by the bounded outcome-blind policy. This does not mean the move was accurate or engine-approved.");
            }
            if (move.persistedEngineEvidence.has_value()) {
                const auto &engine = *move.persistedEngineEvidence;
                lines << QStringLiteral("SHALLOW FIXED-NODE SCREEN")
                      << QStringLiteral("White evaluation: %1 before · %2 after")
                             .arg(engineScoreText(
                                      engine.beforeScoreKind,
                                      engine.beforeCentipawnsWhite,
                                      engine.beforeMateForWhite),
                                  engineScoreText(
                                      engine.afterScoreKind,
                                      engine.afterCentipawnsWhite,
                                      engine.afterMateForWhite))
                      << QStringLiteral("Before: %1 · after: %2")
                             .arg(engineWdlText(engine.beforeWdlWhite),
                                  engineWdlText(engine.afterWdlWhite))
                      << QStringLiteral("Mover expectation: %1 → %2 · loss %3")
                             .arg(percentageText(engine.expectedBeforeMillionths),
                                  percentageText(engine.expectedAfterMillionths),
                                  percentageText(engine.wdlLossMillionths))
                      << QStringLiteral("Shallow candidate label: %1%2")
                             .arg(humanizedToken(engine.severity),
                                  engine.centipawnLoss.has_value()
                                      ? QStringLiteral(" · centipawn loss %1").arg(*engine.centipawnLoss)
                                      : QStringLiteral(" · centipawn loss N/A"));
                if (engine.beforeBestMoveUci.has_value()) {
                    lines << QStringLiteral("Engine-reported best move before play: %1 · recorded move: %2")
                                 .arg(*engine.beforeBestMoveUci, move.notation.uci);
                } else {
                    lines << QStringLiteral("Engine-reported best move before play: unavailable");
                }
                lines << QStringLiteral("Reported PV, not played: %1")
                             .arg(engine.beforePvUci.isEmpty()
                                      ? QStringLiteral("unavailable") : engine.beforePvUci)
                      << QStringLiteral("Search: depth %1 · selective depth %2 · %3 nodes")
                             .arg(engine.beforeDepth)
                             .arg(engine.beforeSelectiveDepth)
                             .arg(engine.beforeNodes);
                if (engine.missedWinningAdvantage) {
                    lines << QStringLiteral("Shallow threshold event: winning advantage was lost under the frozen screen policy.");
                }
                if (engine.missedForcedMate) {
                    lines << QStringLiteral("Shallow threshold event: a forced mate was lost under the frozen screen policy.");
                }
            }
            if (move.ply == pack.moves().size()) {
                lines << QString() << QStringLiteral("RECORDED FINISH")
                      << QStringLiteral("Result: %1. %2").arg(pack.result(), finish);
            }
        }
        lines << QString();
        if (pack.selectiveDeepReview().has_value()) {
            const auto &deep = *pack.selectiveDeepReview();
            lines << QStringLiteral("Deep evidence is limited to %1 outcome-blind selected moment%2 under a %3-node contract. Unselected moves are not certified accurate. ParlAWL exact-joined the retained report but did not rerun its source replay or Stockfish.")
                         .arg(deep.moments.size())
                         .arg(deep.moments.size() == 1 ? QString() : QStringLiteral("s"))
                         .arg(deep.nodeLimit);
            if (pack.persistedEngineEvidence().has_value()) {
                lines << QStringLiteral("The complete-move %1-node layer is shallow screening context, not a final verdict.")
                             .arg(pack.persistedEngineEvidence()->nodeLimit);
            }
        } else if (pack.persistedEngineEvidence().has_value()) {
            const auto &engine = *pack.persistedEngineEvidence();
            lines << QStringLiteral("This is persisted %1-node shallow screening recorded at %2. It is not an objective verdict, proof of a unique best move, causal explanation, or live analysis.")
                         .arg(engine.nodeLimit)
                         .arg(engine.analysisRecordedAtUtc);
        } else {
            lines << QStringLiteral("No best-move, blunder, missed-win, or causal claim is available without joined engine evidence.");
        }
        m_summaryView->setHtml(mechanicalSummaryHtml(lines));
        m_backButton->setEnabled(true);
        m_showEngineLineButton->setEnabled(false);
        m_returnToGameButton->setEnabled(false);
        m_showEngineLineButton->setVisible(false);
        m_returnToGameButton->setVisible(false);
        return;
    }

    setTitle(QStringLiteral("analysis replay"));
    m_openButton->setVisible(true);
    m_backButton->setText(QStringLiteral("Back to Puzzles"));
    m_gameLabel->setText(
        QStringLiteral("%1 vs %2 — %3").arg(pack.whiteUsername(), pack.blackUsername(), pack.result()));
    m_openingLabel->setText(replayOpeningText(pack));
    m_engineLabel->setText(
        QStringLiteral("Supplied engine metadata (not verified): %1 — %2 nodes — config %3")
            .arg(pack.sourceEngineName(), QString::number(pack.sourceEngineNodeLimit()), pack.sourceEngineConfigId()));
    m_showEngineLineButton->setVisible(true);
    m_returnToGameButton->setVisible(true);

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

GameReviewPanel::GameReviewPanel(QWidget *parent)
    : QWidget(parent)
    , m_moveListPanel(new MoveListPanel(this))
    , m_evidencePanel(new ReplayEvidencePanel(this))
{
    setObjectName(QStringLiteral("gameReviewWorkspace"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setObjectName(QStringLiteral("gameReviewSplitter"));
    splitter->setChildrenCollapsible(false);
    m_moveListPanel->setObjectName(QStringLiteral("gameReviewMoveList"));
    m_moveListPanel->setTitle(QStringLiteral("recorded moves and events"));
    m_evidencePanel->setObjectName(QStringLiteral("gameReviewInspector"));
    splitter->addWidget(m_moveListPanel);
    splitter->addWidget(m_evidencePanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({320, 420});
    layout->addWidget(splitter);

    connect(
        m_moveListPanel,
        &MoveListPanel::replayPlyRequested,
        this,
        &GameReviewPanel::replayPlyRequested);
    connect(
        m_evidencePanel,
        &ReplayEvidencePanel::backToPuzzlesRequested,
        this,
        &GameReviewPanel::backToPlayerStatisticsRequested);
}

void GameReviewPanel::setReplayState(
    const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
    const parlawl::puzzle_runner::ReplaySession &session,
    int variationAnchorPly)
{
    m_moveListPanel->setAnnotatedReplay(
        pack,
        session.currentMainlinePly(),
        session.inVariation(),
        variationAnchorPly);
    m_evidencePanel->setReplayState(pack, session, variationAnchorPly);
}

void GameReviewPanel::setEmptyState()
{
    m_evidencePanel->setEmptyState();
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
    , m_exportSolveHistoryButton(new QPushButton(QStringLiteral("Export Solve History"), this))
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

    auto *solveHistoryLabel = new QLabel(QStringLiteral("solve history"), this);
    solveHistoryLabel->setFont(sectionFont);
    layout->addWidget(solveHistoryLabel);
    layout->addWidget(m_exportSolveHistoryButton);
    auto *solveHistoryNote = new QLabel(QStringLiteral(
        "Exports only completed solved or failed attempts against exact retained imported engine-line records. "
        "Local, open, abandoned, invalid, and non-imported attempts are not exported. "
        "Hashes show internal consistency, not authenticity, and exported attempts are not used to train anything automatically."), this);
    solveHistoryNote->setWordWrap(true);
    solveHistoryNote->setTextFormat(Qt::PlainText);
    layout->addWidget(solveHistoryNote);

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
    connect(m_exportSolveHistoryButton, &QPushButton::clicked, this, &SettingsCard::exportSolveHistoryRequested);
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
