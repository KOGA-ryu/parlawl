#include "market_attempt_repository.h"

#include <limits>

#include <QJsonArray>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "append_only_export.h"
#include "market_puzzle_pack.h"
#include "market_score_card.h"

using namespace parlawl::attempts;
using namespace parlawl::market;

using parlawl::storage::canonicalJson;
using parlawl::storage::exactOpaqueUuid;
using parlawl::storage::fail;
using parlawl::storage::kMaximumExportBytes;
using parlawl::storage::kMaximumExportRecords;
using parlawl::storage::kMaximumLineBytes;
using parlawl::storage::parseCanonicalObject;
using parlawl::storage::pythonUtc;
using parlawl::storage::semanticId;
using parlawl::storage::sha256Hex;
using parlawl::storage::writeNewPrivateFile;

namespace {

constexpr qsizetype kMaximumEventPayloadBytes = 262144;

bool marketEventKindAndTerminalKindMatch(const QString &eventKind, const QString &terminalKind)
{
    static const QSet<QString> nonTerminalKinds{
        QStringLiteral("attempt_started"),
        QStringLiteral("ply_answered"),
        QStringLiteral("ply_timed_out"),
        QStringLiteral("calibration_answered"),
        QStringLiteral("reveal_opened"),
        QStringLiteral("retry_requested"),
    };
    if (nonTerminalKinds.contains(eventKind)) {
        return terminalKind.isEmpty();
    }
    return (eventKind == QStringLiteral("attempt_completed") && terminalKind == QStringLiteral("completed"))
        || (eventKind == QStringLiteral("attempt_timed_out") && terminalKind == QStringLiteral("timed_out"))
        || (eventKind == QStringLiteral("attempt_invalidated") && terminalKind == QStringLiteral("invalidated"))
        || (eventKind == QStringLiteral("attempt_abandoned") && terminalKind == QStringLiteral("abandoned"));
}

bool isExportableTerminalKind(const QString &terminalKind)
{
    return terminalKind == QStringLiteral("completed") || terminalKind == QStringLiteral("timed_out");
}

bool isReviewEventKind(const QString &eventKind)
{
    return eventKind == QStringLiteral("reveal_opened")
        || eventKind == QStringLiteral("retry_requested");
}

QJsonObject instanceIdentityObject(const AttemptInstance &instance)
{
    return {
        {QStringLiteral("attempt_instance_id"), instance.attemptInstanceId},
        {QStringLiteral("instance_schema"), QString::fromLatin1(kMarketLocalAttemptSchema)},
        {QStringLiteral("puzzle_id"), instance.puzzleId},
        {QStringLiteral("puzzle_record_id"), instance.puzzleRecordId},
        {QStringLiteral("puzzle_snapshot"), instance.puzzleSnapshot},
        {QStringLiteral("session_id"), instance.sessionId},
        {QStringLiteral("solver_id"), instance.solverId},
        {QStringLiteral("started_at_utc"), pythonUtc(instance.startedAtUtc)},
    };
}

QJsonObject eventIdentityObject(
    const QString &attemptInstanceId,
    int eventIndex,
    const AttemptEventInput &event,
    const QString &previousHash)
{
    return {
        {QStringLiteral("attempt_instance_id"), attemptInstanceId},
        {QStringLiteral("elapsed_milliseconds"), event.elapsedMilliseconds},
        {QStringLiteral("event_index"), eventIndex},
        {QStringLiteral("event_kind"), event.kind},
        {QStringLiteral("event_schema"), QString::fromLatin1(kMarketLocalEventSchema)},
        {QStringLiteral("occurred_at_utc"), pythonUtc(event.occurredAtUtc)},
        {QStringLiteral("payload"), event.payload},
        {QStringLiteral("previous_hash"), previousHash},
        {QStringLiteral("terminal_kind"), event.terminalKind},
    };
}

struct InstanceRow
{
    AttemptInstance value;
    QByteArray snapshotJson;
    QString genesisHash;
};

bool loadInstance(
    QSqlDatabase database,
    const QString &attemptInstanceId,
    InstanceRow *row,
    QString *errorMessage)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT puzzle_id, puzzle_record_id, solver_id, session_id, started_at_utc, "
        "puzzle_snapshot_json, genesis_hash FROM market_solve_attempt_instances "
        "WHERE attempt_instance_id = ?"));
    query.addBindValue(attemptInstanceId);
    if (!query.exec()) {
        return fail(errorMessage, QStringLiteral("failed to read market solve attempt instance: %1")
                                      .arg(query.lastError().text()));
    }
    if (!query.next()) {
        return fail(errorMessage, QStringLiteral("market solve attempt instance '%1' does not exist")
                                      .arg(attemptInstanceId));
    }
    QJsonObject snapshot;
    const QByteArray snapshotJson = query.value(5).toString().toUtf8();
    if (snapshotJson.size() < 2 || snapshotJson.size() > kMaximumLineBytes) {
        return fail(errorMessage, QStringLiteral("market puzzle snapshot exceeds its bounded cell profile"));
    }
    if (!parseCanonicalObject(snapshotJson, &snapshot, errorMessage, QStringLiteral("market puzzle snapshot"))) {
        return false;
    }
    row->value.attemptInstanceId = attemptInstanceId;
    row->value.puzzleId = query.value(0).toString();
    row->value.puzzleRecordId = query.value(1).toString();
    row->value.solverId = query.value(2).toString();
    row->value.sessionId = query.value(3).toString();
    const QString storedStartedAt = query.value(4).toString();
    row->value.startedAtUtc = QDateTime::fromString(storedStartedAt, Qt::ISODate);
    row->value.puzzleSnapshot = snapshot;
    row->snapshotJson = snapshotJson;
    row->genesisHash = query.value(6).toString();
    if (row->value.puzzleId.isEmpty() || row->value.puzzleId.size() > 256
        || row->value.puzzleRecordId.isEmpty() || row->value.puzzleRecordId.size() > 256
        || !exactOpaqueUuid(row->value.attemptInstanceId, QStringLiteral("parlawl-attempt-instance-v1:"))
        || !exactOpaqueUuid(row->value.solverId, QStringLiteral("parlawl-solver-v1:"))
        || !exactOpaqueUuid(row->value.sessionId, QStringLiteral("parlawl-session-v1:"))
        || !row->value.startedAtUtc.isValid()
        || pythonUtc(row->value.startedAtUtc) != storedStartedAt) {
        return fail(errorMessage, QStringLiteral("market solve attempt identity or timestamp is invalid"));
    }
    return true;
}

