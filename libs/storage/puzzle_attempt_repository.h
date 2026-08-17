#pragma once

#include <QJsonValue>
#include <QSqlDatabase>

#include "puzzle_attempt_ledger.h"

class PuzzleAttemptRepository final : public parlawl::attempts::PuzzleAttemptSink
{
public:
    explicit PuzzleAttemptRepository(const QSqlDatabase &database);

    bool appendBatch(
        const parlawl::attempts::AttemptAppendBatch &batch,
        parlawl::attempts::AttemptAppendReceipt *receipt,
        QString *errorMessage = nullptr) override;

    bool exportTerminalAttempts(const QString &path, int *exportedCount = nullptr, QString *errorMessage = nullptr) const;
    bool verifyAttempt(const QString &attemptInstanceId, QString *errorMessage = nullptr) const;

    static QByteArray canonicalJson(const QJsonValue &value);
    static QString semanticId(const QString &prefix, const QJsonObject &value);
    static QString pythonUtc(const QDateTime &value);

private:
    bool verifyAttemptInCurrentSnapshot(const QString &attemptInstanceId, QString *errorMessage) const;

    mutable QSqlDatabase m_database;
};
