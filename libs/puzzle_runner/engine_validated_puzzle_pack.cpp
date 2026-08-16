#include "engine_validated_puzzle_pack.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <limits>

#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QUrl>

#include "chess_position.h"

namespace parlawl::puzzle_runner {

namespace {

constexpr int kMaximumJsonDepth = 48;
constexpr int kMaximumSolutionPlies = 1024;
constexpr int kMaximumThemes = 256;
constexpr int kMaximumEvidenceRows = 512;
constexpr int kMaximumMapRows = 512;
constexpr qint64 kMaximumExactInteger = 9'007'199'254'740'991LL;

const QString kPuzzleSchema = QStringLiteral("esports-probability-lab/puzzle-candidate/v1");
const QString kPuzzleRecordType = QStringLiteral("puzzle_candidate");
const QString kValidatedStatus = QStringLiteral("engine_validated");

const QRegularExpression kUciPattern(QStringLiteral("^[a-h][1-8][a-h][1-8][qrbn]?$"));
const QRegularExpression kShaPattern(QStringLiteral("^[0-9a-f]{64}$"));
const QRegularExpression kPuzzleIdPattern(QStringLiteral("^puzzle-v1:[0-9a-f]{64}$"));
const QRegularExpression kRecordIdPattern(QStringLiteral("^puzzle-record-v1:[0-9a-f]{64}$"));
const QRegularExpression kFeatureKeyPattern(QStringLiteral("^[a-z][a-z0-9_.-]{0,63}$"));
const QRegularExpression kUtcPattern(
    QStringLiteral("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\\.[0-9]{6})?Z$"));

void setError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
}

bool pythonStringLess(const QString &left, const QString &right)
{
    const QList<uint> leftPoints = left.toUcs4();
    const QList<uint> rightPoints = right.toUcs4();
    return std::lexicographical_compare(
        leftPoints.cbegin(), leftPoints.cend(), rightPoints.cbegin(), rightPoints.cend());
}

struct JsonValue
{
    enum class Kind {
        Null,
        Boolean,
        Integer,
        Number,
        String,
        Array,
        Object,
    };

    Kind kind = Kind::Null;
    bool boolean = false;
    qint64 integer = 0;
    double number = 0.0;
    QString string;
    QVector<JsonValue> array;
    QVector<QString> objectKeys;
    QVector<JsonValue> objectValues;
};

const JsonValue *member(const JsonValue &object, const QString &key)
{
    if (object.kind != JsonValue::Kind::Object) {
        return nullptr;
    }
    for (int index = 0; index < object.objectKeys.size(); ++index) {
        if (object.objectKeys.at(index) == key) {
            return &object.objectValues.at(index);
        }
    }
    return nullptr;
}

JsonValue *member(JsonValue &object, const QString &key)
{
    if (object.kind != JsonValue::Kind::Object) {
        return nullptr;
    }
    for (int index = 0; index < object.objectKeys.size(); ++index) {
        if (object.objectKeys.at(index) == key) {
            return &object.objectValues[index];
        }
    }
    return nullptr;
}

void removeMember(JsonValue *object, const QString &key)
{
    if (object == nullptr || object->kind != JsonValue::Kind::Object) {
        return;
    }
    for (int index = 0; index < object->objectKeys.size(); ++index) {
        if (object->objectKeys.at(index) == key) {
            object->objectKeys.removeAt(index);
            object->objectValues.removeAt(index);
            return;
        }
    }
}

class StrictJsonParser
{
public:
    explicit StrictJsonParser(const QByteArray &input)
        : m_input(input)
    {
    }

    bool parse(JsonValue *output, QString *errorMessage)
    {
        skipWhitespace();
        if (!parseValue(0, output, errorMessage)) {
            return false;
        }
        skipWhitespace();
        if (m_offset != m_input.size()) {
            setError(errorMessage, QStringLiteral("JSON value has trailing material"));
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
                return;
            }
            ++m_offset;
        }
    }

    bool parseValue(int depth, JsonValue *output, QString *errorMessage)
    {
        if (depth > kMaximumJsonDepth) {
            setError(errorMessage, QStringLiteral("JSON exceeds the safe nesting depth"));
            return false;
        }
        skipWhitespace();
        if (m_offset >= m_input.size()) {
            setError(errorMessage, QStringLiteral("JSON value is incomplete"));
            return false;
        }
        const char token = m_input.at(m_offset);
        if (token == '{') {
            return parseObject(depth + 1, output, errorMessage);
        }
        if (token == '[') {
            return parseArray(depth + 1, output, errorMessage);
        }
        if (token == '"') {
            output->kind = JsonValue::Kind::String;
            return parseString(&output->string, errorMessage);
        }
        if (token == 't' || token == 'f') {
            output->kind = JsonValue::Kind::Boolean;
            if (token == 't' && consumeLiteral("true")) {
                output->boolean = true;
                return true;
            }
            if (token == 'f' && consumeLiteral("false")) {
                output->boolean = false;
                return true;
            }
            setError(errorMessage, QStringLiteral("JSON contains an invalid boolean"));
            return false;
        }
        if (token == 'n') {
            if (!consumeLiteral("null")) {
                setError(errorMessage, QStringLiteral("JSON contains an invalid null"));
                return false;
            }
            output->kind = JsonValue::Kind::Null;
            return true;
        }
        return parseNumber(output, errorMessage);
    }

    bool parseObject(int depth, JsonValue *output, QString *errorMessage)
    {
        output->kind = JsonValue::Kind::Object;
        ++m_offset;
        skipWhitespace();
        if (m_offset < m_input.size() && m_input.at(m_offset) == '}') {
            ++m_offset;
            return true;
        }

        QSet<QString> keys;
        while (m_offset < m_input.size()) {
            QString key;
            if (!parseString(&key, errorMessage)) {
                return false;
            }
            if (keys.contains(key)) {
                setError(errorMessage, QStringLiteral("JSON contains duplicate key '%1'").arg(key));
                return false;
            }
            keys.insert(key);
            skipWhitespace();
            if (m_offset >= m_input.size() || m_input.at(m_offset) != ':') {
                setError(errorMessage, QStringLiteral("JSON object is missing ':'"));
                return false;
            }
            ++m_offset;
            JsonValue value;
            if (!parseValue(depth, &value, errorMessage)) {
                return false;
            }
            output->objectKeys.append(key);
            output->objectValues.append(std::move(value));
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
            setError(errorMessage, QStringLiteral("JSON object is unterminated"));
            return false;
        }
        setError(errorMessage, QStringLiteral("JSON object is incomplete"));
        return false;
    }

