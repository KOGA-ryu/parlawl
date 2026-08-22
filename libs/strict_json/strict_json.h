#pragma once

// The Python-compatible canonical-JSON core shared by every strict ParlAWL
// import boundary. It is deliberately domain-free: it knows nothing about
// chess, markets, or any record shape. Extracted from
// `libs/puzzle_runner/engine_validated_puzzle_pack.cpp` rather than copied,
// because two divergent copies of a canonical encoder is how one of them ends
// up wrong.

#include <initializer_list>

#include <QByteArray>
#include <QString>
#include <QVector>

namespace parlawl::strictjson {

inline constexpr int kMaximumJsonDepth = 48;
inline constexpr int kMaximumMapRows = 512;
inline constexpr qint64 kMaximumExactInteger = 9'007'199'254'740'991LL;

void setError(QString *target, const QString &message);

//! Python's `str.__lt__`, which orders by code point rather than by UTF-16 unit.
bool pythonStringLess(const QString &left, const QString &right);

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

const JsonValue *member(const JsonValue &object, const QString &key);
JsonValue *member(JsonValue &object, const QString &key);
void removeMember(JsonValue *object, const QString &key);

JsonValue makeString(const QString &value);
JsonValue makeInteger(qint64 value);
JsonValue makeObject(std::initializer_list<std::pair<QString, JsonValue>> entries);

class StrictJsonParser
{
public:
    explicit StrictJsonParser(const QByteArray &input)
        : m_input(input)
    {
    }

    bool parse(JsonValue *output, QString *errorMessage);

private:
    void skipWhitespace();
    bool parseValue(int depth, JsonValue *output, QString *errorMessage);
    bool parseObject(int depth, JsonValue *output, QString *errorMessage);
    bool parseArray(int depth, JsonValue *output, QString *errorMessage);
    bool parseString(QString *output, QString *errorMessage);
    bool parseNumber(JsonValue *output, QString *errorMessage);
    bool consumeLiteral(const char *literal);

    const QByteArray &m_input;
    qsizetype m_offset = 0;
};

void appendJsonString(QByteArray *output, const QString &value);

//! `repr(float)` spelling, including `1e+20`, `-0.0` and `381.0`.
QByteArray pythonFloat(double value);

void appendCanonicalJson(QByteArray *output, const JsonValue &value);
QByteArray canonicalJson(const JsonValue &value);

QString sha256Hex(const QByteArray &bytes);
QString semanticId(const QString &prefix, const JsonValue &value);

bool exactKeys(
    const JsonValue &object,
    std::initializer_list<const char *> expected,
    const QString &context,
    QString *errorMessage);

bool requiredString(
    const JsonValue &object,
    const QString &key,
    QString *output,
    const QString &context,
    QString *errorMessage,
    bool allowEmpty = false);

bool requiredInteger(
    const JsonValue &object,
    const QString &key,
    qint64 minimum,
    qint64 maximum,
    qint64 *output,
    const QString &context,
    QString *errorMessage);

//! A JSON number that the contract canonicalizes as a float, never an integer.
bool requiredFloat(
    const JsonValue &object,
    const QString &key,
    double *output,
    const QString &context,
    QString *errorMessage);

bool requiredBoolean(
    const JsonValue &object,
    const QString &key,
    bool *output,
    const QString &context,
    QString *errorMessage);

bool validateTextMap(const JsonValue &value, const QString &context, QString *errorMessage);

bool validateUtc(const JsonValue &value, const QString &context, bool nullable, QString *errorMessage);

bool isSha256Hex(const QString &value);

} // namespace parlawl::strictjson
