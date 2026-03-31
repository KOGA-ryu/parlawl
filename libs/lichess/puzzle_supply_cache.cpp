#include "puzzle_supply_cache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <optional>

using namespace parlawl::puzzle_runner;

namespace {

QString safeKey(QString value)
{
    value = value.trimmed().toLower();
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    return value.isEmpty() ? QStringLiteral("default") : value;
}

QJsonObject toJson(const PuzzleDefinition &puzzle)
{
    QJsonObject metadata{
        {QStringLiteral("title"), puzzle.metadata.title},
        {QStringLiteral("difficulty"), puzzle.metadata.difficulty},
        {QStringLiteral("source"), puzzle.metadata.source},
        {QStringLiteral("source_label"), puzzle.metadata.sourceLabel},
        {QStringLiteral("rating"), puzzle.metadata.rating},
        {QStringLiteral("rating_hidden"), puzzle.metadata.ratingHidden},
        {QStringLiteral("played_count"), puzzle.metadata.playedCount},
        {QStringLiteral("white_name"), puzzle.metadata.whiteName},
        {QStringLiteral("white_rating"), puzzle.metadata.whiteRating},
        {QStringLiteral("black_name"), puzzle.metadata.blackName},
        {QStringLiteral("black_rating"), puzzle.metadata.blackRating},
    };
    QJsonArray themes;
    for (const QString &theme : puzzle.metadata.themes) {
        themes.append(theme);
    }
    metadata.insert(QStringLiteral("themes"), themes);

    QJsonObject analysisSeed{
        {QStringLiteral("source_game_id"), puzzle.analysisSeed.sourceGameId},
        {QStringLiteral("time_control"), puzzle.analysisSeed.timeControl},
        {QStringLiteral("side_to_move"), puzzle.analysisSeed.sideToMove},
        {QStringLiteral("last_move"), puzzle.analysisSeed.lastMove},
        {QStringLiteral("raw_puzzle_json"), puzzle.analysisSeed.rawPuzzleJson},
        {QStringLiteral("raw_activity_json"), puzzle.analysisSeed.rawActivityJson},
        {QStringLiteral("source_game_pgn"), puzzle.analysisSeed.sourceGamePgn},
        {QStringLiteral("opening_name"), puzzle.analysisSeed.openingName},
    };

    QJsonArray solutionMoves;
    for (const QString &move : puzzle.solutionMoves) {
        solutionMoves.append(move);
    }

    return {
        {QStringLiteral("id"), puzzle.id},
        {QStringLiteral("fen_start"), puzzle.fenStart},
        {QStringLiteral("solution_moves"), solutionMoves},
        {QStringLiteral("metadata"), metadata},
        {QStringLiteral("analysis_seed"), analysisSeed},
    };
}

std::optional<PuzzleDefinition> fromJson(const QJsonObject &object)
{
    PuzzleDefinition puzzle;
    puzzle.id = object.value(QStringLiteral("id")).toString().trimmed();
    puzzle.fenStart = object.value(QStringLiteral("fen_start")).toString().trimmed();
    for (const QJsonValue &value : object.value(QStringLiteral("solution_moves")).toArray()) {
        puzzle.solutionMoves.append(value.toString());
    }
    if (puzzle.id.isEmpty() || puzzle.fenStart.isEmpty() || puzzle.solutionMoves.isEmpty()) {
        return std::nullopt;
    }

    const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
    puzzle.metadata.title = metadata.value(QStringLiteral("title")).toString();
    puzzle.metadata.difficulty = metadata.value(QStringLiteral("difficulty")).toString();
    puzzle.metadata.source = metadata.value(QStringLiteral("source")).toString();
    puzzle.metadata.sourceLabel = metadata.value(QStringLiteral("source_label")).toString();
    puzzle.metadata.rating = metadata.value(QStringLiteral("rating")).toInt();
    puzzle.metadata.ratingHidden = metadata.value(QStringLiteral("rating_hidden")).toBool();
    puzzle.metadata.playedCount = metadata.value(QStringLiteral("played_count")).toInt();
    puzzle.metadata.whiteName = metadata.value(QStringLiteral("white_name")).toString();
    puzzle.metadata.whiteRating = metadata.value(QStringLiteral("white_rating")).toInt();
    puzzle.metadata.blackName = metadata.value(QStringLiteral("black_name")).toString();
    puzzle.metadata.blackRating = metadata.value(QStringLiteral("black_rating")).toInt();
    for (const QJsonValue &value : metadata.value(QStringLiteral("themes")).toArray()) {
        puzzle.metadata.themes.append(value.toString());
    }

    const QJsonObject analysisSeed = object.value(QStringLiteral("analysis_seed")).toObject();
    puzzle.analysisSeed.sourceGameId = analysisSeed.value(QStringLiteral("source_game_id")).toString();
    puzzle.analysisSeed.timeControl = analysisSeed.value(QStringLiteral("time_control")).toString();
    puzzle.analysisSeed.sideToMove = analysisSeed.value(QStringLiteral("side_to_move")).toString();
    puzzle.analysisSeed.lastMove = analysisSeed.value(QStringLiteral("last_move")).toString();
    puzzle.analysisSeed.rawPuzzleJson = analysisSeed.value(QStringLiteral("raw_puzzle_json")).toString();
    puzzle.analysisSeed.rawActivityJson = analysisSeed.value(QStringLiteral("raw_activity_json")).toString();
    puzzle.analysisSeed.sourceGamePgn = analysisSeed.value(QStringLiteral("source_game_pgn")).toString();
    puzzle.analysisSeed.openingName = analysisSeed.value(QStringLiteral("opening_name")).toString();
    return puzzle;
}

bool ensureDirectory(const QString &filePath, QString *errorMessage)
{
    const QString parentPath = QFileInfo(filePath).absolutePath();
    if (QDir(parentPath).exists() || QDir().mkpath(parentPath)) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("unable to create puzzle supply cache directory at %1").arg(parentPath);
    }
    return false;
}

} // namespace

