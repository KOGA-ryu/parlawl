#pragma once

#include <optional>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace parlawl::puzzle_runner {

struct GameReviewDisplayMove {
    int ply = 0;
    int moveNumber = 0;
    QString mover;
    QString playerUsername;
    QString san;
    QString uci;
    QString phase;
    QString beforeFen;
    QString afterFen;
    std::optional<qint64> elapsedMoveMs;
    QString elapsedStatus;
    std::optional<qint64> decisionStartClockMs;
    std::optional<qint64> clockRemainingMs;
    QVector<qint64> pressureThresholdsMetMs;
    QString piece;
    bool capture = false;
    bool check = false;
    bool checkmate = false;
    std::optional<QString> castling;
    QVector<int> selectedMomentIndexes;
};

struct GameReviewDisplayMoment {
    int reviewIndex = 0;
    int ply = 0;
    int moveNumber = 0;
    QString mover;
    QString playerUsername;
    QString phase;
    QString playedSan;
    QString playedUci;
    std::optional<QString> bestMoveSan;
    QString bestMoveUci;
    QString status;
    std::optional<QString> severity;
    QString confidence;
    std::optional<qint64> centipawnLoss;
    std::optional<qint64> bestCentipawnsMover;
    std::optional<qint64> playedCentipawnsMover;
    std::optional<qint64> expectationLossMillionths;
    std::optional<qint64> elapsedMoveMs;
    QString title;
    QString summary;
    QStringList bestLineUci;
    QStringList playedLineUci;
};

struct GameReviewDisplayTimingBucket {
    QString playerColor;
    QString phase;
    int decisionCount = 0;
    int elapsedObservedCount = 0;
    int elapsedMissingCount = 0;
    std::optional<qint64> observedElapsedSumMs;
    std::optional<qint64> meanObservedElapsedMs;
    std::optional<qint64> maximumObservedElapsedMs;
};

struct GameReviewDisplayLongestMove {
    QString playerColor;
    QString playerUsername;
    int ply = 0;
    int moveNumber = 0;
    QString san;
    QString uci;
    QString phase;
    qint64 elapsedMoveMs = 0;
};

struct GameReviewDisplay {
    QString displaySchema;
    QString sourceReportId;

    QString sourceGameId;
    QString canonicalGameUrl;
    QString startTimeUtc;
    QString endTimeUtc;
    QString whiteUsername;
    QString blackUsername;
    std::optional<int> whiteRating;
    std::optional<int> blackRating;
    bool rated = false;
    QString rules;
    QString timeClass;
    QString timeControl;
    int moveCount = 0;
    int selectedMomentCount = 0;

    std::optional<QString> openingEco;
    std::optional<QString> openingName;
    QString openingStatus;
    std::optional<int> deepestExactMatchPositionIndex;
    std::optional<int> firstPlyAfterBook;
    std::optional<QString> firstMoveAfterBookSan;

    QString result;
    std::optional<QString> winnerUsername;
    QString terminationStatus;
    std::optional<QString> terminationClaim;
    QString outcomeSummary;

    QString clockSemantics;
    int decisionCount = 0;
    int elapsedObservedCount = 0;
    int elapsedMissingCount = 0;
    std::optional<qint64> observedElapsedSumMs;
    QVector<GameReviewDisplayTimingBucket> timingBuckets;
    QVector<GameReviewDisplayLongestMove> longestObservedMoves;

    QVector<GameReviewDisplayMove> moves;
    QVector<GameReviewDisplayMoment> criticalMoments;

    [[nodiscard]] const GameReviewDisplayMoment *momentAtReviewIndex(int reviewIndex) const;
    [[nodiscard]] const GameReviewDisplayMoment *firstMomentAtPly(int ply) const;
};

class GameReviewDisplayCatalog final
{
public:
    static std::optional<GameReviewDisplayCatalog> fromDirectory(
        const QString &absoluteDirectoryPath,
        QString *errorMessage = nullptr);

    [[nodiscard]] const GameReviewDisplay *reviewForGame(
        const QString &sourceGameId) const;
    [[nodiscard]] int reviewCount() const { return m_reviewsByGame.size(); }
    [[nodiscard]] QStringList sourceGameIds() const { return m_reviewsByGame.keys(); }
    [[nodiscard]] const QString &directoryPath() const { return m_directoryPath; }

private:
    QString m_directoryPath;
    QHash<QString, GameReviewDisplay> m_reviewsByGame;
};

} // namespace parlawl::puzzle_runner
