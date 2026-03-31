#pragma once

#include <QString>
#include <QStringList>

#include "source_game.h"
#include "tactical_event.h"

struct OpeningResolution
{
    QString display;
    QStringList warnings;
};

OpeningResolution resolveOpeningDisplay(
    const SourceGame &sourceGame,
    const TacticalEvent &tacticalEvent
);