QJsonObject logicalResultContent(const InstanceRow &instance)
{
    return {
        {QStringLiteral("puzzle_id"), instance.value.puzzleId},
        {QStringLiteral("session_id"), instance.value.sessionId},
        {QStringLiteral("solver_id"), instance.value.solverId},
        {QStringLiteral("started_at_utc"), pythonUtc(instance.value.startedAtUtc)},
    };
}

//! The pre-reveal half of a `market-solve-result-v1` record.
//!
//! Scoring cannot be part of it: a score is a function of the sealed key, and
//! the key does not open until this terminal is committed. The graded half
//! arrives with the `reveal_opened` review event and the two are joined at
//! export, which is why the result record is composed there rather than stored
//! whole here.
QJsonObject terminalCoreContent(
    const InstanceRow &instance,
    const TerminalAttemptInput &terminal,
    const QJsonObject &terminalPayload,
    const QString &logicalResultId)
{
    return {
        {QStringLiteral("displayed"), terminalPayload.value(QStringLiteral("displayed"))},
        {QStringLiteral("duration_milliseconds"),
         terminal.durationMilliseconds.has_value()
             ? QJsonValue(*terminal.durationMilliseconds)
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("logical_result_id"), logicalResultId},
        {QStringLiteral("metadata"), terminal.metadata},
        {QStringLiteral("mode"), terminalPayload.value(QStringLiteral("mode"))},
        {QStringLiteral("observed_at_utc"), pythonUtc(terminal.observedAtUtc)},
        {QStringLiteral("outcome"), terminal.outcome},
        {QStringLiteral("prior_exposure_count"), terminalPayload.value(QStringLiteral("prior_exposure_count"))},
        {QStringLiteral("puzzle_id"), instance.value.puzzleId},
        {QStringLiteral("puzzle_record_id"), instance.value.puzzleRecordId},
        {QStringLiteral("responses"), terminalPayload.value(QStringLiteral("responses"))},
        {QStringLiteral("session_id"), instance.value.sessionId},
        {QStringLiteral("solver_id"), instance.value.solverId},
        {QStringLiteral("started_at_utc"), pythonUtc(instance.value.startedAtUtc)},
        {QStringLiteral("task_kind"), terminalPayload.value(QStringLiteral("task_kind"))},
        {QStringLiteral("calibration"), terminalPayload.value(QStringLiteral("calibration"))},
    };
}

bool validateMarketTerminalInput(
    const InstanceRow &instance,
    const AttemptEventInput &lastEvent,
    const TerminalAttemptInput &terminal,
    QString *errorMessage)
{
    if (terminal.outcome != QStringLiteral("answered") && terminal.outcome != QStringLiteral("timed_out")) {
        return fail(errorMessage, QStringLiteral("market terminal outcome must be answered or timed_out"));
    }
    if (terminal.wrongMoveCount != 0 || terminal.hintsUsed != 0 || terminal.solutionRevealed) {
        return fail(errorMessage, QStringLiteral("market terminals carry no chess counters"));
    }
    if (!terminal.observedAtUtc.isValid()
        || terminal.observedAtUtc.toUTC() < instance.value.startedAtUtc.toUTC()) {
        return fail(errorMessage, QStringLiteral("market terminal observation precedes its start"));
    }
    if (pythonUtc(terminal.observedAtUtc) != pythonUtc(lastEvent.occurredAtUtc)) {
        return fail(errorMessage, QStringLiteral("market terminal observation must equal its journal event time"));
    }
    const std::optional<qint64> expectedDuration = lastEvent.elapsedMilliseconds > 0
        ? std::optional<qint64>(lastEvent.elapsedMilliseconds)
        : std::nullopt;
    if (terminal.durationMilliseconds != expectedDuration) {
        return fail(errorMessage, QStringLiteral("market terminal duration must equal the monotonic event elapsed time"));
    }
    if (terminal.metadata != MarketAttemptRepository::exactMarketExportMetadata()) {
        return fail(errorMessage, QStringLiteral("market terminal metadata must use the exact privacy-safe allowlist"));
    }
    const QString expectedTerminalKind = terminal.outcome == QStringLiteral("answered")
        ? QStringLiteral("completed")
        : QStringLiteral("timed_out");
    if (lastEvent.terminalKind != expectedTerminalKind
        || lastEvent.payload.value(QStringLiteral("outcome")) != QJsonValue(terminal.outcome)) {
        return fail(errorMessage, QStringLiteral("market terminal does not match the latest terminal journal event"));
    }
    const QJsonObject displayed = lastEvent.payload.value(QStringLiteral("displayed")).toObject();
    if (displayed.isEmpty()
        || !displayed.contains(QStringLiteral("record_sha256"))
        || !displayed.contains(QStringLiteral("window_digest"))
        || !displayed.contains(QStringLiteral("hud_digest"))) {
        return fail(errorMessage, QStringLiteral("market terminal is missing its display-integrity block"));
    }
    const QString snapshotWindowDigest =
        instance.value.puzzleSnapshot.value(QStringLiteral("window_digest")).toString();
    if (displayed.value(QStringLiteral("window_digest")).toString() != snapshotWindowDigest) {
        // ParlAWL rendered bars that are not the record's bars. That is a
        // display-integrity failure, and averaging it in would launder a
        // rendering bug into a calibration curve.
        return fail(errorMessage, QStringLiteral("rendered window digest disagrees with the retained record"));
    }
    return true;
}

} // namespace

MarketAttemptRepository::MarketAttemptRepository(const QSqlDatabase &database)
    : m_database(database)
{
}

