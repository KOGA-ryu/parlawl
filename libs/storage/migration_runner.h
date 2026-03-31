#pragma once

#include <QStringList>

#include "database_manager.h"

class QSqlDatabase;

class MigrationRunner
{
public:
    DatabaseInitializationResult run(QSqlDatabase &database) const;

private:
    QStringList availableVersions() const;
    bool isSchemaLockedV01(QSqlDatabase &database) const;
};
