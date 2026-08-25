#include "annotated_replay_pack.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QHash>
#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QStringList>
#include <QUrl>

namespace parlawl::puzzle_runner {

namespace {

constexpr int kMaximumReplayPlies = 700;
constexpr int kMaximumFactsPerMove = 32;
constexpr int kMaximumNarrationPerMove = 32;
constexpr int kMaximumVariationPlies = 64;
constexpr int kMaximumVariations = 64;
constexpr qsizetype kMaximumNarrationPerMoveBytes = 1024;
constexpr qsizetype kMaximumNarrationTotalBytes = 1024 * 1024;
constexpr qsizetype kMaximumFactDetailKeyBytes = 96;
constexpr qsizetype kMaximumFactDetailsBytes = 4096;
constexpr qint64 kMaximumExactJsonInteger = 9'007'199'254'740'991LL;
const QString kStandardInitialFen = QStringLiteral(
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

const QRegularExpression kUciPattern(QStringLiteral("^[a-h][1-8][a-h][1-8][qrbn]?$"));
const QRegularExpression kShaPattern(QStringLiteral("^[0-9a-f]{64}$"));
const QRegularExpression kSemanticIdPattern(QStringLiteral("^[a-z0-9][a-z0-9_.-]*-v[0-9]+:[0-9a-f]{64}$"));
const QRegularExpression kSquarePattern(QStringLiteral("^[a-h][1-8]$"));

void setError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
}

class JsonShapeScanner
{
public:
    explicit JsonShapeScanner(const QByteArray &input)
        : m_input(input)
    {
    }

    bool scan(QString *errorMessage)
    {
        skipWhitespace();
        if (!parseValue(0, errorMessage)) {
            return false;
        }
        skipWhitespace();
        if (m_offset != m_input.size()) {
            setError(errorMessage, QStringLiteral("annotated replay has trailing JSON material"));
            return false;
        }
        return true;
    }

private:
    void skipWhitespace()
    {
        while (m_offset < m_input.size()) {
            const char token = m_input.at(m_offset);
            if (token != ' ' && token != '\n' && token != '\r' && token != '\t') {
                break;
            }
            ++m_offset;
        }
    }

    bool parseValue(int depth, QString *errorMessage)
    {
        if (depth > 32) {
            setError(errorMessage, QStringLiteral("annotated replay JSON exceeds its nesting bound"));
            return false;
        }
        skipWhitespace();
        if (m_offset >= m_input.size()) {
            setError(errorMessage, QStringLiteral("annotated replay JSON is incomplete"));
            return false;
        }
        switch (m_input.at(m_offset)) {
        case '{':
            return parseObject(depth + 1, errorMessage);
        case '[':
            return parseArray(depth + 1, errorMessage);
        case '"': {
            QByteArray ignored;
            return parseString(&ignored, errorMessage);
        }
        case 't':
            return parseLiteral("true", errorMessage);
        case 'f':
            return parseLiteral("false", errorMessage);
        case 'n':
            return parseLiteral("null", errorMessage);
        default:
            return parseNumber(errorMessage);
        }
    }

    bool parseObject(int depth, QString *errorMessage)
    {
        ++m_offset;
        skipWhitespace();
        if (m_offset < m_input.size() && m_input.at(m_offset) == '}') {
            ++m_offset;
            return true;
        }
        QSet<QString> keys;
        while (m_offset < m_input.size()) {
            QByteArray rawKey;
            if (!parseString(&rawKey, errorMessage)) {
                return false;
            }
            QJsonParseError keyError;
            const QJsonDocument decoded = QJsonDocument::fromJson(
                QByteArray("[") + rawKey + QByteArray("]"), &keyError);
            if (keyError.error != QJsonParseError::NoError || !decoded.isArray()
                || decoded.array().size() != 1 || !decoded.array().first().isString()) {
                setError(errorMessage, QStringLiteral("annotated replay contains an invalid JSON key"));
                return false;
            }
            const QString key = decoded.array().first().toString();
            if (keys.contains(key)) {
                setError(errorMessage, QStringLiteral("annotated replay contains duplicate JSON key '%1'").arg(key));
                return false;
            }
            keys.insert(key);
            skipWhitespace();
            if (m_offset >= m_input.size() || m_input.at(m_offset) != ':') {
                setError(errorMessage, QStringLiteral("annotated replay JSON object is malformed"));
                return false;
            }
            ++m_offset;
            if (!parseValue(depth, errorMessage)) {
                return false;
            }
            skipWhitespace();
            if (m_offset < m_input.size() && m_input.at(m_offset) == ',') {
                ++m_offset;
                skipWhitespace();
                continue;
            }
            if (m_offset < m_input.size() && m_input.at(m_offset) == '}') {
                ++m_offset;
                return true;
            }
            setError(errorMessage, QStringLiteral("annotated replay JSON object is unterminated"));
            return false;
        }
        setError(errorMessage, QStringLiteral("annotated replay JSON object is incomplete"));
        return false;
    }

    bool parseArray(int depth, QString *errorMessage)
    {
        ++m_offset;
        skipWhitespace();
        if (m_offset < m_input.size() && m_input.at(m_offset) == ']') {
            ++m_offset;
            return true;
        }
        while (m_offset < m_input.size()) {
            if (!parseValue(depth, errorMessage)) {
                return false;
            }
            skipWhitespace();
            if (m_offset < m_input.size() && m_input.at(m_offset) == ',') {
                ++m_offset;
                continue;
            }
            if (m_offset < m_input.size() && m_input.at(m_offset) == ']') {
                ++m_offset;
                return true;
            }
            setError(errorMessage, QStringLiteral("annotated replay JSON array is unterminated"));
            return false;
        }
        setError(errorMessage, QStringLiteral("annotated replay JSON array is incomplete"));
        return false;
    }

    bool parseString(QByteArray *raw, QString *errorMessage)
    {
        skipWhitespace();
        if (m_offset >= m_input.size() || m_input.at(m_offset) != '"') {
            setError(errorMessage, QStringLiteral("annotated replay JSON expected a string"));
            return false;
        }
        const qsizetype begin = m_offset++;
        bool escaped = false;
        while (m_offset < m_input.size()) {
            const unsigned char token = static_cast<unsigned char>(m_input.at(m_offset++));
            if (escaped) {
                if (token == 'u') {
                    for (int index = 0; index < 4; ++index) {
                        if (m_offset >= m_input.size()
                            || !QByteArray("0123456789abcdefABCDEF").contains(m_input.at(m_offset++))) {
                            setError(errorMessage, QStringLiteral("annotated replay JSON has an invalid Unicode escape"));
                            return false;
                        }
                    }
                } else if (!QByteArray("\"\\/bfnrt").contains(static_cast<char>(token))) {
                    setError(errorMessage, QStringLiteral("annotated replay JSON has an invalid escape"));
                    return false;
                }
                escaped = false;
                continue;
            }
            if (token == '\\') {
                escaped = true;
            } else if (token == '"') {
                *raw = m_input.mid(begin, m_offset - begin);
                return true;
            } else if (token < 0x20) {
                setError(errorMessage, QStringLiteral("annotated replay JSON string contains a control byte"));
                return false;
            }
        }
        setError(errorMessage, QStringLiteral("annotated replay JSON string is unterminated"));
        return false;
    }

    bool parseLiteral(const char *literal, QString *errorMessage)
    {
        const QByteArray expected(literal);
        if (m_input.mid(m_offset, expected.size()) != expected) {
            setError(errorMessage, QStringLiteral("annotated replay JSON contains an invalid literal"));
            return false;
        }
        m_offset += expected.size();
        return true;
    }

    bool parseNumber(QString *errorMessage)
    {
        const qsizetype begin = m_offset;
        while (m_offset < m_input.size()
               && QByteArray("-+0123456789.eE").contains(m_input.at(m_offset))) {
            ++m_offset;
        }
        if (begin == m_offset) {
            setError(errorMessage, QStringLiteral("annotated replay JSON contains an invalid token"));
            return false;
        }
        return true;
    }

