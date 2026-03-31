#include "source_game_pgn_cache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

SourceGamePgnCache::SourceGamePgnCache() = default;

QString SourceGamePgnCache::load(const QString &sourceGameId, QString *errorMessage) const
{
    if (sourceGameId.trimmed().isEmpty()) {
        return QString();
    }

    QFile file(cacheFilePath(sourceGameId));
    if (!file.exists()) {
        return QString();
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unable to read cached source game pgn for %1").arg(sourceGameId);
        }
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

bool SourceGamePgnCache::store(const QString &sourceGameId, const QString &pgnText, QString *errorMessage) const
{
    if (sourceGameId.trimmed().isEmpty() || pgnText.trimmed().isEmpty()) {
        return false;
    }

    const QString targetFilePath = cacheFilePath(sourceGameId);
    const QString parentPath = QFileInfo(targetFilePath).absolutePath();
    QDir parentDir(parentPath);
    if (!parentDir.exists() && !QDir().mkpath(parentPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unable to create source game pgn cache directory at %1").arg(parentPath);
        }
        return false;
    }

    QSaveFile file(targetFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unable to write cached source game pgn for %1").arg(sourceGameId);
        }
        return false;
    }
    file.write(pgnText.toUtf8());
    if (!file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unable to commit cached source game pgn for %1").arg(sourceGameId);
        }
        return false;
    }
    return true;
}

QString SourceGamePgnCache::cacheDirectoryPath() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath();
    }
    return QDir(base).filePath(QStringLiteral("parlawl_source_game_pgn"));
}

QString SourceGamePgnCache::cacheFilePath(const QString &sourceGameId) const
{
    QString safeId = sourceGameId.trimmed();
    safeId.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    return QDir(cacheDirectoryPath()).filePath(safeId + QStringLiteral(".pgn"));
}
