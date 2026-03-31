#include "lichess_client.h"

#include <algorithm>

#include "chess_position.h"
#include "puzzle_api_requests.h"
#include "puzzle_api_types.h"
#include "pgn_utils.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>

namespace {

QByteArray firstJsonLine(const QByteArray &body)
{
    const QList<QByteArray> lines = body.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            return trimmed;
        }
    }
    return {};
}

QString sideToMoveFromFen(const QString &fen)
{
    const QStringList parts = fen.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 2) {
        return QStringLiteral("unknown");
    }
    return parts.at(1) == QStringLiteral("w") ? QStringLiteral("white") : QStringLiteral("black");
}

QString timeControlFromObject(const QJsonObject &gameObject)
{
    const QString clockString = gameObject.value(QStringLiteral("clock")).toString();
    if (!clockString.isEmpty()) {
        return clockString;
    }
    const QJsonObject clockObject = gameObject.value(QStringLiteral("clock")).toObject();
    if (!clockObject.isEmpty()) {
        const int initial = clockObject.value(QStringLiteral("initial")).toInt();
        const int increment = clockObject.value(QStringLiteral("increment")).toInt();
        if (initial > 0 || increment > 0) {
            return QStringLiteral("%1+%2").arg(initial).arg(increment);
        }
    }
    return QStringLiteral("unknown");
}

QJsonObject playerObjectFromColor(const QJsonValue &playersValue, const QString &color)
{
    if (playersValue.isObject()) {
        return playersValue.toObject().value(color).toObject();
    }
    if (playersValue.isArray()) {
        for (const QJsonValue &value : playersValue.toArray()) {
            const QJsonObject candidate = value.toObject();
            if (candidate.value(QStringLiteral("color")).toString().compare(color, Qt::CaseInsensitive) == 0) {
                return candidate;
            }
        }
    }
    return {};
}

QString playerNameFromColor(const QJsonValue &playersValue, const QString &color)
{
    const QJsonObject colorObject = playerObjectFromColor(playersValue, color);
    const QJsonObject userObject = colorObject.value(QStringLiteral("user")).toObject();
    if (userObject.contains(QStringLiteral("name"))) {
        return userObject.value(QStringLiteral("name")).toString();
    }
    return colorObject.value(QStringLiteral("name")).toString(QStringLiteral("unknown"));
}

int playerRatingFromColor(const QJsonValue &playersValue, const QString &color)
{
    return playerObjectFromColor(playersValue, color).value(QStringLiteral("rating")).toInt();
}

QStringList stringListFromJson(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &item : value.toArray()) {
        out.append(item.toString());
    }
    return out;
}

