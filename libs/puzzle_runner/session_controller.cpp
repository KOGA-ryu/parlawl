#include "session_controller.h"

#include "engine_validated_puzzle_pack.h"
#include "pgn_utils.h"

#include <cstdlib>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

namespace parlawl::puzzle_runner {

using namespace parlawl::attempts;

namespace {

constexpr qint64 kAttemptClockDriftToleranceMilliseconds = 5000;
constexpr auto kAttemptClockPolicy = "parlawl-attempt-clock-v1";

void setValidationError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool validateImportedProvenance(const PuzzleDefinition &puzzle, QString *errorMessage)
{
    const bool hasImportedProvenance = isImportedEngineRecord(puzzle)
        || puzzle.analysisSeed.sourceRecordSchema == importedEngineRecordSchema()
        || puzzle.analysisSeed.sourceRecordId.startsWith(QStringLiteral("puzzle-record-v1:"))
        || !puzzle.analysisSeed.rawSourceRecordJson.isEmpty();
    if (!hasImportedProvenance) {
        return true;
    }
    if (puzzle.analysisSeed.rawSourceRecordJson.isEmpty()) {
        setValidationError(
            errorMessage,
            QStringLiteral("imported puzzle provenance is missing its authoritative source record"));
        return false;
    }

    QString importError;
    const auto parsedPack = EngineValidatedPuzzlePack::fromJsonLines(
        puzzle.analysisSeed.rawSourceRecordJson.toUtf8(),
        &importError);
    if (!parsedPack.has_value() || parsedPack->puzzles().size() != 1) {
        setValidationError(
            errorMessage,
            QStringLiteral("imported puzzle provenance is invalid: %1").arg(importError));
        return false;
    }
    const PuzzleDefinition &expected = parsedPack->puzzles().first();
    const bool metadataMatches = puzzle.metadata.title == expected.metadata.title
        && puzzle.metadata.difficulty == expected.metadata.difficulty
        && puzzle.metadata.source == expected.metadata.source
        && puzzle.metadata.sourceLabel == expected.metadata.sourceLabel
        && puzzle.metadata.themes == expected.metadata.themes
        && puzzle.metadata.rating == expected.metadata.rating
        && puzzle.metadata.ratingHidden == expected.metadata.ratingHidden
        && puzzle.metadata.playedCount == expected.metadata.playedCount
        && puzzle.metadata.whiteName == expected.metadata.whiteName
        && puzzle.metadata.whiteRating == expected.metadata.whiteRating
        && puzzle.metadata.blackName == expected.metadata.blackName
        && puzzle.metadata.blackRating == expected.metadata.blackRating;
    const bool seedMatches = puzzle.analysisSeed.sourceGameId == expected.analysisSeed.sourceGameId
        && puzzle.analysisSeed.sourceProvider == expected.analysisSeed.sourceProvider
        && puzzle.analysisSeed.sourceRecordSchema == expected.analysisSeed.sourceRecordSchema
        && puzzle.analysisSeed.sourceRecordId == expected.analysisSeed.sourceRecordId
        && puzzle.analysisSeed.timeControl == expected.analysisSeed.timeControl
        && puzzle.analysisSeed.sideToMove == expected.analysisSeed.sideToMove
        && puzzle.analysisSeed.lastMove == expected.analysisSeed.lastMove
        && puzzle.analysisSeed.rawPuzzleJson == expected.analysisSeed.rawPuzzleJson
        && puzzle.analysisSeed.rawActivityJson == expected.analysisSeed.rawActivityJson
        && puzzle.analysisSeed.rawSourceRecordJson == expected.analysisSeed.rawSourceRecordJson
        && puzzle.analysisSeed.sourceGamePgn == expected.analysisSeed.sourceGamePgn
        && puzzle.analysisSeed.openingName == expected.analysisSeed.openingName
        && puzzle.analysisSeed.allowLichessPgnHydration
            == expected.analysisSeed.allowLichessPgnHydration;
    if (puzzle.id != expected.id || puzzle.fenStart != expected.fenStart
        || puzzle.solutionMoves != expected.solutionMoves || !metadataMatches || !seedMatches) {
        setValidationError(
            errorMessage,
            QStringLiteral("imported puzzle fields disagree with its authoritative source record"));
        return false;
    }
    return true;
}

bool validatePuzzleDefinition(const PuzzleDefinition &puzzle, QString *errorMessage)
{
    if (puzzle.id.trimmed().isEmpty() || puzzle.id != puzzle.id.trimmed()) {
        setValidationError(errorMessage, QStringLiteral("puzzle id must be non-empty normalized text"));
        return false;
    }
    if (puzzle.fenStart.trimmed().isEmpty() || puzzle.fenStart != puzzle.fenStart.trimmed()) {
        setValidationError(errorMessage, QStringLiteral("puzzle position must be normalized FEN text"));
        return false;
    }
    if (puzzle.solutionMoves.isEmpty() || puzzle.solutionMoves.size() > 1024) {
        setValidationError(errorMessage, QStringLiteral("puzzle solution must contain 1..1024 moves"));
        return false;
    }

    QString positionError;
    const auto parsedPosition = ChessPosition::fromFen(puzzle.fenStart, &positionError);
    if (!parsedPosition.has_value() || parsedPosition->toFen() != puzzle.fenStart) {
        setValidationError(
            errorMessage,
            QStringLiteral("puzzle position is invalid: %1").arg(positionError));
        return false;
    }

    ChessPosition current = *parsedPosition;
    int whiteKings = 0;
    int blackKings = 0;
    for (int square = 0; square < 64; ++square) {
        const Piece piece = current.pieceAt(square);
        if (piece.type == PieceType::King && piece.color == PieceColor::White) {
            ++whiteKings;
        } else if (piece.type == PieceType::King && piece.color == PieceColor::Black) {
            ++blackKings;
        }
        if (piece.type == PieceType::Pawn
            && (ChessPosition::rankOf(square) == 0 || ChessPosition::rankOf(square) == 7)) {
            setValidationError(errorMessage, QStringLiteral("puzzle position has a pawn on a promotion rank"));
            return false;
        }
    }
    if (whiteKings != 1 || blackKings != 1) {
        setValidationError(errorMessage, QStringLiteral("puzzle position must contain exactly one king per side"));
        return false;
    }

    const QStringList fenFields = puzzle.fenStart.split(QLatin1Char(' '));
    const QString castlingRights = fenFields.value(2);
    const auto castlingRightHasPieces = [&](QChar right, const QString &kingSquare,
                                            const QString &rookSquare, PieceColor color) {
        if (!castlingRights.contains(right)) {
            return true;
        }
        const Piece king = current.pieceAt(ChessPosition::squareFromName(kingSquare));
        const Piece rook = current.pieceAt(ChessPosition::squareFromName(rookSquare));
        return king.type == PieceType::King && king.color == color
            && rook.type == PieceType::Rook && rook.color == color;
    };
    if (!castlingRightHasPieces(QLatin1Char('K'), QStringLiteral("e1"), QStringLiteral("h1"), PieceColor::White)
        || !castlingRightHasPieces(QLatin1Char('Q'), QStringLiteral("e1"), QStringLiteral("a1"), PieceColor::White)
        || !castlingRightHasPieces(QLatin1Char('k'), QStringLiteral("e8"), QStringLiteral("h8"), PieceColor::Black)
        || !castlingRightHasPieces(QLatin1Char('q'), QStringLiteral("e8"), QStringLiteral("a8"), PieceColor::Black)) {
        setValidationError(errorMessage, QStringLiteral("puzzle position castling rights lack their king or rook"));
        return false;
    }
    if (current.isInCheck(ChessPosition::opposite(current.sideToMove()))) {
        setValidationError(errorMessage, QStringLiteral("puzzle position leaves the side that just moved in check"));
        return false;
    }

    for (int index = 0; index < puzzle.solutionMoves.size(); ++index) {
        const QString &uci = puzzle.solutionMoves.at(index);
        const auto move = Move::fromUci(uci);
        if (!move.has_value() || move->uci() != uci
            || current.pieceAt(move->to).type == PieceType::King
            || !current.isLegalMove(*move) || !current.applyMove(*move)) {
            setValidationError(
                errorMessage,
                QStringLiteral("puzzle solution move %1 is not canonical and legal from its recorded position")
                    .arg(index + 1));
            return false;
        }
    }

    return validateImportedProvenance(puzzle, errorMessage);
}

QJsonArray toJsonStringArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QJsonArray appliedMoveArray(const QVector<AppliedMove> &moves)
{
    QJsonArray array;
    for (const AppliedMove &move : moves) {
        array.append(QJsonObject{
            {QStringLiteral("uci"), move.uci},
            {QStringLiteral("user_move"), move.userMove},
        });
    }
    return array;
}

QJsonObject attemptPuzzleSnapshot(const PuzzleDefinition &puzzle)
{
    return {
        {QStringLiteral("difficulty"), puzzle.metadata.difficulty},
        {QStringLiteral("initial_fen"), puzzle.fenStart},
        {QStringLiteral("opening_name"), puzzle.analysisSeed.openingName},
        {QStringLiteral("puzzle_id"), puzzle.id},
        {QStringLiteral("puzzle_record_id"), puzzle.analysisSeed.sourceRecordId},
        {QStringLiteral("puzzle_record_schema"), puzzle.analysisSeed.sourceRecordSchema},
        {QStringLiteral("solution_uci"), toJsonStringArray(puzzle.solutionMoves)},
        {QStringLiteral("source_game_id"), puzzle.analysisSeed.sourceGameId},
        {QStringLiteral("source_provider"), puzzle.analysisSeed.sourceProvider},
        {QStringLiteral("themes"), toJsonStringArray(puzzle.metadata.themes)},
    };
}

QJsonObject exactAttemptMetadata()
{
    return {
        {QStringLiteral("attempt_policy"), QStringLiteral("parlawl-terminal-attempt-v1")},
        {QStringLiteral("interface"), QStringLiteral("ParlAWL")},
    };
}

QJsonObject terminalEventPayload(
    const QString &outcome,
    const std::optional<qint64> &duration,
    int wrongMoveCount,
    int hintsUsed,
    bool solutionRevealed,
    const QJsonObject &details = {})
{
    QJsonObject payload = details;
    payload.insert(
        QStringLiteral("duration_milliseconds"),
        duration.has_value() ? QJsonValue(*duration) : QJsonValue(QJsonValue::Null));
    payload.insert(QStringLiteral("hints_used"), hintsUsed);
    payload.insert(QStringLiteral("outcome"), outcome);
    payload.insert(QStringLiteral("solution_revealed"), solutionRevealed);
    payload.insert(QStringLiteral("wrong_move_count"), wrongMoveCount);
    return payload;
}

void insertClockAnomalyDetails(
    QJsonObject *payload,
    const QDateTime &baselineUtc,
    const QDateTime &wallUtc,
    qint64 monotonicElapsedMilliseconds)
{
    const qint64 wallElapsedMilliseconds = baselineUtc.isValid()
        ? baselineUtc.toUTC().msecsTo(wallUtc.toUTC())
        : 0;
    payload->insert(QStringLiteral("clock_policy"), QString::fromLatin1(kAttemptClockPolicy));
    payload->insert(QStringLiteral("clock_tolerance_milliseconds"), kAttemptClockDriftToleranceMilliseconds);
    payload->insert(QStringLiteral("monotonic_elapsed_milliseconds"), monotonicElapsedMilliseconds);
    payload->insert(QStringLiteral("wall_elapsed_milliseconds"), wallElapsedMilliseconds);
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

QDateTime SessionController::attemptUtcNow() const
{
    return m_attemptUtcNowProvider
        ? m_attemptUtcNowProvider().toUTC()
        : QDateTime::currentDateTimeUtc();
}

void SessionController::setAttemptUtcNowProviderForTesting(std::function<QDateTime()> provider)
{
    if (m_attemptState.exists) {
        emit errorRaised(QStringLiteral("cannot replace the solve-history clock during an active attempt"));
        return;
    }
    m_attemptUtcNowProvider = std::move(provider);
}

void SessionController::configurePuzzleAttemptLedger(
    PuzzleAttemptSink *sink,
    const QString &solverId,
    const QString &sessionId)
{
    if (m_attemptState.exists) {
        emit errorRaised(QStringLiteral("cannot replace the solve-history ledger during an active attempt"));
        return;
    }
    m_attemptSink = sink;
    m_solverId = solverId;
    m_attemptSessionId = sessionId;
    if (m_currentPuzzleIndex >= 0) {
        m_puzzleExposureStartedAtUtc = attemptUtcNow();
        m_attemptElapsed.start();
    }
}

bool SessionController::hasTrackedPuzzleAttempt() const
{
    return m_attemptState.exists;
}

bool SessionController::currentPuzzleSupportsAttemptLedger() const
{
    if (m_attemptSink == nullptr || m_currentPuzzleIndex < 0 || m_currentPuzzleIndex >= m_puzzles.size()) {
        return false;
    }
    const PuzzleDefinition &puzzle = m_puzzles.at(m_currentPuzzleIndex);
    return isImportedEngineRecord(puzzle)
        && puzzle.analysisSeed.sourceRecordSchema == QString::fromLatin1(kPuzzleRecordSchema)
        && !puzzle.analysisSeed.sourceRecordId.isEmpty()
        && !puzzle.analysisSeed.rawSourceRecordJson.isEmpty()
        && !m_solverId.isEmpty() && !m_attemptSessionId.isEmpty();
}

SessionController::AttemptRuntimeState SessionController::attemptCandidate(const QDateTime &nowUtc) const
{
    if (m_attemptState.exists) {
        return m_attemptState;
    }
    AttemptRuntimeState candidate;
    if (!currentPuzzleSupportsAttemptLedger()) {
        return candidate;
    }
    candidate.exists = true;
    candidate.attemptInstanceId = QStringLiteral("parlawl-attempt-instance-v1:")
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    candidate.wallClockBaselineUtc = m_puzzleExposureStartedAtUtc.isValid()
        ? m_puzzleExposureStartedAtUtc.toUTC()
        : nowUtc.toUTC();
    candidate.startedAtUtc = candidate.wallClockBaselineUtc;
    const QString puzzleId = m_puzzles.at(m_currentPuzzleIndex).id;
    const QDateTime priorStart = m_lastAttemptStartByPuzzleId.value(puzzleId);
    if (priorStart.isValid() && candidate.startedAtUtc <= priorStart) {
        candidate.startedAtUtc = priorStart.addMSecs(1);
    }
    return candidate;
}

QDateTime SessionController::eventTimeForAttempt(
    const AttemptRuntimeState &candidate,
    const QDateTime &wallUtc) const
{
    if (!candidate.startedAtUtc.isValid() || wallUtc.toUTC() >= candidate.startedAtUtc.toUTC()) {
        return wallUtc.toUTC();
    }
    // QDateTime has millisecond precision. A same-tick retry may reserve the next
    // millisecond for a unique external logical ID; clamp only that synthetic gap.
    if (!attemptWallClockRolledBack(candidate, wallUtc)) {
        return candidate.startedAtUtc.toUTC();
    }
    return wallUtc.toUTC();
}

bool SessionController::attemptWallClockRolledBack(
    const AttemptRuntimeState &candidate,
    const QDateTime &wallUtc) const
{
    const QDateTime baseline = candidate.wallClockBaselineUtc.isValid()
        ? candidate.wallClockBaselineUtc.toUTC()
        : candidate.startedAtUtc.toUTC();
    if (candidate.lastObservedWallUtc.isValid()
        && wallUtc.toUTC() < candidate.lastObservedWallUtc.toUTC()) {
        return true;
    }
    return baseline.isValid() && wallUtc.toUTC() < baseline;
}

bool SessionController::attemptClockDrifted(
    const AttemptRuntimeState &candidate,
    const QDateTime &wallUtc,
    qint64 monotonicElapsedMilliseconds) const
{
    const QDateTime baseline = candidate.wallClockBaselineUtc.isValid()
        ? candidate.wallClockBaselineUtc.toUTC()
        : candidate.startedAtUtc.toUTC();
    if (!baseline.isValid()) {
        return false;
    }
    if (attemptWallClockRolledBack(candidate, wallUtc)) {
        return true;
    }
    const qint64 wallElapsedMilliseconds = baseline.msecsTo(wallUtc.toUTC());
    return std::abs(wallElapsedMilliseconds - monotonicElapsedMilliseconds)
        > kAttemptClockDriftToleranceMilliseconds;
}

qint64 SessionController::attemptElapsedMilliseconds(const AttemptRuntimeState &candidate) const
{
    if (!m_attemptElapsed.isValid()) {
        return 0;
    }
    if (m_attemptState.exists && candidate.attemptInstanceId != m_attemptState.attemptInstanceId) {
        return 0;
    }
    return std::max<qint64>(0, m_attemptElapsed.elapsed());
}

bool SessionController::persistAttemptEvents(
    const QString &startTrigger,
    const AttemptRuntimeState &candidate,
    const QList<AttemptEventInput> &events,
    const std::optional<TerminalAttemptInput> &terminal,
    QString *errorMessage)
{
    if (!candidate.exists || m_attemptSink == nullptr) {
        return true;
    }
    const bool startsAttempt = !m_attemptState.exists;
    AttemptAppendBatch batch;
    batch.attemptInstanceId = candidate.attemptInstanceId;
    batch.expectedNextEventIndex = startsAttempt ? 0 : m_attemptState.nextEventIndex;
    batch.expectedPreviousHash = startsAttempt ? QString() : m_attemptState.previousHash;

    if (startsAttempt) {
        const PuzzleDefinition &puzzle = m_puzzles.at(m_currentPuzzleIndex);
        RetainedPuzzleRecord retained;
        retained.puzzleRecordId = puzzle.analysisSeed.sourceRecordId;
        retained.puzzleId = puzzle.id;
        retained.recordSchema = puzzle.analysisSeed.sourceRecordSchema;
        retained.canonicalJson = puzzle.analysisSeed.rawSourceRecordJson.toUtf8();
        // Exposure time belongs to the attempt identity. The source bytes are
        // retained only now, in the first synchronous interaction transaction.
        retained.retainedAtUtc = attemptUtcNow();
        batch.retainedPuzzleRecord = retained;

        AttemptInstance instance;
        instance.attemptInstanceId = candidate.attemptInstanceId;
        instance.puzzleId = puzzle.id;
        instance.puzzleRecordId = puzzle.analysisSeed.sourceRecordId;
        instance.solverId = m_solverId;
        instance.sessionId = m_attemptSessionId;
        instance.startedAtUtc = candidate.startedAtUtc;
        instance.puzzleSnapshot = attemptPuzzleSnapshot(puzzle);
        batch.newAttempt = instance;

        AttemptEventInput started;
        started.kind = QStringLiteral("attempt_started");
        started.occurredAtUtc = candidate.startedAtUtc;
        started.payload = {{QStringLiteral("trigger"), startTrigger}};
        batch.events.append(started);
    }
    batch.events.append(events);
    batch.terminalAttempt = terminal;

    AttemptAppendReceipt receipt;
    if (!m_attemptSink->appendBatch(batch, &receipt, errorMessage)) {
        return false;
    }
    AttemptRuntimeState committed = candidate;
    committed.nextEventIndex = receipt.nextEventIndex;
    committed.previousHash = receipt.previousHash;
    m_attemptState = committed;
    if (startsAttempt) {
        const QString puzzleId = m_puzzles.at(m_currentPuzzleIndex).id;
        m_lastAttemptStartByPuzzleId.insert(puzzleId, committed.startedAtUtc.toUTC());
    }
    return true;
}

void SessionController::clearAttemptRuntime()
{
    m_attemptState = {};
}

bool SessionController::finishAttemptForTransition(
    const QString &reason,
    bool recordRetry,
    QString *errorMessage)
{
    if (!m_attemptState.exists) {
        return true;
    }
    if (m_attemptState.terminal) {
        if (recordRetry) {
            AttemptRuntimeState retryState = m_attemptState;
            const QDateTime retryWallUtc = attemptUtcNow();
            retryState.lastObservedWallUtc = retryWallUtc;
            AttemptEventInput retry;
            retry.kind = QStringLiteral("retry_requested");
            retry.occurredAtUtc = eventTimeForAttempt(retryState, retryWallUtc);
            retry.elapsedMilliseconds = attemptElapsedMilliseconds(retryState);
            retry.payload = {{QStringLiteral("reason"), reason}};
            if (!persistAttemptEvents(QString(), retryState, {retry}, std::nullopt, errorMessage)) {
                return false;
            }
        }
        clearAttemptRuntime();
        return true;
    }

    AttemptRuntimeState abandoned = m_attemptState;
    abandoned.terminal = true;
    const QDateTime abandonWallUtc = attemptUtcNow();
    abandoned.lastObservedWallUtc = abandonWallUtc;
    AttemptEventInput event;
    event.kind = QStringLiteral("attempt_abandoned");
    event.terminalKind = QStringLiteral("abandoned");
    event.occurredAtUtc = eventTimeForAttempt(abandoned, abandonWallUtc);
    event.elapsedMilliseconds = attemptElapsedMilliseconds(abandoned);
    event.payload = {{QStringLiteral("reason"), reason}};
    QList<AttemptEventInput> events{event};
    if (recordRetry) {
        AttemptEventInput retry;
        retry.kind = QStringLiteral("retry_requested");
        retry.occurredAtUtc = event.occurredAtUtc;
        retry.elapsedMilliseconds = event.elapsedMilliseconds;
        retry.payload = {{QStringLiteral("reason"), reason}};
        events.append(retry);
    }
    if (!persistAttemptEvents(QString(), abandoned, events, std::nullopt, errorMessage)) {
        return false;
    }
    clearAttemptRuntime();
    return true;
}

bool SessionController::finalizePuzzleAttemptForAppExit(QString *errorMessage)
{
    return finishAttemptForTransition(QStringLiteral("app_exit"), false, errorMessage);
}

bool SessionController::finalizePuzzleAttemptForAnnotatedReplay(QString *errorMessage)
{
    return finishAttemptForTransition(QStringLiteral("annotated_replay_opened"), false, errorMessage);
}

bool SessionController::initialize(QString *errorMessage)
{
    const QVector<PuzzleDefinition> puzzles = m_puzzleSource.loadPuzzles(errorMessage);
    if (puzzles.isEmpty()) {
        return false;
    }
    return replacePuzzles(puzzles, errorMessage);
}

bool SessionController::replacePuzzles(const QVector<PuzzleDefinition> &puzzles, QString *errorMessage)
{
    if (puzzles.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no puzzles available from the selected source");
        }
        return false;
    }

    QVector<int> replacementFilteredIndices;
    replacementFilteredIndices.reserve(puzzles.size());
    QSet<QString> replacementIds;
    for (int index = 0; index < puzzles.size(); ++index) {
        const PuzzleDefinition &puzzle = puzzles.at(index);
        if (replacementIds.contains(puzzle.id)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("replacement puzzle %1 repeats puzzle id '%2'")
                                    .arg(index + 1)
                                    .arg(puzzle.id);
            }
            return false;
        }
        replacementIds.insert(puzzle.id);
        QString validationError;
        if (!validatePuzzleDefinition(puzzle, &validationError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("replacement puzzle %1 is invalid: %2")
                                    .arg(index + 1)
                                    .arg(validationError);
            }
            return false;
        }
        const QString difficulty = puzzle.metadata.difficulty.trimmed().toLower();
        if (m_settings.difficulty == QStringLiteral("all") || difficulty.isEmpty()
            || difficulty == QStringLiteral("all") || difficulty == m_settings.difficulty) {
            replacementFilteredIndices.append(index);
        }
    }
    if (replacementFilteredIndices.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no puzzles available for the selected difficulty");
        }
        return false;
    }

