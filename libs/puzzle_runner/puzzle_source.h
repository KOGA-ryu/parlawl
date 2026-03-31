#pragma once

#include <QString>
#include <QVector>

#include "puzzle_types.h"

namespace parlawl::puzzle_runner {

class PuzzleSource
{
public:
    virtual ~PuzzleSource() = default;

    virtual QVector<PuzzleDefinition> loadPuzzles(QString *errorMessage) const = 0;
};

} // namespace parlawl::puzzle_runner