    const QByteArray &m_input;
    qsizetype m_offset = 0;
};

bool exactKeys(
    const QJsonObject &object,
    std::initializer_list<const char *> expected,
    const QString &context,
    QString *errorMessage)
{
    QSet<QString> expectedKeys;
    for (const char *key : expected) {
        expectedKeys.insert(QString::fromLatin1(key));
    }
    QSet<QString> actualKeys;
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        actualKeys.insert(iterator.key());
    }
    if (actualKeys == expectedKeys) {
        return true;
    }
    QStringList missing((expectedKeys - actualKeys).values());
    QStringList unknown((actualKeys - expectedKeys).values());
    missing.sort();
    unknown.sort();
    setError(
        errorMessage,
        QStringLiteral("%1 fields differ (missing: %2; unknown: %3)")
            .arg(context, missing.join(QStringLiteral(",")), unknown.join(QStringLiteral(","))));
    return false;
}

bool requiredObject(
    const QJsonObject &parent,
    const QString &key,
    QJsonObject *output,
    const QString &context,
    QString *errorMessage)
{
    const QJsonValue value = parent.value(key);
    if (!value.isObject()) {
        setError(errorMessage, QStringLiteral("%1.%2 must be an object").arg(context, key));
        return false;
    }
    *output = value.toObject();
    return true;
}

bool requiredArray(
    const QJsonObject &parent,
    const QString &key,
    QJsonArray *output,
    const QString &context,
    QString *errorMessage)
{
    const QJsonValue value = parent.value(key);
    if (!value.isArray()) {
        setError(errorMessage, QStringLiteral("%1.%2 must be an array").arg(context, key));
        return false;
    }
    *output = value.toArray();
    return true;
}

bool boundedStringValue(
    const QJsonValue &value,
    QString *output,
    const QString &context,
    qsizetype maximumBytes,
    QString *errorMessage,
    bool allowEmpty = false)
{
    if (!value.isString()) {
        setError(errorMessage, QStringLiteral("%1 must be text").arg(context));
        return false;
    }
    const QString text = value.toString();
    if ((!allowEmpty && text.isEmpty()) || text.toUtf8().size() > maximumBytes
        || text.contains(QChar::Null)) {
        setError(errorMessage, QStringLiteral("%1 is empty or exceeds its text bound").arg(context));
        return false;
    }
    *output = text;
    return true;
}

bool requiredString(
    const QJsonObject &object,
    const QString &key,
    QString *output,
    const QString &context,
    QString *errorMessage,
    qsizetype maximumBytes = 256,
    bool allowEmpty = false)
{
    return boundedStringValue(
        object.value(key), output, context + QLatin1Char('.') + key, maximumBytes,
        errorMessage, allowEmpty);
}

bool nullableString(
    const QJsonObject &object,
    const QString &key,
    std::optional<QString> *output,
    const QString &context,
    QString *errorMessage,
    qsizetype maximumBytes = 256)
{
    const QJsonValue value = object.value(key);
    if (value.isNull()) {
        output->reset();
        return true;
    }
    QString text;
    if (!boundedStringValue(
            value, &text, context + QLatin1Char('.') + key, maximumBytes,
            errorMessage)) {
        return false;
    }
    *output = text;
    return true;
}

bool requiredBool(
    const QJsonObject &object,
    const QString &key,
    bool *output,
    const QString &context,
    QString *errorMessage)
{
    const QJsonValue value = object.value(key);
    if (!value.isBool()) {
        setError(errorMessage, QStringLiteral("%1.%2 must be boolean").arg(context, key));
        return false;
    }
    *output = value.toBool();
    return true;
}

bool exactIntegerValue(
    const QJsonValue &value,
    qint64 minimum,
    qint64 maximum,
    qint64 *output,
    const QString &context,
    QString *errorMessage)
{
    if (!value.isDouble()) {
        setError(errorMessage, QStringLiteral("%1 must be an exact integer").arg(context));
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < static_cast<double>(minimum) || number > static_cast<double>(maximum)
        || std::abs(number) > static_cast<double>(kMaximumExactJsonInteger)) {
        setError(errorMessage, QStringLiteral("%1 is outside its exact integer bound").arg(context));
        return false;
    }
    *output = static_cast<qint64>(number);
    return true;
}

bool requiredInteger(
    const QJsonObject &object,
    const QString &key,
    qint64 minimum,
    qint64 maximum,
    qint64 *output,
    const QString &context,
    QString *errorMessage)
{
    return exactIntegerValue(
        object.value(key), minimum, maximum, output,
        context + QLatin1Char('.') + key, errorMessage);
}

bool nullableInteger(
    const QJsonObject &object,
    const QString &key,
    qint64 minimum,
    qint64 maximum,
    std::optional<qint64> *output,
    const QString &context,
    QString *errorMessage)
{
    const QJsonValue value = object.value(key);
    if (value.isNull()) {
        output->reset();
        return true;
    }
    qint64 number = 0;
    if (!exactIntegerValue(
            value, minimum, maximum, &number,
            context + QLatin1Char('.') + key, errorMessage)) {
        return false;
    }
    *output = number;
    return true;
}

bool stringArray(
    const QJsonValue &value,
    QStringList *output,
    int maximumCount,
    const QString &context,
    QString *errorMessage,
    bool allowEmpty = true)
{
    if (!value.isArray()) {
        setError(errorMessage, QStringLiteral("%1 must be an array").arg(context));
        return false;
    }
    const QJsonArray array = value.toArray();
    if ((!allowEmpty && array.isEmpty()) || array.size() > maximumCount) {
        setError(errorMessage, QStringLiteral("%1 exceeds its row bound").arg(context));
        return false;
    }
    QStringList values;
    values.reserve(array.size());
    for (int index = 0; index < array.size(); ++index) {
        QString item;
        if (!boundedStringValue(
                array.at(index), &item,
                QStringLiteral("%1[%2]").arg(context).arg(index), 256,
                errorMessage)) {
            return false;
        }
        values.append(item);
    }
    *output = values;
    return true;
}

bool sortedUnique(const QStringList &values)
{
    for (int index = 1; index < values.size(); ++index) {
        if (values.at(index - 1) >= values.at(index)) {
            return false;
        }
    }
    return true;
}

QString pieceName(PieceType type)
{
    switch (type) {
    case PieceType::Pawn:
        return QStringLiteral("pawn");
    case PieceType::Knight:
        return QStringLiteral("knight");
    case PieceType::Bishop:
        return QStringLiteral("bishop");
    case PieceType::Rook:
        return QStringLiteral("rook");
    case PieceType::Queen:
        return QStringLiteral("queen");
    case PieceType::King:
        return QStringLiteral("king");
    case PieceType::None:
        break;
    }
    return QString();
}

bool validatePositionBasics(const ChessPosition &position, const QString &fen, QString *errorMessage)
{
    int whiteKings = 0;
    int blackKings = 0;
    for (int square = 0; square < 64; ++square) {
        const Piece piece = position.pieceAt(square);
        if (piece.type == PieceType::King && piece.color == PieceColor::White) {
            ++whiteKings;
        } else if (piece.type == PieceType::King && piece.color == PieceColor::Black) {
            ++blackKings;
        }
    }
    if (whiteKings != 1 || blackKings != 1) {
        setError(errorMessage, QStringLiteral("replay FEN must contain exactly one king per side"));
        return false;
    }
    const QStringList fields = fen.split(QLatin1Char(' '));
    if (fields.size() != 6) {
        setError(errorMessage, QStringLiteral("replay FEN is not canonical"));
        return false;
    }
    const QString rights = fields.at(2);
    const auto hasRequiredPieces = [&](QChar right, int kingSquare, int rookSquare, PieceColor color) {
        if (!rights.contains(right)) {
            return true;
        }
        const Piece king = position.pieceAt(kingSquare);
        const Piece rook = position.pieceAt(rookSquare);
        return king.type == PieceType::King && king.color == color
            && rook.type == PieceType::Rook && rook.color == color;
    };
    if (!hasRequiredPieces(QLatin1Char('K'), ChessPosition::squareFromName(QStringLiteral("e1")), ChessPosition::squareFromName(QStringLiteral("h1")), PieceColor::White)
        || !hasRequiredPieces(QLatin1Char('Q'), ChessPosition::squareFromName(QStringLiteral("e1")), ChessPosition::squareFromName(QStringLiteral("a1")), PieceColor::White)
        || !hasRequiredPieces(QLatin1Char('k'), ChessPosition::squareFromName(QStringLiteral("e8")), ChessPosition::squareFromName(QStringLiteral("h8")), PieceColor::Black)
        || !hasRequiredPieces(QLatin1Char('q'), ChessPosition::squareFromName(QStringLiteral("e8")), ChessPosition::squareFromName(QStringLiteral("a8")), PieceColor::Black)) {
        setError(errorMessage, QStringLiteral("replay FEN castling right lacks its king or rook"));
        return false;
    }
    return true;
}

QString canonicalFen(const ChessPosition &position)
{
    QStringList fields = position.toFen().split(QLatin1Char(' '));
    if (fields.size() != 6 || fields.at(3) == QStringLiteral("-")) {
        return position.toFen();
    }
    const int target = ChessPosition::squareFromName(fields.at(3));
    bool legalCapture = false;
    for (const Move &move : position.legalMoves()) {
        const Piece piece = position.pieceAt(move.from);
        if (move.to == target && piece.type == PieceType::Pawn
            && ChessPosition::fileOf(move.from) != ChessPosition::fileOf(move.to)) {
            legalCapture = true;
            break;
        }
    }
    if (!legalCapture) {
        fields[3] = QStringLiteral("-");
    }
    return fields.join(QLatin1Char(' '));
}

QString canonicalFenAfterMove(
    const ChessPosition &before,
    const Move &move,
    const ChessPosition &after)
{
    QStringList fields = canonicalFen(after).split(QLatin1Char(' '));
    const QStringList beforeFields = canonicalFen(before).split(QLatin1Char(' '));
    if (fields.size() != 6 || beforeFields.size() != 6) {
        return canonicalFen(after);
    }
    const Piece moving = before.pieceAt(move.from);
    const Piece captured = before.pieceAt(move.to);
    const bool enPassantCapture = moving.type == PieceType::Pawn && captured.isEmpty()
        && ChessPosition::fileOf(move.from) != ChessPosition::fileOf(move.to);
    bool ok = false;
    const int previousHalfmove = beforeFields.at(4).toInt(&ok);
    if (!ok) {
        return canonicalFen(after);
    }
    fields[4] = QString::number(
        moving.type == PieceType::Pawn || !captured.isEmpty() || enPassantCapture
            ? 0 : previousHalfmove + 1);
    return fields.join(QLatin1Char(' '));
}

std::optional<ChessPosition> exactPositionFromFen(const QString &fen, QString *errorMessage)
{
    QString fenError;
    const auto position = ChessPosition::fromFen(fen, &fenError);
    if (!position.has_value()) {
        setError(errorMessage, QStringLiteral("invalid replay FEN: %1").arg(fenError));
        return std::nullopt;
    }
    if (!validatePositionBasics(*position, fen, errorMessage)) {
        return std::nullopt;
    }
    const QStringList fields = fen.split(QLatin1Char(' '));
    bool halfmoveOk = false;
    bool fullmoveOk = false;
    const int halfmove = fields.value(4).toInt(&halfmoveOk);
    const int fullmove = fields.value(5).toInt(&fullmoveOk);
    if (!halfmoveOk || !fullmoveOk || halfmove < 0 || halfmove > 700
        || fullmove < 1 || fullmove > 351) {
        setError(errorMessage, QStringLiteral("replay FEN counters exceed the game bound"));
        return std::nullopt;
    }
    if (canonicalFen(*position) != fen) {
        setError(errorMessage, QStringLiteral("replay FEN is not the exact canonical board state"));
        return std::nullopt;
    }
    return position;
}

QString sanForMove(const ChessPosition &before, const Move &move, const ChessPosition &after)
{
    const Piece moving = before.pieceAt(move.from);
    const Piece targetPiece = before.pieceAt(move.to);
    const bool enPassant = moving.type == PieceType::Pawn && targetPiece.isEmpty()
        && ChessPosition::fileOf(move.from) != ChessPosition::fileOf(move.to);
    const bool capture = !targetPiece.isEmpty() || enPassant;
    const QString origin = ChessPosition::squareName(move.from);
    const QString target = ChessPosition::squareName(move.to);
    QString san;
    if (moving.type == PieceType::King
        && std::abs(ChessPosition::fileOf(move.from) - ChessPosition::fileOf(move.to)) == 2) {
        san = ChessPosition::fileOf(move.to) == 6
            ? QStringLiteral("O-O") : QStringLiteral("O-O-O");
    } else if (moving.type == PieceType::Pawn) {
        if (capture) {
            san.append(origin.left(1));
            san.append(QLatin1Char('x'));
        }
        san.append(target);
        if (move.promotion != PieceType::None) {
            const QString promoted = pieceName(move.promotion);
            const QChar token = promoted == QStringLiteral("knight")
                ? QLatin1Char('N') : promoted.left(1).toUpper().at(0);
            san.append(QLatin1Char('='));
            san.append(token);
        }
    } else {
        const QString type = pieceName(moving.type);
        const QChar token = type == QStringLiteral("knight")
            ? QLatin1Char('N') : type.left(1).toUpper().at(0);
        san.append(token);
        QVector<int> alternatives;
        for (const Move &candidate : before.legalMoves()) {
            if (candidate.from != move.from && candidate.to == move.to
                && before.pieceAt(candidate.from).type == moving.type) {
                alternatives.append(candidate.from);
            }
        }
        if (!alternatives.isEmpty()) {
            const bool sharesFile = std::any_of(
                alternatives.cbegin(), alternatives.cend(), [&](int square) {
                    return ChessPosition::fileOf(square) == ChessPosition::fileOf(move.from);
                });
            const bool sharesRank = std::any_of(
                alternatives.cbegin(), alternatives.cend(), [&](int square) {
                    return ChessPosition::rankOf(square) == ChessPosition::rankOf(move.from);
                });
            if (!sharesFile) {
                san.append(origin.left(1));
            } else if (!sharesRank) {
                san.append(origin.mid(1, 1));
            } else {
                san.append(origin);
            }
        }
        if (capture) {
            san.append(QLatin1Char('x'));
        }
        san.append(target);
    }
    const bool check = after.isInCheck(after.sideToMove());
    if (check && after.legalMoves().isEmpty()) {
        san.append(QLatin1Char('#'));
    } else if (check) {
        san.append(QLatin1Char('+'));
    }
    return san;
}

bool semanticId(const QString &value)
{
    return value.toUtf8().size() <= 256 && kSemanticIdPattern.match(value).hasMatch();
}

bool validateId(const QString &value, const QString &context, QString *errorMessage)
{
    if (!semanticId(value)) {
        setError(errorMessage, QStringLiteral("%1 is not a semantic identity").arg(context));
        return false;
    }
    return true;
}

bool validateMotifs(
    const QJsonValue &value,
    const QString &context,
    QString *errorMessage)
{
    if (!value.isArray() || value.toArray().size() > 16) {
        setError(errorMessage, QStringLiteral("%1 must be a bounded motif array").arg(context));
        return false;
    }
    QSet<QString> motifIds;
    const QJsonArray motifs = value.toArray();
    for (int index = 0; index < motifs.size(); ++index) {
        if (!motifs.at(index).isObject()) {
            setError(errorMessage, QStringLiteral("%1[%2] must be an object").arg(context).arg(index));
            return false;
        }
        const QJsonObject motif = motifs.at(index).toObject();
        const QString itemContext = QStringLiteral("%1[%2]").arg(context).arg(index);
        if (!exactKeys(
                motif,
                {"attacker_piece", "attacker_square", "authority", "kind", "motif_id", "targets", "vocabulary_version"},
                itemContext, errorMessage)) {
            return false;
        }
        QString motifId;
        QString attackerPiece;
        QString attackerSquare;
        QString authority;
        QString kind;
        QString vocabularyVersion;
        if (!requiredString(motif, QStringLiteral("motif_id"), &motifId, itemContext, errorMessage)
            || !requiredString(motif, QStringLiteral("attacker_piece"), &attackerPiece, itemContext, errorMessage, 16)
            || !requiredString(motif, QStringLiteral("attacker_square"), &attackerSquare, itemContext, errorMessage, 2)
            || !requiredString(motif, QStringLiteral("authority"), &authority, itemContext, errorMessage, 32)
            || !requiredString(motif, QStringLiteral("kind"), &kind, itemContext, errorMessage, 32)
            || !requiredString(motif, QStringLiteral("vocabulary_version"), &vocabularyVersion, itemContext, errorMessage, 64)
            || !validateId(motifId, itemContext + QStringLiteral(".motif_id"), errorMessage)
            || !kSquarePattern.match(attackerSquare).hasMatch()
            || authority != QStringLiteral("mechanical_geometry")
            || (kind != QStringLiteral("geometric_fork") && kind != QStringLiteral("absolute_pin"))) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = itemContext + QStringLiteral(" has invalid motif metadata");
            }
            return false;
        }
        if (motifIds.contains(motifId)) {
            setError(errorMessage, itemContext + QStringLiteral(" repeats a motif identity"));
            return false;
        }
        motifIds.insert(motifId);
        QJsonArray targets;
        if (!requiredArray(motif, QStringLiteral("targets"), &targets, itemContext, errorMessage)
            || targets.size() < 2 || targets.size() > 16
            || (kind == QStringLiteral("absolute_pin") && targets.size() != 2)) {
            setError(errorMessage, itemContext + QStringLiteral(" has invalid motif targets"));
            return false;
        }
        QSet<QString> targetSquares;
        for (int targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
            if (!targets.at(targetIndex).isObject()) {
                setError(errorMessage, itemContext + QStringLiteral(" contains a non-object target"));
                return false;
            }
            const QJsonObject target = targets.at(targetIndex).toObject();
            if (!exactKeys(target, {"piece", "square"}, itemContext + QStringLiteral(".target"), errorMessage)) {
                return false;
            }
            QString piece;
            QString square;
            if (!requiredString(target, QStringLiteral("piece"), &piece, itemContext, errorMessage, 16)
                || !requiredString(target, QStringLiteral("square"), &square, itemContext, errorMessage, 2)
                || !kSquarePattern.match(square).hasMatch() || targetSquares.contains(square)) {
                setError(errorMessage, itemContext + QStringLiteral(" has an invalid or duplicate motif target"));
                return false;
            }
            targetSquares.insert(square);
        }
    }
    return true;
}

