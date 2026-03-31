#pragma once

#include <QString>

class SourceGamePgnCache
{
public:
    SourceGamePgnCache();

    QString load(const QString &sourceGameId, QString *errorMessage = nullptr) const;
    bool store(const QString &sourceGameId, const QString &pgnText, QString *errorMessage = nullptr) const;

private:
    QString cacheDirectoryPath() const;
    QString cacheFilePath(const QString &sourceGameId) const;
};
