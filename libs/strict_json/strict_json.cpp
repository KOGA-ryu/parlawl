#include "strict_json.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace parlawl::strictjson {

namespace {

const QRegularExpression kUtcPattern(
    QStringLiteral("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\\.[0-9]{6})?Z$"));
const QRegularExpression kShaPattern(QStringLiteral("^[0-9a-f]{64}$"));

} // namespace

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

JsonValue makeString(const QString &value)
{
    JsonValue result;
    result.kind = JsonValue::Kind::String;
    result.string = value;
    return result;
}

JsonValue makeInteger(qint64 value)
{
    JsonValue result;
    result.kind = JsonValue::Kind::Integer;
    result.integer = value;
    return result;
}

JsonValue makeObject(std::initializer_list<std::pair<QString, JsonValue>> entries)
{
    JsonValue result;
    result.kind = JsonValue::Kind::Object;
    for (const auto &entry : entries) {
        result.objectKeys.append(entry.first);
        result.objectValues.append(entry.second);
    }
    return result;
}

bool StrictJsonParser::parse(JsonValue *output, QString *errorMessage)
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

void StrictJsonParser::skipWhitespace()
{
    while (m_offset < m_input.size()) {
        const char token = m_input.at(m_offset);
        if (token != ' ' && token != '\n' && token != '\r' && token != '\t') {
            return;
        }
        ++m_offset;
    }
}

bool StrictJsonParser::parseValue(int depth, JsonValue *output, QString *errorMessage)
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

bool StrictJsonParser::parseObject(int depth, JsonValue *output, QString *errorMessage)
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

bool StrictJsonParser::parseArray(int depth, JsonValue *output, QString *errorMessage)
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

bool StrictJsonParser::parseString(QString *output, QString *errorMessage)
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

bool StrictJsonParser::parseNumber(JsonValue *output, QString *errorMessage)
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

bool StrictJsonParser::consumeLiteral(const char *literal)
{
    const QByteArray expected(literal);
    if (m_input.mid(m_offset, expected.size()) != expected) {
        return false;
    }
    m_offset += expected.size();
    return true;
}

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

QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString semanticId(const QString &prefix, const JsonValue &value)
{
    return prefix + QLatin1Char(':') + sha256Hex(canonicalJson(value));
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
    bool allowEmpty)
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

bool requiredFloat(
    const JsonValue &object,
    const QString &key,
    double *output,
    const QString &context,
    QString *errorMessage)
{
    const JsonValue *value = member(object, key);
    if (value == nullptr || value->kind != JsonValue::Kind::Number || !std::isfinite(value->number)) {
        setError(
            errorMessage,
            QStringLiteral("%1.%2 must be a finite float spelled with a decimal point").arg(context, key));
        return false;
    }
    *output = value->number;
    return true;
}

bool requiredBoolean(
    const JsonValue &object,
    const QString &key,
    bool *output,
    const QString &context,
    QString *errorMessage)
{
    const JsonValue *value = member(object, key);
    if (value == nullptr || value->kind != JsonValue::Kind::Boolean) {
        setError(errorMessage, QStringLiteral("%1.%2 must be a boolean").arg(context, key));
        return false;
    }
    *output = value->boolean;
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
            setError(
                errorMessage,
                context + QStringLiteral(" must contain normalized text keys and text values"));
            return false;
        }
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

bool isSha256Hex(const QString &value)
{
    return kShaPattern.match(value).hasMatch();
}

} // namespace parlawl::strictjson