QJsonObject MarketAttemptRepository::exactMarketExportMetadata()
{
    return {
        {QStringLiteral("attempt_policy"), QStringLiteral("parlawl-terminal-attempt-v1")},
        {QStringLiteral("calibration_policy"), marketCalibrationPolicyId()},
        {QStringLiteral("clock_policy"), QStringLiteral("parlawl-attempt-clock-v1")},
        {QStringLiteral("interface"), QStringLiteral("ParlAWL")},
        {QStringLiteral("scoring_policy"), marketScoringPolicyId()},
    };
}

bool MarketAttemptRepository::appendBatch(
    const AttemptAppendBatch &batch,
    AttemptAppendReceipt *receipt,
    QString *errorMessage)
{
    if (!m_database.isOpen()) {
        return fail(errorMessage, QStringLiteral("market solve database is not open"));
    }
    if (batch.attemptInstanceId.trimmed().isEmpty() || batch.events.isEmpty()) {
        return fail(errorMessage, QStringLiteral("market batch requires an instance and at least one event"));
    }
    int exportableTerminalIndex = -1;
    for (int index = 0; index < batch.events.size(); ++index) {
        if (!isExportableTerminalKind(batch.events.at(index).terminalKind)) {
            continue;
        }
        if (exportableTerminalIndex >= 0) {
            return fail(errorMessage, QStringLiteral("market batch contains multiple exportable terminals"));
        }
        exportableTerminalIndex = index;
    }
    if (exportableTerminalIndex >= 0
        && (exportableTerminalIndex != batch.events.size() - 1 || !batch.terminalAttempt.has_value())) {
        return fail(errorMessage, QStringLiteral(
            "an exportable terminal event must be last and include its atomic terminal record"));
    }
    if (exportableTerminalIndex < 0 && batch.terminalAttempt.has_value()) {
        return fail(errorMessage, QStringLiteral("market terminal record has no matching exportable terminal event"));
    }

    QSqlQuery beginQuery(m_database);
    if (!beginQuery.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        return fail(errorMessage, QStringLiteral("failed to start market solve transaction: %1")
                                      .arg(beginQuery.lastError().text()));
    }
    auto rollback = [&]() { m_database.rollback(); };

    if (batch.retainedPuzzleRecord.has_value()) {
        const RetainedPuzzleRecord &record = *batch.retainedPuzzleRecord;
        QString retainedError;
        if (record.recordSchema != QString::fromLatin1(kMarketPuzzleRecordSchema)
            || !record.retainedAtUtc.isValid()
            || !verifyRetainedMarketPuzzleLine(
                record.canonicalJson, record.puzzleRecordId, record.puzzleId, &retainedError)) {
            rollback();
            return fail(
                errorMessage,
                QStringLiteral("retained market puzzle record failed verification: %1").arg(retainedError));
        }
        const QString contentHash = sha256Hex(record.canonicalJson);
        QSqlQuery existing(m_database);
        existing.prepare(QStringLiteral(
            "SELECT puzzle_id, record_schema, canonical_json, canonical_sha256 "
            "FROM market_attempt_puzzle_records WHERE puzzle_record_id = ?"));
        existing.addBindValue(record.puzzleRecordId);
        if (!existing.exec()) {
            rollback();
            return fail(errorMessage, existing.lastError().text());
        }
        if (existing.next()) {
            if (existing.value(0).toString() != record.puzzleId
                || existing.value(1).toString() != record.recordSchema
                || existing.value(2).toByteArray() != record.canonicalJson
                || existing.value(3).toString() != contentHash) {
                rollback();
                return fail(errorMessage, QStringLiteral("retained market record conflicts with stored content"));
            }
        } else {
            QSqlQuery insert(m_database);
            insert.prepare(QStringLiteral(
                "INSERT INTO market_attempt_puzzle_records "
                "(puzzle_record_id, puzzle_id, record_schema, canonical_json, canonical_sha256, retained_at_utc) "
                "VALUES (?, ?, ?, ?, ?, ?)"));
            insert.addBindValue(record.puzzleRecordId);
            insert.addBindValue(record.puzzleId);
            insert.addBindValue(record.recordSchema);
            insert.addBindValue(record.canonicalJson);
            insert.addBindValue(contentHash);
            insert.addBindValue(pythonUtc(record.retainedAtUtc));
            if (!insert.exec()) {
                rollback();
                return fail(errorMessage, QStringLiteral("failed to retain market puzzle record: %1")
                                              .arg(insert.lastError().text()));
            }
        }
    }

    if (batch.newAttempt.has_value()) {
        const AttemptInstance &instance = *batch.newAttempt;
        if (instance.attemptInstanceId != batch.attemptInstanceId || instance.puzzleRecordId.isEmpty()
            || instance.puzzleId.isEmpty()
            || !exactOpaqueUuid(instance.attemptInstanceId, QStringLiteral("parlawl-attempt-instance-v1:"))
            || !exactOpaqueUuid(instance.solverId, QStringLiteral("parlawl-solver-v1:"))
            || !exactOpaqueUuid(instance.sessionId, QStringLiteral("parlawl-session-v1:"))
            || !instance.startedAtUtc.isValid()) {
            rollback();
            return fail(errorMessage, QStringLiteral("new market solve attempt identity is incomplete"));
        }
        const QByteArray snapshotJson = canonicalJson(instance.puzzleSnapshot);
        if (snapshotJson.size() < 2 || snapshotJson.size() > kMaximumLineBytes) {
            rollback();
            return fail(errorMessage, QStringLiteral("market puzzle snapshot exceeds its bounded write profile"));
        }
        const QString genesisHash =
            semanticId(QStringLiteral("parlawl-market-attempt-genesis-v1"), instanceIdentityObject(instance));
        QSqlQuery existing(m_database);
        existing.prepare(QStringLiteral(
            "SELECT genesis_hash FROM market_solve_attempt_instances WHERE attempt_instance_id = ?"));
        existing.addBindValue(instance.attemptInstanceId);
        if (!existing.exec()) {
            rollback();
            return fail(errorMessage, existing.lastError().text());
        }
        if (existing.next()) {
            if (existing.value(0).toString() != genesisHash) {
                rollback();
                return fail(errorMessage, QStringLiteral("market attempt instance conflicts with stored content"));
            }
        } else {
            QSqlQuery insert(m_database);
            insert.prepare(QStringLiteral(
                "INSERT INTO market_solve_attempt_instances "
                "(attempt_instance_id, instance_schema, puzzle_id, puzzle_record_id, solver_id, session_id, "
                "started_at_utc, puzzle_snapshot_json, genesis_hash) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
            insert.addBindValue(instance.attemptInstanceId);
            insert.addBindValue(QString::fromLatin1(kMarketLocalAttemptSchema));
            insert.addBindValue(instance.puzzleId);
            insert.addBindValue(instance.puzzleRecordId);
            insert.addBindValue(instance.solverId);
            insert.addBindValue(instance.sessionId);
            insert.addBindValue(pythonUtc(instance.startedAtUtc));
            insert.addBindValue(QString::fromUtf8(snapshotJson));
            insert.addBindValue(genesisHash);
            if (!insert.exec()) {
                rollback();
                return fail(errorMessage, QStringLiteral("failed to create market solve attempt: %1")
                                              .arg(insert.lastError().text()));
            }
        }
    }

    InstanceRow instance;
    if (!loadInstance(m_database, batch.attemptInstanceId, &instance, errorMessage)) {
        rollback();
        return false;
    }
    if (semanticId(QStringLiteral("parlawl-market-attempt-genesis-v1"), instanceIdentityObject(instance.value))
        != instance.genesisHash) {
        rollback();
        return fail(errorMessage, QStringLiteral("stored market genesis hash does not match"));
    }

    QSqlQuery lastQuery(m_database);
    lastQuery.prepare(QStringLiteral(
        "SELECT event_index, event_hash, terminal_kind FROM market_solve_attempt_events "
        "WHERE attempt_instance_id = ? ORDER BY event_index DESC LIMIT 1"));
    lastQuery.addBindValue(batch.attemptInstanceId);
    if (!lastQuery.exec()) {
        rollback();
        return fail(errorMessage, lastQuery.lastError().text());
    }
    int nextIndex = 0;
    QString previousHash = instance.genesisHash;
    if (lastQuery.next()) {
        nextIndex = lastQuery.value(0).toInt() + 1;
        previousHash = lastQuery.value(1).toString();
    }
    const QString expectedPrevious = batch.expectedPreviousHash.isEmpty() && batch.expectedNextEventIndex == 0
        ? instance.genesisHash
        : batch.expectedPreviousHash;
    if (nextIndex != batch.expectedNextEventIndex || previousHash != expectedPrevious) {
        rollback();
        return fail(errorMessage, QStringLiteral("market solve journal advanced unexpectedly"));
    }

    QSqlQuery terminalSeen(m_database);
    terminalSeen.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM market_solve_attempt_events "
        "WHERE attempt_instance_id = ? AND terminal_kind != ''"));
    terminalSeen.addBindValue(batch.attemptInstanceId);
    if (!terminalSeen.exec() || !terminalSeen.next()) {
        rollback();
        return fail(errorMessage, QStringLiteral("failed to inspect market terminal lifecycle"));
    }
    bool sawTerminal = terminalSeen.value(0).toInt() > 0;

    qint64 previousElapsed = 0;
    if (batch.expectedNextEventIndex > 0) {
        QSqlQuery prior(m_database);
        prior.prepare(QStringLiteral(
            "SELECT elapsed_milliseconds FROM market_solve_attempt_events "
            "WHERE attempt_instance_id = ? AND event_index = ?"));
        prior.addBindValue(batch.attemptInstanceId);
        prior.addBindValue(batch.expectedNextEventIndex - 1);
        if (!prior.exec() || !prior.next()) {
            rollback();
            return fail(errorMessage, QStringLiteral("market preflight predecessor is missing"));
        }
        previousElapsed = prior.value(0).toLongLong();
    }

    for (int offset = 0; offset < batch.events.size(); ++offset) {
        const AttemptEventInput &event = batch.events.at(offset);
        const int eventIndex = batch.expectedNextEventIndex + offset;
        if (!event.occurredAtUtc.isValid() || event.elapsedMilliseconds < previousElapsed
            || !marketEventKindAndTerminalKindMatch(event.kind, event.terminalKind)
            || (eventIndex == 0
                && (event.kind != QStringLiteral("attempt_started") || event.elapsedMilliseconds != 0
                    || pythonUtc(event.occurredAtUtc) != pythonUtc(instance.value.startedAtUtc)))
            || (eventIndex > 0 && event.kind == QStringLiteral("attempt_started"))
            || (sawTerminal && !isReviewEventKind(event.kind))) {
            rollback();
            return fail(errorMessage, QStringLiteral("market solve event preflight failed"));
        }
        if (!event.terminalKind.isEmpty()) {
            if (sawTerminal) {
                rollback();
                return fail(errorMessage, QStringLiteral("market solve attempt already has a terminal event"));
            }
            sawTerminal = true;
        }
        previousElapsed = event.elapsedMilliseconds;
    }

    for (const AttemptEventInput &event : batch.events) {
        const QJsonObject identity =
            eventIdentityObject(batch.attemptInstanceId, nextIndex, event, previousHash);
        const QString eventHash = semanticId(QStringLiteral("parlawl-market-attempt-event-v1"), identity);
        const QByteArray payloadJson = canonicalJson(event.payload);
        if (payloadJson.size() < 2 || payloadJson.size() > kMaximumEventPayloadBytes) {
            rollback();
            return fail(errorMessage, QStringLiteral("market event payload exceeds its bounded write profile"));
        }
        QSqlQuery insert(m_database);
        insert.prepare(QStringLiteral(
            "INSERT INTO market_solve_attempt_events "
            "(event_id, event_schema, attempt_instance_id, event_index, event_kind, occurred_at_utc, "
            "elapsed_milliseconds, payload_json, terminal_kind, previous_hash, event_hash) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(eventHash);
        insert.addBindValue(QString::fromLatin1(kMarketLocalEventSchema));
        insert.addBindValue(batch.attemptInstanceId);
        insert.addBindValue(nextIndex);
        insert.addBindValue(event.kind);
        insert.addBindValue(pythonUtc(event.occurredAtUtc));
        insert.addBindValue(event.elapsedMilliseconds);
        insert.addBindValue(QString::fromUtf8(payloadJson));
        insert.addBindValue(event.terminalKind.isNull() ? QStringLiteral("") : event.terminalKind);
        insert.addBindValue(previousHash);
        insert.addBindValue(eventHash);
        if (!insert.exec()) {
            rollback();
            return fail(errorMessage, QStringLiteral("failed to append market solve event: %1")
                                          .arg(insert.lastError().text()));
        }
        previousHash = eventHash;
        ++nextIndex;
    }

    QString coreResultId;
    if (batch.terminalAttempt.has_value()) {
        const TerminalAttemptInput &terminal = *batch.terminalAttempt;
        const AttemptEventInput &terminalEvent = batch.events.last();
        if (!validateMarketTerminalInput(instance, terminalEvent, terminal, errorMessage)) {
            rollback();
            return false;
        }
        const QString logicalResultId =
            semanticId(QStringLiteral("market-solve-logical-v1"), logicalResultContent(instance));
        const QJsonObject core =
            terminalCoreContent(instance, terminal, terminalEvent.payload, logicalResultId);
        const QByteArray coreJson = canonicalJson(core);
        coreResultId = semanticId(QStringLiteral("market-solve-core-v1"), core);
        const QByteArray metadataJson = canonicalJson(terminal.metadata);
        if (coreJson.size() < 2 || coreJson.size() > kMaximumLineBytes
            || metadataJson.size() < 2 || metadataJson.size() > 4096) {
            rollback();
            return fail(errorMessage, QStringLiteral("market terminal exceeds its bounded write profile"));
        }
        QSqlQuery insert(m_database);
        insert.prepare(QStringLiteral(
            "INSERT INTO market_solve_attempt_terminal_records "
            "(core_result_id, logical_result_id, attempt_instance_id, puzzle_record_id, record_schema, "
            "outcome, mode, task_kind, prior_exposure_count, observed_at_utc, duration_milliseconds, "
            "metadata_json, canonical_json, canonical_sha256) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(coreResultId);
        insert.addBindValue(logicalResultId);
        insert.addBindValue(batch.attemptInstanceId);
        insert.addBindValue(instance.value.puzzleRecordId);
        insert.addBindValue(QString::fromLatin1(kMarketResultRecordSchema));
        insert.addBindValue(terminal.outcome);
        insert.addBindValue(terminalEvent.payload.value(QStringLiteral("mode")).toString());
        insert.addBindValue(terminalEvent.payload.value(QStringLiteral("task_kind")).toString());
        insert.addBindValue(terminalEvent.payload.value(QStringLiteral("prior_exposure_count")).toInt());
        insert.addBindValue(pythonUtc(terminal.observedAtUtc));
        if (terminal.durationMilliseconds.has_value()) {
            insert.addBindValue(*terminal.durationMilliseconds);
        } else {
            insert.addBindValue(QVariant());
        }
        insert.addBindValue(QString::fromUtf8(metadataJson));
        insert.addBindValue(coreJson);
        insert.addBindValue(sha256Hex(coreJson));
        if (!insert.exec()) {
            rollback();
            return fail(errorMessage, QStringLiteral("failed to append market terminal record: %1")
                                          .arg(insert.lastError().text()));
        }
    }

    if (!m_database.commit()) {
        rollback();
        return fail(errorMessage, QStringLiteral("failed to commit market solve batch: %1")
                                      .arg(m_database.lastError().text()));
    }
    if (receipt != nullptr) {
        receipt->nextEventIndex = nextIndex;
        receipt->previousHash = previousHash;
        receipt->terminalAttemptId = coreResultId;
    }
    return true;
}

RevealTicket MarketAttemptRepository::ticketForCommittedTerminal(
    const QString &attemptInstanceId,
    QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT instance.puzzle_id, event.event_hash, event.terminal_kind, event.event_index "
        "FROM market_solve_attempt_events AS event "
        "JOIN market_solve_attempt_instances AS instance "
        "  ON instance.attempt_instance_id = event.attempt_instance_id "
        "WHERE event.attempt_instance_id = ? AND event.terminal_kind != '' "
        "ORDER BY event.event_index DESC LIMIT 1"));
    query.addBindValue(attemptInstanceId);
    if (!query.exec() || !query.next()) {
        fail(errorMessage, QStringLiteral("this rep has no committed terminal event; the reveal stays shut"));
        return {};
    }
    if (!isExportableTerminalKind(query.value(2).toString())) {
        fail(
            errorMessage,
            QStringLiteral("this rep terminated as '%1', which discloses nothing")
                .arg(query.value(2).toString()));
        return {};
    }
    return RevealTicket(attemptInstanceId, query.value(0).toString(), query.value(1).toString());
}

bool MarketAttemptRepository::verifyTerminalEvent(
    const QString &attemptInstanceId,
    const QString &puzzleId,
    const QString &terminalEventHash,
    QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT instance.puzzle_id, event.event_hash, event.terminal_kind "
        "FROM market_solve_attempt_events AS event "
        "JOIN market_solve_attempt_instances AS instance "
        "  ON instance.attempt_instance_id = event.attempt_instance_id "
        "WHERE event.attempt_instance_id = ? AND event.terminal_kind != '' "
        "ORDER BY event.event_index DESC LIMIT 1"));
    query.addBindValue(attemptInstanceId);
    if (!query.exec() || !query.next()) {
        return fail(errorMessage, QStringLiteral("no terminal event exists for this attempt"));
    }
    if (query.value(0).toString() != puzzleId) {
        return fail(errorMessage, QStringLiteral("the attempt's terminal belongs to a different puzzle"));
    }
    if (!isExportableTerminalKind(query.value(2).toString())) {
        return fail(errorMessage, QStringLiteral("the attempt's terminal is not an exportable one"));
    }
    if (query.value(1).toString() != terminalEventHash) {
        return fail(errorMessage, QStringLiteral("the ticket's terminal event hash is not in the journal"));
    }
    return true;
}