QString apiDifficultyFromLocalDifficulty(QString difficulty)
{
    difficulty = difficulty.trimmed().toLower();
    if (difficulty == QStringLiteral("medium")) {
        return puzzle_api::to_string(puzzle_api::puzzle_difficulty::normal);
    }
    return puzzle_api::to_string(puzzle_api::puzzle_difficulty::harder);
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

parlawl::puzzle_runner::PieceType pieceTypeFromSanPrefix(QChar token)
{
    using namespace parlawl::puzzle_runner;
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

bool isCaptureSan(const parlawl::puzzle_runner::ChessPosition &position, const parlawl::puzzle_runner::Move &move)
{
    using namespace parlawl::puzzle_runner;
    const Piece target = position.pieceAt(move.to);
    if (!target.isEmpty()) {
        return true;
    }
    const Piece mover = position.pieceAt(move.from);
    return mover.type == PieceType::Pawn && ChessPosition::fileOf(move.from) != ChessPosition::fileOf(move.to);
}

std::optional<parlawl::puzzle_runner::Move> resolveSanMove(const parlawl::puzzle_runner::ChessPosition &position, QString san)
{
    using namespace parlawl::puzzle_runner;
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

struct replayed_pgn_state
{
    bool ok = false;
    QString errorMessage;
    QString fen;
    QString lastMoveUci;
    QString sideToMove;
};

replayed_pgn_state replayPgnToInitialPly(const QString &pgnText, int initialPly)
{
    using namespace parlawl::puzzle_runner;
    replayed_pgn_state result;
    QString errorMessage;
    const auto initial = ChessPosition::fromFen(QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"), &errorMessage);
    if (!initial.has_value()) {
        result.errorMessage = errorMessage;
        return result;
    }

    ChessPosition current = *initial;
    const QStringList moves = parlawl::puzzle_runner::pgnMoveList(pgnText);
    const int sourcePlyCount = std::min(initialPly + 1, static_cast<int>(moves.size()));
    if (sourcePlyCount <= 0) {
        result.errorMessage = QStringLiteral("batch puzzle did not include enough source moves to reach initialPly");
        return result;
    }

    for (int index = 0; index < sourcePlyCount; ++index) {
        const auto resolvedMove = resolveSanMove(current, moves.at(index));
        if (!resolvedMove.has_value() || !current.applyMove(*resolvedMove)) {
            result.errorMessage = QStringLiteral("unable to replay source PGN at ply %1").arg(index + 1);
            return result;
        }
        result.lastMoveUci = resolvedMove->uci();
    }

    result.ok = true;
    result.fen = current.toFen();
    result.sideToMove = current.sideToMove() == PieceColor::White ? QStringLiteral("white") : QStringLiteral("black");
    return result;
}

QString humanizeToken(QString token)
{
    token = token.trimmed();
    if (token.isEmpty()) {
        return token;
    }
    token.replace(QLatin1Char('_'), QLatin1Char(' '));
    token.replace(QLatin1Char('-'), QLatin1Char(' '));
    token.replace(QRegularExpression(QStringLiteral("([a-z])([A-Z])")), QStringLiteral("\\1 \\2"));
    const QStringList parts = token.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QStringList humanized;
    humanized.reserve(parts.size());
    for (const QString &part : parts) {
        QString formatted = part.toLower();
        formatted[0] = formatted.at(0).toUpper();
        humanized.append(formatted);
    }
    return humanized.join(QLatin1Char(' '));
}

QString puzzleTitleFromThemes(const QStringList &themes, const QString &puzzleId)
{
    if (themes.isEmpty()) {
        return QStringLiteral("lichess puzzle %1").arg(puzzleId);
    }
    QStringList parts;
    for (int i = 0; i < themes.size() && i < 2; ++i) {
        parts.append(humanizeToken(themes.at(i)));
    }
    return parts.join(QStringLiteral(" / "));
}

QString sourceLabelFromGameObject(const QJsonObject &gameObject)
{
    const QString perfName = gameObject.value(QStringLiteral("perf")).toObject().value(QStringLiteral("name")).toString();
    const QString clock = timeControlFromObject(gameObject);
    if (!clock.isEmpty() && !perfName.isEmpty()) {
        return QStringLiteral("From game %1 • %2").arg(clock, perfName);
    }
    if (!clock.isEmpty()) {
        return QStringLiteral("From game %1").arg(clock);
    }
    if (!perfName.isEmpty()) {
        return QStringLiteral("From game • %1").arg(perfName);
    }
    return QStringLiteral("From lichess game");
}

} // namespace

LichessClient::LichessClient(QObject *parent)
    : QObject(parent)
    , m_networkAccessManager(new QNetworkAccessManager(this))
{
}

LichessFetchResult LichessClient::fetchLatestSolvedPuzzle(const QString &apiToken)
{
    if (apiToken.trimmed().isEmpty()) {
        return {false, QStringLiteral("puzzle_activity_fetch_failure"), QStringLiteral("lichess api token is required")};
    }

    const HttpResult activityResult = fetchLatestPuzzleActivity(apiToken);
    if (!activityResult.ok) {
        return {false, QStringLiteral("puzzle_activity_fetch_failure"), activityResult.errorMessage};
    }

    const QJsonDocument activityDocument = QJsonDocument::fromJson(firstJsonLine(activityResult.body));
    const QString puzzleId = activityDocument.object().value(QStringLiteral("puzzle")).toObject().value(QStringLiteral("id")).toString();
    if (puzzleId.isEmpty()) {
        return {false, QStringLiteral("puzzle_activity_fetch_failure"), QStringLiteral("latest solved puzzle activity did not include puzzle.id")};
    }

    const HttpResult detailResult = fetchPuzzleDetail(puzzleId, apiToken);
    if (!detailResult.ok) {
        return {false, QStringLiteral("source_game_fetch_failure"), detailResult.errorMessage};
    }

    const QJsonDocument detailDocument = QJsonDocument::fromJson(detailResult.body);
    const QString sourceGameId = detailDocument.object().value(QStringLiteral("game")).toObject().value(QStringLiteral("id")).toString();
    if (sourceGameId.isEmpty()) {
        return {
            false,
            QStringLiteral("source_game_fetch_failure"),
            QStringLiteral("puzzle detail did not include game.id for puzzle %1").arg(puzzleId)
        };
    }

    const HttpResult pgnResult = fetchSourceGamePgn(sourceGameId, apiToken);
    if (!pgnResult.ok) {
        return {false, QStringLiteral("source_game_fetch_failure"), pgnResult.errorMessage};
    }

    return normalizeFetch(activityResult.body, detailResult.body, pgnResult.body);
}

LichessBatchResult LichessClient::fetchTrainingBatch(const QString &apiToken, int count, const QString &difficulty)
{
    if (apiToken.trimmed().isEmpty()) {
        return {false, QStringLiteral("puzzle_batch_fetch_failure"), QStringLiteral("lichess api token is required"), 0};
    }

    const HttpResult batchResult = fetchPuzzleBatch(std::max(count, 1), difficulty, apiToken);
    if (!batchResult.ok) {
        return {false, QStringLiteral("puzzle_batch_fetch_failure"), batchResult.errorMessage, batchResult.statusCode};
    }

    LichessBatchResult result = normalizeBatchFetch(batchResult.body, difficulty);
    result.statusCode = batchResult.statusCode;
    return result;
}

SourceGamePgnResult LichessClient::fetchSourceGamePgnText(const QString &sourceGameId, const QString &apiToken)
{
    const HttpResult pgnResult = fetchSourceGamePgn(sourceGameId, apiToken);
    if (!pgnResult.ok) {
        return {false, pgnResult.errorMessage, pgnResult.statusCode, QString(), QString()};
    }

    const QString pgnText = QString::fromUtf8(pgnResult.body);
    return {true, QString(), pgnResult.statusCode, pgnText, parlawl::puzzle_runner::pgnOpeningName(pgnText)};
}

LichessClient::HttpResult LichessClient::performGet(
    const QString &url,
    const QString &apiToken,
    const QList<QPair<QByteArray, QByteArray>> &headers
)
{
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("parlawl/0.1"));
    request.setTransferTimeout(10000);
    if (!apiToken.trimmed().isEmpty()) {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + apiToken.trimmed().toUtf8());
    }
    for (const auto &header : headers) {
        request.setRawHeader(header.first, header.second);
    }

    QNetworkReply *reply = m_networkAccessManager->get(request);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    HttpResult result;
    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.ok = reply->error() == QNetworkReply::NoError && result.statusCode >= 200 && result.statusCode < 300;
    if (!result.ok) {
        result.errorMessage = QStringLiteral("GET %1 failed (%2): %3")
                                  .arg(url)
                                  .arg(result.statusCode)
                                  .arg(reply->errorString());
    }
    reply->deleteLater();
    return result;
}

LichessClient::HttpResult LichessClient::performGet(
    const puzzle_api::request_spec &request,
    const QString &apiToken,
    const QList<QPair<QByteArray, QByteArray>> &headers)
{
    return performGet(puzzle_api::serialize_url(QStringLiteral("https://lichess.org"), request), apiToken, headers);
}

LichessClient::HttpResult LichessClient::fetchLatestPuzzleActivity(const QString &apiToken)
{
    const QList<QPair<QByteArray, QByteArray>> headers = {
        {"Accept", "application/x-ndjson, application/json"},
    };

    puzzle_api::request_spec activityRequest;
    QString errorMessage;
    const bool built = puzzle_api::build_puzzle_activity_request(
        puzzle_api::puzzle_activity_options{10, std::nullopt, std::nullopt},
        &activityRequest,
        &errorMessage);
    if (!built) {
        return {false, 0, {}, errorMessage};
    }

    HttpResult result = performGet(activityRequest, apiToken, headers);
    if (result.ok) {
        const QList<QByteArray> lines = result.body.split('\n');
        QByteArray firstSolvedLine;
        for (const QByteArray &line : lines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            const QJsonDocument document = QJsonDocument::fromJson(trimmed);
            if (document.object().value(QStringLiteral("win")).toBool()) {
                firstSolvedLine = trimmed;
                break;
            }
        }
        if (firstSolvedLine.isEmpty()) {
            return {false, 200, result.body, QStringLiteral("no solved puzzle activity found in the latest activity window")};
        }
        result.body = firstSolvedLine;
        return result;
    }

    // Assumption: older tokens or deployments may still expose the prior path.
    HttpResult fallback = performGet(QStringLiteral("https://lichess.org/api/user/puzzle-activity?max=10"), apiToken, headers);
    if (!fallback.ok) {
        return fallback;
    }

    const QList<QByteArray> lines = fallback.body.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(trimmed);
        if (document.object().value(QStringLiteral("win")).toBool()) {
            fallback.body = trimmed;
            return fallback;
        }
    }

    return {false, 200, fallback.body, QStringLiteral("no solved puzzle activity found in fallback activity path")};
}