    bool parseArray(int depth, JsonValue *output, QString *errorMessage)
    {
        output->kind = JsonValue::Kind::Array;
        ++m_offset;
        skipWhitespace();
        if (m_offset < m_input.size() && m_input.at(m_offset) == ']') {
            ++m_offset;
            return true;
        }
        while (m_offset < m_input.size()) {
            JsonValue value;
            if (!parseValue(depth, &value, errorMessage)) {
                return false;
            }
            output->array.append(std::move(value));
            skipWhitespace();
            if (m_offset < m_input.size() && m_input.at(m_offset) == ',') {
                ++m_offset;
                skipWhitespace();
                continue;
            }
            if (m_offset < m_input.size() && m_input.at(m_offset) == ']') {
                ++m_offset;
                return true;
            }
            setError(errorMessage, QStringLiteral("JSON array is unterminated"));
            return false;
        }
        setError(errorMessage, QStringLiteral("JSON array is incomplete"));
        return false;
    }

    bool parseString(QString *output, QString *errorMessage)
    {
        skipWhitespace();
        if (m_offset >= m_input.size() || m_input.at(m_offset) != '"') {
            setError(errorMessage, QStringLiteral("JSON expected a string"));
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
                            setError(errorMessage, QStringLiteral("JSON has an invalid Unicode escape"));
                            return false;
                        }
                    }
                } else if (!QByteArray("\"\\/bfnrt").contains(static_cast<char>(token))) {
                    setError(errorMessage, QStringLiteral("JSON has an invalid escape"));
                    return false;
                }
                escaped = false;
                continue;
            }
            if (token == '\\') {
                escaped = true;
                continue;
            }
            if (token == '"') {
                const QByteArray rawString = m_input.mid(begin, m_offset - begin);
                QJsonParseError parseError;
                const QJsonDocument decoded = QJsonDocument::fromJson(
                    QByteArray("[") + rawString + QByteArray("]"), &parseError);
                if (parseError.error != QJsonParseError::NoError || !decoded.isArray()
                    || decoded.array().size() != 1 || !decoded.array().first().isString()) {
                    setError(errorMessage, QStringLiteral("JSON string is invalid"));
                    return false;
                }
                *output = decoded.array().first().toString();
                return true;
            }
            if (token < 0x20) {
                setError(errorMessage, QStringLiteral("JSON string contains a control byte"));
                return false;
            }
        }
        setError(errorMessage, QStringLiteral("JSON string is unterminated"));
        return false;
    }

    bool parseNumber(JsonValue *output, QString *errorMessage)
    {
        const qsizetype begin = m_offset;
        if (m_offset < m_input.size() && m_input.at(m_offset) == '-') {
            ++m_offset;
        }
        if (m_offset >= m_input.size()) {
            setError(errorMessage, QStringLiteral("JSON number is incomplete"));
            return false;
        }
        if (m_input.at(m_offset) == '0') {
            ++m_offset;
            if (m_offset < m_input.size() && m_input.at(m_offset) >= '0' && m_input.at(m_offset) <= '9') {
                setError(errorMessage, QStringLiteral("JSON number has a leading zero"));
                return false;
            }
        } else if (m_input.at(m_offset) >= '1' && m_input.at(m_offset) <= '9') {
            while (m_offset < m_input.size() && m_input.at(m_offset) >= '0' && m_input.at(m_offset) <= '9') {
                ++m_offset;
            }
        } else {
            setError(errorMessage, QStringLiteral("JSON contains an invalid number"));
            return false;
        }

        bool floating = false;
        if (m_offset < m_input.size() && m_input.at(m_offset) == '.') {
            floating = true;
            ++m_offset;
            const qsizetype digitsBegin = m_offset;
            while (m_offset < m_input.size() && m_input.at(m_offset) >= '0' && m_input.at(m_offset) <= '9') {
                ++m_offset;
            }
            if (m_offset == digitsBegin) {
                setError(errorMessage, QStringLiteral("JSON number has an empty fraction"));
                return false;
            }
        }
        if (m_offset < m_input.size() && (m_input.at(m_offset) == 'e' || m_input.at(m_offset) == 'E')) {
            floating = true;
            ++m_offset;
            if (m_offset < m_input.size() && (m_input.at(m_offset) == '+' || m_input.at(m_offset) == '-')) {
                ++m_offset;
            }
            const qsizetype digitsBegin = m_offset;
            while (m_offset < m_input.size() && m_input.at(m_offset) >= '0' && m_input.at(m_offset) <= '9') {
                ++m_offset;
            }
            if (m_offset == digitsBegin) {
                setError(errorMessage, QStringLiteral("JSON number has an empty exponent"));
                return false;
            }
        }

        const QByteArray token = m_input.mid(begin, m_offset - begin);
        if (!floating) {
            bool ok = false;
            const qint64 number = token.toLongLong(&ok);
            if (!ok || std::abs(static_cast<long double>(number)) > kMaximumExactInteger) {
                setError(errorMessage, QStringLiteral("JSON integer exceeds the exact interchange bound"));
                return false;
            }
            output->kind = JsonValue::Kind::Integer;
            output->integer = number;
            return true;
        }

        bool ok = false;
        const double number = token.toDouble(&ok);
        if (!ok || !std::isfinite(number)) {
            setError(errorMessage, QStringLiteral("JSON number must be finite"));
            return false;
        }
        output->kind = JsonValue::Kind::Number;
        output->number = number;
        return true;
    }

    bool consumeLiteral(const char *literal)
    {
        const QByteArray expected(literal);
        if (m_input.mid(m_offset, expected.size()) != expected) {
            return false;
        }
        m_offset += expected.size();
        return true;
    }

    const QByteArray &m_input;
    qsizetype m_offset = 0;
};

void appendJsonString(QByteArray *output, const QString &value)
{
    output->append('"');
    for (qsizetype index = 0; index < value.size(); ++index) {
        const ushort code = value.at(index).unicode();
        switch (code) {
        case '"':
            output->append("\\\"");
            break;
        case '\\':
            output->append("\\\\");
            break;
        case '\b':
            output->append("\\b");
            break;
        case '\f':
            output->append("\\f");
            break;
        case '\n':
            output->append("\\n");
            break;
        case '\r':
            output->append("\\r");
            break;
        case '\t':
            output->append("\\t");
            break;
        default:
            if (code < 0x20) {
                output->append("\\u");
                output->append(QByteArray::number(code, 16).rightJustified(4, '0'));
            } else if (QChar::isHighSurrogate(code) && index + 1 < value.size()
                       && QChar::isLowSurrogate(value.at(index + 1).unicode())) {
                QString pair;
                pair.append(value.at(index));
                pair.append(value.at(++index));
                output->append(pair.toUtf8());
            } else {
                output->append(QString(value.at(index)).toUtf8());
            }
            break;
        }
    }
    output->append('"');
}

