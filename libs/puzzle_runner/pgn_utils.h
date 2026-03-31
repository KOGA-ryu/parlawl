#pragma once

#include <QString>
#include <QStringList>

namespace parlawl::puzzle_runner {

QStringList pgnMoveList(const QString &pgnText);
QString pgnHeaderValue(const QString &pgnText, const QString &headerName);
QString pgnOpeningName(const QString &pgnText);
QString pgnEcoCode(const QString &pgnText);
QString pgnDisplayOpening(const QString &pgnText);

} // namespace parlawl::puzzle_runner
