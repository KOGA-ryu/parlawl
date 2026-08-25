#include "puzzle_panels.h"

#include <algorithm>
#include <functional>
#include <utility>

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QNativeGestureEvent>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QSplitter>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QJsonDocument>
#include <QJsonObject>

#include "review_engine_adapter.h"
#include "pgn_utils.h"
#include "review_ui_style.h"

namespace {

class FixedScaleTextEdit final : public QTextEdit
{
public:
    explicit FixedScaleTextEdit(QWidget *parent = nullptr)
        : QTextEdit(parent)
    {
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::NativeGesture) {
            auto *gesture = static_cast<QNativeGestureEvent *>(event);
            if (gesture->gestureType() == Qt::ZoomNativeGesture) {
                event->accept();
                return true;
            }
        }
        return QTextEdit::event(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (event->modifiers().testFlag(Qt::ControlModifier)
            || event->modifiers().testFlag(Qt::MetaModifier)) {
            const int delta = !event->pixelDelta().isNull()
                ? event->pixelDelta().y() : event->angleDelta().y() / 3;
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta);
            event->accept();
            return;
        }
        QTextEdit::wheelEvent(event);
    }
};

class ReplayMoveTable final : public QTableWidget
{
public:
    explicit ReplayMoveTable(QWidget *parent = nullptr)
        : QTableWidget(parent)
    {
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && currentRow() >= 0 && currentColumn() >= 0) {
            emit cellActivated(currentRow(), currentColumn());
            event->accept();
            return;
        }
        QTableWidget::keyPressEvent(event);
    }
};

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

QString signedPercentagePointText(int deltaMillionths)
{
    QString magnitude = percentageText(std::abs(deltaMillionths));
    magnitude.chop(1);
    const QString sign = deltaMillionths > 0
        ? QStringLiteral("+")
        : deltaMillionths < 0 ? QString::fromUtf8("−") : QStringLiteral("±");
    return QStringLiteral("%1%2 pp").arg(sign, magnitude);
}

QString compactNodeCount(qint64 nodes)
{
    if (nodes > 0 && nodes % 1000 == 0) {
        return QStringLiteral("%1k").arg(nodes / 1000);
    }
    return QString::number(nodes);
}

QString compactEngineName(QString engineName)
{
    if (engineName.startsWith(QStringLiteral("Stockfish "), Qt::CaseInsensitive)) {
        engineName.replace(0, QStringLiteral("Stockfish ").size(), QStringLiteral("SF"));
    }
    return engineName;
}