QByteArray pythonFloat(double value)
{
    if (value == 0.0) {
        return std::signbit(value) ? QByteArray("-0.0") : QByteArray("0.0");
    }

    char buffer[128] {};
    const auto converted = std::to_chars(
        std::begin(buffer), std::end(buffer), value, std::chars_format::general);
    if (converted.ec != std::errc {}) {
        return {};
    }
    QByteArray shortest(buffer, converted.ptr - buffer);
    bool negative = false;
    if (shortest.startsWith('-')) {
        negative = true;
        shortest.remove(0, 1);
    }

    int explicitExponent = 0;
    const int exponentIndex = std::max(shortest.indexOf('e'), shortest.indexOf('E'));
    QByteArray mantissa = shortest;
    if (exponentIndex >= 0) {
        bool exponentOk = false;
        explicitExponent = shortest.mid(exponentIndex + 1).toInt(&exponentOk);
        if (!exponentOk) {
            return {};
        }
        mantissa = shortest.left(exponentIndex);
    }
    const int dotIndex = mantissa.indexOf('.');
    const int originalDecimalPosition = dotIndex >= 0 ? dotIndex : mantissa.size();
    QByteArray digits = mantissa;
    digits.replace(".", "");
    int firstNonzero = 0;
    while (firstNonzero < digits.size() && digits.at(firstNonzero) == '0') {
        ++firstNonzero;
    }
    if (firstNonzero >= digits.size()) {
        return negative ? QByteArray("-0.0") : QByteArray("0.0");
    }
    const int scientificExponent = explicitExponent + originalDecimalPosition - firstNonzero - 1;
    digits.remove(0, firstNonzero);
    while (digits.size() > 1 && digits.endsWith('0')) {
        digits.chop(1);
    }

    QByteArray formatted;
    if (scientificExponent >= -4 && scientificExponent < 16) {
        const int decimalPosition = scientificExponent + 1;
        if (decimalPosition <= 0) {
            formatted = "0." + QByteArray(-decimalPosition, '0') + digits;
        } else if (decimalPosition >= digits.size()) {
            formatted = digits + QByteArray(decimalPosition - digits.size(), '0') + ".0";
        } else {
            formatted = digits.left(decimalPosition) + "." + digits.mid(decimalPosition);
        }
    } else {
        formatted = digits.left(1);
        if (digits.size() > 1) {
            formatted += "." + digits.mid(1);
        }
        formatted += scientificExponent < 0 ? "e-" : "e+";
        formatted += QByteArray::number(std::abs(scientificExponent)).rightJustified(2, '0');
    }
    return negative ? QByteArray("-") + formatted : formatted;
}

void appendCanonicalJson(QByteArray *output, const JsonValue &value)
{
    switch (value.kind) {
    case JsonValue::Kind::Null:
        output->append("null");
        return;
    case JsonValue::Kind::Boolean:
        output->append(value.boolean ? "true" : "false");
        return;
    case JsonValue::Kind::Integer:
        output->append(QByteArray::number(value.integer));
        return;
    case JsonValue::Kind::Number:
        output->append(pythonFloat(value.number));
        return;
    case JsonValue::Kind::String:
        appendJsonString(output, value.string);
        return;
    case JsonValue::Kind::Array:
        output->append('[');
        for (int index = 0; index < value.array.size(); ++index) {
            if (index > 0) {
                output->append(',');
            }
            appendCanonicalJson(output, value.array.at(index));
        }
        output->append(']');
        return;
    case JsonValue::Kind::Object:
        break;
    }

    QVector<int> indices;
    indices.reserve(value.objectKeys.size());
    for (int index = 0; index < value.objectKeys.size(); ++index) {
        indices.append(index);
    }
    std::sort(indices.begin(), indices.end(), [&](int left, int right) {
        return pythonStringLess(value.objectKeys.at(left), value.objectKeys.at(right));
    });
    output->append('{');
    for (int outputIndex = 0; outputIndex < indices.size(); ++outputIndex) {
        if (outputIndex > 0) {
            output->append(',');
        }
        const int index = indices.at(outputIndex);
        appendJsonString(output, value.objectKeys.at(index));
        output->append(':');
        appendCanonicalJson(output, value.objectValues.at(index));
    }
    output->append('}');
}

QByteArray canonicalJson(const JsonValue &value)
{
    QByteArray output;
    appendCanonicalJson(&output, value);
    return output;
}

QString semanticId(const QString &prefix, const JsonValue &value)
{
    return prefix + QLatin1Char(':')
        + QString::fromLatin1(QCryptographicHash::hash(canonicalJson(value), QCryptographicHash::Sha256).toHex());
}

bool exactKeys(
    const JsonValue &object,
    std::initializer_list<const char *> expected,
    const QString &context,
    QString *errorMessage)
{
    if (object.kind != JsonValue::Kind::Object) {
        setError(errorMessage, context + QStringLiteral(" must be an object"));
        return false;
    }
    QSet<QString> expectedKeys;
    for (const char *key : expected) {
        expectedKeys.insert(QString::fromLatin1(key));
    }
    QSet<QString> actualKeys;
    for (const QString &key : object.objectKeys) {
        actualKeys.insert(key);
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
            .arg(context, missing.join(QLatin1Char(',')), unknown.join(QLatin1Char(','))));
    return false;
}

bool requiredString(
    const JsonValue &object,
    const QString &key,
    QString *output,
    const QString &context,
    QString *errorMessage,
    bool allowEmpty = false)
{
    const JsonValue *value = member(object, key);
    if (value == nullptr || value->kind != JsonValue::Kind::String) {
        setError(errorMessage, QStringLiteral("%1.%2 must be text").arg(context, key));
        return false;
    }
    if ((!allowEmpty && value->string.trimmed().isEmpty()) || value->string.contains(QChar::Null)) {
        setError(errorMessage, QStringLiteral("%1.%2 is empty or contains NUL").arg(context, key));
        return false;
    }
    *output = value->string;
    return true;
}

bool requiredInteger(
    const JsonValue &object,
    const QString &key,
    qint64 minimum,
    qint64 maximum,
    qint64 *output,
    const QString &context,
    QString *errorMessage)
{
    const JsonValue *value = member(object, key);
    if (value == nullptr || value->kind != JsonValue::Kind::Integer
        || value->integer < minimum || value->integer > maximum) {
        setError(errorMessage, QStringLiteral("%1.%2 must be an integer in range").arg(context, key));
        return false;
    }
    *output = value->integer;
    return true;
}

