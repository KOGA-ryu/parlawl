#include "session_controller.h"

#include "pgn_utils.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace parlawl::puzzle_runner {

namespace {

QJsonArray toJsonStringArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QStringList sourceMoveList(const PuzzleDefinition &puzzle)
{
    if (!puzzle.analysisSeed.sourceGamePgn.trimmed().isEmpty()) {
        return pgnMoveList(puzzle.analysisSeed.sourceGamePgn);
    }

    if (puzzle.analysisSeed.rawPuzzleJson.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(puzzle.analysisSeed.rawPuzzleJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    const QString pgnMoves = document.object().value(QStringLiteral("game")).toObject().value(QStringLiteral("pgn")).toString().trimmed();
    return pgnMoves.isEmpty() ? QStringList {} : pgnMoves.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

int puzzleInitialPly(const PuzzleDefinition &puzzle)
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

QString normalizedSan(QString san)
{
    san = san.trimmed();
    while (!san.isEmpty()) {
        const QChar tail = san.back();
        if (tail == QLatin1Char('+') || tail == QLatin1Char('#')
            || tail == QLatin1Char('!') || tail == QLatin1Char('?')) {
            san.chop(1);
            continue;
        }
        break;
    }
    return san;
}

QString normalizedFenState(const QString &fen)
{
    const QStringList parts = fen.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 4) {
        return fen.trimmed();
    }
    return parts.first() + QLatin1Char(' ')
        + parts.at(1) + QLatin1Char(' ')
        + parts.at(2) + QLatin1Char(' ')
        + parts.at(3);
}

PieceType pieceTypeFromSanPrefix(QChar token)
{
    switch (token.toLatin1()) {
    case 'K':
        return PieceType::King;
    case 'Q':
        return PieceType::Queen;
    case 'R':
        return PieceType::Rook;
    case 'B':
        return PieceType::Bishop;
    case 'N':
        return PieceType::Knight;
    default:
        return PieceType::Pawn;
    }
}

bool isCaptureSan(const ChessPosition &position, const Move &move)
{
    const Piece target = position.pieceAt(move.to);
    if (!target.isEmpty()) {
        return true;
    }
    const Piece mover = position.pieceAt(move.from);
    return mover.type == PieceType::Pawn && ChessPosition::fileOf(move.from) != ChessPosition::fileOf(move.to);
}

std::optional<Move> resolveSanMove(const ChessPosition &position, QString san)
{
    san = normalizedSan(std::move(san));
    if (san.isEmpty()) {
        return std::nullopt;
    }

    if (san == QStringLiteral("O-O") || san == QStringLiteral("0-0")) {
        const QString expected = position.sideToMove() == PieceColor::White ? QStringLiteral("e1g1") : QStringLiteral("e8g8");
        for (const Move &move : position.legalMoves()) {
            if (move.uci() == expected) {
                return move;
            }
        }
        return std::nullopt;
    }

    if (san == QStringLiteral("O-O-O") || san == QStringLiteral("0-0-0")) {
        const QString expected = position.sideToMove() == PieceColor::White ? QStringLiteral("e1c1") : QStringLiteral("e8c8");
        for (const Move &move : position.legalMoves()) {
            if (move.uci() == expected) {
                return move;
            }
        }
        return std::nullopt;
    }

    PieceType promotion = PieceType::None;
    const int promotionMarker = san.indexOf(QLatin1Char('='));
    if (promotionMarker >= 0 && promotionMarker + 1 < san.size()) {
        promotion = pieceTypeFromSanPrefix(san.at(promotionMarker + 1));
        san = san.left(promotionMarker);
    }

    if (san.size() < 2) {
        return std::nullopt;
    }

    const QString destination = san.right(2).toLower();
    const int destinationSquare = ChessPosition::squareFromName(destination);
    if (destinationSquare < 0) {
        return std::nullopt;
    }

    QString prefix = san.left(san.size() - 2);
    PieceType pieceType = PieceType::Pawn;
    if (!prefix.isEmpty() && QStringLiteral("KQRBN").contains(prefix.front())) {
        pieceType = pieceTypeFromSanPrefix(prefix.front());
        prefix.remove(0, 1);
    }
    const bool capture = prefix.contains(QLatin1Char('x'));
    prefix.remove(QLatin1Char('x'));

    QVector<Move> matches;
    for (const Move &move : position.legalMoves()) {
        if (move.to != destinationSquare) {
            continue;
        }
        const Piece mover = position.pieceAt(move.from);
        if (mover.type != pieceType || mover.color != position.sideToMove()) {
            continue;
        }
        if (promotion != PieceType::None) {
            if (move.promotion != promotion) {
                continue;
            }
        } else if (move.promotion != PieceType::None) {
            continue;
        }
        if (isCaptureSan(position, move) != capture) {
            continue;
        }

        bool disambiguationMatches = true;
        for (const QChar token : prefix) {
            if (token.isLetter()) {
                if (ChessPosition::fileOf(move.from) != token.toLower().toLatin1() - 'a') {
                    disambiguationMatches = false;
                    break;
                }
            } else if (token.isDigit()) {
                if (ChessPosition::rankOf(move.from) != token.digitValue() - 1) {
                    disambiguationMatches = false;
                    break;
                }
            }
        }
        if (disambiguationMatches) {
            matches.append(move);
        }
    }

    if (matches.isEmpty()) {
        return std::nullopt;
    }
    return matches.first();
}

QVector<ChessPosition> buildSourceHistory(const PuzzleDefinition &puzzle)
{
    QString positionError;
    const auto initial = ChessPosition::fromFen(QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"), &positionError);
    Q_UNUSED(positionError);
    QVector<ChessPosition> history;
    if (!initial.has_value()) {
        return history;
    }

    history.append(*initial);
    ChessPosition current = *initial;
    const QStringList moves = sourceMoveList(puzzle);
    const int sourcePlyCount = std::min(puzzleInitialPly(puzzle), static_cast<int>(moves.size()));
    for (int index = 0; index < sourcePlyCount; ++index) {
        const auto resolvedMove = resolveSanMove(current, moves.at(index));
        if (!resolvedMove.has_value() || !current.applyMove(*resolvedMove)) {
            return {};
        }
        history.append(current);
    }

    QString puzzleError;
    const auto puzzleStart = ChessPosition::fromFen(puzzle.fenStart, &puzzleError);
    if (!puzzleStart.has_value()) {
        return {};
    }

    if (history.isEmpty() || normalizedFenState(history.last().toFen()) != normalizedFenState(puzzleStart->toFen())) {
        return {*puzzleStart};
    }

    return history;
}

} // namespace

SessionController::SessionController(QObject *parent)
    : QObject(parent)
    , m_puzzleSource()
    , m_gameStateStore(this)
{
    connect(&m_gameStateStore, &GameStateStore::stateChanged, this, &SessionController::sessionChanged);
}

bool SessionController::initialize(QString *errorMessage)
{
    m_puzzles = m_puzzleSource.loadPuzzles(errorMessage);
    if (m_puzzles.isEmpty()) {
        return false;
    }
    rebuildFilteredPuzzleList();
    resetVisiblePuzzleWindow();
    applyCurrentPuzzle(errorMessage);
    return m_currentPuzzleIndex >= 0;
}

bool SessionController::replacePuzzles(const QVector<PuzzleDefinition> &puzzles, QString *errorMessage)
{
    m_puzzles = puzzles;
    if (m_puzzles.isEmpty()) {
        m_filteredIndices.clear();
        m_visiblePuzzleCount = 0;
        m_currentPuzzleSlot = -1;
        m_currentPuzzleIndex = -1;
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no puzzles available from the selected source");
        }
        emit sessionChanged();
        return false;
    }
    rebuildFilteredPuzzleList();
    resetVisiblePuzzleWindow();
    applyCurrentPuzzle(errorMessage);
    return m_currentPuzzleIndex >= 0;
}

bool SessionController::setCurrentPuzzleSourceGamePgn(const QString &pgnText, const QString &openingName, QString *errorMessage)
{
    if (m_currentPuzzleIndex < 0 || m_currentPuzzleIndex >= m_puzzles.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no current puzzle is loaded");
        }
        return false;
    }

    PuzzleDefinition &puzzle = m_puzzles[m_currentPuzzleIndex];
    if (pgnText.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("source game pgn is empty");
        }
        return false;
    }
    const QString resolvedOpeningName = openingName.trimmed().isEmpty()
        ? pgnDisplayOpening(pgnText)
        : openingName.trimmed();