QString compactReviewDate(const QString &eventStartUtc)
{
    return eventStartUtc.size() >= 10 ? eventStartUtc.left(10) : eventStartUtc;
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

QString compactOpeningText(const parlawl::puzzle_runner::AnnotatedReplayPack &pack)
{
    if (pack.openingStatus() != QStringLiteral("classified")) {
        return humanizedToken(pack.openingStatus());
    }
    const QString eco = pack.openingEco().value_or(QString());
    QString name = pack.openingName().value_or(QStringLiteral("unnamed opening"));
    const qsizetype separator = name.lastIndexOf(QStringLiteral(": "));
    if (separator >= 0) {
        name = name.mid(separator + 2).trimmed();
    }
    return eco.isEmpty() ? name : QStringLiteral("%1 · %2").arg(eco, name);
}

QString openingBoundaryToolTip(const parlawl::puzzle_runner::AnnotatedReplayPack &pack)
{
    const QString eco = pack.openingEco().value_or(QString());
    const QString name = pack.openingName().value_or(humanizedToken(pack.openingStatus()));
    const QString opening = eco.isEmpty() ? name : QStringLiteral("%1 · %2").arg(eco, name);
    const QString prefix = QStringLiteral("Retained opening: %1.\n").arg(opening);
    if (!pack.openingLastBookPly().has_value()) {
        return prefix + QStringLiteral(
            "The retained classification does not publish an exact opening boundary.");
    }
    const int bookPly = *pack.openingLastBookPly();
    if (bookPly >= pack.moves().size()) {
        return prefix + QStringLiteral(
            "The recorded game follows the retained opening reference through its final move.");
    }
    return prefix + QStringLiteral(
        "The first %1 half-moves match the retained opening reference. Ply %2 is the first move outside that path.")
        .arg(bookPly)
        .arg(bookPly + 1);
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

QString deepEvidenceStatusText(const QString &sourceStatus)
{
    if (sourceStatus == QStringLiteral("confirmed_severe_error")) {
        return QStringLiteral("deep confirmed severe");
    }
    if (sourceStatus == QStringLiteral("confirmed_missed_opportunity")) {
        return QStringLiteral("confirmed missed opportunity");
    }
    if (sourceStatus == QStringLiteral("ambiguous_engine_instability")
        || sourceStatus == QStringLiteral("incomplete_deep_evidence")) {
        return QStringLiteral("ambiguous");
    }
    if (sourceStatus == QStringLiteral("below_confirmation_threshold")) {
        return QStringLiteral("below threshold");
    }
    // Fail closed instead of silently recategorizing an unknown retained token.
    return QStringLiteral("evidence unavailable");
}

bool deepEvidenceStatusIsRecognized(const QString &sourceStatus)
{
    return sourceStatus == QStringLiteral("confirmed_severe_error")
        || sourceStatus == QStringLiteral("confirmed_missed_opportunity")
        || sourceStatus == QStringLiteral("ambiguous_engine_instability")
        || sourceStatus == QStringLiteral("incomplete_deep_evidence")
        || sourceStatus == QStringLiteral("below_confirmation_threshold");
}

bool deepEvidenceStatusAllowsStableChange(const QString &sourceStatus)
{
    return sourceStatus == QStringLiteral("confirmed_severe_error")
        || sourceStatus == QStringLiteral("confirmed_missed_opportunity")
        || sourceStatus == QStringLiteral("below_confirmation_threshold");
}

struct AlternativeNodePresentation {
    QString title;
    QString score;
    QString detail;
    bool recordedMove = false;
};

struct MoveCoachPresentation {
    QString status;
    QString evaluationChange;
    QString explanation;
    QString focusReadText;
    QStringList technicalDetails;
    QVector<AlternativeNodePresentation> alternativeNodes;
    bool hasDeepMoment = false;
    bool hasShallowOnlyEvidence = false;
};

AlternativeNodePresentation alternativeNodePresentation(
    const parlawl::puzzle_runner::SelectiveDeepEngineLine &line,
    const QString &title,
    bool recordedMove)
{
    AlternativeNodePresentation node;
    node.title = title;
    node.recordedMove = recordedMove;
    node.score = engineScoreText(
        line.scoreKind, line.centipawnsWhite, line.mateForWhite);
    QStringList details {
        QStringLiteral("White score: %1").arg(node.score),
        QStringLiteral("Depth %1/%2 · %3 retained nodes")
            .arg(line.depth)
            .arg(line.selectiveDepth)
            .arg(line.nodes),
    };
    if (line.wdlWhite.size() == 3) {
        details << QStringLiteral("White W/D/L: %1 / %2 / %3")
                       .arg(line.wdlWhite.at(0))
                       .arg(line.wdlWhite.at(1))
                       .arg(line.wdlWhite.at(2));
    }
    details << QStringLiteral("PV, not played unless marked recorded: %1")
                   .arg(line.pvUci.isEmpty()
                            ? QStringLiteral("unavailable")
                            : line.pvUci.join(QLatin1Char(' ')));
    node.detail = details.join(QLatin1Char('\n'));
    return node;
}

MoveCoachPresentation moveCoachPresentation(
    const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
    const parlawl::puzzle_runner::ReplayMove &move)
{
    MoveCoachPresentation presentation;
    const QString mover = move.ply % 2 == 1
        ? pack.whiteUsername() : pack.blackUsername();
    const auto *moment = move.selectiveDeepMoments.isEmpty()
        ? nullptr : &move.selectiveDeepMoments.first();
    const bool deepReviewAvailable = pack.selectiveDeepReview().has_value();
    const bool shallowAvailable = move.persistedEngineEvidence.has_value();

    presentation.technicalDetails
        << QStringLiteral("RECORDED MOVE")
        << QStringLiteral("Ply %1 · %2 played %3 (%4)")
               .arg(move.ply)
               .arg(mover, move.notation.san, move.notation.uci)
        << QStringLiteral("Phase: %1 · %2 legal moves · %3")
               .arg(humanizedToken(move.positionPhase))
               .arg(move.legalMoveCount)
               .arg(humanizedToken(move.forcednessStatus))
        << QStringLiteral("Server-accounted time: %1 · clock %2 → %3 · %4")
               .arg(
                   moveTimeText(move.elapsedMoveMs),
                   clockText(move.decisionStartClockMs),
                   clockText(move.clockRemainingAfterMoveMs),
                   humanizedToken(move.elapsedStatus));

    if (moment != nullptr) {
        presentation.hasDeepMoment = true;
        presentation.status = deepEvidenceStatusText(moment->status);
        const QString bestMove = moment->bestMoveUci.value_or(QStringLiteral("unavailable"));
        const QString lossPoints = moment->wdlLossMillionths.has_value()
            ? percentageText(*moment->wdlLossMillionths).chopped(1)
            : QStringLiteral("not stably published");
        const bool stableChangeAvailable = deepEvidenceStatusAllowsStableChange(moment->status)
            && moment->bestExpectationMillionths.has_value()
            && moment->playedExpectationMillionths.has_value()
            && moment->wdlLossMillionths.has_value();
        if (stableChangeAvailable) {
            presentation.evaluationChange = QStringLiteral("◒ %1 → %2   −%3 pp")
                .arg(
                    percentageText(*moment->bestExpectationMillionths),
                    percentageText(*moment->playedExpectationMillionths),
                    percentageText(*moment->wdlLossMillionths).chopped(1));
        } else {
            presentation.evaluationChange = QStringLiteral("◒ no stable change published");
        }

        if (!deepEvidenceStatusIsRecognized(moment->status)) {
            presentation.explanation = QStringLiteral(
                "The retained source status for %1 is not recognized by this view, so no firm move-quality claim is available.")
                .arg(move.notation.san);
        } else if (presentation.status == QStringLiteral("deep confirmed severe")
                   && stableChangeAvailable) {
            presentation.explanation = QStringLiteral(
                "The retained deep comparison measured a stable %1 percentage-point mover-expectation loss after %2; its leading alternative was %3.")
                .arg(lossPoints, move.notation.san, bestMove);
        } else if (presentation.status == QStringLiteral("confirmed missed opportunity")
                   && stableChangeAvailable) {
            presentation.explanation = QStringLiteral(
                "The retained deep comparison measured a stable %1 percentage-point missed opportunity after %2; its leading alternative was %3.")
                .arg(lossPoints, move.notation.san, bestMove);
        } else if (presentation.status == QStringLiteral("deep confirmed severe")
                   || presentation.status == QStringLiteral("confirmed missed opportunity")) {
            presentation.explanation = QStringLiteral(
                "Retained deep evidence labels %1 %2, but no complete stable mover-expectation change was published in the joined record.")
                .arg(move.notation.san, presentation.status);
        } else if (presentation.status == QStringLiteral("below threshold")) {
            presentation.explanation = QStringLiteral(
                "Deep review assessed %1, but the retained comparison stayed below its confirmation threshold.")
                .arg(move.notation.san);
        } else {
            presentation.explanation = QStringLiteral(
                "Deep review did not retain a stable comparison for %1, so no firm move-quality claim is available.")
                .arg(move.notation.san);
        }

        presentation.focusReadText = QStringLiteral(
            "The retained fixed-node review compares the recorded move with retained alternatives. %1 The comparison is bounded evidence: it does not prove player intent, a causal explanation, or a uniquely correct move.")
            .arg(presentation.explanation);
        presentation.technicalDetails
            << QStringLiteral("DEEP SELECTIVE EVIDENCE")
            << QStringLiteral("Evidence status: %1").arg(presentation.status)
            << QStringLiteral("Retained source status: %1").arg(moment->status)
            << presentation.evaluationChange
            << QStringLiteral("Retained leading alternative: %1 · recorded move: %2")
                   .arg(bestMove, moment->playedMoveUci)
            << QStringLiteral("Pair stability: %1 · mate comparison: %2")
                   .arg(humanizedToken(moment->pairStability), humanizedToken(moment->mateComparison));
        for (int index = 0; index < moment->alternativeLines.size(); ++index) {
            const auto &line = moment->alternativeLines.at(index);
            presentation.alternativeNodes.append(alternativeNodePresentation(
                line,
                QStringLiteral("Alternative %1 · %2")
                    .arg(index + 1)
                    .arg(line.rootMoveUci),
                false));
            presentation.technicalDetails
                << QStringLiteral("Alternative %1, not played: %2")
                       .arg(index + 1)
                       .arg(deepLineSummary(line));
        }
        if (moment->playedLine.has_value()) {
            presentation.alternativeNodes.prepend(alternativeNodePresentation(
                *moment->playedLine,
                QStringLiteral("Recorded · %1").arg(move.notation.san),
                true));
            presentation.technicalDetails
                << QStringLiteral("Recorded-move constrained line: %1")
                       .arg(deepLineSummary(*moment->playedLine));
        }
    } else if (deepReviewAvailable) {
        presentation.status = QStringLiteral("not selected for deep review");
        presentation.technicalDetails
            << QStringLiteral("DEEP SELECTIVE EVIDENCE")
            << QStringLiteral("Evidence status: not selected for deep review")
            << QStringLiteral("Unselected moves are not certified accurate.");
    } else if (shallowAvailable) {
        presentation.status = QStringLiteral("shallow screening candidate");
        presentation.hasShallowOnlyEvidence = true;
    } else {
        presentation.status = QStringLiteral("evidence unavailable");
    }

    if (shallowAvailable) {
        const auto &engine = *move.persistedEngineEvidence;
        if (presentation.evaluationChange.isEmpty()) {
            presentation.evaluationChange = QStringLiteral("◒ %1 → %2   %3")
                .arg(
                    percentageText(engine.expectedBeforeMillionths),
                    percentageText(engine.expectedAfterMillionths),
                    signedPercentagePointText(
                        engine.expectedAfterMillionths - engine.expectedBeforeMillionths));
        }
        presentation.technicalDetails
            << QStringLiteral("SHALLOW SCREENING CONTEXT")
            << QStringLiteral("Shallow screen label: %1 · mover expectation %2 → %3 · published loss %4 pp")
                   .arg(
                       humanizedToken(engine.severity),
                       percentageText(engine.expectedBeforeMillionths),
                       percentageText(engine.expectedAfterMillionths),
                       percentageText(engine.wdlLossMillionths).chopped(1))
            << QStringLiteral("White evaluation: %1 before · %2 after")
                   .arg(
                       engineScoreText(
                           engine.beforeScoreKind,
                           engine.beforeCentipawnsWhite,
                           engine.beforeMateForWhite),
                       engineScoreText(
                           engine.afterScoreKind,
                           engine.afterCentipawnsWhite,
                           engine.afterMateForWhite))
            << QStringLiteral("Reported PV, not played: %1")
                   .arg(engine.beforePvUci.isEmpty()
                            ? QStringLiteral("unavailable") : engine.beforePvUci)
            << QStringLiteral("Search: depth %1/%2 · %3 nodes")
                   .arg(engine.beforeDepth)
                   .arg(engine.beforeSelectiveDepth)
                   .arg(engine.beforeNodes);
    }

    presentation.technicalDetails
        << QStringLiteral("CLAIM BOUNDARY")
        << QStringLiteral(
               "ParlAWL displays retained evidence only. It starts no engine, replays no producer source, makes no causal claim, and never certifies an unselected move as accurate.");
    return presentation;
}

QString movePieceIcon(const parlawl::puzzle_runner::ReplayNotation &notation)
{
    const QString piece = notation.piece.trimmed().toLower();
    if (piece == QStringLiteral("king")) {
        return QString::fromUtf8("♔");
    }
    if (piece == QStringLiteral("queen")) {
        return QString::fromUtf8("♕");
    }
    if (piece == QStringLiteral("rook")) {
        return QString::fromUtf8("♖");
    }
    if (piece == QStringLiteral("bishop")) {
        return QString::fromUtf8("♗");
    }
    if (piece == QStringLiteral("knight")) {
        return QString::fromUtf8("♘");
    }
    return QString::fromUtf8("♙");
}

QString reviewPieceIcon(const QString &pieceName, const QString &mover)
{
    const bool black = mover == QStringLiteral("black");
    const QString piece = pieceName.trimmed().toLower();
    if (piece == QStringLiteral("king")) {
        return black ? QString::fromUtf8("♚") : QString::fromUtf8("♔");
    }
    if (piece == QStringLiteral("queen")) {
        return black ? QString::fromUtf8("♛") : QString::fromUtf8("♕");
    }
    if (piece == QStringLiteral("rook")) {
        return black ? QString::fromUtf8("♜") : QString::fromUtf8("♖");
    }
    if (piece == QStringLiteral("bishop")) {
        return black ? QString::fromUtf8("♝") : QString::fromUtf8("♗");
    }
    if (piece == QStringLiteral("knight")) {
        return black ? QString::fromUtf8("♞") : QString::fromUtf8("♘");
    }
    return black ? QString::fromUtf8("♟") : QString::fromUtf8("♙");
}

QString detailedMoveEvidenceText(
    const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
    const parlawl::puzzle_runner::ReplaySession &session)
{
    QStringList lines {
        QStringLiteral("%1 %2  ·  %3  ·  %4 %5")
            .arg(pack.whiteUsername())
            .arg(pack.whiteRating())
            .arg(pack.result())
            .arg(pack.blackUsername())
            .arg(pack.blackRating()),
        QStringLiteral("%1 · %2")
            .arg(pack.openingEco().value_or(QStringLiteral("Opening")),
                 pack.openingName().value_or(humanizedToken(pack.openingStatus()))),
        QString(),
    };
    const int ply = session.currentMainlinePly();
    if (ply <= 0 || ply > pack.moves().size()) {
        lines << QStringLiteral("Start position")
              << QStringLiteral("Select a notation entry to inspect its fixed-format evidence record.");
        return lines.join(QLatin1Char('\n'));
    }

    const auto &move = pack.moves().at(ply - 1);
    const MoveCoachPresentation presentation = moveCoachPresentation(pack, move);
    const int moveNumber = (move.ply + 1) / 2;
    const QString moveLabel = move.ply % 2 == 1
        ? QStringLiteral("%1. %2").arg(moveNumber).arg(move.notation.san)
        : QStringLiteral("%1… %2").arg(moveNumber).arg(move.notation.san);
    lines << QStringLiteral("%1  %2").arg(movePieceIcon(move.notation), moveLabel)
          << QStringLiteral("Played move")
          << QStringLiteral("  SAN  %1").arg(move.notation.san)
          << QStringLiteral("  UCI  %1").arg(move.notation.uci)
          << QString();

    lines << QStringLiteral("Evidence");
    if (presentation.hasDeepMoment || presentation.hasShallowOnlyEvidence) {
        lines << QStringLiteral("  Status  %1").arg(presentation.status);
        if (!presentation.evaluationChange.isEmpty()) {
            lines << QStringLiteral("  Change  %1").arg(presentation.evaluationChange);
        }
    } else if (pack.selectiveDeepReview().has_value()) {
        lines << QStringLiteral("  Status  not selected for deep review")
              << QStringLiteral("  Boundary  this is not an accuracy claim");
    } else {
        lines << QStringLiteral("  Status  evidence unavailable");
    }
    if (!presentation.explanation.isEmpty()) {
        lines << QString() << QStringLiteral("Explanation")
              << QStringLiteral("  %1").arg(presentation.explanation);
    }

    lines << QString()
          << QStringLiteral("Position")
          << QStringLiteral("  Phase  %1").arg(humanizedToken(move.positionPhase))
          << QStringLiteral("  Legal moves  %1").arg(move.legalMoveCount)
          << QStringLiteral("  Forcedness  %1").arg(humanizedToken(move.forcednessStatus))
          << QString()
          << QStringLiteral("Clock")
          << QStringLiteral("  Server-accounted move time  %1")
                 .arg(moveTimeText(move.elapsedMoveMs))
          << QStringLiteral("  Recorded clock  %1 → %2")
                 .arg(clockText(move.decisionStartClockMs),
                      clockText(move.clockRemainingAfterMoveMs));

    if (!presentation.alternativeNodes.isEmpty()) {
        lines << QString() << QStringLiteral("Retained alternatives");
        for (const auto &node : presentation.alternativeNodes) {
            lines << QStringLiteral("  %1  ·  %2")
                         .arg(node.title, node.score);
        }
        lines << QStringLiteral("  These are retained lines, not a complete engine search tree.");
    }

    lines << QString()
          << QStringLiteral("Source boundary")
          << QStringLiteral("  Displays retained evidence only; no engine or network process is started.")
          << QStringLiteral("  It does not prove intent, causality, or a uniquely correct move.");
    return lines.join(QLatin1Char('\n'));
}

QString mechanicalScopeTitle(const QString &scope)
{
    if (scope == QStringLiteral("policy")) return QStringLiteral("Policy");
    if (scope == QStringLiteral("best_move")) return QStringLiteral("Best move");
    if (scope == QStringLiteral("played_move")) return QStringLiteral("Played move");
    if (scope == QStringLiteral("comparison")) return QStringLiteral("Comparison");
    if (scope == QStringLiteral("context")) return QStringLiteral("Context");
    return scope;
}

QString compactJsonObject(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString mechanicalEvidenceText(
    const parlawl::puzzle_runner::GameReviewMechanicalExplanation &explanation,
    const parlawl::puzzle_runner::GameReviewMechanicalMoment &moment)
{
    QStringList lines {
        QStringLiteral("MECHANICAL EXPLANATION"),
        moment.headline,
        QStringLiteral("  Comparison status      %1").arg(moment.comparisonStatus),
        QStringLiteral("  Mechanical fact status %1").arg(moment.mechanicalFactStatus),
    };
    const QStringList scopes {
        QStringLiteral("policy"), QStringLiteral("best_move"),
        QStringLiteral("played_move"), QStringLiteral("comparison"),
        QStringLiteral("context")};
    for (const QString &scope : scopes) {
        bool headingAdded = false;
        for (const auto &fact : moment.facts) {
            if (fact.scope != scope) continue;
            if (!headingAdded) {
                lines << QString() << mechanicalScopeTitle(scope).toUpper();
                headingAdded = true;
            }
            lines << QStringLiteral("  %1").arg(fact.text)
                  << QStringLiteral("    code    %1").arg(fact.code)
                  << QStringLiteral("    values  %1").arg(compactJsonObject(fact.values));
        }
    }
    lines << QString()
          << QStringLiteral("COMPANION SOURCE")
          << QStringLiteral("  Contract       %1").arg(explanation.contractVersion)
          << QStringLiteral("  Source report  %1").arg(explanation.sourceReportId)
          << QStringLiteral("  Claim boundary %1")
                 .arg(compactJsonObject(explanation.claimBoundary));
    return lines.join(QLatin1Char('\n'));
}

class InlineMoveDetail final : public QFrame
{
public:
    InlineMoveDetail(
        const MoveCoachPresentation &presentation,
        std::function<void()> geometryChanged,
        QWidget *parent = nullptr)
        : QFrame(parent)
        , m_geometryChanged(std::move(geometryChanged))
        , m_words(presentation.focusReadText.split(QLatin1Char(' '), Qt::SkipEmptyParts))
    {
        setObjectName(QStringLiteral("inlineMoveDetail"));
        setFrameShape(QFrame::NoFrame);
        setFocusPolicy(Qt::StrongFocus);
        setStyleSheet(QStringLiteral(
            "QFrame#inlineMoveDetail { background: #1d2127; border: 0; border-left: 3px solid #547ca8; }"
            "QLabel#inlineMoveStatus { color: #d7be83; font-weight: 600; }"
            "QLabel#inlineMoveChange { color: #c3ccd8; }"
            "QLabel#inlineMoveMechanics { color: #9ca7b5; }"
            "QPushButton#inlineDetailsButton { background: transparent; color: #aeb8c5; border: 0; padding: 4px 2px; text-align: left; }"
            "QPushButton#inlineFocusButton { background: #293545; color: #e7edf5; border: 1px solid #3a4a5f; border-radius: 7px; padding: 4px 8px; }"
            "QFrame#retainedAlternativeMap { background: #171a1f; border: 0; border-radius: 8px; }"
            "QPushButton#alternativeNodeButton { background: #252b33; color: #d7dee8; border: 1px solid #3a4451; border-radius: 9px; padding: 6px 8px; text-align: left; }"
            "QPushButton#alternativeNodeButton:checked { background: #31445b; border-color: #648bb5; }"
            "QLabel#alternativeNodeDetail { color: #9faab8; font-family: monospace; }"
            "QFrame#inlineFocusFrame { background: #14171b; border: 0; border-radius: 8px; }"
            "QLabel#inlineFocusAnchor { background: #f0e4cf; color: #202020; border: 0; border-radius: 8px; padding: 5px 8px; font-weight: 600; }"
            "QLabel#inlineFocusContext { color: #8f99a7; }"));

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(6);

        if (presentation.hasDeepMoment || presentation.hasShallowOnlyEvidence) {
            auto *status = new QLabel(presentation.status, this);
            status->setObjectName(QStringLiteral("inlineMoveStatus"));
            status->setTextFormat(Qt::PlainText);
            status->setWordWrap(false);
            layout->addWidget(status);
        }
        if (!presentation.evaluationChange.isEmpty()
            && (presentation.hasDeepMoment || presentation.hasShallowOnlyEvidence)) {
            auto *change = new QLabel(presentation.evaluationChange, this);
            change->setObjectName(QStringLiteral("inlineMoveChange"));
            change->setTextFormat(Qt::PlainText);
            change->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            layout->addWidget(change);
        }
        if (presentation.hasDeepMoment && !presentation.explanation.isEmpty()) {
            auto *explanation = new QLabel(presentation.explanation, this);
            explanation->setObjectName(QStringLiteral("inlineMoveExplanation"));
            explanation->setTextFormat(Qt::PlainText);
            explanation->setWordWrap(true);
            layout->addWidget(explanation);
        } else if (presentation.technicalDetails.size() >= 4) {
            auto *mechanics = new QLabel(
                presentation.technicalDetails.at(2) + QLatin1Char('\n')
                    + presentation.technicalDetails.at(3),
                this);
            mechanics->setObjectName(QStringLiteral("inlineMoveMechanics"));
            mechanics->setTextFormat(Qt::PlainText);
            mechanics->setWordWrap(true);
            layout->addWidget(mechanics);
        }

        if (!presentation.alternativeNodes.isEmpty()) {
            auto *map = new QFrame(this);
            map->setObjectName(QStringLiteral("retainedAlternativeMap"));
            auto *mapLayout = new QVBoxLayout(map);
            mapLayout->setContentsMargins(8, 7, 8, 7);
            mapLayout->setSpacing(6);
            auto *mapTitle = new QLabel(
                QStringLiteral("Retained alternatives · published lines, not a complete search tree"),
                map);
            mapTitle->setTextFormat(Qt::PlainText);
            mapLayout->addWidget(mapTitle);
            auto *nodes = new QHBoxLayout();
            nodes->setContentsMargins(0, 0, 0, 0);
            nodes->setSpacing(6);
            auto *nodeDetail = new QLabel(map);
            nodeDetail->setObjectName(QStringLiteral("alternativeNodeDetail"));
            nodeDetail->setTextFormat(Qt::PlainText);
            nodeDetail->setWordWrap(true);
            for (int index = 0; index < presentation.alternativeNodes.size(); ++index) {
                const AlternativeNodePresentation node = presentation.alternativeNodes.at(index);
                auto *button = new QPushButton(
                    QStringLiteral("%1\n%2").arg(node.title, node.score), map);
                button->setObjectName(QStringLiteral("alternativeNodeButton"));
                button->setCheckable(true);
                button->setAutoExclusive(true);
                button->setToolTip(QStringLiteral("Click to inspect this retained line"));
                if (node.recordedMove) {
                    button->setProperty("recordedMove", true);
                }
                if (index == 0) {
                    button->setChecked(true);
                    nodeDetail->setText(node.detail);
                }
                connect(button, &QPushButton::clicked, map, [nodeDetail, node]() {
                    nodeDetail->setText(node.detail);
                });
                nodes->addWidget(button, 1);
            }
            mapLayout->addLayout(nodes);
            mapLayout->addWidget(nodeDetail);
            layout->addWidget(map);
        }

        if (presentation.hasDeepMoment) {
            auto *actions = new QHBoxLayout();
            actions->setContentsMargins(0, 0, 0, 0);
            actions->setSpacing(7);
            m_detailsButton = new QPushButton(QStringLiteral("▸ Details"), this);
            m_detailsButton->setObjectName(QStringLiteral("inlineDetailsButton"));
            m_detailsButton->setAccessibleName(QStringLiteral("Show technical details"));
            m_focusButton = new QPushButton(QStringLiteral("Focus Read"), this);
            m_focusButton->setObjectName(QStringLiteral("inlineFocusButton"));
            actions->addWidget(m_detailsButton);
            actions->addWidget(m_focusButton);
            actions->addStretch(1);
            layout->addLayout(actions);

            m_focusFrame = new QFrame(this);
            m_focusFrame->setObjectName(QStringLiteral("inlineFocusFrame"));
            auto *focusLayout = new QVBoxLayout(m_focusFrame);
            focusLayout->setContentsMargins(8, 8, 8, 7);
            focusLayout->setSpacing(6);
            auto *tape = new QHBoxLayout();
            tape->setContentsMargins(0, 0, 0, 0);
            tape->setSpacing(6);
            m_before = new QLabel(m_focusFrame);
            m_anchor = new QLabel(m_focusFrame);
            m_after = new QLabel(m_focusFrame);
            m_before->setObjectName(QStringLiteral("inlineFocusContext"));
            m_anchor->setObjectName(QStringLiteral("inlineFocusAnchor"));
            m_after->setObjectName(QStringLiteral("inlineFocusContext"));
            m_before->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_anchor->setAlignment(Qt::AlignCenter);
            m_after->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            m_before->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            m_after->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            tape->addWidget(m_before, 1);
            tape->addWidget(m_anchor);
            tape->addWidget(m_after, 1);
            focusLayout->addLayout(tape);
            auto *focusControls = new QHBoxLayout();
            auto *previous = new QPushButton(QStringLiteral("←"), m_focusFrame);
            auto *next = new QPushButton(QStringLiteral("→"), m_focusFrame);
            previous->setAccessibleName(QStringLiteral("Previous Focus Read word"));
            next->setAccessibleName(QStringLiteral("Next Focus Read word"));
            previous->setFocusPolicy(Qt::NoFocus);
            next->setFocusPolicy(Qt::NoFocus);
            m_progress = new QLabel(m_focusFrame);
            focusControls->addWidget(previous);
            focusControls->addWidget(next);
            focusControls->addWidget(m_progress);
            focusControls->addStretch(1);
            focusLayout->addLayout(focusControls);
            m_focusFrame->setVisible(false);
            layout->addWidget(m_focusFrame);

            m_details = new FixedScaleTextEdit(this);
            m_details->setObjectName(QStringLiteral("inlineTechnicalDetails"));
            m_details->setReadOnly(true);
            m_details->setPlainText(presentation.technicalDetails.join(QLatin1Char('\n')));
            m_details->setMinimumHeight(145);
            m_details->setStyleSheet(QStringLiteral(
                "QTextEdit { background: #14171b; border: 0; border-radius: 8px; padding: 7px; font-size: 13px; }"));
            m_details->setVisible(false);
            layout->addWidget(m_details);

            connect(m_detailsButton, &QPushButton::clicked, this, [this]() {
                const bool show = !m_details->isVisible();
                m_details->setVisible(show);
                m_detailsButton->setText(show ? QStringLiteral("▾ Details") : QStringLiteral("▸ Details"));
                m_detailsButton->setAccessibleName(
                    show ? QStringLiteral("Hide technical details")
                         : QStringLiteral("Show technical details"));
                if (show) {
                    m_details->verticalScrollBar()->setValue(0);
                }
                updateGeometryAndRow();
            });
            connect(m_focusButton, &QPushButton::clicked, this, [this]() {
                const bool show = !m_focusFrame->isVisible() && !m_words.isEmpty();
                m_focusFrame->setVisible(show);
                m_focusButton->setText(show ? QStringLiteral("Close Focus Read")
                                            : QStringLiteral("Focus Read"));
                if (show) {
                    updateFocus();
                    setFocus(Qt::OtherFocusReason);
                }
                updateGeometryAndRow();
            });
            connect(previous, &QPushButton::clicked, this, [this]() {
                stepFocus(-1);
                setFocus(Qt::OtherFocusReason);
            });
            connect(next, &QPushButton::clicked, this, [this]() {
                stepFocus(1);
                setFocus(Qt::OtherFocusReason);
            });
            updateFocus();
        }
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (m_focusFrame != nullptr && m_focusFrame->isVisible()) {
            if (event->key() == Qt::Key_Left) {
                stepFocus(-1);
                event->accept();
                return;
            }
            if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Space) {
                stepFocus(1);
                event->accept();
                return;
            }
            if (event->key() == Qt::Key_Escape) {
                m_focusFrame->setVisible(false);
                m_focusButton->setText(QStringLiteral("Focus Read"));
                updateGeometryAndRow();
                event->accept();
                return;
            }
        }
        QFrame::keyPressEvent(event);
    }

private:
    void updateGeometryAndRow()
    {
        updateGeometry();
        adjustSize();
        if (m_geometryChanged) {
            m_geometryChanged();
        }
    }

    void stepFocus(int delta)
    {
        if (m_words.isEmpty()) {
            return;
        }
        m_wordIndex = std::clamp(
            m_wordIndex + delta, 0, static_cast<int>(m_words.size()) - 1);
        updateFocus();
    }

    void updateFocus()
    {
        if (m_words.isEmpty() || m_before == nullptr) {
            return;
        }
        constexpr int contextWords = 4;
        const int beforeStart = std::max(0, m_wordIndex - contextWords);
        const int afterCount = std::min(
            contextWords, static_cast<int>(m_words.size()) - m_wordIndex - 1);
        QString before = m_words.mid(beforeStart, m_wordIndex - beforeStart)
                             .join(QLatin1Char(' '));
        QString after = m_words.mid(m_wordIndex + 1, afterCount).join(QLatin1Char(' '));
        if (beforeStart > 0) {
            before.prepend(QStringLiteral("… "));
        }
        if (m_wordIndex + 1 + afterCount < m_words.size()) {
            after.append(QStringLiteral(" …"));
        }
        m_before->setText(before);
        m_anchor->setText(m_words.at(m_wordIndex));
        m_after->setText(after);
        m_progress->setText(
            QStringLiteral("%1 / %2").arg(m_wordIndex + 1).arg(m_words.size()));
    }

    std::function<void()> m_geometryChanged;
    QStringList m_words;
    int m_wordIndex = 0;
    QPushButton *m_detailsButton = nullptr;
    QPushButton *m_focusButton = nullptr;
    FixedScaleTextEdit *m_details = nullptr;
    QFrame *m_focusFrame = nullptr;
    QLabel *m_before = nullptr;
    QLabel *m_anchor = nullptr;
    QLabel *m_after = nullptr;
    QLabel *m_progress = nullptr;
};

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
          << QStringLiteral("Deep statuses: deep confirmed severe %1 · confirmed missed opportunity %2 · ambiguous %3 · below threshold %4")
                 .arg(statusCounts.value(QStringLiteral("confirmed_severe_error")))
                 .arg(statusCounts.value(QStringLiteral("confirmed_missed_opportunity")))
                 .arg(statusCounts.value(QStringLiteral("ambiguous_engine_instability"))
                      + statusCounts.value(QStringLiteral("incomplete_deep_evidence")))
                 .arg(statusCounts.value(QStringLiteral("below_confirmation_threshold")))
          << QStringLiteral("Selected moments");
    for (const auto &moment : review.moments) {
        const QString player = moment.mover == QStringLiteral("white")
            ? pack.whiteUsername() : pack.blackUsername();
        const QString evidenceStatus = deepEvidenceStatusText(moment.status);
        const QString loss = moment.wdlLossMillionths.has_value()
            ? percentageText(*moment.wdlLossMillionths).chopped(1)
                + QStringLiteral(" pp mover expectation loss")
            : QStringLiteral("no stable loss published");
        lines << QStringLiteral("%1. Ply %2 %3 — %4 — %5 — %6 · retained source status %7 · played %8 · retained leading alternative %9")
                     .arg(moment.presentationOrder)
                     .arg(moment.ply)
                     .arg(moment.san)
                     .arg(player)
                     .arg(evidenceStatus)
                     .arg(loss)
                     .arg(moment.status)
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
                     "%1. Ply %2 %3 — %4 — %5 — %6 pp mover expectation loss · %7 → %8 · %9 · best %10")
                     .arg(++emittedMomentCount)
                     .arg(move.ply)
                     .arg(move.notation.san)
                     .arg(mover)
                     .arg(humanizedToken(engine.severity))
                     .arg(percentageText(engine.wdlLossMillionths).chopped(1))
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
    , m_table(new ReplayMoveTable(this))
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
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setFocusPolicy(Qt::StrongFocus);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->setStyleSheet(QStringLiteral(
        "QTableWidget { background: #171a1f; alternate-background-color: #171a1f; border: 0; font-size: 13px; }"
        "QTableWidget::item { padding: 5px 7px; border-bottom: 1px solid #272c34; }"
        "QTableWidget::item:selected { background: #26384f; color: #edf4ff; }"
        "QHeaderView::section { background: #20242b; color: #aeb6c2; border: 0; padding: 5px; font-size: 12px; }"));
    const auto requestReplayPly = [this](int row, int column) {
        if (!m_showingAnnotatedReplay || column < 1 || column > 2) {
            return;
        }
        const QTableWidgetItem *item = m_table->item(row, column);
        const int ply = item == nullptr ? 0 : item->data(Qt::UserRole).toInt();
        if (ply >= 1 && ply <= m_replayMoveCount) {
            if (ply == m_currentReplayPly && m_inlineDetailRow >= 0) {
                m_selectedExpansionCollapsed = !m_table->isRowHidden(m_inlineDetailRow);
                m_table->setRowHidden(m_inlineDetailRow, m_selectedExpansionCollapsed);
                return;
            }
            emit replayPlyRequested(ply);
        }
    };
    connect(m_table, &QTableWidget::cellClicked, this, requestReplayPly);
    connect(m_table, &QTableWidget::cellActivated, this, requestReplayPly);
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
    if (m_inlineDetailWidget != nullptr) {
        m_table->removeCellWidget(m_inlineDetailRow, 0);
        m_inlineDetailWidget->deleteLater();
        m_inlineDetailWidget = nullptr;
    }
    m_inlineDetailRow = -1;
    m_currentReplayPly = 0;
    m_selectedExpansionCollapsed = false;
    m_table->clearSpans();
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
    m_truthStatusLabel->setVisible(true);
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
    if (m_inlineDetailWidget != nullptr) {
        m_table->removeCellWidget(m_inlineDetailRow, 0);
        m_inlineDetailWidget->deleteLater();
        m_inlineDetailWidget = nullptr;
    }
    m_inlineDetailRow = -1;
    m_table->clearSpans();
    m_table->clearContents();
    if (currentMainlinePly != m_currentReplayPly) {
        m_selectedExpansionCollapsed = false;
        m_currentReplayPly = currentMainlinePly;
    }

    const bool showInlineDetail = pack.isMechanicalGameBreakdown()
        && !variationActive
        && currentMainlinePly > 0
        && currentMainlinePly <= m_replayMoveCount;
    const int baseRowCount = std::max(1, (m_replayMoveCount + 1) / 2);
    const int selectedBaseRow = showInlineDetail ? (currentMainlinePly - 1) / 2 : -1;
    const int rowCount = baseRowCount + (showInlineDetail ? 1 : 0);
    const auto displayRowForBase = [showInlineDetail, selectedBaseRow](int baseRow) {
        return baseRow + (showInlineDetail && baseRow > selectedBaseRow ? 1 : 0);
    };
    m_table->setRowCount(rowCount);
    const int compactRowHeight = m_table->verticalHeader()->defaultSectionSize();
    for (int row = 0; row < rowCount; ++row) {
        m_table->setRowHeight(row, compactRowHeight);
    }
    m_truthStatusLabel->setVisible(!pack.isMechanicalGameBreakdown());
    m_truthStatusLabel->setText(
        pack.isMechanicalGameBreakdown()
            ? pack.selectiveDeepReview().has_value()
                ? QStringLiteral("Selective deep review · moves not selected for deep review are not certified accurate.")
                : pack.persistedEngineEvidence().has_value()
                ? QStringLiteral("Persisted shallow screening candidates · no deep verdicts or accuracy claims.")
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
            item->setBackground(QColor(47, 73, 102));
            item->setForeground(QColor(142, 190, 239));
            QFont currentFont = item->font();
            currentFont.setBold(true);
            item->setFont(currentFont);
        } else if (isVariationAnchor) {
            item->setBackground(QColor(75, 61, 35));
            item->setForeground(QColor(244, 223, 174));
        }
        return item;
    };

    for (int baseRow = 0; baseRow < baseRowCount; ++baseRow) {
        const int displayRow = displayRowForBase(baseRow);
        m_table->setItem(
            displayRow,
            0,
            makeItem(QString::number(baseRow + 1), false, false));
    }
    for (const auto &move : pack.moves()) {
        const int row = displayRowForBase((move.ply - 1) / 2);
        const int column = move.ply % 2 == 1 ? 1 : 2;
        QString text = move.notation.san;
        if (pack.isMechanicalGameBreakdown()) {
            QStringList annotations;
            QStringList tooltips;
            tooltips << QStringLiteral("Ply %1 · %2 · server-accounted time %3")
                            .arg(move.ply)
                            .arg(move.notation.uci, moveTimeText(move.elapsedMoveMs));
            if (pack.openingLastBookPly().has_value()) {
                if (move.ply == *pack.openingLastBookPly() + 1) {
                    annotations << QStringLiteral("◫");
                    tooltips << openingBoundaryToolTip(pack);
                }
            }
            if (move.elapsedMoveMs.has_value() && *move.elapsedMoveMs == longestElapsed) {
                annotations << QStringLiteral("◷");
                tooltips << QStringLiteral("Longest server-accounted move in this game.");
            }
            if (!move.selectiveDeepMoments.isEmpty()) {
                const auto &deep = move.selectiveDeepMoments.first();
                annotations << QStringLiteral("●");
                tooltips << QStringLiteral("Deep evidence status: %1")
                                .arg(deepEvidenceStatusText(deep.status));
            } else if (pack.selectiveDeepReview().has_value()) {
                tooltips << QStringLiteral(
                    "Not selected for deep review; this does not certify the move as accurate.");
            }
            if (!annotations.isEmpty()) {
                text += QStringLiteral(" · ") + annotations.join(QStringLiteral(" · "));
            }
            auto *item = makeItem(text, !variationActive && currentMainlinePly == move.ply,
                                  variationActive && variationAnchorPly == move.ply);
            const QString accessibleSummary = tooltips.join(QLatin1Char('\n'));
            item->setToolTip(accessibleSummary);
            item->setData(Qt::AccessibleTextRole,
                          text + QLatin1Char('\n') + accessibleSummary);
            item->setData(Qt::UserRole, move.ply);
            m_table->setItem(row, column, item);
            continue;
        } else if (move.severity != QStringLiteral("none")) {
            text += QStringLiteral("  [%1]").arg(move.severity);
        }
        const bool current = !variationActive && currentMainlinePly == move.ply;
        const bool anchor = variationActive && variationAnchorPly == move.ply;
        auto *item = makeItem(text, current, anchor);
        item->setData(Qt::UserRole, move.ply);
        m_table->setItem(row, column, item);
    }

    if (showInlineDetail) {
        m_inlineDetailRow = selectedBaseRow + 1;
        m_table->setSpan(m_inlineDetailRow, 0, 1, 3);
        auto *detailCell = new QTableWidgetItem();
        detailCell->setFlags(Qt::NoItemFlags);
        m_table->setItem(m_inlineDetailRow, 0, detailCell);
        const MoveCoachPresentation presentation = moveCoachPresentation(
            pack, pack.moves().at(currentMainlinePly - 1));
        m_inlineDetailWidget = new InlineMoveDetail(
            presentation,
            [this]() {
                if (m_inlineDetailRow >= 0) {
                    m_table->resizeRowToContents(m_inlineDetailRow);
                }
            },
            m_table);
        m_table->setCellWidget(m_inlineDetailRow, 0, m_inlineDetailWidget);
        m_inlineDetailWidget->adjustSize();
        m_table->resizeRowToContents(m_inlineDetailRow);
        m_table->setRowHidden(m_inlineDetailRow, m_selectedExpansionCollapsed);
    }
    if (!variationActive && currentMainlinePly > 0) {
        const int row = displayRowForBase((currentMainlinePly - 1) / 2);
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
    , m_coachCard(new QFrame(this))
    , m_evidenceStatusLabel(new QLabel(m_coachCard))
    , m_evaluationChangeLabel(new QLabel(m_coachCard))
    , m_explanationLabel(new QLabel(m_coachCard))
    , m_technicalDetailsButton(new QPushButton(QStringLiteral("▸ Details"), m_coachCard))
    , m_focusReadButton(new QPushButton(QStringLiteral("Focus Read"), m_coachCard))
    , m_focusReadFrame(new QFrame(m_coachCard))
    , m_focusBeforeLabel(new QLabel(m_focusReadFrame))
    , m_focusAnchorLabel(new QLabel(m_focusReadFrame))
    , m_focusAfterLabel(new QLabel(m_focusReadFrame))
    , m_focusProgressLabel(new QLabel(m_focusReadFrame))
    , m_focusPreviousButton(new QPushButton(QStringLiteral("←"), m_focusReadFrame))
    , m_focusNextButton(new QPushButton(QStringLiteral("→"), m_focusReadFrame))
    , m_summaryView(new FixedScaleTextEdit(this))
    , m_openButton(new QPushButton(QStringLiteral("Open Analysis Replay"), this))
    , m_backButton(new QPushButton(QStringLiteral("Back to Puzzles"), this))
    , m_showEngineLineButton(new QPushButton(QStringLiteral("Show Supplied Engine Line"), this))
    , m_returnToGameButton(new QPushButton(QStringLiteral("Return to Game"), this))
{
    setObjectName(QStringLiteral("reviewEvidencePanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(4);
    auto *actions = new QHBoxLayout();
    actions->addWidget(m_openButton);
    actions->addWidget(m_gameLabel, 1);
    actions->addStretch(1);
    layout->addLayout(actions);
    for (QLabel *label : {m_gameLabel, m_openingLabel, m_engineLabel}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
    }
    m_gameLabel->setObjectName(QStringLiteral("reviewGameHeader"));
    QFont gameFont = m_gameLabel->font();
    gameFont.setBold(true);
    gameFont.setPointSizeF(gameFont.pointSizeF() + 1.0);
    m_gameLabel->setFont(gameFont);
    m_openingLabel->setObjectName(QStringLiteral("reviewOpeningMeta"));
    m_engineLabel->setObjectName(QStringLiteral("reviewEngineMeta"));
    m_openingLabel->setWordWrap(false);
    m_engineLabel->setWordWrap(false);
    m_openingLabel->setToolTip(QStringLiteral("Retained opening classification."));
    QPalette metaPalette = m_openingLabel->palette();
    metaPalette.setColor(QPalette::WindowText, QColor(156, 165, 178));
    m_openingLabel->setPalette(metaPalette);
    m_engineLabel->setPalette(metaPalette);
    const QString contextChipStyle = QStringLiteral(
        "QLabel { color: #b4bdc9; background: #232930; border: 0; border-radius: 6px; padding: 4px 7px; }");
    m_openingLabel->setStyleSheet(contextChipStyle);
    m_engineLabel->setStyleSheet(contextChipStyle);
    auto *metaRow = new QHBoxLayout();
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->setSpacing(6);
    metaRow->addWidget(m_openingLabel);
    metaRow->addWidget(m_engineLabel);
    metaRow->addStretch(1);
    metaRow->addWidget(m_backButton);
    layout->addLayout(metaRow);

    m_coachCard->setObjectName(QStringLiteral("selectedMoveCoachCard"));
    m_coachCard->setFrameShape(QFrame::NoFrame);
    m_coachCard->setStyleSheet(QStringLiteral(
        "QFrame#selectedMoveCoachCard { background: #1d2127; border: 0; border-radius: 10px; }"
        "QLabel#moveEvidenceStatus { color: #d7be83; background: transparent; border: 0; padding: 0; font-weight: 600; }"
        "QLabel#moveEvaluationChange { color: #c3ccd8; }"
        "QPushButton#technicalDetailsButton { background: transparent; color: #aeb8c5; border: 0; padding: 5px 2px; text-align: left; }"
        "QPushButton#technicalDetailsButton:hover { color: #eef3fa; }"
        "QPushButton#focusReadButton { background: #293545; color: #e7edf5; border: 1px solid #3a4a5f; border-radius: 8px; padding: 5px 9px; }"
        "QPushButton#focusReadButton:hover { background: #324156; }"));
    auto *coachLayout = new QVBoxLayout(m_coachCard);
    coachLayout->setContentsMargins(10, 9, 10, 9);
    coachLayout->setSpacing(7);
    m_evidenceStatusLabel->setObjectName(QStringLiteral("moveEvidenceStatus"));
    m_evidenceStatusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_evaluationChangeLabel->setObjectName(QStringLiteral("moveEvaluationChange"));
    m_evaluationChangeLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_explanationLabel->setObjectName(QStringLiteral("moveCoachExplanation"));
    for (QLabel *label : {m_evidenceStatusLabel, m_evaluationChangeLabel,
                          m_explanationLabel}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    m_evidenceStatusLabel->setWordWrap(false);
    coachLayout->addWidget(m_evidenceStatusLabel);
    coachLayout->addWidget(m_evaluationChangeLabel);
    coachLayout->addWidget(m_explanationLabel);
    auto *coachActions = new QHBoxLayout();
    coachActions->setContentsMargins(0, 0, 0, 0);
    coachActions->setSpacing(7);
    m_technicalDetailsButton->setObjectName(QStringLiteral("technicalDetailsButton"));
    m_focusReadButton->setObjectName(QStringLiteral("focusReadButton"));
    m_technicalDetailsButton->setText(QStringLiteral("▸ Details"));
    m_technicalDetailsButton->setAccessibleName(QStringLiteral("Show technical details"));
    m_focusReadButton->setAccessibleName(QStringLiteral("Open Focus Read"));
    coachActions->addWidget(m_technicalDetailsButton);
    coachActions->addWidget(m_focusReadButton);
    coachActions->addStretch(1);
    coachLayout->addLayout(coachActions);

    m_focusReadFrame->setObjectName(QStringLiteral("focusReadFrame"));
    m_focusReadFrame->setStyleSheet(QStringLiteral(
        "QFrame#focusReadFrame { background: #111419; border: 1px solid #3b424d; border-radius: 14px; }"
        "QLabel#focusReadAnchor { background: #f0e4cf; color: #202020; border: 1px solid #c7a96f; border-radius: 11px; padding: 8px 10px; }"
        "QLabel#focusReadContext { color: #9ca5b2; }"
        "QLabel#focusReadProgress { color: #747e8b; }"));
    auto *focusLayout = new QVBoxLayout(m_focusReadFrame);
    focusLayout->setContentsMargins(10, 10, 10, 8);
    focusLayout->setSpacing(7);
    auto *focusTape = new QHBoxLayout();
    focusTape->setContentsMargins(0, 0, 0, 0);
    focusTape->setSpacing(7);
    m_focusBeforeLabel->setObjectName(QStringLiteral("focusReadContext"));
    m_focusAnchorLabel->setObjectName(QStringLiteral("focusReadAnchor"));
    m_focusAfterLabel->setObjectName(QStringLiteral("focusReadContext"));
    for (QLabel *label : {m_focusBeforeLabel, m_focusAnchorLabel, m_focusAfterLabel}) {
        label->setTextFormat(Qt::PlainText);
    }
    m_focusBeforeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_focusAnchorLabel->setAlignment(Qt::AlignCenter);
    m_focusAfterLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_focusBeforeLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_focusAfterLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_focusAnchorLabel->setFixedWidth(122);
    m_focusAnchorLabel->setMinimumHeight(38);
    QFont focusAnchorFont = m_focusAnchorLabel->font();
    focusAnchorFont.setBold(true);
    focusAnchorFont.setPointSizeF(focusAnchorFont.pointSizeF() + 1.0);
    m_focusAnchorLabel->setFont(focusAnchorFont);
    focusTape->addWidget(m_focusBeforeLabel, 1);
    focusTape->addWidget(m_focusAnchorLabel);
    focusTape->addWidget(m_focusAfterLabel, 1);
    focusLayout->addLayout(focusTape);
    auto *focusControls = new QHBoxLayout();
    focusControls->setContentsMargins(0, 0, 0, 0);
    m_focusProgressLabel->setObjectName(QStringLiteral("focusReadProgress"));
    m_focusProgressLabel->setTextFormat(Qt::PlainText);
    m_focusPreviousButton->setObjectName(QStringLiteral("focusReadPreviousButton"));
    m_focusNextButton->setObjectName(QStringLiteral("focusReadNextButton"));
    m_focusPreviousButton->setAccessibleName(QStringLiteral("Previous Focus Read word"));
    m_focusNextButton->setAccessibleName(QStringLiteral("Next Focus Read word"));
    m_focusPreviousButton->setToolTip(QStringLiteral("Previous Focus Read word (Left Arrow)"));
    m_focusNextButton->setToolTip(QStringLiteral("Next Focus Read word (Right Arrow or Space)"));
    m_focusPreviousButton->setFocusPolicy(Qt::NoFocus);
    m_focusNextButton->setFocusPolicy(Qt::NoFocus);
    focusControls->addWidget(m_focusPreviousButton);
    focusControls->addWidget(m_focusNextButton);
    focusControls->addWidget(m_focusProgressLabel);
    focusControls->addStretch(1);
    auto *focusHint = new QLabel(
        QStringLiteral("←/→ step · Space advances · Esc closes"), m_focusReadFrame);
    focusHint->setTextFormat(Qt::PlainText);
    focusControls->addWidget(focusHint);
    focusLayout->addLayout(focusControls);
    m_focusReadFrame->setVisible(false);
    coachLayout->addWidget(m_focusReadFrame);

    m_summaryView->setReadOnly(true);
    m_summaryView->setObjectName(QStringLiteral("technicalDetailsView"));
    m_summaryView->setStyleSheet(QStringLiteral(
        "QTextEdit { background: #14171b; border: 0; border-radius: 8px; padding: 8px; font-size: 13px; }"));
    m_summaryView->setMinimumHeight(150);
    m_summaryView->setVisible(false);
    coachLayout->addWidget(m_summaryView, 1);
    layout->addWidget(m_coachCard, 1);
    auto *variationActions = new QHBoxLayout();
    variationActions->addWidget(m_showEngineLineButton);
    variationActions->addWidget(m_returnToGameButton);
    variationActions->addStretch(1);
    layout->addLayout(variationActions);

    connect(m_openButton, &QPushButton::clicked, this, &ReplayEvidencePanel::openReplayRequested);
    connect(m_backButton, &QPushButton::clicked, this, &ReplayEvidencePanel::backToPuzzlesRequested);
    connect(m_showEngineLineButton, &QPushButton::clicked, this, &ReplayEvidencePanel::showEngineLineRequested);
    connect(m_returnToGameButton, &QPushButton::clicked, this, &ReplayEvidencePanel::returnToGameRequested);
    connect(m_technicalDetailsButton, &QPushButton::clicked, this, [this]() {
        setTechnicalDetailsVisible(!technicalDetailsVisible());
    });
    connect(m_focusReadButton, &QPushButton::clicked, this, [this]() {
        setFocusReadVisible(!focusReadVisible());
    });
    connect(m_focusPreviousButton, &QPushButton::clicked, this, [this]() {
        stepFocusRead(-1);
        setFocus(Qt::OtherFocusReason);
    });
    connect(m_focusNextButton, &QPushButton::clicked, this, [this]() {
        stepFocusRead(1);
        setFocus(Qt::OtherFocusReason);
    });
    m_backButton->setFlat(true);
    m_backButton->setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setEmptyState();
}

void ReplayEvidencePanel::setInlineCoachMode(bool enabled)
{
    m_inlineCoachMode = enabled;
    if (m_inlineCoachMode) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        setMaximumHeight(56);
        m_coachCard->setVisible(false);
        m_summaryView->setVisible(false);
        setFocusReadVisible(false);
    } else {
        setMaximumHeight(QWIDGETSIZE_MAX);
    }
}

void ReplayEvidencePanel::setEmptyState()
{
    setStyleSheet(QString());
    setTitle(QStringLiteral("analysis replay"));
    m_gameLabel->setVisible(true);
    m_gameLabel->setText(QStringLiteral("No annotated replay loaded."));
    m_openingLabel->clear();
    m_engineLabel->setText(QStringLiteral("Import is read-only and does not run Stockfish or use the network."));
    m_summaryView->setPlainText(
        QStringLiteral("Open an annotated-game-replay-v1 JSON file produced by the esports evidence pipeline."));
    for (QWidget *widget : {static_cast<QWidget *>(m_evidenceStatusLabel),
                            static_cast<QWidget *>(m_evaluationChangeLabel),
                            static_cast<QWidget *>(m_explanationLabel),
                            static_cast<QWidget *>(m_technicalDetailsButton),
                            static_cast<QWidget *>(m_focusReadButton)}) {
        widget->setVisible(false);
    }
    m_coachCard->setVisible(true);
    m_summaryView->setVisible(true);
    setFocusReadVisible(false);
    m_lastMechanicalPly = -1;
    m_openButton->setVisible(true);
    m_backButton->setText(QStringLiteral("Back to Puzzles"));
    m_backButton->setEnabled(false);
    m_showEngineLineButton->setEnabled(false);
    m_returnToGameButton->setEnabled(false);
    m_showEngineLineButton->setVisible(true);
    m_returnToGameButton->setVisible(true);
    if (m_inlineCoachMode) {
        m_coachCard->setVisible(false);
        m_summaryView->setVisible(false);
    }
}

void ReplayEvidencePanel::setReplayState(
    const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
    const parlawl::puzzle_runner::ReplaySession &session,
    int variationAnchorPly)
{
    if (pack.isMechanicalGameBreakdown()) {
        setTitle(QString());
        setStyleSheet(QStringLiteral(
            "QGroupBox { border: 0; margin: 0; padding: 0; }"));
        m_openButton->setVisible(false);
        m_gameLabel->setVisible(false);
        m_backButton->setText(QStringLiteral("Explorer"));
        m_backButton->setToolTip(QStringLiteral("Return to the player explorer"));
        m_backButton->setEnabled(true);
        m_gameLabel->setText(
            QStringLiteral("%1 %2  ·  %3  ·  %4 %5\n%6")
                .arg(pack.whiteUsername())
                .arg(pack.whiteRating())
                .arg(pack.result())
                .arg(pack.blackUsername())
                .arg(pack.blackRating())
                .arg(compactReviewDate(pack.eventStartUtc())));
        m_openingLabel->setText(compactOpeningText(pack));
        m_openingLabel->setToolTip(openingBoundaryToolTip(pack));
        if (pack.selectiveDeepReview().has_value()) {
            const auto &deep = *pack.selectiveDeepReview();
            m_engineLabel->setText(
                QStringLiteral("%1 · %2 nodes · %3 selected")
                    .arg(compactEngineName(deep.engineName))
                    .arg(compactNodeCount(deep.nodeLimit))
                    .arg(deep.moments.size()));
            m_engineLabel->setToolTip(QStringLiteral(
                "Retained selective deep-review evidence only. ParlAWL starts no engine process here."));
        } else if (pack.persistedEngineEvidence().has_value()) {
            const auto &engine = *pack.persistedEngineEvidence();
            m_engineLabel->setText(
                QStringLiteral("%1 · %2 nodes · shallow")
                    .arg(compactEngineName(engine.engineName))
                    .arg(compactNodeCount(engine.nodeLimit)));
            m_engineLabel->setToolTip(QStringLiteral(
                "Retained shallow screening evidence only. ParlAWL starts no engine process here."));
        } else {
            m_engineLabel->setText(QStringLiteral("recorded replay"));
            m_engineLabel->setToolTip(QStringLiteral(
                "Engine evidence is not joined. ParlAWL starts no engine process here."));
        }

        const int currentPly = session.currentMainlinePly();
        if (currentPly != m_lastMechanicalPly) {
            setTechnicalDetailsVisible(false);
            setFocusReadVisible(false);
            m_lastMechanicalPly = currentPly;
        }

        if (currentPly == 0) {
            m_evidenceStatusLabel->clear();
            m_evidenceStatusLabel->setVisible(false);
            m_evaluationChangeLabel->clear();
            m_evaluationChangeLabel->setVisible(false);
            m_explanationLabel->clear();
            m_explanationLabel->setVisible(false);
            m_focusReadWords.clear();
            m_focusReadButton->setEnabled(false);
            m_technicalDetailsButton->setVisible(false);
            m_focusReadButton->setVisible(false);
            m_coachCard->setVisible(false);
            QStringList overview = mechanicalGameReportLines(pack);
            if (overview.isEmpty()) {
                overview << QStringLiteral("No joined engine evidence is available for this recorded legal replay.");
            }
            m_summaryView->setPlainText(overview.join(QLatin1Char('\n')));
            m_summaryView->setVisible(false);
        } else {
            const auto &move = pack.moves().at(currentPly - 1);
            const MoveCoachPresentation presentation = moveCoachPresentation(pack, move);
            m_evidenceStatusLabel->setText(presentation.status);
            m_evaluationChangeLabel->setText(presentation.evaluationChange);
            m_explanationLabel->setText(presentation.explanation);
            const bool showCompactCoach = presentation.hasDeepMoment
                || presentation.hasShallowOnlyEvidence;
            m_evidenceStatusLabel->setVisible(showCompactCoach);
            m_evaluationChangeLabel->setVisible(
                showCompactCoach && !presentation.evaluationChange.isEmpty());
            m_explanationLabel->setVisible(
                presentation.hasDeepMoment && !presentation.explanation.isEmpty());
            m_technicalDetailsButton->setVisible(presentation.hasDeepMoment);
            m_focusReadButton->setVisible(presentation.hasDeepMoment);
            m_coachCard->setVisible(showCompactCoach);
            m_focusReadWords = presentation.hasDeepMoment
                ? presentation.focusReadText.split(QLatin1Char(' '), Qt::SkipEmptyParts)
                : QStringList {};
            m_focusReadWordIndex = 0;
            m_focusReadButton->setEnabled(!m_focusReadWords.isEmpty());
            m_technicalDetailsButton->setText(QStringLiteral("▸ Details"));
            m_summaryView->setPlainText(
                presentation.technicalDetails.join(QLatin1Char('\n')));
            updateFocusReadViewport();
        }
        m_showEngineLineButton->setEnabled(false);
        m_returnToGameButton->setEnabled(false);
        m_showEngineLineButton->setVisible(false);
        m_returnToGameButton->setVisible(false);
        if (m_inlineCoachMode) {
            m_coachCard->setVisible(false);
            m_summaryView->setVisible(false);
            setFocusReadVisible(false);
        }
        return;
    }

    setStyleSheet(QString());
    m_gameLabel->setVisible(true);
    for (QWidget *widget : {static_cast<QWidget *>(m_evidenceStatusLabel),
                            static_cast<QWidget *>(m_evaluationChangeLabel),
                            static_cast<QWidget *>(m_explanationLabel),
                            static_cast<QWidget *>(m_technicalDetailsButton),
                            static_cast<QWidget *>(m_focusReadButton)}) {
        widget->setVisible(false);
    }
    setFocusReadVisible(false);
    m_summaryView->setVisible(true);
    m_lastMechanicalPly = -1;
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

QString ReplayEvidencePanel::evidenceStatusText() const
{
    return m_evidenceStatusLabel->text();
}

QString ReplayEvidencePanel::explanationText() const
{
    return m_explanationLabel->text();
}

bool ReplayEvidencePanel::technicalDetailsVisible() const
{
    return m_summaryView->isVisible();
}

bool ReplayEvidencePanel::focusReadVisible() const
{
    return m_focusReadFrame->isVisible();
}

QString ReplayEvidencePanel::focusReadAnchorText() const
{
    return m_focusAnchorLabel->text();
}

bool ReplayEvidencePanel::primaryCoachVisible() const
{
    return !m_inlineCoachMode
        && m_coachCard->isVisible()
        && m_evidenceStatusLabel->isVisible()
        && m_explanationLabel->isVisible();
}

void ReplayEvidencePanel::setTechnicalDetailsVisible(bool visible)
{
    m_summaryView->setVisible(visible);
    m_technicalDetailsButton->setText(
        visible ? QStringLiteral("▾ Details") : QStringLiteral("▸ Details"));
    m_technicalDetailsButton->setAccessibleName(
        visible ? QStringLiteral("Hide technical details")
                : QStringLiteral("Show technical details"));
}

void ReplayEvidencePanel::setFocusReadVisible(bool visible)
{
    const bool show = visible && !m_focusReadWords.isEmpty();
    m_focusReadFrame->setVisible(show);
    m_focusReadButton->setText(
        show ? QStringLiteral("Close Focus Read") : QStringLiteral("Focus Read"));
    if (show) {
        updateFocusReadViewport();
        setFocus(Qt::OtherFocusReason);
    }
}

void ReplayEvidencePanel::updateFocusReadViewport()
{
    if (m_focusReadWords.isEmpty()) {
        m_focusBeforeLabel->clear();
        m_focusAnchorLabel->clear();
        m_focusAfterLabel->clear();
        m_focusProgressLabel->clear();
        return;
    }
    m_focusReadWordIndex = std::clamp(
        m_focusReadWordIndex, 0, static_cast<int>(m_focusReadWords.size()) - 1);
    constexpr int contextWords = 5;
    const int beforeStart = std::max(0, m_focusReadWordIndex - contextWords);
    const int afterCount = std::min(
        contextWords, static_cast<int>(m_focusReadWords.size()) - m_focusReadWordIndex - 1);
    QString before = m_focusReadWords.mid(
        beforeStart, m_focusReadWordIndex - beforeStart).join(QLatin1Char(' '));
    QString after = m_focusReadWords.mid(
        m_focusReadWordIndex + 1, afterCount).join(QLatin1Char(' '));
    if (beforeStart > 0) {
        before.prepend(QStringLiteral("… "));
    }
    if (m_focusReadWordIndex + 1 + afterCount < m_focusReadWords.size()) {
        after.append(QStringLiteral(" …"));
    }
    m_focusBeforeLabel->setText(before);
    m_focusAnchorLabel->setText(m_focusReadWords.at(m_focusReadWordIndex));
    m_focusAfterLabel->setText(after);
    m_focusProgressLabel->setText(
        QStringLiteral("%1 / %2")
            .arg(m_focusReadWordIndex + 1)
            .arg(m_focusReadWords.size()));
    m_focusPreviousButton->setEnabled(m_focusReadWordIndex > 0);
    m_focusNextButton->setEnabled(m_focusReadWordIndex + 1 < m_focusReadWords.size());
}

void ReplayEvidencePanel::stepFocusRead(int delta)
{
    if (m_focusReadWords.isEmpty() || delta == 0) {
        return;
    }
    m_focusReadWordIndex = std::clamp(
        m_focusReadWordIndex + delta, 0, static_cast<int>(m_focusReadWords.size()) - 1);
    updateFocusReadViewport();
}

void ReplayEvidencePanel::keyPressEvent(QKeyEvent *event)
{
    if (focusReadVisible()) {
        if (event->key() == Qt::Key_Left) {
            stepFocusRead(-1);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Space) {
            stepFocusRead(1);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            setFocusReadVisible(false);
            event->accept();
            return;
        }
    }
    QGroupBox::keyPressEvent(event);
}

CoachReviewPanel::CoachReviewPanel(QWidget *parent)
    : QWidget(parent)
    , m_unavailableLabel(new QLabel(QStringLiteral("Review summary not generated"), this))
    , m_card(new QFrame(this))
    , m_openingChip(new QLabel(m_card))
    , m_timingChip(new QLabel(m_card))
    , m_counterChip(new QLabel(m_card))
    , m_titleLabel(new QLabel(m_card))
    , m_mechanicalFrame(new QFrame(m_card))
    , m_mechanicalHeadlineLabel(new QLabel(m_mechanicalFrame))
    , m_mechanicalComparisonLabel(new QLabel(m_mechanicalFrame))
    , m_mechanicalStatusLabel(new QLabel(m_mechanicalFrame))
    , m_mechanicalFactsWidget(new QWidget(m_mechanicalFrame))
    , m_mechanicalFactsLayout(new QVBoxLayout(m_mechanicalFactsWidget))
    , m_summaryLabel(new QLabel(m_card))
    , m_comparisonLabel(new QLabel(m_card))
    , m_scoreLabel(new QLabel(m_card))
    , m_contextLabel(new QLabel(m_card))
    , m_previousMomentButton(new QPushButton(QStringLiteral("← Previous critical moment"), m_card))
    , m_nextMomentButton(new QPushButton(QStringLiteral("Next critical moment →"), m_card))
    , m_bestLineButton(new QPushButton(QStringLiteral("Show best line"), m_card))
    , m_playedLineButton(new QPushButton(QStringLiteral("Show played line"), m_card))
    , m_linePreviewLabel(new QLabel(m_card))
    , m_detailsButton(new QPushButton(QStringLiteral("▸ Detailed Evidence"), m_card))
    , m_detailsView(new FixedScaleTextEdit(m_card))
    , m_focusReadButton(new QPushButton(QStringLiteral("Focus Read"), m_card))
    , m_focusReadFrame(new QFrame(m_card))
    , m_focusBeforeLabel(new QLabel(m_focusReadFrame))
    , m_focusAnchorLabel(new QLabel(m_focusReadFrame))
    , m_focusAfterLabel(new QLabel(m_focusReadFrame))
    , m_focusProgressLabel(new QLabel(m_focusReadFrame))
    , m_focusStartPauseButton(new QPushButton(QStringLiteral("Start"), m_focusReadFrame))
    , m_focusPreviousButton(new QPushButton(QStringLiteral("← Step"), m_focusReadFrame))
    , m_focusNextButton(new QPushButton(QStringLiteral("Step →"), m_focusReadFrame))
    , m_focusReplayButton(new QPushButton(QStringLiteral("Replay"), m_focusReadFrame))
    , m_focusTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("coachReviewPanel"));
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_unavailableLabel->setObjectName(QStringLiteral("coachReviewUnavailable"));
    m_unavailableLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_unavailableLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    layout->addWidget(m_unavailableLabel, 0, Qt::AlignTop);

    m_card->setObjectName(QStringLiteral("coachReviewCard"));
    m_card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(14, 13, 14, 13);
    cardLayout->setSpacing(10);
    cardLayout->setAlignment(Qt::AlignTop);

    auto *chips = new QHBoxLayout();
    chips->setSpacing(6);
    m_openingChip->setObjectName(QStringLiteral("coachOpeningChip"));
    m_timingChip->setObjectName(QStringLiteral("coachTimingChip"));
    m_counterChip->setObjectName(QStringLiteral("coachMomentCounterChip"));
    for (QLabel *chip : {m_openingChip, m_timingChip, m_counterChip}) {
        chip->setTextFormat(Qt::PlainText);
        chip->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        chip->setProperty("uiRole", QStringLiteral("chip"));
        chips->addWidget(chip);
    }
    chips->addStretch(1);
    cardLayout->addLayout(chips);

    m_titleLabel->setObjectName(QStringLiteral("coachReviewTitle"));
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4.0);
    m_titleLabel->setFont(titleFont);
    m_summaryLabel->setObjectName(QStringLiteral("coachReviewSummary"));
    m_comparisonLabel->setObjectName(QStringLiteral("coachReviewMoveComparison"));
    m_scoreLabel->setObjectName(QStringLiteral("coachReviewScoreComparison"));
    m_contextLabel->setObjectName(QStringLiteral("coachReviewContext"));
    for (QLabel *label : {m_titleLabel, m_summaryLabel, m_comparisonLabel,
                          m_scoreLabel, m_contextLabel, m_linePreviewLabel}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    m_scoreLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    cardLayout->addWidget(m_titleLabel);

    m_mechanicalFrame->setObjectName(QStringLiteral("coachMechanicalExplanation"));
    auto *mechanicalLayout = new QVBoxLayout(m_mechanicalFrame);
    mechanicalLayout->setContentsMargins(10, 9, 10, 9);
    mechanicalLayout->setSpacing(7);
    m_mechanicalHeadlineLabel->setObjectName(QStringLiteral("coachMechanicalHeadline"));
    m_mechanicalComparisonLabel->setObjectName(QStringLiteral("coachMechanicalComparison"));
    m_mechanicalStatusLabel->setObjectName(QStringLiteral("coachMechanicalStatus"));
    for (QLabel *label : {m_mechanicalHeadlineLabel, m_mechanicalComparisonLabel,
                          m_mechanicalStatusLabel}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    m_mechanicalComparisonLabel->setProperty("uiRole", QStringLiteral("comparisonWarning"));
    m_mechanicalStatusLabel->setProperty("uiRole", QStringLiteral("mechanicalStatus"));
    m_mechanicalStatusLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    m_mechanicalFactsLayout->setContentsMargins(0, 0, 0, 0);
    m_mechanicalFactsLayout->setSpacing(5);
    mechanicalLayout->addWidget(m_mechanicalHeadlineLabel);
    mechanicalLayout->addWidget(m_mechanicalComparisonLabel);
    mechanicalLayout->addWidget(m_mechanicalStatusLabel, 0, Qt::AlignLeft);
    mechanicalLayout->addWidget(m_mechanicalFactsWidget);
    m_mechanicalFrame->hide();
    cardLayout->addWidget(m_mechanicalFrame);
    cardLayout->addWidget(m_summaryLabel);
    cardLayout->addWidget(m_comparisonLabel);
    cardLayout->addWidget(m_scoreLabel);
    cardLayout->addWidget(m_contextLabel);

    auto *momentControls = new QHBoxLayout();
    m_previousMomentButton->setObjectName(QStringLiteral("previousCriticalMomentButton"));
    m_nextMomentButton->setObjectName(QStringLiteral("nextCriticalMomentButton"));
    m_nextMomentButton->setProperty("uiRole", QStringLiteral("primary"));
    momentControls->addWidget(m_previousMomentButton);
    momentControls->addWidget(m_nextMomentButton);
    momentControls->addStretch(1);
    cardLayout->addLayout(momentControls);

    auto *lineControls = new QHBoxLayout();
    m_bestLineButton->setObjectName(QStringLiteral("showBestRetainedLineButton"));
    m_playedLineButton->setObjectName(QStringLiteral("showPlayedRetainedLineButton"));
    m_bestLineButton->setCheckable(true);
    m_playedLineButton->setCheckable(true);
    lineControls->addWidget(m_bestLineButton);
    lineControls->addWidget(m_playedLineButton);
    lineControls->addStretch(1);
    cardLayout->addLayout(lineControls);
    m_linePreviewLabel->setObjectName(QStringLiteral("retainedLinePreview"));
    cardLayout->addWidget(m_linePreviewLabel);

    auto *readingActions = new QHBoxLayout();
    m_detailsButton->setObjectName(QStringLiteral("coachDetailedEvidenceButton"));
    m_focusReadButton->setObjectName(QStringLiteral("coachFocusReadButton"));
    m_detailsButton->setProperty("uiRole", QStringLiteral("quiet"));
    m_focusReadButton->setProperty("uiRole", QStringLiteral("quiet"));
    readingActions->addWidget(m_detailsButton);
    readingActions->addWidget(m_focusReadButton);
    readingActions->addStretch(1);
    cardLayout->addLayout(readingActions);

    m_focusReadFrame->setObjectName(QStringLiteral("coachFocusReadFrame"));
    auto *focusLayout = new QVBoxLayout(m_focusReadFrame);
    focusLayout->setContentsMargins(9, 9, 9, 8);
    auto *tape = new QHBoxLayout();
    m_focusBeforeLabel->setObjectName(QStringLiteral("coachFocusContext"));
    m_focusAnchorLabel->setObjectName(QStringLiteral("coachFocusAnchor"));
    m_focusAfterLabel->setObjectName(QStringLiteral("coachFocusContext"));
    m_focusBeforeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_focusAnchorLabel->setAlignment(Qt::AlignCenter);
    m_focusAfterLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_focusBeforeLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_focusAfterLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tape->addWidget(m_focusBeforeLabel, 1);
    tape->addWidget(m_focusAnchorLabel);
    tape->addWidget(m_focusAfterLabel, 1);
    focusLayout->addLayout(tape);
    auto *focusControls = new QHBoxLayout();
    m_focusStartPauseButton->setObjectName(QStringLiteral("coachFocusStartPauseButton"));
    m_focusPreviousButton->setObjectName(QStringLiteral("coachFocusPreviousButton"));
    m_focusNextButton->setObjectName(QStringLiteral("coachFocusNextButton"));
    m_focusReplayButton->setObjectName(QStringLiteral("coachFocusReplayButton"));
    m_focusProgressLabel->setObjectName(QStringLiteral("coachFocusProgress"));
    focusControls->addWidget(m_focusStartPauseButton);
    focusControls->addWidget(m_focusPreviousButton);
    focusControls->addWidget(m_focusNextButton);
    focusControls->addWidget(m_focusReplayButton);
    focusControls->addWidget(m_focusProgressLabel);
    focusControls->addStretch(1);
    focusLayout->addLayout(focusControls);
    m_focusReadFrame->hide();
    cardLayout->addWidget(m_focusReadFrame);

    m_detailsView->setObjectName(QStringLiteral("coachDetailedEvidenceView"));
    m_detailsView->setReadOnly(true);
    m_detailsView->setLineWrapMode(QTextEdit::WidgetWidth);
    m_detailsView->setMinimumHeight(320);
    m_detailsView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_detailsView->hide();
    cardLayout->addWidget(m_detailsView, 1);
    layout->addWidget(m_card, 0, Qt::AlignTop);
    layout->addStretch(1);

    setStyleSheet(QStringLiteral(
        "QWidget#coachReviewPanel { background: #181b20; }"
        "QLabel#coachReviewUnavailable { color: #98a3af; background: #20252b; border: 0; border-radius: 8px; padding: 14px; }"
        "QLabel[uiRole=\"chip\"] { color: #b8c1cc; background: #252b33; border: 0; border-radius: 6px; padding: 4px 7px; }"
        "QLabel#coachReviewTitle { color: #f0f3f6; }"
        "QFrame#coachMechanicalExplanation { background: #171c22; border: 0; border-radius: 8px; }"
        "QLabel#coachMechanicalHeadline { color: #edf2f7; font-size: 15px; font-weight: 600; }"
        "QLabel#coachMechanicalComparison { color: #b9cae0; background: #27384b; border: 0; border-radius: 6px; padding: 6px 8px; }"
        "QLabel#coachMechanicalStatus { color: #aeb8c5; background: #242b33; border: 0; border-radius: 5px; padding: 3px 6px; }"
        "QLabel[uiRole=\"mechanicalScope\"] { color: #8f9ba8; font-size: 11px; font-weight: 600; }"
        "QLabel[uiRole=\"mechanicalFact\"] { color: #d4dbe4; background: #1d232a; border: 0; border-radius: 5px; padding: 6px 8px; }"
        "QLabel#coachReviewSummary { color: #dfe5eb; font-size: 15px; }"
        "QLabel#coachReviewMoveComparison { color: #d5dce5; font-weight: 600; }"
        "QLabel#coachReviewScoreComparison { color: #aeb9c7; font-size: 12px; }"
        "QLabel#coachReviewContext { color: #929daa; }"
        "QLabel#retainedLinePreview { color: #aeb8c5; background: #15181c; border: 0; border-radius: 6px; padding: 8px; font-family: monospace; }"
        "QWidget#coachReviewPanel QPushButton { background: #2a2f37; color: #dce2e9; border: 0; border-radius: 6px; padding: 6px 10px; }"
        "QWidget#coachReviewPanel QPushButton:hover { background: #353c46; color: #f5f7fa; }"
        "QWidget#coachReviewPanel QPushButton:disabled { background: #20242a; color: #626b76; }"
        "QWidget#coachReviewPanel QPushButton:checked { background: #344a62; color: #f1f6fb; }"
        "QWidget#coachReviewPanel QPushButton[uiRole=\"primary\"] { background: #31455b; color: #f0f5fb; }"
        "QWidget#coachReviewPanel QPushButton[uiRole=\"quiet\"] { background: transparent; color: #aeb8c5; padding-left: 2px; padding-right: 8px; }"
        "QWidget#coachReviewPanel QPushButton[uiRole=\"quiet\"]:hover { background: #252b33; color: #f0f4f8; }"
        "QFrame#coachFocusReadFrame { background: #111419; border: 0; border-radius: 8px; }"
        "QLabel#coachFocusAnchor { background: #edf1f5; color: #20252b; border: 0; border-radius: 7px; padding: 7px 9px; font-weight: 600; }"
        "QLabel#coachFocusContext { color: #8f9aa7; }"
        "QTextEdit#coachDetailedEvidenceView { color: #cbd3dc; background: #14171b; border: 0; border-radius: 8px; padding: 9px; font-size: 13px; }"));

    m_focusTimer->setInterval(520);
    connect(m_focusTimer, &QTimer::timeout, this, [this]() {
        if (m_focusWordIndex + 1 >= m_focusWords.size()) {
            setFocusPlaybackRunning(false);
            return;
        }
        ++m_focusWordIndex;
        updateFocusRead();
    });
    connect(m_previousMomentButton, &QPushButton::clicked, this, [this]() {
        setCurrentMoment(m_currentMomentVectorIndex - 1, true);
    });
    connect(m_nextMomentButton, &QPushButton::clicked, this, [this]() {
        setCurrentMoment(m_currentMomentVectorIndex + 1, true);
    });
    connect(m_bestLineButton, &QPushButton::clicked, this, [this](bool checked) {
        if (!m_review.has_value() || m_review->criticalMoments.isEmpty()) return;
        const auto &moment = m_review->criticalMoments.at(m_currentMomentVectorIndex);
        const auto &move = m_review->moves.at(moment.ply - 1);
        m_playedLineButton->setChecked(false);
        if (checked) {
            m_linePreviewLabel->setText(
                QStringLiteral("Retained best line · %1").arg(moment.bestLineUci.join(QLatin1Char(' '))));
            m_linePreviewLabel->show();
            emit linePreviewRequested(move.beforeFen, moment.bestMoveUci,
                QStringLiteral("Best retained line · %1")
                    .arg(moment.bestMoveSan.value_or(moment.bestMoveUci)));
        } else {
            refreshCard();
            emit linePreviewRequested(move.beforeFen, moment.playedUci,
                QStringLiteral("Before %1").arg(moment.playedSan));
        }
    });
    connect(m_playedLineButton, &QPushButton::clicked, this, [this](bool checked) {
        if (!m_review.has_value() || m_review->criticalMoments.isEmpty()) return;
        const auto &moment = m_review->criticalMoments.at(m_currentMomentVectorIndex);
        const auto &move = m_review->moves.at(moment.ply - 1);
        m_bestLineButton->setChecked(false);
        if (checked) {
            m_linePreviewLabel->setText(
                QStringLiteral("Retained played line · %1").arg(moment.playedLineUci.join(QLatin1Char(' '))));
            m_linePreviewLabel->show();
            emit linePreviewRequested(move.beforeFen, moment.playedUci,
                QStringLiteral("Played retained line · %1").arg(moment.playedSan));
        } else {
            refreshCard();
            emit linePreviewRequested(move.beforeFen, moment.playedUci,
                QStringLiteral("Before %1").arg(moment.playedSan));
        }
    });
    connect(m_detailsButton, &QPushButton::clicked, this, [this]() {
        const bool visible = !m_detailsView->isVisible();
        m_detailsView->setVisible(visible);
        m_detailsButton->setText(visible
            ? QStringLiteral("▾ Detailed Evidence")
            : QStringLiteral("▸ Detailed Evidence"));
        if (visible) m_detailsView->verticalScrollBar()->setValue(0);
    });
    connect(m_focusReadButton, &QPushButton::clicked, this, [this]() {
        const bool visible = !m_focusReadFrame->isVisible() && !m_focusWords.isEmpty();
        m_focusReadFrame->setVisible(visible);
        m_focusReadButton->setText(visible
            ? QStringLiteral("Close Focus Read") : QStringLiteral("Focus Read"));
        if (!visible) setFocusPlaybackRunning(false);
        if (visible) {
            updateFocusRead();
            setFocus(Qt::OtherFocusReason);
        }
    });
    connect(m_focusStartPauseButton, &QPushButton::clicked, this, [this]() {
        if (m_focusTimer->isActive()) {
            setFocusPlaybackRunning(false);
        } else {
            if (m_focusWordIndex + 1 >= m_focusWords.size()) m_focusWordIndex = 0;
            setFocusPlaybackRunning(true);
        }
    });
    connect(m_focusPreviousButton, &QPushButton::clicked, this, [this]() {
        setFocusPlaybackRunning(false);
        stepFocusRead(-1);
    });
    connect(m_focusNextButton, &QPushButton::clicked, this, [this]() {
        setFocusPlaybackRunning(false);
        stepFocusRead(1);
    });
    connect(m_focusReplayButton, &QPushButton::clicked, this, [this]() {
        m_focusWordIndex = 0;
        updateFocusRead();
        setFocusPlaybackRunning(true);
    });
    setReview(nullptr, nullptr, nullptr, 0);
}

void CoachReviewPanel::setReview(
    const parlawl::puzzle_runner::GameReviewDisplay *review,
    const parlawl::puzzle_runner::GameReviewMechanicalExplanation *explanation,
    const parlawl::puzzle_runner::GameReviewCoverageEntry *coverage,
    int currentPly,
    const QString &unavailableMessage)
{
    const QString previousSource = m_review.has_value() ? m_review->sourceGameId : QString();
    if (review == nullptr) {
        m_review.reset();
        m_explanation.reset();
        m_coverage = coverage != nullptr
            ? std::optional<parlawl::puzzle_runner::GameReviewCoverageEntry>(*coverage)
            : std::nullopt;
        m_card->hide();
        if (!unavailableMessage.isEmpty()) {
            m_unavailableLabel->setText(unavailableMessage);
            m_unavailableLabel->setToolTip(QStringLiteral(
                "Local sidecar join diagnostic; no review content was attached."));
            m_unavailableLabel->setStyleSheet(
                QStringLiteral("color: #d8c6a4; background: #302a22; border: 0; border-left: 3px solid #9a7449; border-radius: 8px; padding: 14px;"));
        } else if (m_coverage.has_value()) {
            m_unavailableLabel->setText(
                m_coverage->statusLabel + QLatin1Char('\n') + m_coverage->statusDetail);
            m_unavailableLabel->setToolTip(
                QStringLiteral("screening_status: %1").arg(m_coverage->screeningStatus));
            m_unavailableLabel->setStyleSheet(
                m_coverage->reviewStatus == QStringLiteral("analysis_incomplete")
                    ? QStringLiteral("color: #d8c6a4; background: #302a22; border: 0; border-left: 3px solid #9a7449; border-radius: 8px; padding: 14px;")
                    : QStringLiteral("color: #b8c2ce; background: #20252b; border: 0; border-left: 3px solid #516476; border-radius: 8px; padding: 14px;"));
        } else {
            m_unavailableLabel->setText(QStringLiteral("Review summary not generated"));
            m_unavailableLabel->setToolTip(QString());
            m_unavailableLabel->setStyleSheet(QString());
        }
        m_unavailableLabel->show();
        setFocusPlaybackRunning(false);
        return;
    }
    const bool changedGame = previousSource != review->sourceGameId;
    m_review = *review;
    m_explanation = explanation != nullptr
        ? std::optional<parlawl::puzzle_runner::GameReviewMechanicalExplanation>(*explanation)
        : std::nullopt;
    m_coverage = coverage != nullptr
        ? std::optional<parlawl::puzzle_runner::GameReviewCoverageEntry>(*coverage)
        : std::nullopt;
    if (changedGame) m_currentMomentVectorIndex = 0;
    if (const auto *moment = m_review->firstMomentAtPly(currentPly); moment != nullptr) {
        m_currentMomentVectorIndex = moment->reviewIndex - 1;
    }
    m_unavailableLabel->hide();
    m_card->show();
    refreshCard();
}

bool CoachReviewPanel::hasMomentAtPly(int ply) const
{
    return m_review.has_value() && m_review->firstMomentAtPly(ply) != nullptr;
}

bool CoachReviewPanel::selectMomentAtPly(int ply)
{
    if (!m_review.has_value()) return false;
    const auto *moment = m_review->firstMomentAtPly(ply);
    if (moment == nullptr) return false;
    setCurrentMoment(moment->reviewIndex - 1, false);
    return true;
}

int CoachReviewPanel::currentReviewIndex() const
{
    if (!m_review.has_value() || m_review->criticalMoments.isEmpty()) return 0;
    return m_review->criticalMoments.at(m_currentMomentVectorIndex).reviewIndex;
}

void CoachReviewPanel::setCurrentMoment(int vectorIndex, bool requestBoardPosition)
{
    if (!m_review.has_value() || vectorIndex < 0
        || vectorIndex >= m_review->criticalMoments.size()) return;
    m_currentMomentVectorIndex = vectorIndex;
    refreshCard();
    if (requestBoardPosition) {
        const auto &moment = m_review->criticalMoments.at(vectorIndex);
        const auto &move = m_review->moves.at(moment.ply - 1);
        emit criticalMomentRequested(moment.ply, move.beforeFen, moment.playedUci,
            QStringLiteral("Before %1").arg(moment.playedSan));
    }
}

void CoachReviewPanel::refreshCard()
{
    if (!m_review.has_value() || m_review->criticalMoments.isEmpty()) return;
    setFocusPlaybackRunning(false);
    const auto &moment = m_review->criticalMoments.at(m_currentMomentVectorIndex);
    m_openingChip->setText(QStringLiteral("%1 · %2")
        .arg(m_review->openingEco.value_or(QStringLiteral("Opening")),
             m_review->openingName.value_or(m_review->openingStatus)));
    m_timingChip->setText(moment.elapsedMoveMs.has_value()
        ? QStringLiteral("%1s server-accounted · %2")
              .arg(QString::number(*moment.elapsedMoveMs / 1000.0, 'f', 1), moment.phase)
        : QStringLiteral("server-accounted time unavailable · %1").arg(moment.phase));
    m_timingChip->setToolTip(QStringLiteral(
        "Server-accounted clock difference; not a measure of cognitive time."));
    m_counterChip->setText(QStringLiteral("Moment %1 of %2")
        .arg(moment.reviewIndex).arg(m_review->criticalMoments.size()));
    m_titleLabel->setText(moment.title);
    m_summaryLabel->setText(moment.summary);
    m_comparisonLabel->setText(
        QStringLiteral("Played  %1    |    Best retained move  %2")
            .arg(moment.playedSan, moment.bestMoveSan.value_or(moment.bestMoveUci)));
    const QString bestScore = moment.bestCentipawnsMover.has_value()
        ? QString::number(*moment.bestCentipawnsMover / 100.0, 'f', 2) : QStringLiteral("—");
    const QString playedScore = moment.playedCentipawnsMover.has_value()
        ? QString::number(*moment.playedCentipawnsMover / 100.0, 'f', 2) : QStringLiteral("—");
    const QString expectation = moment.expectationLossMillionths.has_value()
        ? QStringLiteral("−%1 pp").arg(
              QString::number(*moment.expectationLossMillionths / 10000.0, 'f', 2))
        : QStringLiteral("—");
    m_scoreLabel->setText(QStringLiteral("Eval  %1 → %2    ◒ %3")
        .arg(bestScore, playedScore, expectation));
    m_scoreLabel->setToolTip(QStringLiteral(
        "Mover-perspective engine scores and retained expectation loss."));
    m_contextLabel->setText(QStringLiteral("%1 · %2")
        .arg(moment.playerUsername, moment.phase));
    rebuildMechanicalFacts();
    m_previousMomentButton->setEnabled(m_currentMomentVectorIndex > 0);
    m_nextMomentButton->setEnabled(
        m_currentMomentVectorIndex + 1 < m_review->criticalMoments.size());
    m_bestLineButton->setChecked(false);
    m_playedLineButton->setChecked(false);
    m_linePreviewLabel->clear();
    m_linePreviewLabel->hide();
    m_detailsView->setPlainText(detailedEvidenceText());
    m_focusWords = (moment.title + QStringLiteral(". ") + moment.summary)
        .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    m_focusWordIndex = 0;
    updateFocusRead();
    const bool neutral = moment.status == QStringLiteral("ambiguous_engine_instability")
        || moment.status == QStringLiteral("below_confirmation_threshold");
    m_card->setStyleSheet(neutral
        ? QStringLiteral("QFrame#coachReviewCard { background: #1c2229; border: 0; border-left: 3px solid #4a6a88; }")
        : QStringLiteral("QFrame#coachReviewCard { background: #1e2024; border: 0; border-left: 3px solid #8a6a4f; }"));
}

void CoachReviewPanel::rebuildMechanicalFacts()
{
    while (QLayoutItem *item = m_mechanicalFactsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget(); widget != nullptr) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    if (!m_review.has_value() || !m_explanation.has_value()
        || m_review->criticalMoments.isEmpty()) {
        m_mechanicalFrame->hide();
        return;
    }
    const auto &displayMoment =
        m_review->criticalMoments.at(m_currentMomentVectorIndex);
    const auto *moment = m_explanation->moment(
        displayMoment.reviewIndex, displayMoment.ply);
    if (moment == nullptr) {
        m_mechanicalFrame->hide();
        return;
    }

    m_mechanicalHeadlineLabel->setText(moment->headline);
    const bool warning =
        moment->comparisonStatus == QStringLiteral("ambiguous_engine_stability")
        || moment->comparisonStatus == QStringLiteral("below_policy_threshold");
    m_mechanicalComparisonLabel->setText(
        QStringLiteral("Comparison confidence · %1").arg(moment->comparisonStatus));
    m_mechanicalComparisonLabel->setVisible(warning);
    m_mechanicalStatusLabel->setText(
        QStringLiteral("Mechanical facts · %1").arg(moment->mechanicalFactStatus));
    m_mechanicalStatusLabel->setMinimumWidth(
        m_mechanicalStatusLabel->fontMetrics().horizontalAdvance(
            m_mechanicalStatusLabel->text()) + 22);

    const QStringList scopes {
        QStringLiteral("policy"), QStringLiteral("best_move"),
        QStringLiteral("played_move"), QStringLiteral("comparison"),
        QStringLiteral("context")};
    for (const QString &scope : scopes) {
        bool headingAdded = false;
        for (const auto &fact : moment->facts) {
            if (fact.scope != scope) continue;
            if (!headingAdded) {
                auto *heading = new QLabel(mechanicalScopeTitle(scope), m_mechanicalFactsWidget);
                heading->setTextFormat(Qt::PlainText);
                heading->setProperty("uiRole", QStringLiteral("mechanicalScope"));
                heading->setObjectName(QStringLiteral("mechanicalScope_%1").arg(scope));
                m_mechanicalFactsLayout->addWidget(heading);
                headingAdded = true;
            }
            auto *row = new QLabel(fact.text, m_mechanicalFactsWidget);
            row->setTextFormat(Qt::PlainText);
            row->setWordWrap(true);
            row->setTextInteractionFlags(Qt::TextSelectableByMouse);
            row->setProperty("uiRole", QStringLiteral("mechanicalFact"));
            row->setObjectName(QStringLiteral("mechanicalFact_%1").arg(fact.code));
            m_mechanicalFactsLayout->addWidget(row);
        }
    }
    m_mechanicalFrame->show();
}

QString CoachReviewPanel::detailedEvidenceText() const
{
    if (!m_review.has_value() || m_review->criticalMoments.isEmpty()) return {};
    const auto &moment = m_review->criticalMoments.at(m_currentMomentVectorIndex);
    const auto &move = m_review->moves.at(moment.ply - 1);
    const QString moveLabel = moment.mover == QStringLiteral("white")
        ? QStringLiteral("%1. %2").arg(moment.moveNumber).arg(moment.playedSan)
        : QStringLiteral("%1… %2").arg(moment.moveNumber).arg(moment.playedSan);
    const auto optionalNumber = [](const std::optional<qint64> &value) {
        return value.has_value() ? QString::number(*value) : QStringLiteral("unavailable");
    };
    QStringList lines {
        moment.title,
        moment.summary,
        QString(),
        QStringLiteral("MOVE"),
        QStringLiteral("  %1  %2 · ply %3 · %4")
            .arg(reviewPieceIcon(move.piece, moment.mover), moveLabel)
            .arg(moment.ply)
            .arg(moment.playerUsername),
        QStringLiteral("  Played SAN/UCI  %1 / %2").arg(moment.playedSan, moment.playedUci),
        QStringLiteral("  Best SAN/UCI    %1 / %2")
            .arg(moment.bestMoveSan.value_or(QStringLiteral("unavailable")), moment.bestMoveUci),
        QString(),
        QStringLiteral("ENGINE EVIDENCE"),
        QStringLiteral("  Status      %1").arg(moment.status),
        QStringLiteral("  Severity    %1").arg(moment.severity.value_or(QStringLiteral("unavailable"))),
        QStringLiteral("  Confidence  %1").arg(moment.confidence),
        QStringLiteral("  Centipawn loss          %1").arg(optionalNumber(moment.centipawnLoss)),
        QStringLiteral("  Best mover centipawns   %1").arg(optionalNumber(moment.bestCentipawnsMover)),
        QStringLiteral("  Played mover centipawns %1").arg(optionalNumber(moment.playedCentipawnsMover)),
        QStringLiteral("  Expectation loss millionths  %1").arg(optionalNumber(moment.expectationLossMillionths)),
        QString(),
        QStringLiteral("TIMING & POSITION"),
        QStringLiteral("  Phase  %1").arg(moment.phase),
        QStringLiteral("  Server-accounted elapsed time  %1 ms").arg(optionalNumber(moment.elapsedMoveMs)),
        QStringLiteral("  Elapsed status  %1").arg(move.elapsedStatus),
        QStringLiteral("  Before FEN  %1").arg(move.beforeFen),
        QString(),
        QStringLiteral("RETAINED LINES"),
        QStringLiteral("  Best, not a complete search tree"),
        QStringLiteral("    %1").arg(moment.bestLineUci.join(QLatin1Char(' '))),
        QStringLiteral("  Played, not a complete search tree"),
        QStringLiteral("    %1").arg(moment.playedLineUci.join(QLatin1Char(' '))),
        QString(),
        QStringLiteral("SOURCE"),
        QStringLiteral("  Display schema  %1").arg(m_review->displaySchema),
        QStringLiteral("  Source report   %1").arg(m_review->sourceReportId),
        QStringLiteral("  Backend display projection only; no complete-game error coverage or safety claim."),
    };
    if (m_explanation.has_value()) {
        if (const auto *mechanical = m_explanation->moment(
                moment.reviewIndex, moment.ply);
            mechanical != nullptr) {
            lines << QString() << mechanicalEvidenceText(*m_explanation, *mechanical);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void CoachReviewPanel::updateFocusRead()
{
    if (m_focusWords.isEmpty()) return;
    constexpr int contextWords = 4;
    const int beforeStart = std::max(0, m_focusWordIndex - contextWords);
    const int afterCount = std::min(
        contextWords, static_cast<int>(m_focusWords.size()) - m_focusWordIndex - 1);
    m_focusBeforeLabel->setText(
        m_focusWords.mid(beforeStart, m_focusWordIndex - beforeStart).join(QLatin1Char(' ')));
    m_focusAnchorLabel->setText(m_focusWords.at(m_focusWordIndex));
    m_focusAfterLabel->setText(
        m_focusWords.mid(m_focusWordIndex + 1, afterCount).join(QLatin1Char(' ')));
    m_focusProgressLabel->setText(
        QStringLiteral("%1 / %2").arg(m_focusWordIndex + 1).arg(m_focusWords.size()));
}

void CoachReviewPanel::stepFocusRead(int delta)
{
    if (m_focusWords.isEmpty()) return;
    m_focusWordIndex = std::clamp(
        m_focusWordIndex + delta, 0, static_cast<int>(m_focusWords.size()) - 1);
    updateFocusRead();
}

void CoachReviewPanel::setFocusPlaybackRunning(bool running)
{
    if (running && !m_focusWords.isEmpty()) {
        m_focusTimer->start();
        m_focusStartPauseButton->setText(QStringLiteral("Pause"));
    } else {
        m_focusTimer->stop();
        m_focusStartPauseButton->setText(QStringLiteral("Start"));
    }
}

void CoachReviewPanel::keyPressEvent(QKeyEvent *event)
{
    if (m_focusReadFrame->isVisible()) {
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
            setFocusPlaybackRunning(false);
            stepFocusRead(event->key() == Qt::Key_Left ? -1 : 1);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Space) {
            setFocusPlaybackRunning(!m_focusTimer->isActive());
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            setFocusPlaybackRunning(false);
            m_focusReadFrame->hide();
            m_focusReadButton->setText(QStringLiteral("Focus Read"));
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

GameReviewPanel::GameReviewPanel(QWidget *parent)
    : QWidget(parent)
    , m_reviewModes(new QTabWidget(this))
    , m_splitter(new QSplitter(Qt::Vertical, this))
    , m_moveListPanel(new MoveListPanel(this))
    , m_evidencePanel(new ReplayEvidencePanel(this))
    , m_detailedEvidenceView(new FixedScaleTextEdit(this))
    , m_coachReviewPanel(new CoachReviewPanel(this))
{
    setObjectName(QStringLiteral("gameReviewWorkspace"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_reviewModes->setObjectName(QStringLiteral("gameReviewModes"));
    m_reviewModes->setDocumentMode(true);
    m_reviewModes->setStyleSheet(parlawl::review_ui::calmTabStyleSheet());
    auto *visualPage = new QWidget(m_reviewModes);
    auto *visualLayout = new QVBoxLayout(visualPage);
    visualLayout->setContentsMargins(0, 0, 0, 0);
    visualLayout->setSpacing(0);

    m_splitter->setObjectName(QStringLiteral("gameReviewSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(0);
    m_moveListPanel->setObjectName(QStringLiteral("gameReviewMoveList"));
    m_moveListPanel->setTitle(QString());
    m_moveListPanel->setStyleSheet(QStringLiteral(
        "QGroupBox#gameReviewMoveList { border: 0; margin: 0; padding: 0; }"));
    m_evidencePanel->setObjectName(QStringLiteral("gameReviewInspector"));
    m_evidencePanel->setInlineCoachMode(true);
    m_splitter->addWidget(m_evidencePanel);
    m_splitter->addWidget(m_moveListPanel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({52, 708});
    visualLayout->addWidget(m_splitter);

    m_detailedEvidenceView->setObjectName(QStringLiteral("detailedEvidenceView"));
    m_detailedEvidenceView->setReadOnly(true);
    m_detailedEvidenceView->setLineWrapMode(QTextEdit::WidgetWidth);
    m_detailedEvidenceView->setStyleSheet(QStringLiteral(
        "QTextEdit { color: #d4dbe4; background: #171a1f; border: 0; padding: 16px; font-size: 14px; }"));
    m_detailedEvidenceView->setFont(
        QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_reviewModes->addTab(visualPage, QStringLiteral("Visual Map"));
    m_reviewModes->addTab(m_detailedEvidenceView, QStringLiteral("Detailed Evidence"));
    m_reviewModes->addTab(m_coachReviewPanel, QStringLiteral("Coach Review"));
    layout->addWidget(m_reviewModes);

    connect(m_moveListPanel, &MoveListPanel::replayPlyRequested,
            this, [this](int ply) {
                if (m_coachReviewPanel->selectMomentAtPly(ply)) {
                    m_reviewModes->setCurrentWidget(m_coachReviewPanel);
                }
                emit replayPlyRequested(ply);
            });
    connect(
        m_evidencePanel,
        &ReplayEvidencePanel::backToPuzzlesRequested,
        this,
        &GameReviewPanel::backToPlayerStatisticsRequested);
    connect(m_coachReviewPanel, &CoachReviewPanel::criticalMomentRequested,
            this, &GameReviewPanel::criticalMomentRequested);
    connect(m_coachReviewPanel, &CoachReviewPanel::linePreviewRequested,
            this, &GameReviewPanel::coachLinePreviewRequested);
}

void GameReviewPanel::setReplayState(
    const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
    const parlawl::puzzle_runner::ReplaySession &session,
    int variationAnchorPly)
{
    m_currentReplayPly = session.currentMainlinePly();
    m_moveListPanel->setAnnotatedReplay(
        pack,
        session.currentMainlinePly(),
        session.inVariation(),
        variationAnchorPly);
    m_evidencePanel->setReplayState(pack, session, variationAnchorPly);
    m_baseDetailedEvidenceText = detailedMoveEvidenceText(pack, session);
    refreshDetailedEvidence();
    m_splitter->setSizes({52, 708});
}

void GameReviewPanel::setGameReviewDisplay(
    const parlawl::puzzle_runner::GameReviewDisplay *review,
    const parlawl::puzzle_runner::GameReviewMechanicalExplanation *explanation,
    const parlawl::puzzle_runner::GameReviewCoverageEntry *coverage,
    const QString &unavailableMessage)
{
    m_review = review != nullptr
        ? std::optional<parlawl::puzzle_runner::GameReviewDisplay>(*review)
        : std::nullopt;
    m_explanation = explanation != nullptr
        ? std::optional<parlawl::puzzle_runner::GameReviewMechanicalExplanation>(*explanation)
        : std::nullopt;
    m_coverage = coverage != nullptr
        ? std::optional<parlawl::puzzle_runner::GameReviewCoverageEntry>(*coverage)
        : std::nullopt;
    m_coachReviewPanel->setReview(
        review, explanation, coverage, m_currentReplayPly, unavailableMessage);
    refreshDetailedEvidence();
}

void GameReviewPanel::setEmptyState()
{
    m_evidencePanel->setEmptyState();
    m_detailedEvidenceView->setPlainText(
        QStringLiteral("Select a game and move to inspect its evidence."));
    m_baseDetailedEvidenceText.clear();
    m_review.reset();
    m_explanation.reset();
    m_coverage.reset();
    m_coachReviewPanel->setReview(nullptr, nullptr, nullptr, 0);
}

void GameReviewPanel::refreshDetailedEvidence()
{
    QString text = m_baseDetailedEvidenceText;
    if (m_review.has_value() && m_explanation.has_value()) {
        if (const auto *displayMoment = m_review->firstMomentAtPly(m_currentReplayPly);
            displayMoment != nullptr) {
            if (const auto *mechanical = m_explanation->moment(
                    displayMoment->reviewIndex, displayMoment->ply);
                mechanical != nullptr) {
                if (!text.isEmpty()) text += QStringLiteral("\n\n");
                text += mechanicalEvidenceText(*m_explanation, *mechanical);
            }
        }
    }
    if (!m_review.has_value() && m_coverage.has_value()) {
        if (!text.isEmpty()) text += QStringLiteral("\n\n");
        text += QStringList {
            QStringLiteral("COACH REVIEW DELIVERY"),
            m_coverage->statusLabel,
            m_coverage->statusDetail,
            QStringLiteral("screening_status  %1").arg(m_coverage->screeningStatus),
        }.join(QLatin1Char('\n'));
    }
    m_detailedEvidenceView->setPlainText(text);
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
    m_previousButton->setObjectName(QStringLiteral("previousMoveButton"));
    m_retryButton->setObjectName(QStringLiteral("playMovesButton"));
    m_nextButton->setObjectName(QStringLiteral("nextMoveButton"));
    layout->addWidget(m_previousButton);
    layout->addWidget(m_retryButton);
    layout->addWidget(m_nextButton);
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
    m_previousButton->setText(enabled ? QStringLiteral("Previous") : QStringLiteral("Prev"));
    m_nextButton->setText(QStringLiteral("Next"));
    m_retryButton->setText(enabled ? QStringLiteral("▶ Play") : QStringLiteral("Retry"));
    m_retryButton->setToolTip(
        enabled
            ? QStringLiteral("play or pause the recorded legal move sequence")
            : QStringLiteral("reset the current puzzle attempt to the starting position"));
}

void TransportControls::setPlaying(bool playing)
{
    m_retryButton->setText(playing ? QStringLiteral("❚❚ Pause") : QStringLiteral("▶ Play"));
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