bool validateTextMap(const JsonValue &value, const QString &context, QString *errorMessage)
{
    if (value.kind != JsonValue::Kind::Object || value.objectKeys.size() > kMaximumMapRows) {
        setError(errorMessage, context + QStringLiteral(" must be a bounded object"));
        return false;
    }
    for (int index = 0; index < value.objectKeys.size(); ++index) {
        const QString &key = value.objectKeys.at(index);
        const JsonValue &item = value.objectValues.at(index);
        if (key.trimmed().isEmpty() || key != key.trimmed() || key.contains(QChar::Null)
            || item.kind != JsonValue::Kind::String || item.string.contains(QChar::Null)) {
            setError(errorMessage, context + QStringLiteral(" must contain normalized text keys and text values"));
            return false;
        }
    }
    return true;
}

bool validateEvidenceArray(
    const JsonValue &value,
    const QString &context,
    bool allowEmpty,
    QString *errorMessage)
{
    if (value.kind != JsonValue::Kind::Array
        || (!allowEmpty && value.array.isEmpty())
        || value.array.size() > kMaximumEvidenceRows) {
        setError(errorMessage, context + QStringLiteral(" must be a bounded evidence array"));
        return false;
    }
    QByteArray prior;
    for (int index = 0; index < value.array.size(); ++index) {
        const JsonValue &evidence = value.array.at(index);
        const QString itemContext = QStringLiteral("%1[%2]").arg(context).arg(index);
        if (!exactKeys(evidence, {"details", "evidence_type", "reference_id"}, itemContext, errorMessage)) {
            return false;
        }
        QString evidenceType;
        QString referenceId;
        if (!requiredString(evidence, QStringLiteral("evidence_type"), &evidenceType, itemContext, errorMessage)
            || !requiredString(evidence, QStringLiteral("reference_id"), &referenceId, itemContext, errorMessage)
            || evidenceType != evidenceType.trimmed() || referenceId != referenceId.trimmed()) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = itemContext + QStringLiteral(" identifiers must be normalized");
            }
            return false;
        }
        const JsonValue *details = member(evidence, QStringLiteral("details"));
        if (details == nullptr || !validateTextMap(*details, itemContext + QStringLiteral(".details"), errorMessage)) {
            return false;
        }
        const QByteArray canonical = canonicalJson(evidence);
        if (!prior.isEmpty() && canonical <= prior) {
            setError(errorMessage, context + QStringLiteral(" must be sorted and unique"));
            return false;
        }
        prior = canonical;
    }
    return true;
}

bool validateUtc(const JsonValue &value, const QString &context, bool nullable, QString *errorMessage)
{
    if (nullable && value.kind == JsonValue::Kind::Null) {
        return true;
    }
    if (value.kind != JsonValue::Kind::String || !kUtcPattern.match(value.string).hasMatch()) {
        setError(errorMessage, context + QStringLiteral(" must be canonical ISO-8601 UTC text"));
        return false;
    }
    const qsizetype fractionMarker = value.string.indexOf(QLatin1Char('.'));
    if (fractionMarker >= 0
        && value.string.mid(fractionMarker + 1, 6) == QStringLiteral("000000")) {
        setError(errorMessage, context + QStringLiteral(" must omit a zero microsecond fraction"));
        return false;
    }
    const QDateTime parsed = QDateTime::fromString(value.string, Qt::ISODateWithMs);
    if (!parsed.isValid() || parsed.offsetFromUtc() != 0) {
        setError(errorMessage, context + QStringLiteral(" is not a valid UTC instant"));
        return false;
    }
    return true;
}

bool validateSourceGame(
    const JsonValue &source,
    QString *provider,
    QString *gameId,
    QString *whiteName,
    QString *blackName,
    QString *errorMessage)
{
    if (!exactKeys(
            source,
            {"acquired_at_utc", "acquisition_id", "acquisition_sha256", "game_id", "game_played_at_utc", "metadata", "provider", "source_ply", "source_url"},
            QStringLiteral("source_game"), errorMessage)) {
        return false;
    }
    QString acquisitionId;
    QString acquisitionSha;
    qint64 sourcePly = 0;
    if (!requiredString(source, QStringLiteral("provider"), provider, QStringLiteral("source_game"), errorMessage)
        || !requiredString(source, QStringLiteral("game_id"), gameId, QStringLiteral("source_game"), errorMessage)
        || !requiredString(source, QStringLiteral("acquisition_id"), &acquisitionId, QStringLiteral("source_game"), errorMessage)
        || !requiredString(source, QStringLiteral("acquisition_sha256"), &acquisitionSha, QStringLiteral("source_game"), errorMessage)
        || *provider != provider->trimmed() || *gameId != gameId->trimmed()
        || acquisitionId != acquisitionId.trimmed()
        || !kShaPattern.match(acquisitionSha).hasMatch()
        || !requiredInteger(source, QStringLiteral("source_ply"), 0, kMaximumExactInteger, &sourcePly, QStringLiteral("source_game"), errorMessage)) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("source_game identifiers are invalid");
        }
        return false;
    }
    const JsonValue *acquired = member(source, QStringLiteral("acquired_at_utc"));
    const JsonValue *played = member(source, QStringLiteral("game_played_at_utc"));
    const JsonValue *metadata = member(source, QStringLiteral("metadata"));
    const JsonValue *urlValue = member(source, QStringLiteral("source_url"));
    if (acquired == nullptr || played == nullptr || metadata == nullptr || urlValue == nullptr
        || !validateUtc(*acquired, QStringLiteral("source_game.acquired_at_utc"), false, errorMessage)
        || !validateUtc(*played, QStringLiteral("source_game.game_played_at_utc"), true, errorMessage)
        || !validateTextMap(*metadata, QStringLiteral("source_game.metadata"), errorMessage)) {
        return false;
    }
    if (urlValue->kind != JsonValue::Kind::Null) {
        if (urlValue->kind != JsonValue::Kind::String || urlValue->string.trimmed().isEmpty()
            || urlValue->string != urlValue->string.trimmed()
            || urlValue->string.contains(QLatin1Char('?')) || urlValue->string.contains(QLatin1Char('#'))) {
            setError(errorMessage, QStringLiteral("source_game.source_url must be a query-free absolute URL or null"));
            return false;
        }
        const QUrl url(urlValue->string, QUrl::StrictMode);
        if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
            || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()) {
            setError(errorMessage, QStringLiteral("source_game.source_url is not a credential-free HTTP(S) URL"));
            return false;
        }
    }
    if (const JsonValue *white = member(*metadata, QStringLiteral("white")); white != nullptr) {
        *whiteName = white->string;
    }
    if (const JsonValue *black = member(*metadata, QStringLiteral("black")); black != nullptr) {
        *blackName = black->string;
    }
    return true;
}

