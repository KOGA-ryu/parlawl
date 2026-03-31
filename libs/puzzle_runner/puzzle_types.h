#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace parlawl::puzzle_runner {

enum class SessionStatus {
    Ready,
    Active,
    Solved,
    Failed,
};

struct PuzzleMetadata {
    QString title;
    QString difficulty;
    QString source;
    QString sourceLabel;
    QStringList themes;
    int rating = 0;
    bool ratingHidden = false;
    int playedCount = 0;
    QString whiteName;
    int whiteRating = 0;
    QString blackName;
    int blackRating = 0;
};

struct PuzzleAnalysisSeed {
    QString sourceGameId;
    QString timeControl;
    QString sideToMove;
    QString lastMove;
    QString rawPuzzleJson;
    QString rawActivityJson;
    QString sourceGamePgn;
    QString openingName;
};

struct PuzzleDefinition {
    QString id;
    QString fenStart;
    QStringList solutionMoves;
    PuzzleMetadata metadata;
    PuzzleAnalysisSeed analysisSeed;
};

struct AppliedMove {
    QString uci;
    bool userMove = false;
};

struct SessionSettings {
    bool autoAdvance = false;
    QString difficulty = QStringLiteral("hard");
    int queueSize = 10;
    bool refillWhenLow = true;
    int refillThreshold = 5;
};

inline QString sessionStatusLabel(SessionStatus status)
{
    switch (status) {
    case SessionStatus::Ready:
        return QStringLiteral("ready");
    case SessionStatus::Active:
        return QStringLiteral("active");
    case SessionStatus::Solved:
        return QStringLiteral("solved");
    case SessionStatus::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

} // namespace parlawl::puzzle_runner