    if (puzzle.analysisSeed.sourceGamePgn == pgnText
        && (resolvedOpeningName.isEmpty() || puzzle.analysisSeed.openingName == resolvedOpeningName)) {
        return true;
    }

    puzzle.analysisSeed.sourceGamePgn = pgnText;
    puzzle.analysisSeed.openingName = resolvedOpeningName;
    applyCurrentPuzzle(errorMessage);
    return m_currentPuzzleIndex >= 0;
}

bool SessionController::appendPuzzles(const QVector<PuzzleDefinition> &puzzles, QString *errorMessage)
{
    if (puzzles.isEmpty()) {
        return !m_puzzles.isEmpty();
    }

    QStringList existingIds;
    existingIds.reserve(m_puzzles.size());
    for (const PuzzleDefinition &puzzle : m_puzzles) {
        existingIds.append(puzzle.id);
    }

    int appendedCount = 0;
    for (const PuzzleDefinition &puzzle : puzzles) {
        if (puzzle.id.isEmpty() || existingIds.contains(puzzle.id)) {
            continue;
        }
        m_puzzles.append(puzzle);
        existingIds.append(puzzle.id);
        ++appendedCount;
    }

    if (appendedCount == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no new puzzles were available to append");
        }
        return false;
    }

    rebuildFilteredPuzzleList();
    m_visiblePuzzleCount = std::min(
        std::max(m_visiblePuzzleCount, m_currentPuzzleSlot + 1) + appendedCount,
        static_cast<int>(m_filteredIndices.size()));
    emit sessionChanged();
    return true;
}