LichessClient::HttpResult LichessClient::fetchPuzzleBatch(int count, const QString &difficulty, const QString &apiToken)
{
    puzzle_api::puzzle_batch_fetch_options options;
    options.angle = QStringLiteral("mix");
    options.nb = std::clamp(count, 1, 50);
    options.difficulty = puzzle_api::parse_puzzle_difficulty(apiDifficultyFromLocalDifficulty(difficulty));

    puzzle_api::request_spec request;
    QString errorMessage;
    if (!puzzle_api::build_puzzle_batch_fetch_request(options, &request, &errorMessage)) {
        return {false, 0, {}, errorMessage};
    }
    return performGet(request, apiToken, {{"Accept", "application/json"}});
}

LichessClient::HttpResult LichessClient::fetchPuzzleDetail(const QString &puzzleId, const QString &apiToken)
{
    puzzle_api::request_spec request;
    QString errorMessage;
    if (!puzzle_api::build_puzzle_id_request({puzzleId}, &request, &errorMessage)) {
        return {false, 0, {}, errorMessage};
    }
    return performGet(request, apiToken, {{"Accept", "application/json"}});
}

LichessClient::HttpResult LichessClient::fetchSourceGamePgn(const QString &sourceGameId, const QString &apiToken)
{
    return performGet(
        QStringLiteral("https://lichess.org/game/export/%1?evals=false&clocks=false").arg(sourceGameId),
        apiToken,
        {{"Accept", "application/x-chess-pgn"}}
    );
}

