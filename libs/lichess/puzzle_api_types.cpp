#include "puzzle_api_types.h"

namespace puzzle_api {

QString to_string(const puzzle_difficulty value)
{
    switch (value) {
    case puzzle_difficulty::easiest:
        return QStringLiteral("easiest");
    case puzzle_difficulty::easier:
        return QStringLiteral("easier");
    case puzzle_difficulty::normal:
        return QStringLiteral("normal");
    case puzzle_difficulty::harder:
        return QStringLiteral("harder");
    case puzzle_difficulty::hardest:
        return QStringLiteral("hardest");
    }
    return QString();
}

QString to_string(const puzzle_color value)
{
    switch (value) {
    case puzzle_color::white:
        return QStringLiteral("white");
    case puzzle_color::black:
        return QStringLiteral("black");
    }
    return QString();
}

std::optional<puzzle_difficulty> parse_puzzle_difficulty(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("easiest")) {
        return puzzle_difficulty::easiest;
    }
    if (normalized == QStringLiteral("easier")) {
        return puzzle_difficulty::easier;
    }
    if (normalized == QStringLiteral("normal")) {
        return puzzle_difficulty::normal;
    }
    if (normalized == QStringLiteral("harder")) {
        return puzzle_difficulty::harder;
    }
    if (normalized == QStringLiteral("hardest")) {
        return puzzle_difficulty::hardest;
    }
    return std::nullopt;
}

std::optional<puzzle_color> parse_puzzle_color(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("white")) {
        return puzzle_color::white;
    }
    if (normalized == QStringLiteral("black")) {
        return puzzle_color::black;
    }
    return std::nullopt;
}

} // namespace puzzle_api
