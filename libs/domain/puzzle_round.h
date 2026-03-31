#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

struct PuzzleRound
{
    QString puzzleId;
    int puzzleRating = 0;
    QString timeControl;
    QString whitePlayer;
    int whiteRating = 0;
    QString blackPlayer;
    int blackRating = 0;
    QString sideToMove;
    QDateTime fetchedAtUtc;
    QString sourceGameId;
    QString initialFen;
    QString lastMove;
    bool solved = false;
    QString rawPuzzleJson;
    QString rawActivityJson;
    QString solutionMovesJson;
    QString themesJson;
};

inline QString ratingRangeFor(const PuzzleRound &round)
{
    const int average = (round.whiteRating + round.blackRating) / 2;
    if (average <= 0) {
        return QStringLiteral("unknown");
    }
    if (average < 1400) {
        return QStringLiteral("under_1400");
    }
    if (average < 1800) {
        return QStringLiteral("1400_1799");
    }
    if (average < 2200) {
        return QStringLiteral("1800_2199");
    }
    return QStringLiteral("2200_plus");
}

inline QString sideLabelFromFen(const QString &fen)
{
    const QStringList parts = fen.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 2) {
        return QStringLiteral("unknown");
    }
    return parts.at(1) == QStringLiteral("w") ? QStringLiteral("white") : QStringLiteral("black");
}

inline int materialScoreFromFen(const QString &fen, const QString &sideToMove)
{
    const QString board = fen.section(' ', 0, 0);
    int white = 0;
    int black = 0;

    auto pieceValue = [](QChar c) {
        switch (c.toLower().unicode()) {
        case 'p':
            return 1;
        case 'n':
        case 'b':
            return 3;
        case 'r':
            return 5;
        case 'q':
            return 9;
        default:
            return 0;
        }
    };

    for (const QChar c : board) {
        if (!c.isLetter()) {
            continue;
        }
        if (c.isUpper()) {
            white += pieceValue(c);
        } else {
            black += pieceValue(c);
        }
    }

    if (sideToMove == QStringLiteral("black")) {
        return black - white;
    }
    return white - black;
}