bool MarketAttemptRepository::verifyAttempt(
    const QString &attemptInstanceId,
    QString *errorMessage) const
{
    QSqlQuery begin(m_database);
    if (!begin.exec(QStringLiteral("BEGIN"))) {
        return fail(errorMessage, QStringLiteral("failed to open market verification snapshot"));
    }
    const bool ok = verifyAttemptInCurrentSnapshot(attemptInstanceId, errorMessage);
    if (!ok) {
        m_database.rollback();
        return false;
    }
    if (!m_database.commit()) {
        m_database.rollback();
        return fail(errorMessage, QStringLiteral("failed to close market verification snapshot"));
    }
    return true;
}

bool MarketAttemptRepository::verifyAttemptInCurrentSnapshot(
    const QString &attemptInstanceId,
    QString *errorMessage) const
{
    InstanceRow instance;
    if (!loadInstance(m_database, attemptInstanceId, &instance, errorMessage)) {
        return false;
    }
    if (semanticId(QStringLiteral("parlawl-market-attempt-genesis-v1"), instanceIdentityObject(instance.value))
        != instance.genesisHash) {
        return fail(errorMessage, QStringLiteral("market genesis hash mismatch"));
    }

    QSqlQuery recordQuery(m_database);
    recordQuery.prepare(QStringLiteral(
        "SELECT puzzle_id, record_schema, canonical_json, canonical_sha256, retained_at_utc "
        "FROM market_attempt_puzzle_records WHERE puzzle_record_id = ?"));
    recordQuery.addBindValue(instance.value.puzzleRecordId);
    if (!recordQuery.exec() || !recordQuery.next()) {
        return fail(errorMessage, QStringLiteral("market retained puzzle record is missing"));
    }
    const QByteArray recordJson = recordQuery.value(2).toByteArray();
    const QString retainedAtText = recordQuery.value(4).toString();
    const QDateTime retainedAtUtc = QDateTime::fromString(retainedAtText, Qt::ISODate);
    QString retainedError;
    if (recordQuery.value(0).toString() != instance.value.puzzleId
        || recordQuery.value(1).toString() != QString::fromLatin1(kMarketPuzzleRecordSchema)
        || recordQuery.value(3).toString() != sha256Hex(recordJson)
        || !retainedAtUtc.isValid() || pythonUtc(retainedAtUtc) != retainedAtText
        || !verifyRetainedMarketPuzzleLine(
            recordJson, instance.value.puzzleRecordId, instance.value.puzzleId, &retainedError)) {
        return fail(errorMessage, QStringLiteral("market retained record failed exact verification: %1")
                                      .arg(retainedError));
    }

    QString previousHash = instance.genesisHash;
    int expectedIndex = 0;
    qint64 previousElapsed = 0;
    bool sawTerminal = false;
    QString exportableTerminalKind;
    AttemptEventInput exportableTerminalEvent;
    QSqlQuery events(m_database);
    events.prepare(QStringLiteral(
        "SELECT event_id, event_index, event_kind, occurred_at_utc, elapsed_milliseconds, payload_json, "
        "terminal_kind, previous_hash, event_hash FROM market_solve_attempt_events "
        "WHERE attempt_instance_id = ? ORDER BY event_index"));
    events.addBindValue(attemptInstanceId);
    if (!events.exec()) {
        return fail(errorMessage, events.lastError().text());
    }
    while (events.next()) {
        QJsonObject payload;
        const QByteArray payloadBytes = events.value(5).toString().toUtf8();
        if (payloadBytes.size() < 2 || payloadBytes.size() > kMaximumEventPayloadBytes) {
            return fail(errorMessage, QStringLiteral("market event payload exceeds its bounded cell profile"));
        }
        if (!parseCanonicalObject(payloadBytes, &payload, errorMessage, QStringLiteral("market event payload"))) {
            return false;
        }
        AttemptEventInput event;
        event.kind = events.value(2).toString();
        const QString occurredAtText = events.value(3).toString();
        event.occurredAtUtc = QDateTime::fromString(occurredAtText, Qt::ISODate);
        event.elapsedMilliseconds = events.value(4).toLongLong();
        event.payload = payload;
        event.terminalKind = events.value(6).toString();
        if (!event.occurredAtUtc.isValid() || pythonUtc(event.occurredAtUtc) != occurredAtText
            || event.elapsedMilliseconds < previousElapsed
            || (expectedIndex == 0
                && (event.kind != QStringLiteral("attempt_started") || event.elapsedMilliseconds != 0
                    || occurredAtText != pythonUtc(instance.value.startedAtUtc)))
            || (expectedIndex > 0 && event.kind == QStringLiteral("attempt_started"))
            || !marketEventKindAndTerminalKindMatch(event.kind, event.terminalKind)
            || (sawTerminal && !isReviewEventKind(event.kind))) {
            return fail(errorMessage, QStringLiteral("market event chronology or kind is invalid"));
        }
        if (!event.terminalKind.isEmpty()) {
            if (sawTerminal) {
                return fail(errorMessage, QStringLiteral("market attempt has multiple terminal events"));
            }
            sawTerminal = true;
        }
        const QString expectedHash = semanticId(
            QStringLiteral("parlawl-market-attempt-event-v1"),
            eventIdentityObject(attemptInstanceId, expectedIndex, event, previousHash));
        if (events.value(0).toString() != expectedHash || events.value(1).toInt() != expectedIndex
            || events.value(7).toString() != previousHash || events.value(8).toString() != expectedHash) {
            return fail(errorMessage, QStringLiteral("market event hash chain mismatch"));
        }
        if (isExportableTerminalKind(event.terminalKind)) {
            exportableTerminalKind = event.terminalKind;
            exportableTerminalEvent = event;
        }
        previousHash = expectedHash;
        previousElapsed = event.elapsedMilliseconds;
        ++expectedIndex;
    }
    if (expectedIndex == 0) {
        return fail(errorMessage, QStringLiteral("market attempt has no start event"));
    }

    QSqlQuery terminalQuery(m_database);
    terminalQuery.prepare(QStringLiteral(
        "SELECT core_result_id, logical_result_id, puzzle_record_id, record_schema, outcome, "
        "observed_at_utc, duration_milliseconds, metadata_json, canonical_json, canonical_sha256 "
        "FROM market_solve_attempt_terminal_records WHERE attempt_instance_id = ?"));
    terminalQuery.addBindValue(attemptInstanceId);
    if (!terminalQuery.exec()) {
        return fail(errorMessage, terminalQuery.lastError().text());
    }
    if (!terminalQuery.next()) {
        if (!exportableTerminalKind.isEmpty()) {
            return fail(errorMessage, QStringLiteral("exportable market terminal event has no terminal record"));
        }
        return true;
    }
    if (exportableTerminalKind.isEmpty()
        || terminalQuery.value(2).toString() != instance.value.puzzleRecordId
        || terminalQuery.value(3).toString() != QString::fromLatin1(kMarketResultRecordSchema)) {
        return fail(errorMessage, QStringLiteral("market terminal record is not linked to its journal"));
    }

    QJsonObject metadata;
    const QByteArray metadataBytes = terminalQuery.value(7).toString().toUtf8();
    if (!parseCanonicalObject(metadataBytes, &metadata, errorMessage, QStringLiteral("market terminal metadata"))) {
        return false;
    }
    TerminalAttemptInput terminal;
    terminal.outcome = terminalQuery.value(4).toString();
    const QString observedAtText = terminalQuery.value(5).toString();
    terminal.observedAtUtc = QDateTime::fromString(observedAtText, Qt::ISODate);
    if (!terminalQuery.value(6).isNull()) {
        terminal.durationMilliseconds = terminalQuery.value(6).toLongLong();
    }
    terminal.metadata = metadata;
    if (!terminal.observedAtUtc.isValid() || pythonUtc(terminal.observedAtUtc) != observedAtText) {
        return fail(errorMessage, QStringLiteral("market terminal timestamp is invalid"));
    }
    QString terminalValidationError;
    if (!validateMarketTerminalInput(instance, exportableTerminalEvent, terminal, &terminalValidationError)) {
        return fail(errorMessage, QStringLiteral("market terminal record does not match its journal: %1")
                                      .arg(terminalValidationError));
    }
    const QString logicalResultId =
        semanticId(QStringLiteral("market-solve-logical-v1"), logicalResultContent(instance));
    const QJsonObject core =
        terminalCoreContent(instance, terminal, exportableTerminalEvent.payload, logicalResultId);
    const QByteArray expectedJson = canonicalJson(core);
    if (terminalQuery.value(0).toString() != semanticId(QStringLiteral("market-solve-core-v1"), core)
        || terminalQuery.value(1).toString() != logicalResultId
        || terminalQuery.value(8).toByteArray() != expectedJson
        || terminalQuery.value(9).toString() != sha256Hex(expectedJson)) {
        return fail(errorMessage, QStringLiteral("market terminal record does not reconstruct from its journal"));
    }
    return true;
}

