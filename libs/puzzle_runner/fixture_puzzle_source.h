#pragma once

#include "puzzle_source.h"

namespace parlawl::puzzle_runner {

class FixturePuzzleSource final : public PuzzleSource
{
public:
    explicit FixturePuzzleSource(QString resourcePath = QStringLiteral(":/puzzle_runner/fixtures/puzzles_v1.json"));

    QVector<PuzzleDefinition> loadPuzzles(QString *errorMessage) const override;

private:
    QString m_resourcePath;
};

} // namespace parlawl::puzzle_runner
