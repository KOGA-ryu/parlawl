#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>

struct DatabaseInitializationResult
{
    bool ok = false;
    QString message;
};

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(QObject *parent = nullptr, const QString &connectionName = QStringLiteral("parlawl-main"));
    ~DatabaseManager() override;

    DatabaseInitializationResult initialize(const QString &databasePath);
    QSqlDatabase database() const;
    QString connectionName() const;
    QString databasePath() const;

private:
    void closeConnection();

    QString m_connectionName;
    QString m_databasePath;
};