bool MarketAttemptRepository::composeResultRecord(
    const QString &attemptInstanceId,
    QJsonObject *record,
    QString *errorMessage) const
{
    QSqlQuery terminalQuery(m_database);
    terminalQuery.prepare(QStringLiteral(
        "SELECT canonical_json FROM market_solve_attempt_terminal_records WHERE attempt_instance_id = ?"));
    terminalQuery.addBindValue(attemptInstanceId);
    if (!terminalQuery.exec() || !terminalQuery.next()) {
        return fail(errorMessage, QStringLiteral("market terminal record is missing"));
    }
    QJsonObject core;
    if (!parseCanonicalObject(
            terminalQuery.value(0).toByteArray(), &core, errorMessage, QStringLiteral("market terminal core"))) {
        return false;
    }

    QSqlQuery packQuery(m_database);
    packQuery.prepare(QStringLiteral(
        "SELECT record.puzzle_id, instance.puzzle_snapshot_json FROM market_solve_attempt_instances AS instance "
        "JOIN market_attempt_puzzle_records AS record ON record.puzzle_record_id = instance.puzzle_record_id "
        "WHERE instance.attempt_instance_id = ?"));
    packQuery.addBindValue(attemptInstanceId);
    if (!packQuery.exec() || !packQuery.next()) {
        return fail(errorMessage, QStringLiteral("market retained record is missing"));
    }
    QJsonObject snapshot;
    if (!parseCanonicalObject(
            packQuery.value(1).toString().toUtf8(), &snapshot, errorMessage,
            QStringLiteral("market attempt snapshot"))) {
        return false;
    }
    const QString packId = snapshot.value(QStringLiteral("pack_id")).toString();
    if (packId.isEmpty()) {
        // §2.1 makes this the join key. Exporting a result that cannot name its
        // pack would hand Arc a row it can only file as unregradable, so the
        // export fails closed instead of shipping the gap.
        return fail(
            errorMessage,
            QStringLiteral("market attempt '%1' journaled no pack_id; its result cannot be joined "
                           "to a pack and is not exportable").arg(attemptInstanceId));
    }

    QSqlQuery revealQuery(m_database);
    revealQuery.prepare(QStringLiteral(
        "SELECT occurred_at_utc, elapsed_milliseconds, payload_json FROM market_solve_attempt_events "
        "WHERE attempt_instance_id = ? AND event_kind = 'reveal_opened' ORDER BY event_index LIMIT 1"));
    revealQuery.addBindValue(attemptInstanceId);
    if (!revealQuery.exec()) {
        return fail(errorMessage, revealQuery.lastError().text());
    }

    QJsonObject result = core;
    result.insert(QStringLiteral("pack_id"), packId);
    if (revealQuery.next()) {
        QJsonObject revealPayload;
        if (!parseCanonicalObject(
                revealQuery.value(2).toString().toUtf8(), &revealPayload, errorMessage,
                QStringLiteral("market reveal payload"))) {
            return false;
        }
        const QString revealedAtUtc = revealQuery.value(0).toString();
        const QString observedAtUtc = core.value(QStringLiteral("observed_at_utc")).toString();
        if (revealedAtUtc < observedAtUtc) {
            return fail(
                errorMessage,
                QStringLiteral("a reveal earlier than its terminal is malformed and is refused at export"));
        }
        const qint64 duration = core.value(QStringLiteral("duration_milliseconds")).toVariant().toLongLong();
        result.insert(
            QStringLiteral("reveal"),
            QJsonObject{
                {QStringLiteral("reveal_latency_ms"), revealQuery.value(1).toLongLong() - duration},
                {QStringLiteral("revealed_at_utc"), revealedAtUtc},
                {QStringLiteral("terminal_event_hash"),
                 revealPayload.value(QStringLiteral("terminal_event_hash"))},
            });
        result.insert(QStringLiteral("scores"), revealPayload.value(QStringLiteral("scores")));
        result.insert(QStringLiteral("line_score"), revealPayload.value(QStringLiteral("line_score")));
        result.insert(QStringLiteral("brier_score"), revealPayload.value(QStringLiteral("brier_score")));
        QJsonValue calibration = result.value(QStringLiteral("calibration"));
        const QJsonValue graded = revealPayload.value(QStringLiteral("calibration"));
        if (calibration.isObject() && graded.isObject()) {
            QJsonObject merged = calibration.toObject();
            const QJsonObject gradedObject = graded.toObject();
            for (auto it = gradedObject.constBegin(); it != gradedObject.constEnd(); ++it) {
                merged.insert(it.key(), it.value());
            }
            result.insert(QStringLiteral("calibration"), merged);
        }
    } else {
        // An ungraded rep is exported honestly as ungraded rather than with
        // zeros that would read as a perfect miss.
        result.insert(QStringLiteral("reveal"), QJsonValue(QJsonValue::Null));
        result.insert(QStringLiteral("scores"), QJsonValue(QJsonValue::Null));
        result.insert(QStringLiteral("line_score"), QJsonValue(QJsonValue::Null));
        result.insert(QStringLiteral("brier_score"), QJsonValue(QJsonValue::Null));
    }

    const QString resultId = semanticId(QStringLiteral("market-solve-result-v1"), result);
    result.insert(QStringLiteral("result_id"), resultId);
    result.insert(QStringLiteral("record_type"), QStringLiteral("market_solve_result"));
    result.insert(QStringLiteral("schema"), QString::fromLatin1(kMarketResultRecordSchema));
    *record = result;
    return true;
}

