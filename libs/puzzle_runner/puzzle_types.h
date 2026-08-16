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
    QString sourceProvider;
    QString sourceRecordSchema;
    QString sourceRecordId;
    QString timeControl;
    QString sideToMove;
    QString lastMove;
    QString rawPuzzleJson;
    QString rawActivityJson;
    QString rawSourceRecordJson;
    QString sourceGamePgn;
    QString openingName;
    bool allowLichessPgnHydration = false;
};

struct PuzzleDefinition {
    QString id;
    QString fenStart;
    QStringList solutionMoves;
    PuzzleMetadata metadata;
    PuzzleAnalysisSeed analysisSeed;
};

inline bool allowsLichessPgnHydration(const PuzzleAnalysisSeed &seed)
{
    return seed.allowLichessPgnHydration
        && seed.sourceProvider == QStringLiteral("lichess");
}

inline QString importedEngineRecordSource()
{
    return QStringLiteral("imported_declared_engine_validated_v1");
}

inline QString importedEngineRecordSchema()
{
    return QStringLiteral("esports-probability-lab/puzzle-candidate/v1");
}

inline QString importedEngineRecordTitle()
{
    return QStringLiteral(
        "Imported record declares engine_validated; not independently verified by ParlAWL");
}

inline QString importedEngineRecordSourceLabel(const QString &provider)
{
    return QStringLiteral(
               "Imported record from %1 declares engine_validated; not independently verified by ParlAWL. "
               "ParlAWL checked the schema, self-consistent content IDs, known cross-links, and legal replay only; "
               "it did not authenticate the producer and did not rerun the engine. These checks do not prove "
               "engine optimality, uniqueness, or forced play.")
        .arg(provider);
}

inline bool isImportedEngineRecord(const PuzzleDefinition &puzzle)
{
    return puzzle.metadata.source == importedEngineRecordSource();
}

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
