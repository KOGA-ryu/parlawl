#pragma once

#include <optional>

#include <QString>

namespace puzzle_api {

enum class puzzle_difficulty
{
    easiest,
    easier,
    normal,
    harder,
    hardest,
};

enum class puzzle_color
{
    white,
    black,
};

QString to_string(puzzle_difficulty value);
QString to_string(puzzle_color value);

std::optional<puzzle_difficulty> parse_puzzle_difficulty(const QString &value);
std::optional<puzzle_color> parse_puzzle_color(const QString &value);

} // namespace puzzle_api