LichessFetchResult LichessClient::normalizeFetch(
    const QByteArray &activityBody,
    const QByteArray &detailBody,
    const QByteArray &pgnBody
) const
{
    const QJsonDocument activityDocument = QJsonDocument::fromJson(firstJsonLine(activityBody));
    const QJsonObject activityObject = activityDocument.object();
    const QJsonObject activityPuzzle = activityObject.value(QStringLiteral("puzzle")).toObject();

    const QJsonDocument detailDocument = QJsonDocument::fromJson(detailBody);
    const QJsonObject detailObject = detailDocument.object();
    const QJsonObject detailPuzzle = detailObject.value(QStringLiteral("puzzle")).toObject();
    const QJsonObject gameObject = detailObject.value(QStringLiteral("game")).toObject();
    const QJsonValue playersValue = gameObject.value(QStringLiteral("players"));

    PuzzleRound puzzleRound;
    puzzleRound.puzzleId = detailPuzzle.value(QStringLiteral("id")).toString(activityPuzzle.value(QStringLiteral("id")).toString());
    puzzleRound.puzzleRating = detailPuzzle.value(QStringLiteral("rating")).toInt(activityPuzzle.value(QStringLiteral("rating")).toInt());
    puzzleRound.timeControl = timeControlFromObject(gameObject);
    puzzleRound.whitePlayer = playerNameFromColor(playersValue, QStringLiteral("white"));
    puzzleRound.whiteRating = playerRatingFromColor(playersValue, QStringLiteral("white"));
    puzzleRound.blackPlayer = playerNameFromColor(playersValue, QStringLiteral("black"));
    puzzleRound.blackRating = playerRatingFromColor(playersValue, QStringLiteral("black"));
    puzzleRound.initialFen = detailPuzzle.value(QStringLiteral("fen")).toString(activityPuzzle.value(QStringLiteral("fen")).toString());
    puzzleRound.sideToMove = sideToMoveFromFen(puzzleRound.initialFen);
    puzzleRound.fetchedAtUtc = QDateTime::currentDateTimeUtc();
    puzzleRound.sourceGameId = gameObject.value(QStringLiteral("id")).toString();
    puzzleRound.lastMove = detailPuzzle.value(QStringLiteral("lastMove")).toString(activityPuzzle.value(QStringLiteral("lastMove")).toString());
    puzzleRound.solved = activityObject.value(QStringLiteral("win")).toBool();
    puzzleRound.rawPuzzleJson = QString::fromUtf8(detailDocument.toJson(QJsonDocument::Compact));
    puzzleRound.rawActivityJson = QString::fromUtf8(activityDocument.toJson(QJsonDocument::Compact));
    puzzleRound.solutionMovesJson = QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(stringListFromJson(detailPuzzle.value(QStringLiteral("solution"))))).toJson(QJsonDocument::Compact));
    puzzleRound.themesJson = QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(stringListFromJson(detailPuzzle.value(QStringLiteral("themes"))))).toJson(QJsonDocument::Compact));

    SourceGame sourceGame;
    sourceGame.sourceGameId = puzzleRound.sourceGameId;
    sourceGame.pgnText = QString::fromUtf8(pgnBody);
    sourceGame.openingName = parlawl::puzzle_runner::pgnOpeningName(sourceGame.pgnText);
    sourceGame.fetchedAtUtc = QDateTime::currentDateTimeUtc();

    return {true, QString(), QString(), puzzleRound, sourceGame};
}