bool validateEngineEvidence(
    const JsonValue &engine,
    QString *engineName,
    QString *engineRunId,
    QString *errorMessage)
{
    if (!exactKeys(
            engine,
            {"adapter_version", "analysis_parameters", "engine_binary_sha256", "engine_name", "run_id", "source_position_index"},
            QStringLiteral("engine_evidence"), errorMessage)) {
        return false;
    }
    QString adapterVersion;
    QString binarySha;
    qint64 sourcePosition = 0;
    if (!requiredString(engine, QStringLiteral("run_id"), engineRunId, QStringLiteral("engine_evidence"), errorMessage)
        || !requiredString(engine, QStringLiteral("engine_name"), engineName, QStringLiteral("engine_evidence"), errorMessage)
        || !requiredString(engine, QStringLiteral("engine_binary_sha256"), &binarySha, QStringLiteral("engine_evidence"), errorMessage)
        || !requiredString(engine, QStringLiteral("adapter_version"), &adapterVersion, QStringLiteral("engine_evidence"), errorMessage)
        || *engineRunId != engineRunId->trimmed() || *engineName != engineName->trimmed()
        || adapterVersion != adapterVersion.trimmed() || !kShaPattern.match(binarySha).hasMatch()
        || !requiredInteger(engine, QStringLiteral("source_position_index"), 0, kMaximumExactInteger, &sourcePosition, QStringLiteral("engine_evidence"), errorMessage)) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("engine_evidence identifiers are invalid");
        }
        return false;
    }
    const JsonValue *parameters = member(engine, QStringLiteral("analysis_parameters"));
    return parameters != nullptr
        && validateTextMap(*parameters, QStringLiteral("engine_evidence.analysis_parameters"), errorMessage);
}

const JsonValue *requiredProfileText(
    const JsonValue &map,
    const QString &key,
    const QString &context,
    QString *errorMessage)
{
    const JsonValue *value = member(map, key);
    if (value == nullptr || value->kind != JsonValue::Kind::String
        || value->string.trimmed().isEmpty()) {
        setError(
            errorMessage,
            QStringLiteral("%1.%2 is required by the tactical_deep_validation profile")
                .arg(context, key));
        return nullptr;
    }
    return value;
}

bool validateKnownTacticalProfile(
    const JsonValue &source,
    const JsonValue &engine,
    const JsonValue &validation,
    const JsonValue &solution,
    const ChessPosition &initialPosition,
    QString *errorMessage)
{
    const JsonValue *tacticalEvidence = nullptr;
    for (const JsonValue &entry : validation.array) {
        const JsonValue *evidenceType = member(entry, QStringLiteral("evidence_type"));
        if (evidenceType == nullptr
            || evidenceType->string != QStringLiteral("tactical_deep_validation")) {
            continue;
        }
        if (tacticalEvidence != nullptr) {
            setError(
                errorMessage,
                QStringLiteral("validation_evidence contains multiple tactical_deep_validation rows"));
            return false;
        }
        tacticalEvidence = &entry;
    }
    if (tacticalEvidence == nullptr) {
        return true;
    }

    const JsonValue *metadata = member(source, QStringLiteral("metadata"));
    const JsonValue *parameters = member(engine, QStringLiteral("analysis_parameters"));
    const JsonValue *details = member(*tacticalEvidence, QStringLiteral("details"));
    const JsonValue *sourcePly = member(source, QStringLiteral("source_ply"));
    const JsonValue *sourcePosition = member(engine, QStringLiteral("source_position_index"));
    const JsonValue *engineRunId = member(engine, QStringLiteral("run_id"));
    const JsonValue *referenceId = member(*tacticalEvidence, QStringLiteral("reference_id"));
    if (metadata == nullptr || parameters == nullptr || details == nullptr
        || sourcePly == nullptr || sourcePosition == nullptr
        || engineRunId == nullptr || referenceId == nullptr) {
        setError(errorMessage, QStringLiteral("tactical_deep_validation profile is incomplete"));
        return false;
    }

    const JsonValue *candidatePly = requiredProfileText(
        *metadata,
        QStringLiteral("tactical_candidate_ply_one_based"),
        QStringLiteral("source_game.metadata"),
        errorMessage);
    const JsonValue *candidateId = requiredProfileText(
        *metadata,
        QStringLiteral("tactical_candidate_id"),
        QStringLiteral("source_game.metadata"),
        errorMessage);
    const JsonValue *parameterBest = requiredProfileText(
        *parameters,
        QStringLiteral("deep_best_move_uci"),
        QStringLiteral("engine_evidence.analysis_parameters"),
        errorMessage);
    const JsonValue *parameterPv = requiredProfileText(
        *parameters,
        QStringLiteral("deep_pv_uci"),
        QStringLiteral("engine_evidence.analysis_parameters"),
        errorMessage);
    const JsonValue *detailBest = requiredProfileText(
        *details,
        QStringLiteral("deep_best_move_uci"),
        QStringLiteral("tactical_deep_validation.details"),
        errorMessage);
    const JsonValue *detailRunId = requiredProfileText(
        *details,
        QStringLiteral("deep_run_id"),
        QStringLiteral("tactical_deep_validation.details"),
        errorMessage);
    if (candidatePly == nullptr || candidateId == nullptr || parameterBest == nullptr
        || parameterPv == nullptr || detailBest == nullptr || detailRunId == nullptr) {
        return false;
    }

    bool plyOk = false;
    const qint64 oneBasedPly = candidatePly->string.toLongLong(&plyOk);
    if (sourcePly->integer != sourcePosition->integer || !plyOk || oneBasedPly < 1
        || oneBasedPly > kMaximumExactInteger
        || candidatePly->string != QString::number(oneBasedPly)
        || oneBasedPly - 1 != sourcePly->integer) {
        setError(
            errorMessage,
            QStringLiteral(
                "tactical profile position cross-link disagrees across source_ply, source_position_index, and tactical_candidate_ply_one_based"));
        return false;
    }

    QStringList solutionMoves;
    solutionMoves.reserve(solution.array.size());
    for (const JsonValue &move : solution.array) {
        solutionMoves.append(move.string);
    }
    const QStringList pvMoves = parameterPv->string.simplified().split(
        QLatin1Char(' '), Qt::SkipEmptyParts);
    if (solutionMoves.isEmpty() || parameterBest->string != solutionMoves.first()
        || detailBest->string != solutionMoves.first() || pvMoves != solutionMoves) {
        setError(
            errorMessage,
            QStringLiteral(
                "tactical profile solution cross-link disagrees with deep_best_move_uci or deep_pv_uci"));
        return false;
    }
    if (candidateId->string != referenceId->string) {
        setError(
            errorMessage,
            QStringLiteral("tactical profile candidate ID disagrees with validation reference_id"));
        return false;
    }
    if (engineRunId->string != detailRunId->string) {
        setError(
            errorMessage,
            QStringLiteral("tactical profile engine run ID disagrees with validation deep_run_id"));
        return false;
    }

    if (const JsonValue *playedMove = member(*details, QStringLiteral("played_move_uci"));
        playedMove != nullptr) {
        const auto parsedPlayedMove = playedMove->kind == JsonValue::Kind::String
            ? Move::fromUci(playedMove->string)
            : std::nullopt;
        if (!parsedPlayedMove.has_value()
            || !kUciPattern.match(playedMove->string).hasMatch()
            || playedMove->string == parameterBest->string
            || initialPosition.pieceAt(parsedPlayedMove->to).type == PieceType::King
            || !initialPosition.isLegalMove(*parsedPlayedMove)) {
            setError(
                errorMessage,
                QStringLiteral(
                    "tactical profile played_move_uci must be canonical, legal from fen, and differ from the best move"));
            return false;
        }
    }
    return true;
}