bool SessionController::canSubmitMoves() const
{
    return m_puzzleEngine.status() == SessionStatus::Active
        && m_gameStateStore.isViewingLatest();
}

bool SessionController::shouldFetchMorePuzzles() const
{
    if (!m_settings.refillWhenLow || m_filteredIndices.isEmpty()) {
        return false;
    }
    if (m_visiblePuzzleCount < m_filteredIndices.size()) {
        return false;
    }
    return remainingVisiblePuzzleCount() < m_settings.refillThreshold;
}

bool SessionController::canAnalyzeCurrentPuzzle() const
{
    if (m_puzzleEngine.status() != SessionStatus::Solved
        && m_puzzleEngine.status() != SessionStatus::Failed) {
        return false;
    }

    const PuzzleDefinition &puzzle = m_gameStateStore.currentPuzzle();
    return !puzzle.analysisSeed.sourceGameId.isEmpty()
        && !puzzle.analysisSeed.sourceGamePgn.trimmed().isEmpty();
}

bool SessionController::buildAnalysisInput(PuzzleRound *puzzleRound, SourceGame *sourceGame, QString *errorMessage) const
{
    const PuzzleDefinition &puzzle = m_gameStateStore.currentPuzzle();
    const PuzzleAnalysisSeed &seed = puzzle.analysisSeed;
    if (m_puzzleEngine.status() != SessionStatus::Solved
        && m_puzzleEngine.status() != SessionStatus::Failed) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("analysis handoff is available after solve or fail");
        }
        return false;
    }
    if (seed.sourceGameId.isEmpty() || seed.sourceGamePgn.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("current puzzle does not include source-game data for analysis handoff");
        }
        return false;
    }

    if (puzzleRound != nullptr) {
        puzzleRound->puzzleId = puzzle.id;
        puzzleRound->puzzleRating = puzzle.metadata.rating;
        puzzleRound->timeControl = seed.timeControl;
        puzzleRound->whitePlayer = puzzle.metadata.whiteName;
        puzzleRound->whiteRating = puzzle.metadata.whiteRating;
        puzzleRound->blackPlayer = puzzle.metadata.blackName;
        puzzleRound->blackRating = puzzle.metadata.blackRating;
        puzzleRound->sideToMove = seed.sideToMove.isEmpty() ? sideLabelFromFen(puzzle.fenStart) : seed.sideToMove;
        puzzleRound->fetchedAtUtc = QDateTime::currentDateTimeUtc();
        puzzleRound->sourceGameId = seed.sourceGameId;
        puzzleRound->initialFen = puzzle.fenStart;
        puzzleRound->lastMove = seed.lastMove;
        puzzleRound->solved = true;
        puzzleRound->rawPuzzleJson = seed.rawPuzzleJson;
        puzzleRound->rawActivityJson = seed.rawActivityJson;
        puzzleRound->solutionMovesJson = QString::fromUtf8(QJsonDocument(toJsonStringArray(puzzle.solutionMoves)).toJson(QJsonDocument::Compact));
        puzzleRound->themesJson = QString::fromUtf8(QJsonDocument(toJsonStringArray(puzzle.metadata.themes)).toJson(QJsonDocument::Compact));
    }

    if (sourceGame != nullptr) {
        sourceGame->sourceGameId = seed.sourceGameId;
        sourceGame->pgnText = seed.sourceGamePgn;
        sourceGame->openingName = !seed.openingName.trimmed().isEmpty()
            ? seed.openingName
            : pgnDisplayOpening(seed.sourceGamePgn);
        sourceGame->fetchedAtUtc = QDateTime::currentDateTimeUtc();
    }

    return true;
}