bool parseNotation(
    const QJsonObject &object,
    ReplayNotation *output,
    const QString &context,
    QString *errorMessage)
{
    if (!exactKeys(
            object,
            {"after_fen", "after_position_id", "after_replay_state_id", "before_fen", "before_position_id", "before_replay_state_id", "capture", "captured_piece", "castling", "check", "checkmate", "en_passant", "match_id", "motifs", "move_number", "mover", "notation_id", "notation_version", "origin", "piece", "ply", "promotion_piece", "san", "target", "uci"},
            context, errorMessage)) {
        return false;
    }
    qint64 ply = 0;
    qint64 moveNumber = 0;
    if (!requiredString(object, QStringLiteral("notation_id"), &output->notationId, context, errorMessage)
        || !requiredString(object, QStringLiteral("notation_version"), &output->notationVersion, context, errorMessage, 64)
        || !requiredString(object, QStringLiteral("match_id"), &output->matchId, context, errorMessage)
        || !requiredInteger(object, QStringLiteral("ply"), 1, kMaximumReplayPlies, &ply, context, errorMessage)
        || !requiredInteger(object, QStringLiteral("move_number"), 1, 351, &moveNumber, context, errorMessage)
        || !requiredString(object, QStringLiteral("mover"), &output->mover, context, errorMessage, 5)
        || !requiredString(object, QStringLiteral("uci"), &output->uci, context, errorMessage, 5)
        || !requiredString(object, QStringLiteral("san"), &output->san, context, errorMessage, 20)
        || !requiredString(object, QStringLiteral("piece"), &output->piece, context, errorMessage, 16)
        || !requiredString(object, QStringLiteral("origin"), &output->origin, context, errorMessage, 2)
        || !requiredString(object, QStringLiteral("target"), &output->target, context, errorMessage, 2)
        || !nullableString(object, QStringLiteral("captured_piece"), &output->capturedPiece, context, errorMessage, 16)
        || !requiredBool(object, QStringLiteral("capture"), &output->capture, context, errorMessage)
        || !requiredBool(object, QStringLiteral("en_passant"), &output->enPassant, context, errorMessage)
        || !requiredBool(object, QStringLiteral("check"), &output->check, context, errorMessage)
        || !requiredBool(object, QStringLiteral("checkmate"), &output->checkmate, context, errorMessage)
        || !nullableString(object, QStringLiteral("castling"), &output->castling, context, errorMessage, 16)
        || !nullableString(object, QStringLiteral("promotion_piece"), &output->promotionPiece, context, errorMessage, 16)
        || !requiredString(object, QStringLiteral("before_fen"), &output->beforeFen, context, errorMessage, 160)
        || !requiredString(object, QStringLiteral("after_fen"), &output->afterFen, context, errorMessage, 160)
        || !requiredString(object, QStringLiteral("before_position_id"), &output->beforePositionId, context, errorMessage)
        || !requiredString(object, QStringLiteral("after_position_id"), &output->afterPositionId, context, errorMessage)
        || !requiredString(object, QStringLiteral("before_replay_state_id"), &output->beforeReplayStateId, context, errorMessage)
        || !requiredString(object, QStringLiteral("after_replay_state_id"), &output->afterReplayStateId, context, errorMessage)
        || !validateMotifs(object.value(QStringLiteral("motifs")), context + QStringLiteral(".motifs"), errorMessage)) {
        return false;
    }
    output->ply = static_cast<int>(ply);
    output->moveNumber = static_cast<int>(moveNumber);
    if (!validateId(output->notationId, context + QStringLiteral(".notation_id"), errorMessage)
        || !validateId(output->beforePositionId, context + QStringLiteral(".before_position_id"), errorMessage)
        || !validateId(output->afterPositionId, context + QStringLiteral(".after_position_id"), errorMessage)
        || !validateId(output->beforeReplayStateId, context + QStringLiteral(".before_replay_state_id"), errorMessage)
        || !validateId(output->afterReplayStateId, context + QStringLiteral(".after_replay_state_id"), errorMessage)
        || output->notationVersion != QStringLiteral("strict-uci-to-san-v1")
        || !kUciPattern.match(output->uci).hasMatch()
        || !kSquarePattern.match(output->origin).hasMatch()
        || !kSquarePattern.match(output->target).hasMatch()
        || output->origin != output->uci.left(2)
        || output->target != output->uci.mid(2, 2)
        || output->moveNumber != (output->ply + 1) / 2
        || (output->mover != QStringLiteral("white") && output->mover != QStringLiteral("black"))) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = context + QStringLiteral(" contains inconsistent notation metadata");
        }
        return false;
    }
    return true;
}

bool parseFact(
    const QJsonObject &object,
    ReplayFact *output,
    const QString &context,
    QString *errorMessage)
{
    if (!exactKeys(object, {"authority", "details", "fact_id", "kind", "status", "summary", "supporting_ids"}, context, errorMessage)
        || !requiredString(object, QStringLiteral("fact_id"), &output->factId, context, errorMessage)
        || !requiredString(object, QStringLiteral("authority"), &output->authority, context, errorMessage, 32)
        || !requiredString(object, QStringLiteral("kind"), &output->kind, context, errorMessage, 96)
        || !requiredString(object, QStringLiteral("status"), &output->status, context, errorMessage, 32)
        || !requiredString(object, QStringLiteral("summary"), &output->summary, context, errorMessage, 1024)
        || !stringArray(object.value(QStringLiteral("supporting_ids")), &output->supportingIds, 32, context + QStringLiteral(".supporting_ids"), errorMessage)) {
        return false;
    }
    if (!validateId(output->factId, context + QStringLiteral(".fact_id"), errorMessage)
        || !sortedUnique(output->supportingIds)
        || !object.value(QStringLiteral("details")).isObject()) {
        setError(errorMessage, context + QStringLiteral(" has invalid fact identity or details"));
        return false;
    }
    const QJsonObject details = object.value(QStringLiteral("details")).toObject();
    if (details.size() > 32) {
        setError(errorMessage, context + QStringLiteral(" exceeds the fact-detail bound"));
        return false;
    }
    qsizetype detailBytes = 0;
    for (auto iterator = details.constBegin(); iterator != details.constEnd(); ++iterator) {
        const qsizetype keyBytes = iterator.key().toUtf8().size();
        if (iterator.key().isEmpty() || iterator.key().contains(QChar::Null)
            || keyBytes > kMaximumFactDetailKeyBytes) {
            setError(errorMessage, context + QStringLiteral(" contains an invalid fact-detail key"));
            return false;
        }
        QString value;
        if (!boundedStringValue(iterator.value(), &value, context + QStringLiteral(".details.") + iterator.key(), 512, errorMessage, true)) {
            return false;
        }
        detailBytes += keyBytes + value.toUtf8().size();
        if (detailBytes > kMaximumFactDetailsBytes) {
            setError(errorMessage, context + QStringLiteral(" exceeds the aggregate fact-detail byte bound"));
            return false;
        }
        output->details.append(qMakePair(iterator.key(), value));
    }
    return true;
}

bool parseNarration(
    const QJsonObject &object,
    ReplayNarration *output,
    const QString &context,
    QString *errorMessage)
{
    return exactKeys(object, {"narration_id", "supporting_fact_ids", "template_id", "text"}, context, errorMessage)
        && requiredString(object, QStringLiteral("narration_id"), &output->narrationId, context, errorMessage)
        && requiredString(object, QStringLiteral("template_id"), &output->templateId, context, errorMessage, 96)
        && requiredString(object, QStringLiteral("text"), &output->text, context, errorMessage, 1024)
        && stringArray(object.value(QStringLiteral("supporting_fact_ids")), &output->supportingFactIds, 32, context + QStringLiteral(".supporting_fact_ids"), errorMessage, false)
        && validateId(output->narrationId, context + QStringLiteral(".narration_id"), errorMessage)
        && sortedUnique(output->supportingFactIds);
}

bool parseVariationStep(
    const QJsonObject &object,
    ReplayVariationStep *output,
    const QString &context,
    QString *errorMessage)
{
    if (!exactKeys(
            object,
            {"after_fen", "after_position_id", "after_replay_state_id", "before_fen", "before_position_id", "before_replay_state_id", "local_ply", "mechanical_fact_ids", "notation_id", "san", "step_id", "uci", "variation_context_id"},
            context, errorMessage)) {
        return false;
    }
    qint64 localPly = 0;
    if (!requiredString(object, QStringLiteral("step_id"), &output->stepId, context, errorMessage)
        || !requiredString(object, QStringLiteral("variation_context_id"), &output->variationContextId, context, errorMessage)
        || !requiredInteger(object, QStringLiteral("local_ply"), 1, kMaximumVariationPlies, &localPly, context, errorMessage)
        || !requiredString(object, QStringLiteral("uci"), &output->uci, context, errorMessage, 5)
        || !requiredString(object, QStringLiteral("san"), &output->san, context, errorMessage, 20)
        || !requiredString(object, QStringLiteral("notation_id"), &output->notationId, context, errorMessage)
        || !requiredString(object, QStringLiteral("before_fen"), &output->beforeFen, context, errorMessage, 160)
        || !requiredString(object, QStringLiteral("after_fen"), &output->afterFen, context, errorMessage, 160)
        || !requiredString(object, QStringLiteral("before_position_id"), &output->beforePositionId, context, errorMessage)
        || !requiredString(object, QStringLiteral("after_position_id"), &output->afterPositionId, context, errorMessage)
        || !requiredString(object, QStringLiteral("before_replay_state_id"), &output->beforeReplayStateId, context, errorMessage)
        || !requiredString(object, QStringLiteral("after_replay_state_id"), &output->afterReplayStateId, context, errorMessage)
        || !stringArray(object.value(QStringLiteral("mechanical_fact_ids")), &output->mechanicalFactIds, 32, context + QStringLiteral(".mechanical_fact_ids"), errorMessage)) {
        return false;
    }
    output->localPly = static_cast<int>(localPly);
    if (!validateId(output->stepId, context + QStringLiteral(".step_id"), errorMessage)
        || !validateId(output->variationContextId, context + QStringLiteral(".variation_context_id"), errorMessage)
        || !validateId(output->notationId, context + QStringLiteral(".notation_id"), errorMessage)
        || !validateId(output->beforePositionId, context + QStringLiteral(".before_position_id"), errorMessage)
        || !validateId(output->afterPositionId, context + QStringLiteral(".after_position_id"), errorMessage)
        || !validateId(output->beforeReplayStateId, context + QStringLiteral(".before_replay_state_id"), errorMessage)
        || !validateId(output->afterReplayStateId, context + QStringLiteral(".after_replay_state_id"), errorMessage)
        || !kUciPattern.match(output->uci).hasMatch()
        || !sortedUnique(output->mechanicalFactIds)) {
        return false;
    }
    return true;
}

