#pragma once

#include <optional>

#include <QByteArray>
#include <QString>
#include <QVector>

#include "puzzle_types.h"

namespace parlawl::puzzle_runner {

inline constexpr qsizetype kMaximumValidatedPuzzleLineBytes = 1024 * 1024;
inline constexpr qsizetype kMaximumValidatedPuzzlePackBytes = 64 * 1024 * 1024;
inline constexpr int kMaximumValidatedPuzzleRecords = 100'000;

class EngineValidatedPuzzlePack
{
public:
    static std::optional<EngineValidatedPuzzlePack> fromJsonLines(
        const QByteArray &rawJsonLines,
        QString *errorMessage = nullptr);

    [[nodiscard]] const QVector<PuzzleDefinition> &puzzles() const { return m_puzzles; }

private:
    QVector<PuzzleDefinition> m_puzzles;
};

} // namespace parlawl::puzzle_runner
