#include "migration_runner.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

DatabaseInitializationResult MigrationRunner::run(QSqlDatabase &database) const
{
    QSqlQuery bootstrapQuery(database);
    if (!bootstrapQuery.exec(
            QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations (version TEXT PRIMARY KEY, applied_at_utc TEXT NOT NULL)")
        )) {
        return {false, QStringLiteral("failed to bootstrap schema_migrations: %1").arg(bootstrapQuery.lastError().text())};
    }

    const QStringList versions = availableVersions();
    for (const QString &version : versions) {
        QSqlQuery existsQuery(database);
        existsQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = ?"));
        existsQuery.addBindValue(version);
        if (!existsQuery.exec() || !existsQuery.next()) {
            return {false, QStringLiteral("failed to read schema_migrations: %1").arg(existsQuery.lastError().text())};
        }
        if (existsQuery.value(0).toInt() > 0) {
            existsQuery.finish();
            continue;
        }
        existsQuery.finish();

        if (version == QStringLiteral("002_lock_schema_v0_1") && isSchemaLockedV01(database)) {
            QSqlQuery recordQuery(database);
            recordQuery.prepare(QStringLiteral("INSERT INTO schema_migrations(version, applied_at_utc) VALUES (?, datetime('now'))"));
            recordQuery.addBindValue(version);
            if (!recordQuery.exec()) {
                return {false, QStringLiteral("failed to record migration %1: %2").arg(version, recordQuery.lastError().text())};
            }
            continue;
        }

        QFile migrationFile(QStringLiteral(":/schemas/%1.sql").arg(version));
        if (!migrationFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return {false, QStringLiteral("failed to open migration resource %1").arg(migrationFile.fileName())};
        }

        const QString sql = QString::fromUtf8(migrationFile.readAll());
        const QStringList statements = sql.split(QStringLiteral(";\n"), Qt::SkipEmptyParts);

        if (!database.transaction()) {
            return {false, QStringLiteral("failed to start migration transaction: %1").arg(database.lastError().text())};
        }

        for (const QString &statement : statements) {
            const QString trimmed = statement.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }

            QSqlQuery query(database);
            if (!query.exec(trimmed)) {
                database.rollback();
                return {false, QStringLiteral("migration %1 failed: %2").arg(version, query.lastError().text())};
            }
        }

        QSqlQuery recordQuery(database);
        recordQuery.prepare(QStringLiteral("INSERT INTO schema_migrations(version, applied_at_utc) VALUES (?, datetime('now'))"));
        recordQuery.addBindValue(version);
        if (!recordQuery.exec()) {
            database.rollback();
            return {false, QStringLiteral("failed to record migration %1: %2").arg(version, recordQuery.lastError().text())};
        }

        if (!database.commit()) {
            database.rollback();
            return {false, QStringLiteral("failed to commit migration %1: %2").arg(version, database.lastError().text())};
        }
    }

    return {true, QStringLiteral("sqlite schema is current")};
}

QStringList MigrationRunner::availableVersions() const
{
    return {
        QStringLiteral("001_init"),
        QStringLiteral("002_lock_schema_v0_1"),
        QStringLiteral("003_evidence_assistant_boundary"),
        QStringLiteral("004_engine_evidence_slice"),
        QStringLiteral("005_mapping_and_continuation_evidence"),
        QStringLiteral("006_ranked_candidate_evidence"),
        QStringLiteral("007_mate_aware_only_move_diagnostics"),
        QStringLiteral("008_adjacent_ply_coherence_summary"),
        QStringLiteral("009_retained_break_explanation"),
        QStringLiteral("010_normalized_divergence_summary"),
        QStringLiteral("011_retained_break_format_fields"),
        QStringLiteral("012_normalized_collapse_sequence_fields"),
        QStringLiteral("013_normalized_critical_move_fields"),
        QStringLiteral("014_normalized_candidate_ranking_fields"),
        QStringLiteral("015_structural_evidence_v1"),
        QStringLiteral("016_local_target_selection_v1_1"),
        QStringLiteral("017_critical_move_structural_links"),
        QStringLiteral("018_structural_evidence_v2"),
        QStringLiteral("019_critical_move_structural_v2_links"),
        QStringLiteral("020_structural_evidence_v3"),
        QStringLiteral("021_structural_evidence_v4"),
        QStringLiteral("022_critical_move_structural_v4_links"),
        QStringLiteral("023_puzzle_attempt_ledger_v1"),
    };
}

bool MigrationRunner::isSchemaLockedV01(QSqlDatabase &database) const
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(puzzle_rounds)"))) {
        return false;
    }

    QStringList columns;
    while (query.next()) {
        columns.append(query.value(1).toString());
    }
    query.finish();

    return columns.contains(QStringLiteral("time_control"))
        && columns.contains(QStringLiteral("white_player"))
        && columns.contains(QStringLiteral("fetched_at_utc"))
        && columns.contains(QStringLiteral("raw_puzzle_json"));
}