bool parseVariation(
    const QJsonObject &object,
    ReplayPreferredVariation *output,
    const QString &context,
    QString *errorMessage)
{
    if (!exactKeys(
            object,
            {"anchor_ply", "checkpoint_fen", "checkpoint_position_id", "checkpoint_replay_state_id", "displayed_steps", "engine_config_id", "presentation_label", "reported_best_move_uci", "reported_pv_move_count", "reported_pv_sha256", "restore_after_fen", "restore_after_position_id", "restore_after_replay_state_id", "restore_played_uci", "root_centipawns_white", "root_depth", "root_mate_for_white", "root_nodes", "root_position_fact_id", "root_score_kind", "root_selective_depth", "root_wdl_white", "source_game_id", "source_run_id", "variation_context_id", "variation_id"},
            context, errorMessage)) {
        return false;
    }
    qint64 anchorPly = 0;
    qint64 reportedCount = 0;
    if (!requiredString(object, QStringLiteral("variation_id"), &output->variationId, context, errorMessage)
        || !requiredString(object, QStringLiteral("variation_context_id"), &output->variationContextId, context, errorMessage)
        || !requiredString(object, QStringLiteral("source_game_id"), &output->sourceGameId, context, errorMessage)
        || !requiredString(object, QStringLiteral("source_run_id"), &output->sourceRunId, context, errorMessage)
        || !requiredString(object, QStringLiteral("engine_config_id"), &output->engineConfigId, context, errorMessage)
        || !requiredInteger(object, QStringLiteral("anchor_ply"), 1, kMaximumReplayPlies, &anchorPly, context, errorMessage)
        || !requiredString(object, QStringLiteral("checkpoint_fen"), &output->checkpointFen, context, errorMessage, 160)
        || !requiredString(object, QStringLiteral("checkpoint_position_id"), &output->checkpointPositionId, context, errorMessage)
        || !requiredString(object, QStringLiteral("checkpoint_replay_state_id"), &output->checkpointReplayStateId, context, errorMessage)
        || !requiredString(object, QStringLiteral("reported_best_move_uci"), &output->reportedBestMoveUci, context, errorMessage, 5)
        || !requiredString(object, QStringLiteral("reported_pv_sha256"), &output->reportedPvSha256, context, errorMessage, 64)
        || !requiredInteger(object, QStringLiteral("reported_pv_move_count"), 2, kMaximumVariationPlies, &reportedCount, context, errorMessage)
        || !requiredString(object, QStringLiteral("root_position_fact_id"), &output->rootPositionFactId, context, errorMessage)
        || !requiredString(object, QStringLiteral("root_score_kind"), &output->rootScoreKind, context, errorMessage, 8)
        || !nullableInteger(object, QStringLiteral("root_centipawns_white"), -kMaximumExactJsonInteger, kMaximumExactJsonInteger, &output->rootCentipawnsWhite, context, errorMessage)
        || !nullableInteger(object, QStringLiteral("root_mate_for_white"), -kMaximumExactJsonInteger, kMaximumExactJsonInteger, &output->rootMateForWhite, context, errorMessage)
        || !requiredInteger(object, QStringLiteral("root_depth"), 1, kMaximumExactJsonInteger, &output->rootDepth, context, errorMessage)
        || !requiredInteger(object, QStringLiteral("root_selective_depth"), 1, kMaximumExactJsonInteger, &output->rootSelectiveDepth, context, errorMessage)
        || !requiredInteger(object, QStringLiteral("root_nodes"), 1, kMaximumExactJsonInteger, &output->rootNodes, context, errorMessage)
        || !requiredString(object, QStringLiteral("restore_played_uci"), &output->restorePlayedUci, context, errorMessage, 5)
        || !requiredString(object, QStringLiteral("restore_after_fen"), &output->restoreAfterFen, context, errorMessage, 160)
        || !requiredString(object, QStringLiteral("restore_after_position_id"), &output->restoreAfterPositionId, context, errorMessage)
        || !requiredString(object, QStringLiteral("restore_after_replay_state_id"), &output->restoreAfterReplayStateId, context, errorMessage)
        || !requiredString(object, QStringLiteral("presentation_label"), &output->presentationLabel, context, errorMessage, 64)) {
        return false;
    }
    output->anchorPly = static_cast<int>(anchorPly);
    output->reportedPvMoveCount = static_cast<int>(reportedCount);
    if (!validateId(output->variationId, context + QStringLiteral(".variation_id"), errorMessage)
        || !validateId(output->variationContextId, context + QStringLiteral(".variation_context_id"), errorMessage)
        || !validateId(output->sourceRunId, context + QStringLiteral(".source_run_id"), errorMessage)
        || !validateId(output->engineConfigId, context + QStringLiteral(".engine_config_id"), errorMessage)
        || !validateId(output->checkpointPositionId, context + QStringLiteral(".checkpoint_position_id"), errorMessage)
        || !validateId(output->checkpointReplayStateId, context + QStringLiteral(".checkpoint_replay_state_id"), errorMessage)
        || !validateId(output->rootPositionFactId, context + QStringLiteral(".root_position_fact_id"), errorMessage)
        || !validateId(output->restoreAfterPositionId, context + QStringLiteral(".restore_after_position_id"), errorMessage)
        || !validateId(output->restoreAfterReplayStateId, context + QStringLiteral(".restore_after_replay_state_id"), errorMessage)
        || !kUciPattern.match(output->reportedBestMoveUci).hasMatch()
        || !kUciPattern.match(output->restorePlayedUci).hasMatch()
        || !kShaPattern.match(output->reportedPvSha256).hasMatch()
        || output->presentationLabel != QStringLiteral("engine line, not played")
        || (output->rootScoreKind != QStringLiteral("cp") && output->rootScoreKind != QStringLiteral("mate"))
        || (output->rootScoreKind == QStringLiteral("cp")
            && (!output->rootCentipawnsWhite.has_value() || output->rootMateForWhite.has_value()))
        || (output->rootScoreKind == QStringLiteral("mate")
            && (output->rootCentipawnsWhite.has_value() || !output->rootMateForWhite.has_value()
                || *output->rootMateForWhite == 0))) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = context + QStringLiteral(" contains invalid variation metadata");
        }
        return false;
    }
    QJsonArray wdl;
    if (!requiredArray(object, QStringLiteral("root_wdl_white"), &wdl, context, errorMessage)
        || wdl.size() != 3) {
        setError(errorMessage, context + QStringLiteral(".root_wdl_white must contain three values"));
        return false;
    }
    int wdlSum = 0;
    for (int index = 0; index < wdl.size(); ++index) {
        qint64 value = 0;
        if (!exactIntegerValue(wdl.at(index), 0, 1000, &value, context + QStringLiteral(".root_wdl_white"), errorMessage)) {
            return false;
        }
        output->rootWdlWhite.append(static_cast<int>(value));
        wdlSum += static_cast<int>(value);
    }
    if (wdlSum != 1000) {
        setError(errorMessage, context + QStringLiteral(".root_wdl_white must sum to 1000"));
        return false;
    }
    QJsonArray steps;
    if (!requiredArray(object, QStringLiteral("displayed_steps"), &steps, context, errorMessage)
        || steps.size() < 2 || steps.size() > output->reportedPvMoveCount) {
        setError(errorMessage, context + QStringLiteral(".displayed_steps violates its bound"));
        return false;
    }
    output->displayedSteps.reserve(steps.size());
    for (int index = 0; index < steps.size(); ++index) {
        if (!steps.at(index).isObject()) {
            setError(errorMessage, context + QStringLiteral(".displayed_steps contains a non-object"));
            return false;
        }
        ReplayVariationStep step;
        if (!parseVariationStep(
                steps.at(index).toObject(), &step,
                QStringLiteral("%1.displayed_steps[%2]").arg(context).arg(index),
                errorMessage)
            || step.localPly != index + 1
            || step.variationContextId != output->variationContextId) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = context + QStringLiteral(" has a misaligned variation step");
            }
            return false;
        }
        output->displayedSteps.append(step);
    }
    if (output->displayedSteps.first().uci != output->reportedBestMoveUci) {
        setError(errorMessage, context + QStringLiteral(" first displayed move differs from reported best move"));
        return false;
    }
    return true;
}

bool parseMove(
    const QJsonObject &object,
    ReplayMove *output,
    const QString &context,
    QString *errorMessage)
{
    if (!exactKeys(
            object,
            {"alternative_status", "alternative_unavailable_reason", "annotation_id", "centipawn_loss", "engine_preferred_alternative", "expected_after_millionths", "expected_before_millionths", "facts", "label_id", "missed_forced_mate", "missed_winning_advantage", "move_fact_id", "narration", "notation", "ply", "severity", "wdl_loss_millionths"},
            context, errorMessage)) {
        return false;
    }
    qint64 ply = 0;
    qint64 before = 0;
    qint64 after = 0;
    qint64 loss = 0;
    if (!requiredInteger(object, QStringLiteral("ply"), 1, kMaximumReplayPlies, &ply, context, errorMessage)
        || !requiredString(object, QStringLiteral("annotation_id"), &output->annotationId, context, errorMessage)
        || !requiredString(object, QStringLiteral("move_fact_id"), &output->moveFactId, context, errorMessage)
        || !requiredString(object, QStringLiteral("label_id"), &output->labelId, context, errorMessage)
        || !requiredString(object, QStringLiteral("severity"), &output->severity, context, errorMessage, 16)
        || !requiredInteger(object, QStringLiteral("expected_before_millionths"), 0, 1'000'000, &before, context, errorMessage)
        || !requiredInteger(object, QStringLiteral("expected_after_millionths"), 0, 1'000'000, &after, context, errorMessage)
        || !requiredInteger(object, QStringLiteral("wdl_loss_millionths"), 0, 1'000'000, &loss, context, errorMessage)
        || !nullableInteger(object, QStringLiteral("centipawn_loss"), 0, kMaximumExactJsonInteger, &output->centipawnLoss, context, errorMessage)
        || !requiredBool(object, QStringLiteral("missed_winning_advantage"), &output->missedWinningAdvantage, context, errorMessage)
        || !requiredBool(object, QStringLiteral("missed_forced_mate"), &output->missedForcedMate, context, errorMessage)
        || !requiredString(object, QStringLiteral("alternative_status"), &output->alternativeStatus, context, errorMessage, 32)
        || !nullableString(object, QStringLiteral("alternative_unavailable_reason"), &output->alternativeUnavailableReason, context, errorMessage, 128)) {
        return false;
    }
    output->ply = static_cast<int>(ply);
    output->expectedBeforeMillionths = static_cast<int>(before);
    output->expectedAfterMillionths = static_cast<int>(after);
    output->wdlLossMillionths = static_cast<int>(loss);
    const QSet<QString> severities {
        QStringLiteral("none"), QStringLiteral("inaccuracy"),
        QStringLiteral("mistake"), QStringLiteral("severe")};
    const QSet<QString> statuses {
        QStringLiteral("available"), QStringLiteral("not_eligible"),
        QStringLiteral("played_reported_best"), QStringLiteral("variation_limit_reached"),
        QStringLiteral("unavailable")};
    if (!validateId(output->annotationId, context + QStringLiteral(".annotation_id"), errorMessage)
        || !validateId(output->moveFactId, context + QStringLiteral(".move_fact_id"), errorMessage)
        || !validateId(output->labelId, context + QStringLiteral(".label_id"), errorMessage)
        || !severities.contains(output->severity) || !statuses.contains(output->alternativeStatus)
        || output->wdlLossMillionths
            != std::max(0, output->expectedBeforeMillionths - output->expectedAfterMillionths)) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = context + QStringLiteral(" contains inconsistent move facts");
        }
        return false;
    }
    QJsonObject notation;
    if (!requiredObject(object, QStringLiteral("notation"), &notation, context, errorMessage)
        || !parseNotation(notation, &output->notation, context + QStringLiteral(".notation"), errorMessage)
        || output->notation.ply != output->ply) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = context + QStringLiteral(" notation is misaligned");
        }
        return false;
    }
    QJsonArray facts;
    if (!requiredArray(object, QStringLiteral("facts"), &facts, context, errorMessage)
        || facts.size() < 2 || facts.size() > kMaximumFactsPerMove) {
        setError(errorMessage, context + QStringLiteral(".facts violates its bound"));
        return false;
    }
    QSet<QString> factIds;
    QString previousFactId;
    output->facts.reserve(facts.size());
    for (int index = 0; index < facts.size(); ++index) {
        if (!facts.at(index).isObject()) {
            setError(errorMessage, context + QStringLiteral(".facts contains a non-object"));
            return false;
        }
        ReplayFact fact;
        if (!parseFact(
                facts.at(index).toObject(), &fact,
                QStringLiteral("%1.facts[%2]").arg(context).arg(index), errorMessage)
            || factIds.contains(fact.factId)
            || (!previousFactId.isEmpty() && previousFactId >= fact.factId)) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = context + QStringLiteral(" facts are duplicate or non-canonical");
            }
            return false;
        }
        factIds.insert(fact.factId);
        previousFactId = fact.factId;
        output->facts.append(fact);
    }
    QJsonArray narration;
    if (!requiredArray(object, QStringLiteral("narration"), &narration, context, errorMessage)
        || narration.isEmpty() || narration.size() > kMaximumNarrationPerMove) {
        setError(errorMessage, context + QStringLiteral(".narration violates its bound"));
        return false;
    }
    qsizetype narrationBytes = 0;
    QSet<QString> narrationIds;
    output->narration.reserve(narration.size());
    for (int index = 0; index < narration.size(); ++index) {
        if (!narration.at(index).isObject()) {
            setError(errorMessage, context + QStringLiteral(".narration contains a non-object"));
            return false;
        }
        ReplayNarration line;
        if (!parseNarration(
                narration.at(index).toObject(), &line,
                QStringLiteral("%1.narration[%2]").arg(context).arg(index), errorMessage)
            || narrationIds.contains(line.narrationId)) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = context + QStringLiteral(" narration repeats an identity");
            }
            return false;
        }
        narrationIds.insert(line.narrationId);
        narrationBytes += line.text.toUtf8().size();
        for (const QString &factId : line.supportingFactIds) {
            if (!factIds.contains(factId)) {
                setError(errorMessage, context + QStringLiteral(" narration cites a fact outside the move"));
                return false;
            }
        }
        if (line.supportingFactIds.size() != 1) {
            setError(errorMessage, context + QStringLiteral(" deterministic narration must cite one fact"));
            return false;
        }
        const auto supported = std::find_if(
            output->facts.cbegin(), output->facts.cend(), [&](const ReplayFact &fact) {
                return fact.factId == line.supportingFactIds.first();
            });
        const QHash<QString, QString> expectedKinds {
            {QStringLiteral("absolute_pin-v1"), QStringLiteral("absolute_pin")},
            {QStringLiteral("engine-reported-alternative-v1"), QStringLiteral("engine_preferred_alternative")},
            {QStringLiteral("geometric_fork-v1"), QStringLiteral("geometric_fork")},
            {QStringLiteral("literal-move-v1"), QStringLiteral("literal_move")},
            {QStringLiteral("threshold-loss-v1"), QStringLiteral("move_loss")},
        };
        if (supported == output->facts.cend()
            || expectedKinds.value(line.templateId) != supported->kind
            || line.text != supported->summary) {
            setError(errorMessage, context + QStringLiteral(" deterministic narration differs from its cited fact"));
            return false;
        }
        output->narration.append(line);
    }
    if (narrationBytes > kMaximumNarrationPerMoveBytes) {
        setError(errorMessage, context + QStringLiteral(" narration exceeds its byte bound"));
        return false;
    }
    const QJsonValue alternative = object.value(QStringLiteral("engine_preferred_alternative"));
    if (output->alternativeStatus == QStringLiteral("available")) {
        if (!alternative.isObject() || output->alternativeUnavailableReason.has_value()) {
            setError(errorMessage, context + QStringLiteral(" available alternative is incomplete"));
            return false;
        }
        ReplayPreferredVariation variation;
        if (!parseVariation(
                alternative.toObject(), &variation,
                context + QStringLiteral(".engine_preferred_alternative"), errorMessage)) {
            return false;
        }
        output->preferredVariation = variation;
    } else if (!alternative.isNull()) {
        setError(errorMessage, context + QStringLiteral(" unavailable alternative carries a branch"));
        return false;
    } else if (output->alternativeStatus == QStringLiteral("unavailable")
               && !output->alternativeUnavailableReason.has_value()) {
        setError(errorMessage, context + QStringLiteral(" unavailable alternative lacks a reason"));
        return false;
    }
    return true;
}

