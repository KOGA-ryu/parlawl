#include "fixture_puzzle_source.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace parlawl::puzzle_runner {

FixturePuzzleSource::FixturePuzzleSource(QString resourcePath)
    : m_resourcePath(std::move(resourcePath))
{
}

QVector<PuzzleDefinition> FixturePuzzleSource::loadPuzzles(QString *errorMessage) const
{
    QFile file(m_resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unable to open puzzle fixture resource %1").arg(m_resourcePath);
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("invalid puzzle fixture json: %1").arg(parseError.errorString());
        }
        return {};
    }

    const QJsonArray puzzlesArray = document.object().value(QStringLiteral("puzzles")).toArray();
    QVector<PuzzleDefinition> puzzles;
    puzzles.reserve(puzzlesArray.size());

    for (const QJsonValue &value : puzzlesArray) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString().trimmed();
        const QString fenStart = object.value(QStringLiteral("fen_start")).toString().trimmed();
        const QJsonArray solutionArray = object.value(QStringLiteral("solution_moves")).toArray();
        if (id.isEmpty() || fenStart.isEmpty() || solutionArray.isEmpty()) {
            continue;
        }

        PuzzleDefinition puzzle;
        puzzle.id = id;
        puzzle.fenStart = fenStart;
        for (const QJsonValue &moveValue : solutionArray) {
            const QString move = moveValue.toString().trimmed().toLower();
            if (!move.isEmpty()) {
                puzzle.solutionMoves.append(move);
            }
        }
        puzzle.metadata.title = object.value(QStringLiteral("title")).toString();
        puzzle.metadata.difficulty = object.value(QStringLiteral("difficulty")).toString(QStringLiteral("all"));
        puzzle.metadata.source = object.value(QStringLiteral("source")).toString(QStringLiteral("local_fixture"));
        puzzle.metadata.sourceLabel = object.value(QStringLiteral("source_label")).toString();
        puzzle.metadata.rating = object.value(QStringLiteral("rating")).toInt();
        puzzle.metadata.ratingHidden = object.value(QStringLiteral("rating_hidden")).toBool(false);
        puzzle.metadata.playedCount = object.value(QStringLiteral("played_count")).toInt();
        puzzle.metadata.whiteName = object.value(QStringLiteral("white_name")).toString();
        puzzle.metadata.whiteRating = object.value(QStringLiteral("white_rating")).toInt();
        puzzle.metadata.blackName = object.value(QStringLiteral("black_name")).toString();
        puzzle.metadata.blackRating = object.value(QStringLiteral("black_rating")).toInt();
        puzzle.analysisSeed.sourceGameId = object.value(QStringLiteral("source_game_id")).toString().trimmed();
        puzzle.analysisSeed.sourceProvider = object.value(QStringLiteral("source_provider")).toString().trimmed();
        puzzle.analysisSeed.sourceRecordSchema = object.value(QStringLiteral("source_record_schema")).toString().trimmed();
        puzzle.analysisSeed.sourceRecordId = object.value(QStringLiteral("source_record_id")).toString().trimmed();
        puzzle.analysisSeed.timeControl = object.value(QStringLiteral("time_control")).toString().trimmed();
        puzzle.analysisSeed.sideToMove = object.value(QStringLiteral("side_to_move")).toString().trimmed();
        puzzle.analysisSeed.lastMove = object.value(QStringLiteral("last_move")).toString().trimmed().toLower();
        puzzle.analysisSeed.rawPuzzleJson = object.value(QStringLiteral("raw_puzzle_json")).toString();
        puzzle.analysisSeed.rawActivityJson = object.value(QStringLiteral("raw_activity_json")).toString();
        puzzle.analysisSeed.rawSourceRecordJson = object.value(QStringLiteral("raw_source_record_json")).toString();
        puzzle.analysisSeed.sourceGamePgn = object.value(QStringLiteral("source_game_pgn")).toString();
        puzzle.analysisSeed.openingName = object.value(QStringLiteral("opening_name")).toString();
        puzzle.analysisSeed.allowLichessPgnHydration = object.contains(QStringLiteral("allow_lichess_pgn_hydration"))
            ? object.value(QStringLiteral("allow_lichess_pgn_hydration")).toBool()
            : false;
        for (const QJsonValue &themeValue : object.value(QStringLiteral("themes")).toArray()) {
            const QString theme = themeValue.toString().trimmed();
            if (!theme.isEmpty()) {
                puzzle.metadata.themes.append(theme);
            }
        }
        if (!puzzle.solutionMoves.isEmpty()) {
            puzzles.append(puzzle);
        }
    }

    if (puzzles.isEmpty() && errorMessage != nullptr) {
        *errorMessage = QStringLiteral("puzzle fixture resource %1 does not contain any puzzles").arg(m_resourcePath);
    }
    return puzzles;
}

} // namespace parlawl::puzzle_runner
