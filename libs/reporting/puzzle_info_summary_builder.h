#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "critical_move.h"
#include "puzzle_round.h"
#include "source_game.h"
#include "tactical_event.h"

struct PuzzleInfoSummary
{
    QString opening;
    QString strategicError;
    QString plan;
    QString criticalMistake;
    QString lastPracticalMistake;
    QString tacticalTheme;
    QStringList warnings;
    QString rawEvidenceText;
    bool available = false;
};

class PuzzleInfoSummaryBuilder
{
public:
    static PuzzleInfoSummary build(
        const PuzzleRound &puzzleRound,
        const SourceGame &sourceGame,
        const TacticalEvent &tacticalEvent,
        const QList<CriticalMove> &criticalMoves
    );
};
