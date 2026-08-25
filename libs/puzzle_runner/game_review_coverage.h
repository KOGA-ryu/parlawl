#pragma once

#include <optional>

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace parlawl::puzzle_runner {

struct GameReviewCoverageEntry {
    QString canonicalGameUrl;
    std::optional<QString> displayFileName;
    bool displayReviewAvailable = false;
    bool mechanicalExplanationAvailable = false;
    std::optional<QString> mechanicalExplanationFileName;
    bool reportAvailable = false;
    QString reviewStatus;
    QString screeningStatus;
    std::optional<int> selectedMomentCount;
    QString sourceGameId;
    int sourceOrdinal = 0;
    std::optional<QString> sourceReportId;
    QString statusDetail;
    QString statusLabel;
};

struct GameReviewCoverageIndex {
    QString coverageId;
    QJsonObject claimBoundary;
    QString contractVersion;
    QHash<QString, int> counts;
    QVector<GameReviewCoverageEntry> entries;
    QString sourcePgnSha256;
    QString filePath;

    static std::optional<GameReviewCoverageIndex> fromFile(
        const QString &absoluteFilePath,
        QString *errorMessage = nullptr);

    [[nodiscard]] const GameReviewCoverageEntry *entryForGame(
        const QString &sourceGameId) const;
};

} // namespace parlawl::puzzle_runner