PuzzleSupplyCache::PuzzleSupplyCache(QString cacheDirectoryPath)
    : m_cacheDirectoryPath(std::move(cacheDirectoryPath))
{
}

QVector<PuzzleDefinition> PuzzleSupplyCache::loadBatch(const QString &difficulty, QString *errorMessage) const
{
    QFile file(batchCacheFilePath(difficulty));
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unable to read cached puzzle batch for %1").arg(difficulty);
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("cached puzzle batch for %1 is invalid").arg(difficulty);
        }
        return {};
    }

    QVector<PuzzleDefinition> puzzles;
    for (const QJsonValue &value : document.array()) {
        const auto puzzle = fromJson(value.toObject());
        if (puzzle.has_value()) {
            puzzles.append(*puzzle);
        }
    }
    return puzzles;
}

bool PuzzleSupplyCache::storeBatch(const QString &difficulty, const QVector<PuzzleDefinition> &puzzles, QString *errorMessage) const
{
    if (puzzles.isEmpty()) {
        return false;
    }

    const QString path = batchCacheFilePath(difficulty);
    if (!ensureDirectory(path, errorMessage)) {
        return false;
    }

    QJsonArray array;
    for (const PuzzleDefinition &puzzle : puzzles) {
        array.append(toJson(puzzle));
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unable to write cached puzzle batch for %1").arg(difficulty);
        }
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unable to commit cached puzzle batch for %1").arg(difficulty);
        }
        return false;
    }
    return true;
}

QDateTime PuzzleSupplyCache::loadCooldownUntilUtc(QString *errorMessage) const
{
    QFile file(stateCacheFilePath());
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unable to read puzzle supply cache state");
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("puzzle supply cache state is invalid");
        }
        return {};
    }

    return QDateTime::fromString(
        document.object().value(QStringLiteral("cooldown_until_utc")).toString(),
        Qt::ISODate);
}

bool PuzzleSupplyCache::storeCooldownUntilUtc(const QDateTime &cooldownUntilUtc, QString *errorMessage) const
{
    const QString path = stateCacheFilePath();
    if (!ensureDirectory(path, errorMessage)) {
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unable to write puzzle supply cache state");
        }
        return false;
    }
    file.write(QJsonDocument(QJsonObject{
        {QStringLiteral("cooldown_until_utc"), cooldownUntilUtc.isValid() ? cooldownUntilUtc.toUTC().toString(Qt::ISODate) : QString()}
    }).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unable to commit puzzle supply cache state");
        }
        return false;
    }
    return true;
}

QString PuzzleSupplyCache::cacheDirectoryPath() const
{
    if (!m_cacheDirectoryPath.trimmed().isEmpty()) {
        return m_cacheDirectoryPath;
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath();
    }
    return QDir(base).filePath(QStringLiteral("puzzle_supply_cache"));
}

QString PuzzleSupplyCache::batchCacheFilePath(const QString &difficulty) const
{
    return QDir(cacheDirectoryPath()).filePath(safeKey(difficulty) + QStringLiteral(".json"));
}

QString PuzzleSupplyCache::stateCacheFilePath() const
{
    return QDir(cacheDirectoryPath()).filePath(QStringLiteral("state.json"));
}