bool MarketAttemptRepository::exportMarketSolveResults(
    const QString &path,
    int *exportedCount,
    QString *errorMessage) const
{
    if (exportedCount != nullptr) {
        *exportedCount = 0;
    }
    QSqlQuery begin(m_database);
    if (!begin.exec(QStringLiteral("BEGIN"))) {
        return fail(errorMessage, QStringLiteral("failed to open market export read snapshot"));
    }
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT attempt_instance_id, mode FROM market_solve_attempt_terminal_records "
            "ORDER BY core_result_id"))) {
        m_database.rollback();
        return fail(errorMessage, QStringLiteral("failed to read market terminal records: %1")
                                      .arg(query.lastError().text()));
    }
    QStringList instanceIds;
    QMap<QString, int> byMode;
    while (query.next()) {
        instanceIds.append(query.value(0).toString());
        byMode[query.value(1).toString()] += 1;
    }
    query.finish();
    if (instanceIds.isEmpty()) {
        m_database.rollback();
        return fail(errorMessage, QStringLiteral("no terminal market reps are available to export"));
    }
    if (instanceIds.size() > kMaximumExportRecords) {
        m_database.rollback();
        return fail(errorMessage, QStringLiteral("market export exceeds 100000 records"));
    }

    QMap<QString, QByteArray> linesByResultId;
    QSet<QString> packIds;
    for (const QString &instanceId : instanceIds) {
        QString verifyError;
        if (!verifyAttemptInCurrentSnapshot(instanceId, &verifyError)) {
            m_database.rollback();
            return fail(errorMessage, QStringLiteral("market rep '%1' failed closed: %2")
                                          .arg(instanceId, verifyError));
        }
        QJsonObject record;
        if (!composeResultRecord(instanceId, &record, errorMessage)) {
            m_database.rollback();
            return false;
        }
        packIds.insert(record.value(QStringLiteral("pack_id")).toString());
        linesByResultId.insert(record.value(QStringLiteral("result_id")).toString(), canonicalJson(record));
    }
    if (!m_database.commit()) {
        m_database.rollback();
        return fail(errorMessage, QStringLiteral("failed to close market export read snapshot"));
    }

    QJsonObject byModeObject;
    for (auto it = byMode.constBegin(); it != byMode.constEnd(); ++it) {
        byModeObject.insert(it.key(), it.value());
    }

    // `market-solve-results-v1` §2.1: the join key for ingest. Arc regrades a
    // results file only against packs it holds, and a file that names no pack
    // is ingested as unregradable and kept in a separate stratum. Sorted, so
    // two exports of the same journal are byte-identical apart from the clock.
    QStringList sortedPackIds(packIds.constBegin(), packIds.constEnd());
    sortedPackIds.sort();
    QJsonArray packsReferenced;
    for (const QString &packId : std::as_const(sortedPackIds)) {
        packsReferenced.append(packId);
    }

    const QJsonObject header{
        {QStringLiteral("counts"),
         QJsonObject{
             {QStringLiteral("by_mode"), byModeObject},
             {QStringLiteral("records"), static_cast<int>(linesByResultId.size())},
         }},
        {QStringLiteral("exported_at_utc"), pythonUtc(QDateTime::currentDateTimeUtc())},
        {QStringLiteral("exporter"), exactMarketExportMetadata()},
        {QStringLiteral("packs_referenced"), packsReferenced},
        {QStringLiteral("record_type"), QStringLiteral("market_solve_results_header")},
        {QStringLiteral("schema"), QStringLiteral("arc/market-solve-results/v1")},
    };

    QByteArray output = canonicalJson(header) + '\n';
    for (auto it = linesByResultId.constBegin(); it != linesByResultId.constEnd(); ++it) {
        const QByteArray line = it.value() + '\n';
        if (line.size() > kMaximumLineBytes) {
            return fail(errorMessage, QStringLiteral("market result line exceeds 1 MiB"));
        }
        if (output.size() + line.size() > kMaximumExportBytes) {
            return fail(errorMessage, QStringLiteral("market export exceeds 64 MiB"));
        }
        output.append(line);
    }
    if (!writeNewPrivateFile(
            path, output, errorMessage, QByteArrayLiteral(".parlawl-market-export-"))) {
        return false;
    }
    if (exportedCount != nullptr) {
        *exportedCount = static_cast<int>(linesByResultId.size());
    }
    return true;
}
