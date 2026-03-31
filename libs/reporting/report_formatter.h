#pragma once

#include <QList>
#include <QString>

#include "critical_move.h"
#include "puzzle_round.h"
#include "source_game.h"
#include "tactical_event.h"

class ReportFormatter
{
public:
    static QString formatAnalysisReport(
        const PuzzleRound &puzzleRound,
        const SourceGame &sourceGame,
        const TacticalEvent &tacticalEvent,
        const QList<CriticalMove> &criticalMoves
    );
};