std::optional<QString> capturedPieceForMove(const ChessPosition &before, const Move &move)
{
    Piece captured = before.pieceAt(move.to);
    const Piece moving = before.pieceAt(move.from);
    if (captured.isEmpty() && moving.type == PieceType::Pawn
        && ChessPosition::fileOf(move.from) != ChessPosition::fileOf(move.to)) {
        const int rank = ChessPosition::rankOf(move.to)
            + (moving.color == PieceColor::White ? -1 : 1);
        captured = before.pieceAt(
            ChessPosition::squareIndex(ChessPosition::fileOf(move.to), rank));
    }
    if (captured.isEmpty()) {
        return std::nullopt;
    }
    return pieceName(captured.type);
}

bool validateNotationAgainstPosition(
    const ReplayNotation &notation,
    const ChessPosition &before,
    ChessPosition *after,
    QString *errorMessage,
    const QString &context)
{
    if (canonicalFen(before) != notation.beforeFen) {
        setError(errorMessage, context + QStringLiteral(" before_fen differs from the current board"));
        return false;
    }
    const auto parsedMove = Move::fromUci(notation.uci);
    if (!parsedMove.has_value() || !before.isLegalMove(*parsedMove)) {
        setError(errorMessage, context + QStringLiteral(" UCI is not legal from before_fen"));
        return false;
    }
    const Piece moving = before.pieceAt(parsedMove->from);
    if (moving.isEmpty() || before.pieceAt(parsedMove->to).type == PieceType::King) {
        setError(errorMessage, context + QStringLiteral(" moving piece is invalid"));
        return false;
    }
    ChessPosition calculated = before;
    if (!calculated.applyMove(*parsedMove)) {
        setError(errorMessage, context + QStringLiteral(" UCI could not be applied"));
        return false;
    }
    const QString calculatedAfterFen = canonicalFenAfterMove(before, *parsedMove, calculated);
    if (calculatedAfterFen != notation.afterFen) {
        setError(errorMessage, context + QStringLiteral(" after_fen differs from legal replay"));
        return false;
    }
    QString afterFenError;
    const auto recordedAfter = exactPositionFromFen(notation.afterFen, &afterFenError);
    if (!recordedAfter.has_value()) {
        setError(errorMessage, context + QStringLiteral(" after_fen is invalid: ") + afterFenError);
        return false;
    }
    const QString calculatedSan = sanForMove(before, *parsedMove, calculated);
    const bool isCheck = calculated.isInCheck(calculated.sideToMove());
    const bool isCheckmate = isCheck && calculated.legalMoves().isEmpty();
    const bool isEnPassant = moving.type == PieceType::Pawn
        && before.pieceAt(parsedMove->to).isEmpty()
        && ChessPosition::fileOf(parsedMove->from) != ChessPosition::fileOf(parsedMove->to);
    std::optional<QString> castling;
    if (moving.type == PieceType::King
        && std::abs(ChessPosition::fileOf(parsedMove->from) - ChessPosition::fileOf(parsedMove->to)) == 2) {
        castling = ChessPosition::fileOf(parsedMove->to) == 6
            ? QStringLiteral("kingside") : QStringLiteral("queenside");
    }
    std::optional<QString> promotion;
    if (parsedMove->promotion != PieceType::None) {
        promotion = pieceName(parsedMove->promotion);
    }
    const std::optional<QString> captured = capturedPieceForMove(before, *parsedMove);
    const QString mover = before.sideToMove() == PieceColor::White
        ? QStringLiteral("white") : QStringLiteral("black");
    if (notation.mover != mover || notation.piece != pieceName(moving.type)
        || notation.origin != ChessPosition::squareName(parsedMove->from)
        || notation.target != ChessPosition::squareName(parsedMove->to)
        || notation.capturedPiece != captured || notation.capture != captured.has_value()
        || notation.enPassant != isEnPassant || notation.check != isCheck
        || notation.checkmate != isCheckmate || notation.castling != castling
        || notation.promotionPiece != promotion || notation.san != calculatedSan) {
        setError(errorMessage, context + QStringLiteral(" SAN or mechanical notation differs from legal replay"));
        return false;
    }
    *after = *recordedAfter;
    return true;
}

bool validateVariation(
    const ReplayPreferredVariation &variation,
    const ReplayMove &move,
    const QString &sourceGameId,
    const QString &sourceRunId,
    const QString &engineConfigId,
    int displayedVariationPlies,
    QString *errorMessage)
{
    const ReplayNotation &notation = move.notation;
    if (variation.anchorPly != move.ply || variation.sourceGameId != sourceGameId
        || variation.sourceRunId != sourceRunId
        || variation.engineConfigId != engineConfigId
        || variation.checkpointFen != notation.beforeFen
        || variation.checkpointPositionId != notation.beforePositionId
        || variation.checkpointReplayStateId != notation.beforeReplayStateId
        || variation.restorePlayedUci != notation.uci
        || variation.restoreAfterFen != notation.afterFen
        || variation.restoreAfterPositionId != notation.afterPositionId
        || variation.restoreAfterReplayStateId != notation.afterReplayStateId
        || variation.displayedSteps.size()
            != std::min(variation.reportedPvMoveCount, displayedVariationPlies)) {
        setError(errorMessage, QStringLiteral("available variation does not match its exact mainline checkpoint"));
        return false;
    }
    QString fenError;
    auto current = exactPositionFromFen(variation.checkpointFen, &fenError);
    if (!current.has_value()) {
        setError(errorMessage, QStringLiteral("variation checkpoint FEN is invalid: ") + fenError);
        return false;
    }
    QString priorPositionId = variation.checkpointPositionId;
    QString priorReplayStateId = variation.checkpointReplayStateId;
    for (int index = 0; index < variation.displayedSteps.size(); ++index) {
        const ReplayVariationStep &step = variation.displayedSteps.at(index);
        if (step.localPly != index + 1 || step.variationContextId != variation.variationContextId
            || step.beforeFen != canonicalFen(*current)
            || step.beforePositionId != priorPositionId
            || step.beforeReplayStateId != priorReplayStateId) {
            setError(errorMessage, QStringLiteral("variation step chain is disconnected"));
            return false;
        }
        const auto moveToken = Move::fromUci(step.uci);
        if (!moveToken.has_value() || !current->isLegalMove(*moveToken)
            || current->pieceAt(moveToken->to).type == PieceType::King) {
            setError(errorMessage, QStringLiteral("variation contains an illegal UCI move"));
            return false;
        }
        ChessPosition calculated = *current;
        if (!calculated.applyMove(*moveToken)
            || canonicalFenAfterMove(*current, *moveToken, calculated) != step.afterFen
            || sanForMove(*current, *moveToken, calculated) != step.san) {
            setError(errorMessage, QStringLiteral("variation SAN or after_fen differs from legal replay"));
            return false;
        }
        auto recorded = exactPositionFromFen(step.afterFen, &fenError);
        if (!recorded.has_value()) {
            setError(errorMessage, QStringLiteral("variation after_fen is invalid: ") + fenError);
            return false;
        }
        priorPositionId = step.afterPositionId;
        priorReplayStateId = step.afterReplayStateId;
        current = recorded;
    }
    auto restore = exactPositionFromFen(variation.checkpointFen, &fenError);
    const auto played = Move::fromUci(variation.restorePlayedUci);
    if (!restore.has_value() || !played.has_value() || !restore->isLegalMove(*played)) {
        setError(errorMessage, QStringLiteral("variation restore move is not legal from its checkpoint"));
        return false;
    }
    ChessPosition restored = *restore;
    if (!restored.applyMove(*played)
        || canonicalFenAfterMove(*restore, *played, restored) != variation.restoreAfterFen) {
        setError(errorMessage, QStringLiteral("variation restore_after_fen differs from the played move"));
        return false;
    }
    return true;
}

} // namespace

