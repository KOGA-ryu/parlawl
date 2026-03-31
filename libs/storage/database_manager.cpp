#include "database_manager.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>

#include "migration_runner.h"

DatabaseManager::DatabaseManager(QObject *parent, const QString &connectionName)
    : QObject(parent)
    , m_connectionName(connectionName)
    , m_databasePath()
{
}

DatabaseManager::~DatabaseManager()
{
    closeConnection();
}

void DatabaseManager::closeConnection()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        auto db = QSqlDatabase::database(m_connectionName);
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

DatabaseInitializationResult DatabaseManager::initialize(const QString &databasePath)
{
    const QString normalizedPath = QFileInfo(databasePath).absoluteFilePath();
    if (!m_databasePath.isEmpty() && m_databasePath != normalizedPath) {
        closeConnection();
    }

    QFileInfo info(databasePath);
    QDir directory = info.dir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        qWarning() << "parlawl failed to create sqlite directory" << directory.absolutePath();
        return {
            false,
            QStringLiteral("failed to create sqlite directory: %1").arg(directory.absolutePath())
        };
    }

    auto db = QSqlDatabase::contains(m_connectionName)
                  ? QSqlDatabase::database(m_connectionName)
                  : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(databasePath);

    if (!db.open()) {
        qWarning() << "parlawl sqlite open failed at" << databasePath << ":" << db.lastError().text();
        return {false, QStringLiteral("sqlite open failed: %1").arg(db.lastError().text())};
    }

    MigrationRunner migrationRunner;
    const auto migrationResult = migrationRunner.run(db);
    if (!migrationResult.ok) {
        qWarning() << "parlawl migration failed:" << migrationResult.message;
        return migrationResult;
    }

    m_databasePath = normalizedPath;
    qInfo() << "parlawl sqlite initialized at" << databasePath;
    return {true, QStringLiteral("sqlite initialized at %1").arg(databasePath)};
}

QSqlDatabase DatabaseManager::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

QString DatabaseManager::connectionName() const
{
    return m_connectionName;
}

QString DatabaseManager::databasePath() const
{
    return m_databasePath;
}