bool validateFen(
    const QString &fen,
    const QString &sideToMove,
    ChessPosition *position,
    QString *errorMessage)
{
    if (fen != fen.trimmed() || fen.contains(QStringLiteral("  "))) {
        setError(errorMessage, QStringLiteral("fen must be normalized text"));
        return false;
    }
    QString fenError;
    const auto parsed = ChessPosition::fromFen(fen, &fenError);
    if (!parsed.has_value() || parsed->toFen() != fen) {
        setError(errorMessage, QStringLiteral("fen is not a canonical ParlAWL position: %1").arg(fenError));
        return false;
    }
    const QStringList fields = fen.split(QLatin1Char(' '));
    bool halfmoveOk = false;
    bool fullmoveOk = false;
    const qint64 halfmove = fields.value(4).toLongLong(&halfmoveOk);
    const qint64 fullmove = fields.value(5).toLongLong(&fullmoveOk);
    if (!halfmoveOk || !fullmoveOk || halfmove < 0 || fullmove < 1
        || halfmove > kMaximumExactInteger || fullmove > kMaximumExactInteger) {
        setError(errorMessage, QStringLiteral("fen move counters are invalid"));
        return false;
    }
    const QString expectedSide = parsed->sideToMove() == PieceColor::White
        ? QStringLiteral("white") : QStringLiteral("black");
    if (sideToMove != expectedSide) {
        setError(errorMessage, QStringLiteral("side_to_move disagrees with fen"));
        return false;
    }

    int whiteKings = 0;
    int blackKings = 0;
    for (int square = 0; square < 64; ++square) {
        const Piece piece = parsed->pieceAt(square);
        if (piece.type == PieceType::King && piece.color == PieceColor::White) {
            ++whiteKings;
        } else if (piece.type == PieceType::King && piece.color == PieceColor::Black) {
            ++blackKings;
        }
        if (piece.type == PieceType::Pawn
            && (ChessPosition::rankOf(square) == 0 || ChessPosition::rankOf(square) == 7)) {
            setError(errorMessage, QStringLiteral("fen places a pawn on a promotion rank"));
            return false;
        }
    }
    if (whiteKings != 1 || blackKings != 1) {
        setError(errorMessage, QStringLiteral("fen must contain exactly one king per side"));
        return false;
    }

    const QString rights = fields.at(2);
    const auto rightHasPieces = [&](QChar right, const QString &kingName, const QString &rookName, PieceColor color) {
        if (!rights.contains(right)) {
            return true;
        }
        const Piece king = parsed->pieceAt(ChessPosition::squareFromName(kingName));
        const Piece rook = parsed->pieceAt(ChessPosition::squareFromName(rookName));
        return king.type == PieceType::King && king.color == color
            && rook.type == PieceType::Rook && rook.color == color;
    };
    if (!rightHasPieces(QLatin1Char('K'), QStringLiteral("e1"), QStringLiteral("h1"), PieceColor::White)
        || !rightHasPieces(QLatin1Char('Q'), QStringLiteral("e1"), QStringLiteral("a1"), PieceColor::White)
        || !rightHasPieces(QLatin1Char('k'), QStringLiteral("e8"), QStringLiteral("h8"), PieceColor::Black)
        || !rightHasPieces(QLatin1Char('q'), QStringLiteral("e8"), QStringLiteral("a8"), PieceColor::Black)) {
        setError(errorMessage, QStringLiteral("fen castling rights lack their king or rook"));
        return false;
    }
    if (parsed->isInCheck(ChessPosition::opposite(parsed->sideToMove()))) {
        setError(errorMessage, QStringLiteral("fen leaves the side that just moved in check"));
        return false;
    }
    *position = *parsed;
    return true;
}

bool validateThemes(
    const JsonValue &themes,
    const JsonValue &themeEvidence,
    QStringList *output,
    QString *errorMessage)
{
    if (themes.kind != JsonValue::Kind::Array || themes.array.size() > kMaximumThemes
        || themeEvidence.kind != JsonValue::Kind::Object || themeEvidence.objectKeys.size() > kMaximumThemes) {
        setError(errorMessage, QStringLiteral("themes and theme_evidence must be bounded"));
        return false;
    }
    QString prior;
    QSet<QString> declared;
    for (int index = 0; index < themes.array.size(); ++index) {
        const JsonValue &theme = themes.array.at(index);
        if (theme.kind != JsonValue::Kind::String || theme.string.trimmed().isEmpty()
            || theme.string != theme.string.trimmed()
            || (!prior.isEmpty() && !pythonStringLess(prior, theme.string))) {
            setError(errorMessage, QStringLiteral("themes must be normalized, sorted, and unique text"));
            return false;
        }
        prior = theme.string;
        declared.insert(theme.string);
        output->append(theme.string);
    }
    for (int index = 0; index < themeEvidence.objectKeys.size(); ++index) {
        const QString &theme = themeEvidence.objectKeys.at(index);
        if (!declared.contains(theme)
            || !validateEvidenceArray(
                themeEvidence.objectValues.at(index),
                QStringLiteral("theme_evidence[%1]").arg(theme), true, errorMessage)) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("theme_evidence key is not declared in themes");
            }
            return false;
        }
    }
    return true;
}