void SessionController::setAutoAdvance(bool autoAdvance)
{
    if (m_settings.autoAdvance == autoAdvance) {
        return;
    }
    m_settings.autoAdvance = autoAdvance;
    emit sessionChanged();
}

void SessionController::setDifficulty(const QString &difficulty)
{
    QString normalized = difficulty.trimmed().toLower();
    if (normalized != QStringLiteral("medium") && normalized != QStringLiteral("hard")) {
        normalized = QStringLiteral("hard");
    }
    if (m_settings.difficulty == normalized) {
        return;
    }
    m_settings.difficulty = normalized;
    rebuildFilteredPuzzleList();
    resetVisiblePuzzleWindow();
    applyCurrentPuzzle();
}

void SessionController::setQueueSize(int queueSize)
{
    const int normalized = std::max(queueSize, 1);
    if (m_settings.queueSize == normalized) {
        return;
    }
    m_settings.queueSize = normalized;
    resetVisiblePuzzleWindow();
    applyCurrentPuzzle();
}

void SessionController::setRefillWhenLow(bool enabled)
{
    if (m_settings.refillWhenLow == enabled) {
        return;
    }
    m_settings.refillWhenLow = enabled;
    maybeRefillVisiblePuzzleWindow(false);
    emit sessionChanged();
}

void SessionController::setRefillThreshold(int threshold)
{
    const int normalized = std::max(threshold, 0);
    if (m_settings.refillThreshold == normalized) {
        return;
    }
    m_settings.refillThreshold = normalized;
    maybeRefillVisiblePuzzleWindow(false);
    emit sessionChanged();
}

void SessionController::submitUserMove(const QString &moveUci)
{
    if (!canSubmitMoves()) {
        return;
    }

    const SubmissionResult result = m_puzzleEngine.submitUserMove(m_gameStateStore.latestPosition(), moveUci);
    if (!result.accepted) {
        if (!result.prompt.isEmpty()) {
            emit promptChanged(result.prompt);
        }
        return;
    }

    m_gameStateStore.applyMoveSequence(result.appliedMoves);
    emit promptChanged(result.prompt);
    emit sessionChanged();
    if (result.status == SessionStatus::Solved && m_settings.autoAdvance) {
        nextPuzzle();
    }
}

void SessionController::requestHint()
{
    const QString hint = m_puzzleEngine.nextExpectedMove();
    emit hintAvailable(hint.isEmpty() ? QStringLiteral("no hint available") : QStringLiteral("next move: %1").arg(hint));
}

void SessionController::revealSolution()
{
    applyCurrentPuzzle();
    const SubmissionResult result = m_puzzleEngine.revealSolution(m_gameStateStore.latestPosition());
    if (!result.accepted) {
        emit errorRaised(result.prompt);
        return;
    }
    m_gameStateStore.applyMoveSequence(result.appliedMoves);
    emit promptChanged(result.prompt);
    emit sessionChanged();
}

void SessionController::retryPuzzle()
{
    applyCurrentPuzzle();
}

void SessionController::stepBackward()
{
    if (m_gameStateStore.stepBackward()) {
        emit sessionChanged();
    }
}

void SessionController::stepForward()
{
    if (m_gameStateStore.stepForward()) {
        emit sessionChanged();
    }
}