LichessBatchResult LichessClient::normalizeBatchFetch(
    const QByteArray &batchBody,
    const QString &requestedDifficulty
)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(batchBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {false, QStringLiteral("puzzle_batch_parse_failure"), QStringLiteral("invalid batch response: %1").arg(parseError.errorString()), 0};
    }

    const QJsonArray puzzlesArray = document.object().value(QStringLiteral("puzzles")).toArray();
    if (puzzlesArray.isEmpty()) {
        return {false, QStringLiteral("puzzle_batch_parse_failure"), QStringLiteral("batch response did not contain any puzzles"), 0};
    }

    QVector<parlawl::puzzle_runner::PuzzleDefinition> puzzles;
    QSet<QString> seenIds;
    puzzles.reserve(puzzlesArray.size());

    for (const QJsonValue &value : puzzlesArray) {
        const QJsonObject entryObject = value.toObject();
        const QJsonObject puzzleObject = entryObject.value(QStringLiteral("puzzle")).toObject();
        const QJsonObject gameObject = entryObject.value(QStringLiteral("game")).toObject();
        const QString puzzleId = puzzleObject.value(QStringLiteral("id")).toString().trimmed();
        const QString sourceGameId = gameObject.value(QStringLiteral("id")).toString().trimmed();
        const QString pgnText = gameObject.value(QStringLiteral("pgn")).toString().trimmed();
        const int initialPly = puzzleObject.value(QStringLiteral("initialPly")).toInt(-1);
        const QStringList solution = stringListFromJson(puzzleObject.value(QStringLiteral("solution")));
        if (puzzleId.isEmpty() || sourceGameId.isEmpty() || pgnText.isEmpty() || initialPly < 0
            || solution.isEmpty() || seenIds.contains(puzzleId)) {
            continue;
        }

        const replayed_pgn_state replay = replayPgnToInitialPly(pgnText, initialPly);
        if (!replay.ok) {
            continue;
        }

        parlawl::puzzle_runner::PuzzleDefinition puzzle;
        puzzle.id = puzzleId;
        puzzle.fenStart = replay.fen;
        puzzle.solutionMoves = solution;
        puzzle.metadata.themes = stringListFromJson(puzzleObject.value(QStringLiteral("themes")));
        puzzle.metadata.title = puzzleTitleFromThemes(puzzle.metadata.themes, puzzleId);
        puzzle.metadata.difficulty = requestedDifficulty.trimmed().isEmpty() ? QStringLiteral("all") : requestedDifficulty.trimmed().toLower();
        puzzle.metadata.source = QStringLiteral("lichess_batch");
        puzzle.metadata.sourceLabel = sourceLabelFromGameObject(gameObject);
        puzzle.metadata.rating = puzzleObject.value(QStringLiteral("rating")).toInt();
        puzzle.metadata.playedCount = puzzleObject.value(QStringLiteral("plays")).toInt();
        const QJsonValue playersValue = gameObject.value(QStringLiteral("players"));
        puzzle.metadata.whiteName = playerNameFromColor(playersValue, QStringLiteral("white"));
        puzzle.metadata.whiteRating = playerRatingFromColor(playersValue, QStringLiteral("white"));
        puzzle.metadata.blackName = playerNameFromColor(playersValue, QStringLiteral("black"));
        puzzle.metadata.blackRating = playerRatingFromColor(playersValue, QStringLiteral("black"));

        puzzle.analysisSeed.sourceGameId = sourceGameId;
        puzzle.analysisSeed.timeControl = timeControlFromObject(gameObject);
        puzzle.analysisSeed.sideToMove = replay.sideToMove;
        puzzle.analysisSeed.lastMove = replay.lastMoveUci;
        puzzle.analysisSeed.rawPuzzleJson = QString::fromUtf8(QJsonDocument(entryObject).toJson(QJsonDocument::Compact));
        puzzle.analysisSeed.sourceGamePgn = pgnText;
        puzzle.analysisSeed.openingName = parlawl::puzzle_runner::pgnOpeningName(puzzle.analysisSeed.sourceGamePgn);
        seenIds.insert(puzzleId);
        puzzles.append(puzzle);
    }

    if (puzzles.isEmpty()) {
        return {false, QStringLiteral("puzzle_batch_parse_failure"), QStringLiteral("no usable puzzles were returned by the lichess batch response"), 0};
    }

    return {true, QString(), QString(), 200, puzzles};
}