bool validateDifficultyFeatures(
    const JsonValue &features,
    int solutionPlies,
    QString *errorMessage)
{
    if (features.kind != JsonValue::Kind::Object || features.objectKeys.isEmpty()
        || features.objectKeys.size() > kMaximumMapRows) {
        setError(errorMessage, QStringLiteral("difficulty_features must be a bounded object"));
        return false;
    }
    static const QSet<QString> forbidden{
        QStringLiteral("difficulty"),
        QStringLiteral("human_difficulty"),
        QStringLiteral("human_rating"),
        QStringLiteral("rating"),
        QStringLiteral("solve_probability"),
    };
    bool foundSolutionPlies = false;
    for (int index = 0; index < features.objectKeys.size(); ++index) {
        const QString &key = features.objectKeys.at(index);
        const JsonValue &value = features.objectValues.at(index);
        if (!kFeatureKeyPattern.match(key).hasMatch() || forbidden.contains(key)
            || value.kind != JsonValue::Kind::Number || !std::isfinite(value.number)) {
            setError(errorMessage, QStringLiteral("difficulty_features contains an invalid key or non-float value"));
            return false;
        }
        if (key == QStringLiteral("solution_plies")) {
            foundSolutionPlies = true;
            if (value.number != static_cast<double>(solutionPlies)) {
                setError(errorMessage, QStringLiteral("difficulty_features.solution_plies disagrees with the solution"));
                return false;
            }
        }
    }
    if (!foundSolutionPlies) {
        setError(errorMessage, QStringLiteral("difficulty_features must include solution_plies"));
        return false;
    }
    return true;
}

std::optional<PuzzleDefinition> parseRecord(
    const QByteArray &line,
    int lineNumber,
    QString *recordId,
    QString *errorMessage)
{
    JsonValue root;
    QString parseError;
    StrictJsonParser parser(line);
    if (!parser.parse(&root, &parseError)) {
        setError(errorMessage, QStringLiteral("puzzle JSONL line %1: %2").arg(lineNumber).arg(parseError));
        return std::nullopt;
    }
    if (!exactKeys(
            root,
            {"difficulty_features", "engine_evidence", "fen", "puzzle_id", "record_id", "record_type", "schema", "side_to_move", "solution_uci", "source_game", "status", "theme_evidence", "themes", "validation_evidence"},
            QStringLiteral("puzzle JSONL line %1").arg(lineNumber), errorMessage)) {
        return std::nullopt;
    }

    QString schema;
    QString recordType;
    QString puzzleId;
    QString status;
    QString fen;
    QString sideToMove;
    if (!requiredString(root, QStringLiteral("schema"), &schema, QStringLiteral("puzzle record"), errorMessage)
        || !requiredString(root, QStringLiteral("record_type"), &recordType, QStringLiteral("puzzle record"), errorMessage)
        || !requiredString(root, QStringLiteral("record_id"), recordId, QStringLiteral("puzzle record"), errorMessage)
        || !requiredString(root, QStringLiteral("puzzle_id"), &puzzleId, QStringLiteral("puzzle record"), errorMessage)
        || !requiredString(root, QStringLiteral("status"), &status, QStringLiteral("puzzle record"), errorMessage)
        || !requiredString(root, QStringLiteral("fen"), &fen, QStringLiteral("puzzle record"), errorMessage)
        || !requiredString(root, QStringLiteral("side_to_move"), &sideToMove, QStringLiteral("puzzle record"), errorMessage)) {
        return std::nullopt;
    }
    if (schema != kPuzzleSchema || recordType != kPuzzleRecordType) {
        setError(errorMessage, QStringLiteral("puzzle record uses an unsupported schema or record_type"));
        return std::nullopt;
    }
    if (status != kValidatedStatus) {
        setError(errorMessage, QStringLiteral("puzzle record is not engine_validated"));
        return std::nullopt;
    }
    if (!kPuzzleIdPattern.match(puzzleId).hasMatch() || !kRecordIdPattern.match(*recordId).hasMatch()) {
        setError(errorMessage, QStringLiteral("puzzle record identities have invalid syntax"));
        return std::nullopt;
    }
    if (sideToMove != QStringLiteral("white") && sideToMove != QStringLiteral("black")) {
        setError(errorMessage, QStringLiteral("side_to_move must be white or black"));
        return std::nullopt;
    }

    const JsonValue *source = member(root, QStringLiteral("source_game"));
    const JsonValue *engine = member(root, QStringLiteral("engine_evidence"));
    const JsonValue *validation = member(root, QStringLiteral("validation_evidence"));
    const JsonValue *themes = member(root, QStringLiteral("themes"));
    const JsonValue *themeEvidence = member(root, QStringLiteral("theme_evidence"));
    const JsonValue *solution = member(root, QStringLiteral("solution_uci"));
    const JsonValue *difficulty = member(root, QStringLiteral("difficulty_features"));
    if (source == nullptr || engine == nullptr || validation == nullptr || themes == nullptr
        || themeEvidence == nullptr || solution == nullptr || difficulty == nullptr
        || engine->kind == JsonValue::Kind::Null) {
        setError(errorMessage, QStringLiteral("engine_validated record is missing its evidence objects"));
        return std::nullopt;
    }

    QString provider;
    QString sourceGameId;
    QString whiteName;
    QString blackName;
    QString engineName;
    QString engineRunId;
    if (!validateSourceGame(*source, &provider, &sourceGameId, &whiteName, &blackName, errorMessage)
        || !validateEngineEvidence(*engine, &engineName, &engineRunId, errorMessage)
        || !validateEvidenceArray(*validation, QStringLiteral("validation_evidence"), false, errorMessage)) {
        return std::nullopt;
    }

    QStringList themeNames;
    if (!validateThemes(*themes, *themeEvidence, &themeNames, errorMessage)) {
        return std::nullopt;
    }
    if (solution->kind != JsonValue::Kind::Array || solution->array.isEmpty()
        || solution->array.size() > kMaximumSolutionPlies) {
        setError(errorMessage, QStringLiteral("solution_uci must contain 1..1024 plies"));
        return std::nullopt;
    }
    if (!validateDifficultyFeatures(*difficulty, solution->array.size(), errorMessage)) {
        return std::nullopt;
    }
    ChessPosition current;
    if (!validateFen(fen, sideToMove, &current, errorMessage)) {
        return std::nullopt;
    }
    if (!validateKnownTacticalProfile(
            *source, *engine, *validation, *solution, current, errorMessage)) {
        return std::nullopt;
    }
    QStringList solutionMoves;
    solutionMoves.reserve(solution->array.size());
    for (int index = 0; index < solution->array.size(); ++index) {
        const JsonValue &moveValue = solution->array.at(index);
        if (moveValue.kind != JsonValue::Kind::String
            || !kUciPattern.match(moveValue.string).hasMatch()
            || moveValue.string.left(2) == moveValue.string.mid(2, 2)) {
            setError(errorMessage, QStringLiteral("solution_uci[%1] is not canonical UCI").arg(index));
            return std::nullopt;
        }
        const auto move = Move::fromUci(moveValue.string);
        if (!move.has_value() || current.pieceAt(move->to).type == PieceType::King
            || !current.isLegalMove(*move) || !current.applyMove(*move)) {
            setError(errorMessage, QStringLiteral("solution_uci[%1] is not legal from its recorded position").arg(index));
            return std::nullopt;
        }
        solutionMoves.append(moveValue.string);
    }

    JsonValue puzzleIdentity;
    puzzleIdentity.kind = JsonValue::Kind::Object;
    puzzleIdentity.objectKeys = {QStringLiteral("fen_without_fullmove"), QStringLiteral("solution_uci")};
    JsonValue identityFen;
    identityFen.kind = JsonValue::Kind::String;
    identityFen.string = fen.split(QLatin1Char(' ')).mid(0, 5).join(QLatin1Char(' '));
    puzzleIdentity.objectValues = {identityFen, *solution};
    if (semanticId(QStringLiteral("puzzle-v1"), puzzleIdentity) != puzzleId) {
        setError(errorMessage, QStringLiteral("puzzle_id does not match the canonical position and solution"));
        return std::nullopt;
    }

    JsonValue recordContent = root;
    removeMember(&recordContent, QStringLiteral("record_id"));
    removeMember(&recordContent, QStringLiteral("record_type"));
    removeMember(&recordContent, QStringLiteral("schema"));
    if (semanticId(QStringLiteral("puzzle-record-v1"), recordContent) != *recordId) {
        setError(errorMessage, QStringLiteral("record_id does not match the canonical record content"));
        return std::nullopt;
    }
    if (canonicalJson(root) != line) {
        setError(errorMessage, QStringLiteral("puzzle record is not canonical JSONL emitted by the v1 contract"));
        return std::nullopt;
    }

    PuzzleDefinition puzzle;
    puzzle.id = puzzleId;
    puzzle.fenStart = fen;
    puzzle.solutionMoves = solutionMoves;
    puzzle.metadata.title = importedEngineRecordTitle();
    puzzle.metadata.difficulty = QStringLiteral("all");
    puzzle.metadata.source = importedEngineRecordSource();
    puzzle.metadata.sourceLabel = importedEngineRecordSourceLabel(provider);
    puzzle.metadata.rating = 0;
    puzzle.metadata.ratingHidden = true;
    puzzle.metadata.playedCount = 0;
    puzzle.metadata.whiteName = whiteName;
    puzzle.metadata.blackName = blackName;
    puzzle.metadata.themes = themeNames;
    puzzle.analysisSeed.sourceGameId = sourceGameId;
    puzzle.analysisSeed.sideToMove = sideToMove;
    puzzle.analysisSeed.sourceProvider = provider;
    puzzle.analysisSeed.sourceRecordSchema = schema;
    puzzle.analysisSeed.sourceRecordId = *recordId;
    puzzle.analysisSeed.rawSourceRecordJson = QString::fromUtf8(line);
    puzzle.analysisSeed.allowLichessPgnHydration = false;
    return puzzle;
}

} // namespace