    QString transitionError;
    if (!finishAttemptForTransition(QStringLiteral("queue_replaced"), false, &transitionError)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("could not preserve the current solve attempt before replacing the queue: %1")
                                .arg(transitionError);
        }
        emit errorRaised(transitionError);
        return false;
    }

    // Commit only after the complete replacement batch has passed validation.
    m_puzzles = puzzles;
    m_filteredIndices = replacementFilteredIndices;
    m_currentPuzzleSlot = 0;
    m_currentPuzzleIndex = -1;
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
    if (isImportedEngineRecord(puzzle)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "imported engine-line records keep their pack provenance session-only and disallow source-game hydration");
        }
        return false;
    }
    if (m_attemptState.exists || !m_gameStateStore.moves().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("source-game history cannot reset a puzzle after its solve attempt has started");
        }
        return false;
    }
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

    QVector<PuzzleDefinition> additions;
    additions.reserve(puzzles.size());
    QSet<QString> incomingIds;
    for (const PuzzleDefinition &puzzle : puzzles) {
        if (incomingIds.contains(puzzle.id)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("append input repeats puzzle id '%1'").arg(puzzle.id);
            }
            return false;
        }
        incomingIds.insert(puzzle.id);
        if (existingIds.contains(puzzle.id)) {
            continue;
        }
        QString validationError;
        if (!validatePuzzleDefinition(puzzle, &validationError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("appended puzzle '%1' is invalid: %2")
                                    .arg(puzzle.id, validationError);
            }
            return false;
        }
        additions.append(puzzle);
        existingIds.append(puzzle.id);
    }

    if (additions.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no new puzzles were available to append");
        }
        return false;
    }

    m_puzzles += additions;
    const int appendedCount = additions.size();

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
        && m_gameStateStore.isViewingLatest()
        && !m_solutionWasRevealed
        && !m_attemptBlockedByDataError;
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
    QString attemptError;
    if (!finishAttemptForTransition(QStringLiteral("difficulty_changed"), false, &attemptError)) {
        emit errorRaised(QStringLiteral("difficulty change stopped because solve history could not be saved: %1")
                             .arg(attemptError));
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
    QString attemptError;
    if (!finishAttemptForTransition(QStringLiteral("queue_size_changed"), false, &attemptError)) {
        emit errorRaised(QStringLiteral("queue-size change stopped because solve history could not be saved: %1")
                             .arg(attemptError));
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

    const QDateTime wallUtc = attemptUtcNow();
    AttemptRuntimeState attempt = attemptCandidate(wallUtc);
    const QDateTime eventUtc = eventTimeForAttempt(attempt, wallUtc);
    const qint64 elapsed = attemptElapsedMilliseconds(attempt);
    const bool clockRollback = attempt.exists && attemptWallClockRolledBack(attempt, wallUtc);
    const bool clockAnomaly = attempt.exists && attemptClockDrifted(attempt, wallUtc, elapsed);
    attempt.lastObservedWallUtc = wallUtc.toUTC();
    const int solutionIndexBefore = m_puzzleEngine.solutionIndex();
    const QString expectedMove = m_puzzleEngine.nextExpectedMove();
    PuzzleEngine stagedEngine = m_puzzleEngine;
    const SubmissionResult result = stagedEngine.submitUserMove(m_gameStateStore.latestPosition(), moveUci);
    const QString normalizedMove = moveUci.trimmed().toLower();
    QJsonObject details{
        {QStringLiteral("applied_moves"), appliedMoveArray(result.appliedMoves)},
        {QStringLiteral("expected_move_uci"), expectedMove},
        {QStringLiteral("move_uci"), normalizedMove},
        {QStringLiteral("solution_index_after"), stagedEngine.solutionIndex()},
        {QStringLiteral("solution_index_before"), solutionIndexBefore},
    };
    AttemptEventInput event;
    event.occurredAtUtc = eventUtc;
    event.elapsedMilliseconds = elapsed;
    std::optional<TerminalAttemptInput> terminal;

    if (clockAnomaly) {
        attempt.terminal = true;
        event.kind = QStringLiteral("attempt_invalidated");
        event.terminalKind = QStringLiteral("invalidated");
        details.insert(
            QStringLiteral("reason"),
            clockRollback ? QStringLiteral("wall_clock_rollback") : QStringLiteral("wall_clock_drift"));
        details.insert(QStringLiteral("attempted_action"), QStringLiteral("move"));
        insertClockAnomalyDetails(
            &details, attempt.wallClockBaselineUtc, wallUtc, elapsed);
        if (result.accepted) {
            details.insert(
                QStringLiteral("solver_outcome"),
                result.status == SessionStatus::Solved
                    ? QStringLiteral("solved")
                    : (result.status == SessionStatus::Failed
                            ? QStringLiteral("failed")
                            : QStringLiteral("correct_move")));
        }
        event.payload = details;
    } else if (!result.accepted) {
        event.kind = QStringLiteral("move_rejected");
        details.insert(QStringLiteral("reason"), QStringLiteral("illegal_or_malformed"));
        event.payload = details;
        QString attemptError;
        if (!persistAttemptEvents(QStringLiteral("move"), attempt, {event}, std::nullopt, &attemptError)) {
            emit errorRaised(QStringLiteral("move was not applied because solve history could not be saved: %1")
                                 .arg(attemptError));
            return;
        }
        if (!result.prompt.isEmpty()) {
            emit promptChanged(result.prompt);
        }
        return;
    }

    if (!clockAnomaly && result.status == SessionStatus::Solved) {
        attempt.terminal = true;
        event.kind = QStringLiteral("attempt_solved");
        event.terminalKind = QStringLiteral("solved");
        const std::optional<qint64> duration = elapsed > 0 ? std::optional<qint64>(elapsed) : std::nullopt;
        event.payload = terminalEventPayload(
            QStringLiteral("solved"), duration, attempt.wrongMoveCount, attempt.hintsUsed, false, details);
        TerminalAttemptInput terminalValue;
        terminalValue.outcome = QStringLiteral("solved");
        terminalValue.observedAtUtc = eventUtc;
        terminalValue.durationMilliseconds = duration;
        terminalValue.wrongMoveCount = attempt.wrongMoveCount;
        terminalValue.hintsUsed = attempt.hintsUsed;
        terminalValue.metadata = exactAttemptMetadata();
        terminal = terminalValue;
    } else if (!clockAnomaly && result.status == SessionStatus::Failed && normalizedMove != expectedMove) {
        attempt.terminal = true;
        ++attempt.wrongMoveCount;
        event.kind = QStringLiteral("attempt_failed_wrong_move");
        event.terminalKind = QStringLiteral("failed_wrong_move");
        const std::optional<qint64> duration = elapsed > 0 ? std::optional<qint64>(elapsed) : std::nullopt;
        event.payload = terminalEventPayload(
            QStringLiteral("failed"), duration, attempt.wrongMoveCount, attempt.hintsUsed, false, details);
        TerminalAttemptInput terminalValue;
        terminalValue.outcome = QStringLiteral("failed");
        terminalValue.observedAtUtc = eventUtc;
        terminalValue.durationMilliseconds = duration;
        terminalValue.wrongMoveCount = attempt.wrongMoveCount;
        terminalValue.hintsUsed = attempt.hintsUsed;
        terminalValue.metadata = exactAttemptMetadata();
        terminal = terminalValue;
    } else if (!clockAnomaly && result.status == SessionStatus::Failed) {
        attempt.terminal = true;
        event.kind = QStringLiteral("attempt_invalidated");
        event.terminalKind = QStringLiteral("invalidated");
        details.insert(QStringLiteral("reason"), QStringLiteral("solution_continuation_error"));
        event.payload = details;
    } else if (!clockAnomaly) {
        event.kind = QStringLiteral("move_correct");
        event.payload = details;
    }

    QString attemptError;
    if (!persistAttemptEvents(QStringLiteral("move"), attempt, {event}, terminal, &attemptError)) {
        emit errorRaised(QStringLiteral("move was not applied because solve history could not be saved: %1")
                             .arg(attemptError));
        return;
    }
    const bool continuationError = result.accepted
        && result.status == SessionStatus::Failed
        && normalizedMove == expectedMove;
    if (continuationError) {
        m_attemptBlockedByDataError = true;
        emit errorRaised(QStringLiteral(
            "the supplied solution continuation could not be replayed; the attempt was invalidated and the board was left unchanged"));
        emit promptChanged(currentPrompt());
        emit sessionChanged();
        return;
    }
    m_puzzleEngine = stagedEngine;
    m_gameStateStore.applyMoveSequence(result.appliedMoves);
    if (clockAnomaly) {
        m_attemptBlockedByDataError = true;
        emit errorRaised(QStringLiteral(
            "the wall clock diverged from monotonic time; this action was retained locally but excluded from solve-history export"));
    }
    emit promptChanged(clockAnomaly ? currentPrompt() : result.prompt);
    emit sessionChanged();
    if (!clockAnomaly && result.status == SessionStatus::Solved && m_settings.autoAdvance) {
        nextPuzzle();
    }
}

void SessionController::requestHint()
{
    if (!canSubmitMoves()) {
        emit hintAvailable(QStringLiteral("no hint available"));
        return;
    }
    const QString hint = m_puzzleEngine.nextExpectedMove();
    if (!hint.isEmpty()) {
        const int solutionIndex = m_puzzleEngine.solutionIndex();
        if (m_attemptState.exists && m_attemptState.disclosedHintIndices.contains(solutionIndex)) {
            emit hintAvailable(QStringLiteral("next move: %1").arg(hint));
            return;
        }
        const QDateTime wallUtc = attemptUtcNow();
        AttemptRuntimeState attempt = attemptCandidate(wallUtc);
        AttemptEventInput event;
        event.occurredAtUtc = eventTimeForAttempt(attempt, wallUtc);
        event.elapsedMilliseconds = attemptElapsedMilliseconds(attempt);
        const bool clockRollback = attempt.exists && attemptWallClockRolledBack(attempt, wallUtc);
        const bool clockAnomaly = attempt.exists
            && attemptClockDrifted(attempt, wallUtc, event.elapsedMilliseconds);
        attempt.lastObservedWallUtc = wallUtc.toUTC();
        if (clockAnomaly) {
            attempt.terminal = true;
            event.kind = QStringLiteral("attempt_invalidated");
            event.terminalKind = QStringLiteral("invalidated");
            event.payload = {
                {QStringLiteral("attempted_action"), QStringLiteral("hint")},
                {QStringLiteral("hint_move_uci"), hint},
                {QStringLiteral("reason"), clockRollback
                     ? QStringLiteral("wall_clock_rollback")
                     : QStringLiteral("wall_clock_drift")},
                {QStringLiteral("solution_index"), solutionIndex},
            };
            insertClockAnomalyDetails(
                &event.payload,
                attempt.wallClockBaselineUtc,
                wallUtc,
                event.elapsedMilliseconds);
        } else {
            ++attempt.hintsUsed;
            attempt.disclosedHintIndices.insert(solutionIndex);
            event.kind = QStringLiteral("hint_granted");
            event.payload = {
                {QStringLiteral("hint_move_uci"), hint},
                {QStringLiteral("solution_index"), solutionIndex},
            };
        }
        QString attemptError;
        if (!persistAttemptEvents(QStringLiteral("hint"), attempt, {event}, std::nullopt, &attemptError)) {
            emit errorRaised(QStringLiteral("hint was withheld because solve history could not be saved: %1")
                                 .arg(attemptError));
            return;
        }
        if (clockAnomaly) {
            m_attemptBlockedByDataError = true;
            emit errorRaised(QStringLiteral(
                "the wall clock diverged from monotonic time; the hint was retained locally but this attempt cannot be exported"));
            emit promptChanged(currentPrompt());
        }
    }
    emit hintAvailable(hint.isEmpty() ? QStringLiteral("no hint available") : QStringLiteral("next move: %1").arg(hint));
}

void SessionController::revealSolution()
{
    if (m_currentPuzzleIndex < 0 || m_currentPuzzleIndex >= m_puzzles.size()) {
        return;
    }
    const PuzzleDefinition &puzzle = m_puzzles.at(m_currentPuzzleIndex);
    QString positionError;
    const auto initialPosition = ChessPosition::fromFen(puzzle.fenStart, &positionError);
    if (!initialPosition.has_value()) {
        emit errorRaised(positionError);
        return;
    }
    PuzzleEngine stagedEngine;
    stagedEngine.loadPuzzle(puzzle);
    const SubmissionResult result = stagedEngine.revealSolution(*initialPosition);
    if (!result.accepted) {
        emit errorRaised(result.prompt);
        return;
    }

    const QDateTime wallUtc = attemptUtcNow();
    AttemptRuntimeState attempt = attemptCandidate(wallUtc);
    const bool activeAttempt = !(m_attemptState.exists && m_attemptState.terminal);
    const QDateTime eventUtc = eventTimeForAttempt(attempt, wallUtc);
    const qint64 elapsed = attemptElapsedMilliseconds(attempt);
    const bool clockRollback = attempt.exists && activeAttempt
        && attemptWallClockRolledBack(attempt, wallUtc);
    const bool clockAnomaly = attempt.exists && activeAttempt
        && attemptClockDrifted(attempt, wallUtc, elapsed);
    attempt.lastObservedWallUtc = wallUtc.toUTC();
    QJsonObject details{
        {QStringLiteral("applied_moves"), appliedMoveArray(result.appliedMoves)},
        {QStringLiteral("solution_index_after"), stagedEngine.solutionIndex()},
    };
    AttemptEventInput event;
    event.occurredAtUtc = eventUtc;
    event.elapsedMilliseconds = elapsed;
    std::optional<TerminalAttemptInput> terminal;
    if (m_attemptState.exists && m_attemptState.terminal) {
        event.kind = QStringLiteral("solution_revealed_review");
        event.payload = details;
    } else if (result.status == SessionStatus::Solved) {
        attempt.terminal = true;
        event.kind = QStringLiteral("solution_revealed");
        event.terminalKind = QStringLiteral("revealed_failed");
        const std::optional<qint64> duration = elapsed > 0 ? std::optional<qint64>(elapsed) : std::nullopt;
        event.payload = terminalEventPayload(
            QStringLiteral("failed"), duration, attempt.wrongMoveCount, attempt.hintsUsed, true, details);
        TerminalAttemptInput terminalValue;
        terminalValue.outcome = QStringLiteral("failed");
        terminalValue.observedAtUtc = eventUtc;
        terminalValue.durationMilliseconds = duration;
        terminalValue.wrongMoveCount = attempt.wrongMoveCount;
        terminalValue.hintsUsed = attempt.hintsUsed;
        terminalValue.solutionRevealed = true;
        terminalValue.metadata = exactAttemptMetadata();
        terminal = terminalValue;
    } else {
        attempt.terminal = true;
        event.kind = QStringLiteral("attempt_invalidated");
        event.terminalKind = QStringLiteral("invalidated");
        details.insert(QStringLiteral("reason"), QStringLiteral("solution_replay_error"));
        event.payload = details;
    }
    if (clockAnomaly) {
        terminal.reset();
        attempt.terminal = true;
        event.kind = QStringLiteral("attempt_invalidated");
        event.terminalKind = QStringLiteral("invalidated");
        event.payload = details;
        event.payload.insert(
            QStringLiteral("reason"),
            clockRollback ? QStringLiteral("wall_clock_rollback") : QStringLiteral("wall_clock_drift"));
        event.payload.insert(QStringLiteral("solver_outcome"), QStringLiteral("revealed_failed"));
        insertClockAnomalyDetails(
            &event.payload, attempt.wallClockBaselineUtc, wallUtc, elapsed);
    }
    QString attemptError;
    if (!persistAttemptEvents(QStringLiteral("reveal"), attempt, {event}, terminal, &attemptError)) {
        emit errorRaised(QStringLiteral("solution was not revealed because solve history could not be saved: %1")
                             .arg(attemptError));
        return;
    }
    if (result.status != SessionStatus::Solved) {
        m_attemptBlockedByDataError = true;
        emit errorRaised(QStringLiteral(
            "the supplied solution could not be replayed; the attempt was invalidated and the board was left unchanged"));
        emit promptChanged(currentPrompt());
        emit sessionChanged();
        return;
    }
    m_puzzleEngine = stagedEngine;
    m_gameStateStore.resetToInitial();
    m_gameStateStore.applyMoveSequence(result.appliedMoves);
    m_solutionWasRevealed = true;
    if (clockAnomaly) {
        m_attemptBlockedByDataError = true;
        emit errorRaised(QStringLiteral(
            "the wall clock diverged from monotonic time; the revealed line was retained locally but excluded from solve-history export"));
    }
    emit promptChanged(currentPrompt());
    emit sessionChanged();
}

void SessionController::retryPuzzle()
{
    QString attemptError;
    if (!finishAttemptForTransition(QStringLiteral("retry"), true, &attemptError)) {
        emit errorRaised(QStringLiteral("retry stopped because solve history could not be saved: %1")
                             .arg(attemptError));
        return;
    }
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
    QString attemptError;
    if (!finishAttemptForTransition(QStringLiteral("previous_puzzle"), false, &attemptError)) {
        emit errorRaised(QStringLiteral("navigation stopped because solve history could not be saved: %1")
                             .arg(attemptError));
        return;
    }
    --m_currentPuzzleSlot;
    applyCurrentPuzzle();
}

void SessionController::nextPuzzle()
{
    const bool canExpandAtBoundary = m_currentPuzzleSlot >= 0
        && m_currentPuzzleSlot >= m_visiblePuzzleCount - 1
        && m_visiblePuzzleCount < m_filteredIndices.size();
    if (!canGoToNextPuzzle() && !canExpandAtBoundary) {
        return;
    }
    QString attemptError;
    if (!finishAttemptForTransition(QStringLiteral("next_puzzle"), false, &attemptError)) {
        emit errorRaised(QStringLiteral("navigation stopped because solve history could not be saved: %1")
                             .arg(attemptError));
        return;
    }
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
    m_solutionWasRevealed = false;
    m_attemptBlockedByDataError = false;
    m_puzzleExposureStartedAtUtc = attemptUtcNow();
    m_attemptElapsed.start();
    emit promptChanged(currentPrompt());
    emit sessionChanged();
}

void SessionController::rebuildFilteredPuzzleList()
{
    m_filteredIndices.clear();
    for (int index = 0; index < m_puzzles.size(); ++index) {
        const QString difficulty = m_puzzles.at(index).metadata.difficulty.trimmed().toLower();
        if (m_settings.difficulty == QStringLiteral("all") || difficulty.isEmpty()
            || difficulty == QStringLiteral("all") || difficulty == m_settings.difficulty) {
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
    if (m_attemptBlockedByDataError) {
        return QStringLiteral("solve history invalidated by a system or puzzle-data error; retry to continue");
    }
    if (m_solutionWasRevealed) {
        return QStringLiteral("solution shown — attempt not solved; review the line or retry");
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