void SessionController::previousPuzzle()
{
    if (!canGoToPreviousPuzzle()) {
        return;
    }
    --m_currentPuzzleSlot;
    applyCurrentPuzzle();
}

void SessionController::nextPuzzle()
{
    maybeRefillVisiblePuzzleWindow(true);
    if (!canGoToNextPuzzle()) {
        return;
    }
    ++m_currentPuzzleSlot;
    maybeRefillVisiblePuzzleWindow(false);
    applyCurrentPuzzle();
}

void SessionController::applyCurrentPuzzle(QString *errorMessage)
{
    if (m_filteredIndices.isEmpty()) {
        m_currentPuzzleSlot = -1;
        m_currentPuzzleIndex = -1;
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no puzzles available for the selected difficulty");
        }
        emit sessionChanged();
        return;
    }

    if (m_currentPuzzleSlot < 0 || m_currentPuzzleSlot >= m_filteredIndices.size()) {
        m_currentPuzzleSlot = 0;
    }
    m_currentPuzzleIndex = m_filteredIndices.at(m_currentPuzzleSlot);
    const PuzzleDefinition &puzzle = m_puzzles.at(m_currentPuzzleIndex);
    QString positionError;
    const auto position = ChessPosition::fromFen(puzzle.fenStart, &positionError);
    if (!position.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = positionError;
        }
        emit errorRaised(positionError);
        return;
    }

    QVector<ChessPosition> sourceHistory = buildSourceHistory(puzzle);
    if (sourceHistory.isEmpty()) {
        sourceHistory = {*position};
    }

    m_puzzleEngine.loadPuzzle(puzzle);
    m_gameStateStore.loadPuzzle(puzzle, sourceHistory);
    emit promptChanged(currentPrompt());
    emit sessionChanged();
}

void SessionController::rebuildFilteredPuzzleList()
{
    m_filteredIndices.clear();
    for (int index = 0; index < m_puzzles.size(); ++index) {
        const QString difficulty = m_puzzles.at(index).metadata.difficulty.trimmed().toLower();
        if (m_settings.difficulty == QStringLiteral("all") || difficulty == m_settings.difficulty) {
            m_filteredIndices.append(index);
        }
    }
    if (m_currentPuzzleIndex >= 0) {
        const int slot = m_filteredIndices.indexOf(m_currentPuzzleIndex);
        m_currentPuzzleSlot = slot >= 0 ? slot : 0;
    }
}

void SessionController::resetVisiblePuzzleWindow()
{
    if (m_filteredIndices.isEmpty()) {
        m_visiblePuzzleCount = 0;
        return;
    }
    m_visiblePuzzleCount = std::min(std::max(m_settings.queueSize, 1), static_cast<int>(m_filteredIndices.size()));
    if (m_currentPuzzleSlot >= m_visiblePuzzleCount) {
        m_currentPuzzleSlot = 0;
    }
}

void SessionController::maybeRefillVisiblePuzzleWindow(bool forceAtBoundary)
{
    if (m_filteredIndices.isEmpty() || m_visiblePuzzleCount >= m_filteredIndices.size()) {
        return;
    }

    const int remainingVisible = m_visiblePuzzleCount - (m_currentPuzzleSlot + 1);
    const bool lowWater = m_settings.refillWhenLow && remainingVisible < m_settings.refillThreshold;
    const bool boundaryAdvance = forceAtBoundary && m_currentPuzzleSlot >= m_visiblePuzzleCount - 1;
    if (!lowWater && !boundaryAdvance) {
        return;
    }

    m_visiblePuzzleCount = std::min(
        m_visiblePuzzleCount + std::max(m_settings.queueSize, 1),
        static_cast<int>(m_filteredIndices.size()));
}

QString SessionController::currentPrompt() const
{
    if (!m_gameStateStore.isViewingLatest()) {
        return QStringLiteral("reviewing previous position");
    }
    if (m_puzzleEngine.status() == SessionStatus::Failed) {
        return QStringLiteral("puzzle failed; review the position or retry");
    }
    if (m_puzzleEngine.status() == SessionStatus::Solved) {
        return QStringLiteral("puzzle solved");
    }
    return m_puzzleEngine.prompt();
}

} // namespace parlawl::puzzle_runner