std::optional<EngineValidatedPuzzlePack> EngineValidatedPuzzlePack::fromJsonLines(
    const QByteArray &rawJsonLines,
    QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (rawJsonLines.size() > kMaximumValidatedPuzzlePackBytes) {
        setError(errorMessage, QStringLiteral("engine-line record pack exceeds 64 MiB"));
        return std::nullopt;
    }
    if (rawJsonLines.startsWith("\xEF\xBB\xBF") || rawJsonLines.contains('\0')) {
        setError(errorMessage, QStringLiteral("engine-line record pack must be UTF-8 without BOM or NUL"));
        return std::nullopt;
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder.decode(rawJsonLines);
    if (decoder.hasError()) {
        setError(errorMessage, QStringLiteral("engine-line record pack is not strict UTF-8"));
        return std::nullopt;
    }

    EngineValidatedPuzzlePack pack;
    QHash<QString, QByteArray> recordsById;
    QHash<QString, QString> recordsByPuzzleId;
    qsizetype offset = 0;
    int lineNumber = 0;
    while (offset < rawJsonLines.size()) {
        ++lineNumber;
        if (lineNumber > kMaximumValidatedPuzzleRecords) {
            setError(errorMessage, QStringLiteral("engine-line record pack exceeds 100000 JSONL rows"));
            return std::nullopt;
        }
        qsizetype newline = rawJsonLines.indexOf('\n', offset);
        if (newline < 0) {
            newline = rawJsonLines.size();
        }
        const qsizetype rawLength = newline - offset;
        const qsizetype physicalLength = rawLength + (newline < rawJsonLines.size() ? 1 : 0);
        if (physicalLength > kMaximumValidatedPuzzleLineBytes) {
            setError(errorMessage, QStringLiteral("puzzle JSONL line %1 exceeds 1 MiB").arg(lineNumber));
            return std::nullopt;
        }
        QByteArray line = rawJsonLines.mid(offset, rawLength);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        offset = newline < rawJsonLines.size() ? newline + 1 : newline;
        if (line.trimmed().isEmpty()) {
            continue;
        }

        QString recordId;
        const auto puzzle = parseRecord(line, lineNumber, &recordId, errorMessage);
        if (!puzzle.has_value()) {
            return std::nullopt;
        }
        const auto existingRecord = recordsById.constFind(recordId);
        if (existingRecord != recordsById.cend()) {
            if (*existingRecord != line) {
                setError(errorMessage, QStringLiteral("conflicting content reuses record_id %1").arg(recordId));
                return std::nullopt;
            }
            continue;
        }
        const auto existingPuzzle = recordsByPuzzleId.constFind(puzzle->id);
        if (existingPuzzle != recordsByPuzzleId.cend() && *existingPuzzle != recordId) {
            setError(
                errorMessage,
                QStringLiteral("multiple source record versions that declare engine_validated claim puzzle_id %1")
                    .arg(puzzle->id));
            return std::nullopt;
        }
        recordsById.insert(recordId, line);
        recordsByPuzzleId.insert(puzzle->id, recordId);
        pack.m_puzzles.append(*puzzle);
    }
    if (pack.m_puzzles.isEmpty()) {
        setError(errorMessage, QStringLiteral("engine-line record pack does not contain any records"));
        return std::nullopt;
    }
    return pack;
}

} // namespace parlawl::puzzle_runner
