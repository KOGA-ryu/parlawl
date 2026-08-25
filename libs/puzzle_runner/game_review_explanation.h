#pragma once

#include <optional>

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "game_review_display.h"

namespace parlawl::puzzle_runner {

struct GameReviewMechanicalFact {
    QString code;
    QString scope;
    QString text;
    QJsonObject values;
};

struct GameReviewMechanicalMoment {
    int reviewIndex = 0;
    int ply = 0;
    QString headline;
    QString comparisonStatus;
    QString mechanicalFactStatus;
    QVector<GameReviewMechanicalFact> facts;
};

struct GameReviewMechanicalExplanation {
    QString contractVersion;
    QString sourceGameId;
    QString sourceReportId;
    QJsonObject claimBoundary;
    QVector<GameReviewMechanicalMoment> moments;

    [[nodiscard]] const GameReviewMechanicalMoment *moment(
        int reviewIndex,
        int ply) const;
    [[nodiscard]] bool matchesDisplay(
        const GameReviewDisplay &display,
        QString *errorMessage = nullptr) const;
};

class GameReviewMechanicalExplanationCatalog final
{
public:
    static std::optional<GameReviewMechanicalExplanationCatalog> fromDirectory(
        const QString &absoluteDirectoryPath,
        QString *errorMessage = nullptr);

    [[nodiscard]] const GameReviewMechanicalExplanation *explanationForGame(
        const QString &sourceGameId) const;
    [[nodiscard]] QString fileNameForGame(const QString &sourceGameId) const
    {
        return m_fileNamesByGame.value(sourceGameId);
    }
    [[nodiscard]] int explanationCount() const { return m_explanationsByGame.size(); }
    [[nodiscard]] int momentCount() const;
    [[nodiscard]] QStringList sourceGameIds() const { return m_explanationsByGame.keys(); }
    [[nodiscard]] const QString &directoryPath() const { return m_directoryPath; }

private:
    QString m_directoryPath;
    QHash<QString, GameReviewMechanicalExplanation> m_explanationsByGame;
    QHash<QString, QString> m_fileNamesByGame;
};

} // namespace parlawl::puzzle_runner
