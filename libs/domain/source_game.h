#pragma once

#include <QDateTime>
#include <QString>

struct SourceGame
{
    QString sourceGameId;
    QString pgnText;
    QString openingName;
    QDateTime fetchedAtUtc;
};