std::optional<AnnotatedReplayPack> AnnotatedReplayPack::fromMechanicalGame(
    const MechanicalReplayGame &game,
    QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    const auto plainText = [](const QString &value, qsizetype maximumBytes) {
        if (value.isEmpty() || value != value.trimmed()
            || value.toUtf8().size() > maximumBytes || value.contains(QChar::Null)) {
            return false;
        }
        return std::none_of(value.cbegin(), value.cend(), [](QChar character) {
            return character.unicode() < 32 || character.unicode() == 127;
        });
    };
    const QUrl gameUrl(game.canonicalGameUrl);
    const QDateTime eventStart = QDateTime::fromString(game.eventStartUtc, Qt::ISODate);
    const bool classified = game.openingStatus == QStringLiteral("classified");
    if (!semanticId(game.sourceGameId)
        || !plainText(game.canonicalGameUrl, 1'024)
        || !gameUrl.isValid() || gameUrl.scheme() != QStringLiteral("https")
        || !plainText(game.eventStartUtc, 64) || !eventStart.isValid()
        || eventStart.offsetFromUtc() != 0
        || !plainText(game.whiteUsername, 128)
        || !plainText(game.blackUsername, 128)
        || game.whiteUsername.compare(game.blackUsername, Qt::CaseInsensitive) == 0
        || game.whiteRating < 100 || game.whiteRating > 5'000
        || game.blackRating < 100 || game.blackRating > 5'000
        || (game.result != QStringLiteral("1-0")
            && game.result != QStringLiteral("0-1")
            && game.result != QStringLiteral("1/2-1/2"))
        || (game.openingStatus != QStringLiteral("classified")
            && game.openingStatus != QStringLiteral("ambiguous")
            && game.openingStatus != QStringLiteral("unknown"))
        || (classified
            && (!game.openingEco.has_value() || !game.openingName.has_value()
                || !QRegularExpression(QStringLiteral("^[A-E][0-9]{2}$"))
                        .match(*game.openingEco).hasMatch()
                || !plainText(*game.openingName, 160)))
        || (!classified && (game.openingEco.has_value() || game.openingName.has_value()))
        || (game.viewedPlayerColor != QStringLiteral("white")
            && game.viewedPlayerColor != QStringLiteral("black"))
        || game.moves.size() < 2 || game.moves.size() > kMaximumReplayPlies
        || (game.openingLastBookPly.has_value()
            && (*game.openingLastBookPly < 0
                || *game.openingLastBookPly > game.moves.size()))) {
        setError(errorMessage, QStringLiteral("mechanical game metadata is invalid"));
        return std::nullopt;
    }
    if (game.engineEvidence.has_value()) {
        const PersistedEngineGameEvidence &engine = *game.engineEvidence;
        bool thresholdsValid = engine.wdlLossThresholds.size() == 3;
        int previousThreshold = 0;
        for (const int threshold : engine.wdlLossThresholds) {
            thresholdsValid = thresholdsValid
                && threshold > previousThreshold && threshold <= 1'000'000;
            previousThreshold = threshold;
        }
        const QDateTime recordedAt = QDateTime::fromString(
            engine.analysisRecordedAtUtc, Qt::ISODate);
        if (!semanticId(engine.evidenceId)
            || !semanticId(engine.representativeRunId)
            || !semanticId(engine.engineConfigId)
            || !plainText(engine.analysisRecordedAtUtc, 64)
            || !recordedAt.isValid() || recordedAt.offsetFromUtc() != 0
            || engine.lineageCount < 1 || engine.lineageCount > 20'000
            || !plainText(engine.engineName, 256)
            || !plainText(engine.engineAuthor, 256)
            || !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$"))
                    .match(engine.engineBinarySha256).hasMatch()
            || !plainText(engine.engineAdapterVersion, 128)
            || engine.nodeLimit < 1'000 || engine.nodeLimit > 1'000'000
            || engine.hashMebibytes < 1 || engine.hashMebibytes > 1'024
            || engine.threads != 1 || !thresholdsValid
            || engine.winningExpectationMillionths < 500'000
            || engine.winningExpectationMillionths > 1'000'000) {
            setError(errorMessage, QStringLiteral("persisted engine game metadata is invalid"));
            return std::nullopt;
        }
    }
    const bool allMovesCarryEngine = std::all_of(
        game.moves.cbegin(), game.moves.cend(), [](const MechanicalReplayMove &move) {
            return move.engineEvidence.has_value();
        });
    const bool noMovesCarryEngine = std::none_of(
        game.moves.cbegin(), game.moves.cend(), [](const MechanicalReplayMove &move) {
            return move.engineEvidence.has_value();
        });
    if ((game.engineEvidence.has_value() && !allMovesCarryEngine)
        || (!game.engineEvidence.has_value() && !noMovesCarryEngine)) {
        setError(errorMessage, QStringLiteral("persisted engine coverage must be complete per game"));
        return std::nullopt;
    }

    QHash<int, QVector<SelectiveDeepMoment>> deepMomentsByPly;
    if (game.selectiveDeepReview.has_value()) {
        const SelectiveDeepGameReview &review = *game.selectiveDeepReview;
        const QSet<QString> statuses {
            QStringLiteral("played_move_matches_best"),
            QStringLiteral("confirmed_missed_opportunity"),
            QStringLiteral("confirmed_severe_error"),
            QStringLiteral("below_confirmation_threshold"),
            QStringLiteral("ambiguous_engine_instability"),
            QStringLiteral("incomplete_deep_evidence"),
            QStringLiteral("categorical_mate_comparison"),
        };
        const auto validDeepLine = [&plainText](const SelectiveDeepEngineLine &line) {
            const bool scoreValid = line.scoreKind == QStringLiteral("cp")
                ? line.centipawnsWhite.has_value() && !line.mateForWhite.has_value()
                : line.scoreKind == QStringLiteral("mate")
                    && !line.centipawnsWhite.has_value()
                    && line.mateForWhite.has_value() && *line.mateForWhite != 0;
            return semanticId(line.observationId)
                && semanticId(line.engineContractId)
                && semanticId(line.transitionId)
                && plainText(line.fen, 256)
                && (line.sideToMove == QStringLiteral("white")
                    || line.sideToMove == QStringLiteral("black"))
                && scoreValid
                && std::abs(line.centipawnsWhite.value_or(0)) <= 1'000'000
                && std::abs(line.mateForWhite.value_or(0)) <= 1'000'000
                && line.wdlWhite.size() == 3
                && std::all_of(line.wdlWhite.cbegin(), line.wdlWhite.cend(), [](int value) {
                    return value >= 0 && value <= 1'000;
                })
                && std::accumulate(line.wdlWhite.cbegin(), line.wdlWhite.cend(), 0) == 1'000
                && kUciPattern.match(line.rootMoveUci).hasMatch()
                && line.lineRank >= 1 && line.lineRank <= 10
                && line.depth >= 1 && line.selectiveDepth >= 0 && line.nodes >= 1
                && !line.pvUci.isEmpty() && line.pvUci.size() <= 256
                && line.pvUci.first() == line.rootMoveUci
                && std::all_of(line.pvUci.cbegin(), line.pvUci.cend(), [](const QString &move) {
                    return kUciPattern.match(move).hasMatch();
                })
                && plainText(line.scoreKind, 16);
        };
        if (!semanticId(review.reportId)
            || !semanticId(review.selectionReceiptId)
            || !semanticId(review.interpretationId)
            || !semanticId(review.engineContractId)
            || review.sourceGameId != game.sourceGameId
            || review.canonicalGameUrl != game.canonicalGameUrl
            || review.eventStartUtc != game.eventStartUtc
            || review.whiteUsername.compare(game.whiteUsername, Qt::CaseInsensitive) != 0
            || review.blackUsername.compare(game.blackUsername, Qt::CaseInsensitive) != 0
            || review.whiteRating != game.whiteRating
            || review.blackRating != game.blackRating
            || review.result != game.result
            || !plainText(review.engineName, 256)
            || !plainText(review.engineAuthor, 256)
            || !kShaPattern.match(review.engineBinarySha256).hasMatch()
            || review.nodeLimit < 1'000 || review.nodeLimit > 1'000'000
            || review.alternativeLineCount < 2 || review.alternativeLineCount > 10
            || review.mainline.size() != game.moves.size()
            || review.moments.isEmpty() || review.moments.size() > 3) {
            setError(errorMessage, QStringLiteral("selective deep report authority differs from the mechanical game"));
            return std::nullopt;
        }
        QSet<QString> occurrenceIds;
        for (int index = 0; index < review.moments.size(); ++index) {
            const SelectiveDeepMoment &moment = review.moments.at(index);
            const bool comparisonAvailable = moment.bestExpectationMillionths.has_value()
                || moment.playedExpectationMillionths.has_value()
                || moment.signedExpectationDeltaMillionths.has_value();
            if (moment.presentationOrder != index + 1
                || moment.priorityRank < 1 || moment.priorityRank > 512
                || moment.ply < 1 || moment.ply > game.moves.size()
                || !semanticId(moment.assessmentId)
                || !semanticId(moment.occurrenceId)
                || !semanticId(moment.episodeId)
                || !semanticId(moment.transitionId)
                || occurrenceIds.contains(moment.occurrenceId)
                || !statuses.contains(moment.status)
                || (moment.mover != QStringLiteral("white")
                    && moment.mover != QStringLiteral("black"))
                || (moment.phase != QStringLiteral("opening")
                    && moment.phase != QStringLiteral("middlegame")
                    && moment.phase != QStringLiteral("endgame"))
                || !plainText(moment.san, 32)
                || !kUciPattern.match(moment.playedMoveUci).hasMatch()
                || !plainText(moment.beforeFen, 256)
                || (moment.severity.has_value()
                    && *moment.severity != QStringLiteral("inaccuracy")
                    && *moment.severity != QStringLiteral("mistake")
                    && *moment.severity != QStringLiteral("severe"))
                || (moment.bestMoveUci.has_value()
                    && !kUciPattern.match(*moment.bestMoveUci).hasMatch())
                || (comparisonAvailable
                    && (!moment.bestExpectationMillionths.has_value()
                        || !moment.playedExpectationMillionths.has_value()
                        || !moment.signedExpectationDeltaMillionths.has_value()
                        || *moment.bestExpectationMillionths < 0
                        || *moment.bestExpectationMillionths > 1'000'000
                        || *moment.playedExpectationMillionths < 0
                        || *moment.playedExpectationMillionths > 1'000'000
                        || *moment.signedExpectationDeltaMillionths
                            != *moment.bestExpectationMillionths
                                - *moment.playedExpectationMillionths))
                || (moment.wdlLossMillionths.has_value()
                    && (!moment.signedExpectationDeltaMillionths.has_value()
                        || *moment.signedExpectationDeltaMillionths < 0
                        || *moment.wdlLossMillionths
                            != *moment.signedExpectationDeltaMillionths))
                || (moment.centipawnLoss.has_value() && *moment.centipawnLoss < 0)
                || moment.alternativeLines.size() > review.alternativeLineCount
                || !std::all_of(
                    moment.alternativeLines.cbegin(), moment.alternativeLines.cend(), validDeepLine)
                || (moment.playedLine.has_value()
                    && (!validDeepLine(*moment.playedLine)
                        || moment.playedLine->rootMoveUci != moment.playedMoveUci))) {
                setError(errorMessage, QStringLiteral("selective deep moment is structurally inconsistent"));
                return std::nullopt;
            }
            occurrenceIds.insert(moment.occurrenceId);
            for (int lineIndex = 0; lineIndex < moment.alternativeLines.size(); ++lineIndex) {
                const SelectiveDeepEngineLine &line = moment.alternativeLines.at(lineIndex);
                if (line.engineContractId != review.engineContractId
                    || line.transitionId != moment.transitionId
                    || line.fen != moment.beforeFen || line.sideToMove != moment.mover
                    || line.lineRank != lineIndex + 1) {
                    setError(errorMessage,
                        QStringLiteral("selective deep alternative line authority is inconsistent"));
                    return std::nullopt;
                }
            }
            if (moment.playedLine.has_value()
                && (moment.playedLine->engineContractId != review.engineContractId
                    || moment.playedLine->transitionId != moment.transitionId
                    || moment.playedLine->fen != moment.beforeFen
                    || moment.playedLine->sideToMove != moment.mover
                    || moment.playedLine->lineRank != 1)) {
                setError(errorMessage,
                    QStringLiteral("selective deep played-line authority is inconsistent"));
                return std::nullopt;
            }
            deepMomentsByPly[moment.ply].append(moment);
        }
    }

    QString fenError;
    auto current = exactPositionFromFen(kStandardInitialFen, &fenError);
    if (!current.has_value()) {
        setError(errorMessage, QStringLiteral("standard starting position is unavailable: ") + fenError);
        return std::nullopt;
    }

    AnnotatedReplayPack pack;
    pack.m_sourceGameId = game.sourceGameId;
    pack.m_whiteUsername = game.whiteUsername;
    pack.m_blackUsername = game.blackUsername;
    pack.m_whiteRating = game.whiteRating;
    pack.m_blackRating = game.blackRating;
    pack.m_result = game.result;
    pack.m_openingStatus = game.openingStatus;
    pack.m_openingEco = game.openingEco;
    pack.m_openingName = game.openingName;
    pack.m_openingLastBookPly = game.openingLastBookPly;
    pack.m_canonicalGameUrl = game.canonicalGameUrl;
    pack.m_eventStartUtc = game.eventStartUtc;
    pack.m_viewedPlayerColor = game.viewedPlayerColor;
    pack.m_mechanicalGameBreakdown = true;
    pack.m_persistedEngineEvidence = game.engineEvidence;
    pack.m_selectiveDeepReview = game.selectiveDeepReview;
    if (game.engineEvidence.has_value()) {
        pack.m_sourceRunId = game.engineEvidence->representativeRunId;
        pack.m_sourceEngineConfigId = game.engineEvidence->engineConfigId;
        pack.m_sourceEngineName = game.engineEvidence->engineName;
        pack.m_sourceEngineAuthor = game.engineEvidence->engineAuthor;
        pack.m_sourceEngineBinarySha256 = game.engineEvidence->engineBinarySha256;
        pack.m_sourceEngineNodeLimit = game.engineEvidence->nodeLimit;
    }
    pack.m_mainlinePositions.reserve(game.moves.size() + 1);
    pack.m_mainlinePositions.append(*current);
    pack.m_moves.reserve(game.moves.size());

    QCryptographicHash replayHash(QCryptographicHash::Sha256);
    const auto addHashField = [&replayHash](const QByteArray &field) {
        replayHash.addData(QByteArray::number(field.size()));
        replayHash.addData(QByteArrayLiteral(":"));
        replayHash.addData(field);
        replayHash.addData(QByteArrayLiteral("|"));
    };
    const auto addOptionalInteger = [&addHashField](const std::optional<qint64> &value) {
        addHashField(value.has_value() ? QByteArray::number(*value) : QByteArrayLiteral("null"));
    };
    for (const QString &value : {
             game.sourceGameId, game.canonicalGameUrl, game.eventStartUtc,
             game.whiteUsername, game.blackUsername, game.result,
             game.openingStatus, game.openingEco.value_or(QString()),
             game.openingName.value_or(QString()), game.viewedPlayerColor,
         }) {
        addHashField(value.toUtf8());
    }
    addHashField(QByteArray::number(game.whiteRating));
    addHashField(QByteArray::number(game.blackRating));
    addHashField(game.openingLastBookPly.has_value()
            ? QByteArray::number(*game.openingLastBookPly) : QByteArrayLiteral("null"));
    addHashField(game.engineEvidence.has_value() ? QByteArrayLiteral("engine") : QByteArrayLiteral("no-engine"));
    if (game.engineEvidence.has_value()) {
        const PersistedEngineGameEvidence &engine = *game.engineEvidence;
        for (const QString &value : {
                 engine.evidenceId, engine.representativeRunId,
                 engine.analysisRecordedAtUtc, engine.engineConfigId,
                 engine.engineName, engine.engineAuthor,
                 engine.engineBinarySha256, engine.engineAdapterVersion,
             }) {
            addHashField(value.toUtf8());
        }
        for (const int value : engine.wdlLossThresholds) {
            addHashField(QByteArray::number(value));
        }
        for (const int value : {
                 engine.lineageCount, engine.nodeLimit, engine.hashMebibytes,
                 engine.threads, engine.winningExpectationMillionths,
             }) {
            addHashField(QByteArray::number(value));
        }
    }

    const QSet<QString> phases {
        QStringLiteral("opening"), QStringLiteral("middlegame"),
        QStringLiteral("endgame")};
    const QSet<QString> elapsedStatuses {
        QStringLiteral("observed_emt"),
        QStringLiteral("derived_clock_difference"),
        QStringLiteral("derived_clock_difference_rounded_zero"),
        QStringLiteral("missing_clock_annotation"),
        QStringLiteral("missing_prior_clock"),
        QStringLiteral("missing_time_control"),
        QStringLiteral("missing_time_control_stage"),
        QStringLiteral("not_applicable_untimed"),
        QStringLiteral("unsupported_hourglass"),
        QStringLiteral("clock_increase_unreconciled"),
    };
    const auto validClock = [](const std::optional<qint64> &value) {
        return !value.has_value() || (*value >= 0 && *value <= 86'400'000);
    };
    const auto validWdl = [](const QVector<int> &values) {
        return values.size() == 3
            && std::all_of(values.cbegin(), values.cend(), [](int value) {
                return value >= 0 && value <= 1'000;
            })
            && std::accumulate(values.cbegin(), values.cend(), 0) == 1'000;
    };
    const auto validScore = [](const QString &kind,
                                const std::optional<qint64> &centipawns,
                                const std::optional<qint64> &mate,
                                bool allowTerminal) {
        if (kind == QStringLiteral("cp")) {
            return centipawns.has_value() && !mate.has_value()
                && std::abs(*centipawns) <= 1'000'000;
        }
        if (kind == QStringLiteral("mate")) {
            return !centipawns.has_value() && mate.has_value()
                && *mate != 0 && std::abs(*mate) <= 1'000'000;
        }
        if (allowTerminal && kind == QStringLiteral("terminal_mate")) {
            return !centipawns.has_value() && mate.value_or(-1) == 0;
        }
        return allowTerminal && kind == QStringLiteral("terminal_draw")
            && centipawns.value_or(-1) == 0 && !mate.has_value();
    };

    for (int index = 0; index < game.moves.size(); ++index) {
        const MechanicalReplayMove &source = game.moves.at(index);
        const int expectedPly = index + 1;
        const QString expectedForcedness = source.legalMoveCount == 1
            ? QStringLiteral("forced-single-legal-move") : QStringLiteral("nonforced");
        const bool elapsedObserved = source.elapsedMoveMs.has_value();
        const bool statusObserved = source.elapsedStatus == QStringLiteral("observed_emt")
            || source.elapsedStatus == QStringLiteral("derived_clock_difference")
            || source.elapsedStatus == QStringLiteral("derived_clock_difference_rounded_zero");
        const auto move = Move::fromUci(source.uci);
        const QVector<Move> legalMoves = current->legalMoves();
        if (source.ply != expectedPly
            || !plainText(source.san, 20)
            || !kUciPattern.match(source.uci).hasMatch()
            || !phases.contains(source.positionPhase)
            || source.legalMoveCount != legalMoves.size()
            || source.forcednessStatus != expectedForcedness
            || !elapsedStatuses.contains(source.elapsedStatus)
            || elapsedObserved != statusObserved
            || !validClock(source.decisionStartClockMs)
            || !validClock(source.clockRemainingAfterMoveMs)
            || !validClock(source.elapsedMoveMs)
            || !move.has_value() || !current->isLegalMove(*move)
            || current->pieceAt(move->to).type == PieceType::King) {
            setError(errorMessage, QStringLiteral("mechanical move %1 is malformed or illegal").arg(expectedPly));
            return std::nullopt;
        }
        if (source.engineEvidence.has_value()) {
            const PersistedEngineMoveEvidence &engine = *source.engineEvidence;
            const QVector<int> &thresholds = game.engineEvidence->wdlLossThresholds;
            const QString expectedSeverity = engine.wdlLossMillionths >= thresholds.at(2)
                ? QStringLiteral("severe")
                : engine.wdlLossMillionths >= thresholds.at(1)
                    ? QStringLiteral("mistake")
                    : engine.wdlLossMillionths >= thresholds.at(0)
                        ? QStringLiteral("inaccuracy") : QStringLiteral("none");
            if (engine.expectedBeforeMillionths < 0
                || engine.expectedBeforeMillionths > 1'000'000
                || engine.expectedAfterMillionths < 0
                || engine.expectedAfterMillionths > 1'000'000
                || engine.wdlLossMillionths
                    != std::max(0, engine.expectedBeforeMillionths - engine.expectedAfterMillionths)
                || engine.centipawnLoss.value_or(0) < 0
                || engine.severity != expectedSeverity
                || !validScore(engine.beforeScoreKind,
                    engine.beforeCentipawnsWhite, engine.beforeMateForWhite, false)
                || !validScore(engine.afterScoreKind,
                    engine.afterCentipawnsWhite, engine.afterMateForWhite, true)
                || !validWdl(engine.beforeWdlWhite) || !validWdl(engine.afterWdlWhite)
                || (engine.beforeBestMoveUci.has_value()
                    && !kUciPattern.match(*engine.beforeBestMoveUci).hasMatch())
                || engine.beforeDepth < 1 || engine.beforeSelectiveDepth < 0
                || engine.beforeNodes < 1 || engine.beforePvUci.toUtf8().size() > 16'384) {
                setError(errorMessage, QStringLiteral("persisted engine move %1 is invalid").arg(expectedPly));
                return std::nullopt;
            }
        }

        const Piece moving = current->pieceAt(move->from);
        ChessPosition after = *current;
        if (!after.applyMove(*move)) {
            setError(errorMessage, QStringLiteral("mechanical move %1 could not be applied").arg(expectedPly));
            return std::nullopt;
        }
        const QString calculatedSan = sanForMove(*current, *move, after);
        if (calculatedSan != source.san) {
            setError(errorMessage, QStringLiteral("mechanical move %1 SAN differs from legal replay").arg(expectedPly));
            return std::nullopt;
        }
        if (game.selectiveDeepReview.has_value()) {
            const SelectiveDeepMainlineMove &deepMove =
                game.selectiveDeepReview->mainline.at(index);
            const QString beforeFen = canonicalFen(*current);
            const QString afterFen = canonicalFenAfterMove(*current, *move, after);
            const bool hasMoment = deepMomentsByPly.contains(expectedPly);
            if (deepMove.ply != expectedPly
                || deepMove.mover != (expectedPly % 2 == 1
                    ? QStringLiteral("white") : QStringLiteral("black"))
                || deepMove.phase != source.positionPhase
                || deepMove.san != source.san || deepMove.uci != source.uci
                || deepMove.beforeFen != beforeFen || deepMove.afterFen != afterFen
                || hasMoment != (deepMove.selectionStatus
                    == QStringLiteral("selected_for_deep_assessment"))) {
                setError(errorMessage, QStringLiteral("selective deep report mainline differs at ply %1").arg(expectedPly));
                return std::nullopt;
            }
            for (const SelectiveDeepMoment &moment : deepMomentsByPly.value(expectedPly)) {
                if (moment.mover != deepMove.mover || moment.phase != deepMove.phase
                    || moment.san != deepMove.san || moment.playedMoveUci != deepMove.uci
                    || moment.beforeFen != beforeFen) {
                    setError(errorMessage, QStringLiteral("selective deep moment differs at ply %1").arg(expectedPly));
                    return std::nullopt;
                }
            }
        }

        ReplayMove output;
        output.ply = expectedPly;
        output.severity = QStringLiteral("none");
        output.alternativeStatus = QStringLiteral("unavailable");
        output.positionPhase = source.positionPhase;
        output.forcednessStatus = source.forcednessStatus;
        output.legalMoveCount = source.legalMoveCount;
        output.decisionStartClockMs = source.decisionStartClockMs;
        output.clockRemainingAfterMoveMs = source.clockRemainingAfterMoveMs;
        output.elapsedMoveMs = source.elapsedMoveMs;
        output.elapsedStatus = source.elapsedStatus;
        output.persistedEngineEvidence = source.engineEvidence;
        output.selectiveDeepMoments = deepMomentsByPly.value(expectedPly);
        if (source.engineEvidence.has_value()) {
            output.severity = source.engineEvidence->severity;
            output.expectedBeforeMillionths = source.engineEvidence->expectedBeforeMillionths;
            output.expectedAfterMillionths = source.engineEvidence->expectedAfterMillionths;
            output.wdlLossMillionths = source.engineEvidence->wdlLossMillionths;
            output.centipawnLoss = source.engineEvidence->centipawnLoss;
            output.missedWinningAdvantage = source.engineEvidence->missedWinningAdvantage;
            output.missedForcedMate = source.engineEvidence->missedForcedMate;
        }
        output.notation.ply = expectedPly;
        output.notation.moveNumber = (expectedPly + 1) / 2;
        output.notation.matchId = game.sourceGameId;
        output.notation.mover = expectedPly % 2 == 1
            ? QStringLiteral("white") : QStringLiteral("black");
        output.notation.uci = source.uci;
        output.notation.san = source.san;
        output.notation.piece = pieceName(moving.type);
        output.notation.origin = ChessPosition::squareName(move->from);
        output.notation.target = ChessPosition::squareName(move->to);
        output.notation.capturedPiece = capturedPieceForMove(*current, *move);
        output.notation.capture = output.notation.capturedPiece.has_value();
        output.notation.enPassant = moving.type == PieceType::Pawn
            && current->pieceAt(move->to).isEmpty()
            && ChessPosition::fileOf(move->from) != ChessPosition::fileOf(move->to);
        output.notation.check = after.isInCheck(after.sideToMove());
        output.notation.checkmate = output.notation.check && after.legalMoves().isEmpty();
        if (moving.type == PieceType::King
            && std::abs(ChessPosition::fileOf(move->from) - ChessPosition::fileOf(move->to)) == 2) {
            output.notation.castling = ChessPosition::fileOf(move->to) == 6
                ? QStringLiteral("kingside") : QStringLiteral("queenside");
        }
        if (move->promotion != PieceType::None) {
            output.notation.promotionPiece = pieceName(move->promotion);
        }
        output.notation.beforeFen = canonicalFen(*current);
        output.notation.afterFen = canonicalFenAfterMove(*current, *move, after);
        pack.m_moves.append(output);
        pack.m_mainlinePositions.append(after);
        current = after;

        for (const QString &value : {
                 source.san, source.uci, source.positionPhase,
                 source.forcednessStatus, source.elapsedStatus,
             }) {
            addHashField(value.toUtf8());
        }
        addHashField(QByteArray::number(source.ply));
        addHashField(QByteArray::number(source.legalMoveCount));
        addOptionalInteger(source.decisionStartClockMs);
        addOptionalInteger(source.clockRemainingAfterMoveMs);
        addOptionalInteger(source.elapsedMoveMs);
        addHashField(source.engineEvidence.has_value()
                ? QByteArrayLiteral("engine") : QByteArrayLiteral("no-engine"));
        if (source.engineEvidence.has_value()) {
            const PersistedEngineMoveEvidence &engine = *source.engineEvidence;
            for (const int value : {
                     engine.expectedBeforeMillionths, engine.expectedAfterMillionths,
                     engine.wdlLossMillionths, engine.beforeDepth,
                     engine.beforeSelectiveDepth, engine.beforeNodes,
                 }) {
                addHashField(QByteArray::number(value));
            }
            addOptionalInteger(engine.centipawnLoss);
            addOptionalInteger(engine.beforeCentipawnsWhite);
            addOptionalInteger(engine.beforeMateForWhite);
            addOptionalInteger(engine.afterCentipawnsWhite);
            addOptionalInteger(engine.afterMateForWhite);
            addHashField(engine.missedWinningAdvantage
                    ? QByteArrayLiteral("true") : QByteArrayLiteral("false"));
            addHashField(engine.missedForcedMate
                    ? QByteArrayLiteral("true") : QByteArrayLiteral("false"));
            for (const QString &value : {
                     engine.severity, engine.beforeScoreKind,
                     engine.beforeBestMoveUci.value_or(QString()),
                     engine.beforePvUci, engine.afterScoreKind,
                 }) {
                addHashField(value.toUtf8());
            }
            for (const int value : engine.beforeWdlWhite) {
                addHashField(QByteArray::number(value));
            }
            for (const int value : engine.afterWdlWhite) {
                addHashField(QByteArray::number(value));
            }
        }
    }
    if (game.selectiveDeepReview.has_value()) {
        const SelectiveDeepGameReview &review = *game.selectiveDeepReview;
        for (const QString &value : {
                 review.reportId, review.selectionReceiptId, review.interpretationId,
                 review.engineContractId, review.engineName, review.engineAuthor,
                 review.engineBinarySha256,
             }) {
            addHashField(value.toUtf8());
        }
        addHashField(QByteArray::number(review.nodeLimit));
        addHashField(QByteArray::number(review.alternativeLineCount));
        const auto hashLine = [&addHashField, &addOptionalInteger](
                                  const SelectiveDeepEngineLine &line) {
            for (const QString &value : {
                     line.observationId, line.engineContractId, line.transitionId,
                     line.fen, line.sideToMove, line.scoreKind, line.rootMoveUci,
                 }) {
                addHashField(value.toUtf8());
            }
            addOptionalInteger(line.centipawnsWhite);
            addOptionalInteger(line.mateForWhite);
            addHashField(QByteArray::number(line.lineRank));
            addHashField(QByteArray::number(line.depth));
            addHashField(QByteArray::number(line.selectiveDepth));
            addHashField(QByteArray::number(line.nodes));
            for (const int value : line.wdlWhite) {
                addHashField(QByteArray::number(value));
            }
            for (const QString &move : line.pvUci) {
                addHashField(move.toUtf8());
            }
        };
        for (const SelectiveDeepMoment &moment : review.moments) {
            for (const QString &value : {
                     moment.assessmentId, moment.occurrenceId, moment.episodeId,
                     moment.transitionId, moment.mover, moment.phase, moment.san,
                     moment.playedMoveUci, moment.beforeFen, moment.status,
                     moment.severity.value_or(QString()),
                     moment.bestMoveUci.value_or(QString()), moment.mateComparison,
                     moment.pairStability,
                 }) {
                addHashField(value.toUtf8());
            }
            addHashField(QByteArray::number(moment.presentationOrder));
            addHashField(QByteArray::number(moment.priorityRank));
            addHashField(QByteArray::number(moment.ply));
            addOptionalInteger(moment.bestExpectationMillionths);
            addOptionalInteger(moment.playedExpectationMillionths);
            addOptionalInteger(moment.signedExpectationDeltaMillionths);
            addOptionalInteger(moment.wdlLossMillionths);
            addOptionalInteger(moment.centipawnLoss);
            for (const SelectiveDeepEngineLine &line : moment.alternativeLines) {
                hashLine(line);
            }
            if (moment.playedLine.has_value()) {
                hashLine(*moment.playedLine);
            } else {
                addHashField(QByteArrayLiteral("no-played-deep-line"));
            }
        }
    }
    pack.m_replayId = QStringLiteral("chess-mechanical-game-replay-v1:")
        + QString::fromLatin1(replayHash.result().toHex());
    return pack;
}

std::optional<AnnotatedReplayPack> AnnotatedReplayPack::fromJson(
    const QByteArray &rawJson,
    QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (rawJson.isEmpty() || rawJson.size() > kMaximumAnnotatedReplayBytes) {
        setError(errorMessage, QStringLiteral("annotated replay must contain 1..4194304 bytes"));
        return std::nullopt;
    }
    if (rawJson.startsWith("\xEF\xBB\xBF") || rawJson.contains('\0')) {
        setError(errorMessage, QStringLiteral("annotated replay must be UTF-8 without BOM or NUL"));
        return std::nullopt;
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder.decode(rawJson);
    if (decoder.hasError()) {
        setError(errorMessage, QStringLiteral("annotated replay is not strict UTF-8"));
        return std::nullopt;
    }
    JsonShapeScanner scanner(rawJson);
    if (!scanner.scan(errorMessage)) {
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(rawJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(
            errorMessage,
            QStringLiteral("annotated replay is not one JSON object: %1")
                .arg(parseError.errorString()));
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    if (!exactKeys(
            root,
            {"black_username", "classification_evidence_id", "contract_version", "json_acquisition_id", "move_evidence_id", "move_vocabulary_id", "moves", "opening", "pgn_acquisition_id", "policy", "replay_id", "result", "source_bundle_id", "source_engine", "source_game_fingerprint", "source_game_id", "source_run_id", "white_username"},
            QStringLiteral("annotated replay"), errorMessage)) {
        return std::nullopt;
    }

    AnnotatedReplayPack pack;
    QString contractVersion;
    QString sourceBundleId;
    QString jsonAcquisitionId;
    QString pgnAcquisitionId;
    QString sourceGameFingerprint;
    QString moveEvidenceId;
    QString classificationEvidenceId;
    QString moveVocabularyId;
    if (!requiredString(root, QStringLiteral("contract_version"), &contractVersion, QStringLiteral("annotated replay"), errorMessage, 64)
        || contractVersion != QStringLiteral("annotated-game-replay-v1")
        || !requiredString(root, QStringLiteral("replay_id"), &pack.m_replayId, QStringLiteral("annotated replay"), errorMessage)
        || !requiredString(root, QStringLiteral("source_game_id"), &pack.m_sourceGameId, QStringLiteral("annotated replay"), errorMessage)
        || !requiredString(root, QStringLiteral("source_run_id"), &pack.m_sourceRunId, QStringLiteral("annotated replay"), errorMessage)
        || !requiredString(root, QStringLiteral("source_bundle_id"), &sourceBundleId, QStringLiteral("annotated replay"), errorMessage)
        || !requiredString(root, QStringLiteral("json_acquisition_id"), &jsonAcquisitionId, QStringLiteral("annotated replay"), errorMessage)
        || !requiredString(root, QStringLiteral("pgn_acquisition_id"), &pgnAcquisitionId, QStringLiteral("annotated replay"), errorMessage)
        || !requiredString(root, QStringLiteral("source_game_fingerprint"), &sourceGameFingerprint, QStringLiteral("annotated replay"), errorMessage, 64)
        || !requiredString(root, QStringLiteral("move_evidence_id"), &moveEvidenceId, QStringLiteral("annotated replay"), errorMessage)
        || !requiredString(root, QStringLiteral("classification_evidence_id"), &classificationEvidenceId, QStringLiteral("annotated replay"), errorMessage)
        || !requiredString(root, QStringLiteral("move_vocabulary_id"), &moveVocabularyId, QStringLiteral("annotated replay"), errorMessage)
        || !requiredString(root, QStringLiteral("white_username"), &pack.m_whiteUsername, QStringLiteral("annotated replay"), errorMessage, 128)
        || !requiredString(root, QStringLiteral("black_username"), &pack.m_blackUsername, QStringLiteral("annotated replay"), errorMessage, 128)
        || !requiredString(root, QStringLiteral("result"), &pack.m_result, QStringLiteral("annotated replay"), errorMessage, 8)) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("annotated replay contract_version is unsupported");
        }
        return std::nullopt;
    }
    if (!validateId(pack.m_replayId, QStringLiteral("replay_id"), errorMessage)
        || !validateId(pack.m_sourceGameId, QStringLiteral("source_game_id"), errorMessage)
        || !validateId(pack.m_sourceRunId, QStringLiteral("source_run_id"), errorMessage)
        || !validateId(sourceBundleId, QStringLiteral("source_bundle_id"), errorMessage)
        || !validateId(jsonAcquisitionId, QStringLiteral("json_acquisition_id"), errorMessage)
        || !validateId(pgnAcquisitionId, QStringLiteral("pgn_acquisition_id"), errorMessage)
        || !validateId(moveEvidenceId, QStringLiteral("move_evidence_id"), errorMessage)
        || !validateId(classificationEvidenceId, QStringLiteral("classification_evidence_id"), errorMessage)
        || !validateId(moveVocabularyId, QStringLiteral("move_vocabulary_id"), errorMessage)
        || !kShaPattern.match(sourceGameFingerprint).hasMatch()
        || (pack.m_result != QStringLiteral("1-0") && pack.m_result != QStringLiteral("0-1")
            && pack.m_result != QStringLiteral("1/2-1/2"))
        || pack.m_whiteUsername.compare(pack.m_blackUsername, Qt::CaseInsensitive) == 0) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("annotated replay lineage or game metadata is invalid");
        }
        return std::nullopt;
    }

    QJsonObject sourceEngine;
    if (!requiredObject(root, QStringLiteral("source_engine"), &sourceEngine, QStringLiteral("annotated replay"), errorMessage)
        || !exactKeys(sourceEngine, {"author", "binary_sha256", "config_id", "name", "node_limit"}, QStringLiteral("source_engine"), errorMessage)
        || !requiredString(sourceEngine, QStringLiteral("config_id"), &pack.m_sourceEngineConfigId, QStringLiteral("source_engine"), errorMessage)
        || !requiredString(sourceEngine, QStringLiteral("name"), &pack.m_sourceEngineName, QStringLiteral("source_engine"), errorMessage, 256)
        || !requiredString(sourceEngine, QStringLiteral("author"), &pack.m_sourceEngineAuthor, QStringLiteral("source_engine"), errorMessage, 256)
        || !requiredString(sourceEngine, QStringLiteral("binary_sha256"), &pack.m_sourceEngineBinarySha256, QStringLiteral("source_engine"), errorMessage, 64)
        || !requiredInteger(sourceEngine, QStringLiteral("node_limit"), 1000, 1'000'000, &pack.m_sourceEngineNodeLimit, QStringLiteral("source_engine"), errorMessage)
        || !validateId(pack.m_sourceEngineConfigId, QStringLiteral("source_engine.config_id"), errorMessage)
        || !kShaPattern.match(pack.m_sourceEngineBinarySha256).hasMatch()) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("annotated replay source engine metadata is invalid");
        }
        return std::nullopt;
    }

    QJsonObject opening;
    QString openingClassificationId;
    QString openingCorpusId;
    if (!requiredObject(root, QStringLiteral("opening"), &opening, QStringLiteral("annotated replay"), errorMessage)
        || !exactKeys(opening, {"classification_id", "corpus_id", "eco", "name", "status"}, QStringLiteral("opening"), errorMessage)
        || !requiredString(opening, QStringLiteral("classification_id"), &openingClassificationId, QStringLiteral("opening"), errorMessage)
        || !requiredString(opening, QStringLiteral("corpus_id"), &openingCorpusId, QStringLiteral("opening"), errorMessage)
        || !requiredString(opening, QStringLiteral("status"), &pack.m_openingStatus, QStringLiteral("opening"), errorMessage, 16)
        || !nullableString(opening, QStringLiteral("eco"), &pack.m_openingEco, QStringLiteral("opening"), errorMessage, 3)
        || !nullableString(opening, QStringLiteral("name"), &pack.m_openingName, QStringLiteral("opening"), errorMessage, 160)
        || !validateId(openingClassificationId, QStringLiteral("opening.classification_id"), errorMessage)
        || !validateId(openingCorpusId, QStringLiteral("opening.corpus_id"), errorMessage)) {
        return std::nullopt;
    }
    if (pack.m_openingStatus == QStringLiteral("classified")) {
        if (!pack.m_openingEco.has_value() || !pack.m_openingName.has_value()) {
            setError(errorMessage, QStringLiteral("classified opening is missing its exact label"));
            return std::nullopt;
        }
    } else if ((pack.m_openingStatus != QStringLiteral("ambiguous")
                && pack.m_openingStatus != QStringLiteral("unknown"))
               || pack.m_openingEco.has_value() || pack.m_openingName.has_value()) {
        setError(errorMessage, QStringLiteral("non-classified opening chose an unsupported label"));
        return std::nullopt;
    }

    QJsonObject policy;
    QString policyContract;
    QString templateVersion;
    QString minimumSeverity;
    QString policyId;
    qint64 displayedVariationPlies = 0;
    qint64 maximumVariations = 0;
    if (!requiredObject(root, QStringLiteral("policy"), &policy, QStringLiteral("annotated replay"), errorMessage)
        || !exactKeys(policy, {"contract_version", "displayed_variation_plies", "maximum_variations", "minimum_variation_severity", "policy_id", "template_version"}, QStringLiteral("policy"), errorMessage)
        || !requiredString(policy, QStringLiteral("contract_version"), &policyContract, QStringLiteral("policy"), errorMessage, 64)
        || !requiredString(policy, QStringLiteral("template_version"), &templateVersion, QStringLiteral("policy"), errorMessage, 64)
        || !requiredString(policy, QStringLiteral("minimum_variation_severity"), &minimumSeverity, QStringLiteral("policy"), errorMessage, 16)
        || !requiredString(policy, QStringLiteral("policy_id"), &policyId, QStringLiteral("policy"), errorMessage)
        || !requiredInteger(policy, QStringLiteral("displayed_variation_plies"), 2, kMaximumVariationPlies, &displayedVariationPlies, QStringLiteral("policy"), errorMessage)
        || !requiredInteger(policy, QStringLiteral("maximum_variations"), 1, kMaximumVariations, &maximumVariations, QStringLiteral("policy"), errorMessage)
        || policyContract != QStringLiteral("annotated-game-replay-v1")
        || templateVersion != QStringLiteral("deterministic-chess-explanations-v1")
        || !QSet<QString>{QStringLiteral("none"), QStringLiteral("inaccuracy"), QStringLiteral("mistake"), QStringLiteral("severe")}.contains(minimumSeverity)
        || !validateId(policyId, QStringLiteral("policy.policy_id"), errorMessage)) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("annotated replay policy is invalid");
        }
        return std::nullopt;
    }

    QJsonArray moves;
    if (!requiredArray(root, QStringLiteral("moves"), &moves, QStringLiteral("annotated replay"), errorMessage)
        || moves.size() < 2 || moves.size() > kMaximumReplayPlies) {
        setError(errorMessage, QStringLiteral("annotated replay mainline must contain 2..700 plies"));
        return std::nullopt;
    }
    pack.m_moves.reserve(moves.size());
    QSet<QString> annotationIds;
    qsizetype totalNarrationBytes = 0;
    int variationCount = 0;
    for (int index = 0; index < moves.size(); ++index) {
        if (!moves.at(index).isObject()) {
            setError(errorMessage, QStringLiteral("annotated replay move %1 must be an object").arg(index + 1));
            return std::nullopt;
        }
        ReplayMove move;
        if (!parseMove(
                moves.at(index).toObject(), &move,
                QStringLiteral("moves[%1]").arg(index), errorMessage)
            || move.ply != index + 1 || move.notation.ply != index + 1
            || move.notation.matchId != pack.m_sourceGameId
            || move.notation.mover != ((index % 2 == 0) ? QStringLiteral("white") : QStringLiteral("black"))
            || annotationIds.contains(move.annotationId)) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("annotated replay move %1 is misaligned").arg(index + 1);
            }
            return std::nullopt;
        }
        annotationIds.insert(move.annotationId);
        for (const ReplayNarration &line : move.narration) {
            totalNarrationBytes += line.text.toUtf8().size();
        }
        if (move.preferredVariation.has_value()) {
            ++variationCount;
        }
        pack.m_moves.append(move);
    }
    if (totalNarrationBytes > kMaximumNarrationTotalBytes
        || variationCount > maximumVariations) {
        setError(errorMessage, QStringLiteral("annotated replay exceeds its narration or variation policy"));
        return std::nullopt;
    }

    QString fenError;
    auto current = exactPositionFromFen(pack.m_moves.first().notation.beforeFen, &fenError);
    if (!current.has_value()) {
        setError(errorMessage, QStringLiteral("annotated replay initial FEN is invalid: ") + fenError);
        return std::nullopt;
    }
    if (pack.m_moves.first().notation.beforeFen != kStandardInitialFen) {
        setError(errorMessage, QStringLiteral("annotated replay must begin at the standard initial position"));
        return std::nullopt;
    }
    pack.m_mainlinePositions.reserve(pack.m_moves.size() + 1);
    pack.m_mainlinePositions.append(*current);
    QString positionId = pack.m_moves.first().notation.beforePositionId;
    QString replayStateId = pack.m_moves.first().notation.beforeReplayStateId;
    for (int index = 0; index < pack.m_moves.size(); ++index) {
        const ReplayMove &move = pack.m_moves.at(index);
        if (move.notation.beforeFen != canonicalFen(*current)
            || move.notation.beforePositionId != positionId
            || move.notation.beforeReplayStateId != replayStateId) {
            setError(errorMessage, QStringLiteral("annotated replay mainline disconnects before ply %1").arg(index + 1));
            return std::nullopt;
        }
        ChessPosition after;
        if (!validateNotationAgainstPosition(
                move.notation, *current, &after, errorMessage,
                QStringLiteral("moves[%1].notation").arg(index))) {
            return std::nullopt;
        }
        if (move.preferredVariation.has_value()
            && !validateVariation(
                *move.preferredVariation, move, pack.m_sourceGameId, pack.m_sourceRunId,
                pack.m_sourceEngineConfigId, static_cast<int>(displayedVariationPlies),
                errorMessage)) {
            return std::nullopt;
        }
        pack.m_mainlinePositions.append(after);
        current = after;
        positionId = move.notation.afterPositionId;
        replayStateId = move.notation.afterReplayStateId;
    }
    return pack;
}

const ReplayPreferredVariation *AnnotatedReplayPack::preferredVariation(int anchorPly) const
{
    if (anchorPly < 1 || anchorPly > m_moves.size()) {
        return nullptr;
    }
    const ReplayMove &move = m_moves.at(anchorPly - 1);
    return move.preferredVariation.has_value() ? &*move.preferredVariation : nullptr;
}

} // namespace parlawl::puzzle_runner
