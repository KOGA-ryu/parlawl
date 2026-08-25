#pragma once

#include <optional>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace parlawl::puzzle_runner {

struct SelectiveDeepEngineLine {
    QString observationId;
    QString engineContractId;
    QString transitionId;
    QString fen;
    QString sideToMove;
    QString scoreKind;
    std::optional<qint64> centipawnsWhite;
    std::optional<qint64> mateForWhite;
    QVector<int> wdlWhite;
    QString rootMoveUci;
    int lineRank = 0;
    int depth = 0;
    int selectiveDepth = 0;
    qint64 nodes = 0;
    QStringList pvUci;
};

struct SelectiveDeepMoment {
    int presentationOrder = 0;
    int priorityRank = 0;
    int ply = 0;
    QString assessmentId;
    QString occurrenceId;
    QString episodeId;
    QString transitionId;
    QString mover;
    QString phase;
    QString san;
    QString playedMoveUci;
    QString beforeFen;
    QString status;
    std::optional<QString> severity;
    std::optional<QString> bestMoveUci;
    std::optional<qint64> bestExpectationMillionths;
    std::optional<qint64> playedExpectationMillionths;
    std::optional<qint64> signedExpectationDeltaMillionths;
    std::optional<qint64> wdlLossMillionths;
    std::optional<qint64> centipawnLoss;
    QString mateComparison;
    QString pairStability;
    QVector<SelectiveDeepEngineLine> alternativeLines;
    std::optional<SelectiveDeepEngineLine> playedLine;
};

struct SelectiveDeepMainlineMove {
    int ply = 0;
    QString mover;
    QString phase;
    QString san;
    QString uci;
    QString beforeFen;
    QString afterFen;
    QString selectionStatus;
};

struct SelectiveDeepGameReview {
    QString reportId;
    QString selectionReceiptId;
    QString interpretationId;
    QString sourceGameId;
    QString canonicalGameUrl;
    QString eventStartUtc;
    QString whiteUsername;
    QString blackUsername;
    int whiteRating = 0;
    int blackRating = 0;
    QString result;
    QString engineContractId;
    QString engineName;
    QString engineAuthor;
    QString engineBinarySha256;
    int nodeLimit = 0;
    int alternativeLineCount = 0;
    QVector<SelectiveDeepMainlineMove> mainline;
    QVector<SelectiveDeepMoment> moments;
};

class SelectiveDeepReportCatalog
{
public:
    static std::optional<SelectiveDeepReportCatalog> fromDirectory(
        const QString &absoluteDirectoryPath,
        QString *errorMessage = nullptr);

    [[nodiscard]] const SelectiveDeepGameReview *reviewForGame(
        const QString &sourceGameId) const;
    [[nodiscard]] int reportCount() const { return m_reviewsByGame.size(); }
    [[nodiscard]] const QString &directoryPath() const { return m_directoryPath; }

private:
    QString m_directoryPath;
    QHash<QString, SelectiveDeepGameReview> m_reviewsByGame;
};

} // namespace parlawl::puzzle_runner
