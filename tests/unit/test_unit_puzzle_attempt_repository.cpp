#include <sys/stat.h>
#include <unistd.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "database_manager.h"
#include "engine_validated_puzzle_pack.h"
#include "puzzle_attempt_repository.h"
#include "session_controller.h"

using namespace parlawl::attempts;

namespace {

QByteArray fixtureRecord()
{
    QFile file(QString::fromUtf8(PARLAWL_TEST_SOURCE_DIR)
               + QStringLiteral("/tests/fixtures/engine_validated_puzzle_v1.jsonl"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QByteArray bytes = file.readAll();
    while (bytes.endsWith('\n') || bytes.endsWith('\r')) {
        bytes.chop(1);
    }
    return bytes;
}

QJsonObject fixedMetadata()
{
    return {
        {QStringLiteral("attempt_policy"), QStringLiteral("parlawl-terminal-attempt-v1")},
        {QStringLiteral("interface"), QStringLiteral("ParlAWL")},
    };
}

QJsonArray textArray(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values) {
        result.append(value);
    }
    return result;
}

QJsonObject instanceIdentityForTest(const AttemptInstance &instance)
{
    return {
        {QStringLiteral("attempt_instance_id"), instance.attemptInstanceId},
        {QStringLiteral("instance_schema"), QString::fromLatin1(kLocalAttemptSchema)},
        {QStringLiteral("puzzle_id"), instance.puzzleId},
        {QStringLiteral("puzzle_record_id"), instance.puzzleRecordId},
        {QStringLiteral("puzzle_snapshot"), instance.puzzleSnapshot},
        {QStringLiteral("session_id"), instance.sessionId},
        {QStringLiteral("solver_id"), instance.solverId},
        {QStringLiteral("started_at_utc"), PuzzleAttemptRepository::pythonUtc(instance.startedAtUtc)},
    };
}

QJsonObject eventIdentityForTest(
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
        {QStringLiteral("event_schema"), QString::fromLatin1(kLocalEventSchema)},
        {QStringLiteral("occurred_at_utc"), PuzzleAttemptRepository::pythonUtc(event.occurredAtUtc)},
        {QStringLiteral("payload"), event.payload},
        {QStringLiteral("previous_hash"), previousHash},
        {QStringLiteral("terminal_kind"), event.terminalKind},
    };
}

AttemptAppendBatch solvedBatch(const QByteArray &recordBytes)
{
    const QJsonObject record = QJsonDocument::fromJson(recordBytes).object();
    QString parseError;
    const auto parsedPack = parlawl::puzzle_runner::EngineValidatedPuzzlePack::fromJsonLines(
        recordBytes + '\n', &parseError);
    const parlawl::puzzle_runner::PuzzleDefinition puzzle = parsedPack->puzzles().first();
    const QDateTime started = QDateTime::fromString(QStringLiteral("2026-08-16T16:00:00.123Z"), Qt::ISODate);
    const QDateTime observed = QDateTime::fromString(QStringLiteral("2026-08-16T16:00:42.456Z"), Qt::ISODate);

    AttemptAppendBatch batch;
    RetainedPuzzleRecord retained;
    retained.puzzleRecordId = record.value(QStringLiteral("record_id")).toString();
    retained.puzzleId = record.value(QStringLiteral("puzzle_id")).toString();
    retained.recordSchema = record.value(QStringLiteral("schema")).toString();
    retained.canonicalJson = recordBytes;
    retained.retainedAtUtc = started;
    batch.retainedPuzzleRecord = retained;

    AttemptInstance instance;
    instance.attemptInstanceId = QStringLiteral("parlawl-attempt-instance-v1:11111111-2222-4333-8444-555555555555");
    instance.puzzleId = retained.puzzleId;
    instance.puzzleRecordId = retained.puzzleRecordId;
    instance.solverId = QStringLiteral("parlawl-solver-v1:aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    instance.sessionId = QStringLiteral("parlawl-session-v1:12345678-1234-4234-8234-123456789abc");
    instance.startedAtUtc = started;
    instance.puzzleSnapshot = {
        {QStringLiteral("difficulty"), puzzle.metadata.difficulty},
        {QStringLiteral("initial_fen"), puzzle.fenStart},
        {QStringLiteral("opening_name"), puzzle.analysisSeed.openingName},
        {QStringLiteral("puzzle_id"), retained.puzzleId},
        {QStringLiteral("puzzle_record_id"), retained.puzzleRecordId},
        {QStringLiteral("puzzle_record_schema"), puzzle.analysisSeed.sourceRecordSchema},
        {QStringLiteral("solution_uci"), textArray(puzzle.solutionMoves)},
        {QStringLiteral("source_game_id"), puzzle.analysisSeed.sourceGameId},
        {QStringLiteral("source_provider"), puzzle.analysisSeed.sourceProvider},
        {QStringLiteral("themes"), textArray(puzzle.metadata.themes)},
    };
    batch.newAttempt = instance;
    batch.attemptInstanceId = instance.attemptInstanceId;

    AttemptEventInput startedEvent;
    startedEvent.kind = QStringLiteral("attempt_started");
    startedEvent.occurredAtUtc = started;
    startedEvent.payload = {{QStringLiteral("trigger"), QStringLiteral("move")}};
    batch.events.append(startedEvent);

    AttemptEventInput solvedEvent;
    solvedEvent.kind = QStringLiteral("attempt_solved");
    solvedEvent.occurredAtUtc = observed;
    solvedEvent.elapsedMilliseconds = 42333;
    solvedEvent.terminalKind = QStringLiteral("solved");
    solvedEvent.payload = {
        {QStringLiteral("duration_milliseconds"), 42333},
        {QStringLiteral("hints_used"), 0},
        {QStringLiteral("outcome"), QStringLiteral("solved")},
        {QStringLiteral("solution_revealed"), false},
        {QStringLiteral("wrong_move_count"), 0},
    };
    batch.events.append(solvedEvent);

    TerminalAttemptInput terminal;
    terminal.outcome = QStringLiteral("solved");
    terminal.observedAtUtc = observed;
    terminal.durationMilliseconds = 42333;
    terminal.metadata = fixedMetadata();
    batch.terminalAttempt = terminal;
    return batch;
}

AttemptAppendBatch revealedFailedBatch(const QByteArray &recordBytes)
{
    AttemptAppendBatch batch = solvedBatch(recordBytes);
    const QDateTime started = QDateTime::fromString(QStringLiteral("2026-08-16T17:00:00Z"), Qt::ISODate);
    const QDateTime observed = QDateTime::fromString(QStringLiteral("2026-08-16T17:00:10Z"), Qt::ISODate);
    batch.attemptInstanceId = QStringLiteral(
        "parlawl-attempt-instance-v1:66666666-7777-4888-8999-aaaaaaaaaaaa");
    batch.newAttempt->attemptInstanceId = batch.attemptInstanceId;
    batch.newAttempt->startedAtUtc = started;
    batch.events[0].occurredAtUtc = started;
    batch.events[0].payload = {{QStringLiteral("trigger"), QStringLiteral("reveal")}};
    batch.events[1].kind = QStringLiteral("solution_revealed");
    batch.events[1].occurredAtUtc = observed;
    batch.events[1].elapsedMilliseconds = 10000;
    batch.events[1].terminalKind = QStringLiteral("revealed_failed");
    batch.events[1].payload = {
        {QStringLiteral("duration_milliseconds"), 10000},
        {QStringLiteral("hints_used"), 1},
        {QStringLiteral("outcome"), QStringLiteral("failed")},
        {QStringLiteral("solution_revealed"), true},
        {QStringLiteral("wrong_move_count"), 0},
    };
    batch.terminalAttempt->outcome = QStringLiteral("failed");
    batch.terminalAttempt->observedAtUtc = observed;
    batch.terminalAttempt->durationMilliseconds = 10000;
    batch.terminalAttempt->hintsUsed = 1;
    batch.terminalAttempt->solutionRevealed = true;
    return batch;
}

} // namespace

class TestPuzzleAttemptRepository : public QObject
{
    Q_OBJECT

private slots:
    void appendsVerifiesAndExportsTerminalAttempt();
    void rejectsMutationReplaceAndConflicts();
    void enforcesStrictSchemaAndTriggerBoundaries();
    void rejectsPoisonChronologyBeforeCommit();
    void rejectsPersistedStartTimestampMismatch();
    void retainsOpenAndClockRollbackAttemptsWithoutExport();
    void failsClosedWhenTerminalRecordIsTampered();
    void canonicalizesUnicodeAndPythonUtc();
    void controllerLifecycleCommitsThroughRealRepository();
    void exportsAcceptanceFixtureWhenRequested();
};

void TestPuzzleAttemptRepository::appendsVerifiesAndExportsTerminalAttempt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DatabaseManager manager(nullptr, QStringLiteral("attempt-export-test"));
    QVERIFY2(manager.initialize(directory.filePath(QStringLiteral("ledger.sqlite3"))).ok, "database init failed");
    PuzzleAttemptRepository repository(manager.database());

    const AttemptAppendBatch batch = solvedBatch(fixtureRecord());
    AttemptAppendReceipt receipt;
    QString error;
    QVERIFY2(repository.appendBatch(batch, &receipt, &error), qPrintable(error));
    QCOMPARE(receipt.nextEventIndex, 2);
    QVERIFY(receipt.previousHash.startsWith(QStringLiteral("parlawl-attempt-event-v1:")));
    QVERIFY(receipt.terminalAttemptId.startsWith(QStringLiteral("puzzle-attempt-v1:")));
    QVERIFY2(repository.verifyAttempt(batch.attemptInstanceId, &error), qPrintable(error));

    AttemptAppendReceipt replayReceipt;
    QVERIFY2(repository.appendBatch(batch, &replayReceipt, &error), qPrintable(error));
    QCOMPARE(replayReceipt.terminalAttemptId, receipt.terminalAttemptId);

    const QString safeDirectoryPath = QFileInfo(directory.path()).canonicalFilePath();
    const QString exportPath = safeDirectoryPath + QStringLiteral("/attempts.jsonl");
    int exported = 0;
    QVERIFY2(repository.exportTerminalAttempts(exportPath, &exported, &error), qPrintable(error));
    QCOMPARE(exported, 1);
    QFile exportedFile(exportPath);
    QVERIFY(exportedFile.open(QIODevice::ReadOnly));
    const QByteArray line = exportedFile.readAll();
    QVERIFY(line.endsWith('\n'));
    const QJsonObject object = QJsonDocument::fromJson(line.trimmed()).object();
    QCOMPARE(object.value(QStringLiteral("schema")).toString(), QString::fromLatin1(kAttemptRecordSchema));
    QCOMPARE(object.value(QStringLiteral("attempt_id")).toString(), receipt.terminalAttemptId);
    QCOMPARE(object.value(QStringLiteral("outcome")).toString(), QStringLiteral("solved"));
    QCOMPARE(object.value(QStringLiteral("started_at_utc")).toString(), QStringLiteral("2026-08-16T16:00:00.123000Z"));
    struct stat modeStat {};
    QVERIFY(::stat(QFile::encodeName(exportPath).constData(), &modeStat) == 0);
    QCOMPARE(modeStat.st_mode & 0777, static_cast<mode_t>(0600));

    QVERIFY(!repository.exportTerminalAttempts(exportPath, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("already exists")));

    const QString symlinkPath = safeDirectoryPath + QStringLiteral("/attempts-link.jsonl");
    QVERIFY(::symlink(QFile::encodeName(exportPath).constData(), QFile::encodeName(symlinkPath).constData()) == 0);
    QVERIFY(!repository.exportTerminalAttempts(symlinkPath, nullptr, &error));

    const QString realParent = safeDirectoryPath + QStringLiteral("/real-parent");
    QVERIFY(QDir().mkpath(realParent));
    const QString linkedParent = safeDirectoryPath + QStringLiteral("/linked-parent");
    QVERIFY(::symlink(QFile::encodeName(realParent).constData(), QFile::encodeName(linkedParent).constData()) == 0);
    QVERIFY(!repository.exportTerminalAttempts(linkedParent + QStringLiteral("/blocked.jsonl"), nullptr, &error));
}

void TestPuzzleAttemptRepository::rejectsMutationReplaceAndConflicts()
{
    QTemporaryDir directory;
    DatabaseManager manager(nullptr, QStringLiteral("attempt-trigger-test"));
    QVERIFY(manager.initialize(directory.filePath(QStringLiteral("ledger.sqlite3"))).ok);
    PuzzleAttemptRepository repository(manager.database());
    AttemptAppendBatch batch = solvedBatch(fixtureRecord());
    QString error;
    QVERIFY2(repository.appendBatch(batch, nullptr, &error), qPrintable(error));

    QSqlQuery query(manager.database());
    QVERIFY(!query.exec(QStringLiteral("UPDATE solve_attempt_instances SET puzzle_id = 'forged'")));
    QVERIFY(!query.exec(QStringLiteral("DELETE FROM solve_attempt_events")));
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO attempt_puzzle_records "
        "(puzzle_record_id,puzzle_id,record_schema,canonical_json,canonical_sha256,retained_at_utc) "
        "SELECT puzzle_record_id,'forged',record_schema,canonical_json,canonical_sha256,retained_at_utc "
        "FROM attempt_puzzle_records LIMIT 1"));
    QVERIFY(!query.exec());

    batch.events[1].payload.insert(QStringLiteral("wrong_move_count"), 1);
    QVERIFY(!repository.appendBatch(batch, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("advanced unexpectedly")));

    AttemptAppendBatch forgedSnapshot = solvedBatch(fixtureRecord());
    forgedSnapshot.attemptInstanceId = QStringLiteral(
        "parlawl-attempt-instance-v1:77777777-2222-4333-8444-555555555555");
    forgedSnapshot.newAttempt->attemptInstanceId = forgedSnapshot.attemptInstanceId;
    forgedSnapshot.newAttempt->puzzleSnapshot.insert(
        QStringLiteral("initial_fen"), QStringLiteral("8/8/8/8/8/8/8/8 w - - 0 1"));
    QVERIFY(!repository.appendBatch(forgedSnapshot, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("snapshot")));

    AttemptAppendBatch forged = solvedBatch(fixtureRecord());
    forged.retainedPuzzleRecord->canonicalJson.replace("engine_validated", "engine_validateD");
    forged.newAttempt->attemptInstanceId = QStringLiteral("parlawl-attempt-instance-v1:99999999-2222-4333-8444-555555555555");
    forged.attemptInstanceId = forged.newAttempt->attemptInstanceId;
    QVERIFY(!repository.appendBatch(forged, nullptr, &error));

    AttemptAppendBatch poison = solvedBatch(fixtureRecord());
    poison.attemptInstanceId = QStringLiteral(
        "parlawl-attempt-instance-v1:88888888-2222-4333-8444-555555555555");
    poison.newAttempt->attemptInstanceId = poison.attemptInstanceId;
    poison.terminalAttempt.reset();
    AttemptEventInput review;
    review.kind = QStringLiteral("solution_revealed_review");
    review.occurredAtUtc = poison.events.last().occurredAtUtc;
    review.elapsedMilliseconds = poison.events.last().elapsedMilliseconds;
    review.payload = {{QStringLiteral("review"), true}};
    poison.events.append(review);
    QVERIFY(!repository.appendBatch(poison, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("must be last")));
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM solve_attempt_instances")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

void TestPuzzleAttemptRepository::enforcesStrictSchemaAndTriggerBoundaries()
{
    QTemporaryDir directory;
    DatabaseManager manager(nullptr, QStringLiteral("attempt-boundary-test"));
    const QString databasePath = directory.filePath(QStringLiteral("ledger.sqlite3"));
    QVERIFY(manager.initialize(databasePath).ok);
    QVERIFY(manager.initialize(databasePath).ok);
    PuzzleAttemptRepository repository(manager.database());
    AttemptAppendBatch batch = solvedBatch(fixtureRecord());
    QString error;
    QVERIFY2(repository.appendBatch(batch, nullptr, &error), qPrintable(error));

    QSqlQuery query(manager.database());
    QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = '023_puzzle_attempt_ledger_v1'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT name, strict FROM pragma_table_list WHERE name IN "
        "('attempt_puzzle_records','solve_attempt_instances','solve_attempt_events','solve_attempt_terminal_records')")));
    int strictTables = 0;
    while (query.next()) {
        QCOMPARE(query.value(1).toInt(), 1);
        ++strictTables;
    }
    QCOMPARE(strictTables, 4);

    const QStringList tables{
        QStringLiteral("attempt_puzzle_records"),
        QStringLiteral("solve_attempt_instances"),
        QStringLiteral("solve_attempt_events"),
        QStringLiteral("solve_attempt_terminal_records"),
    };
    for (const QString &table : tables) {
        QVERIFY2(!query.exec(QStringLiteral("UPDATE %1 SET rowid = rowid").arg(table)), qPrintable(table));
        QVERIFY2(!query.exec(QStringLiteral("DELETE FROM %1").arg(table)), qPrintable(table));
    }
    QVERIFY(!query.exec(QStringLiteral("INSERT OR REPLACE INTO attempt_puzzle_records SELECT * FROM attempt_puzzle_records LIMIT 1")));
    QVERIFY(!query.exec(QStringLiteral("INSERT OR REPLACE INTO solve_attempt_instances SELECT * FROM solve_attempt_instances LIMIT 1")));
    QVERIFY(!query.exec(QStringLiteral("INSERT OR REPLACE INTO solve_attempt_events SELECT * FROM solve_attempt_events LIMIT 1")));
    QVERIFY(!query.exec(QStringLiteral("INSERT OR REPLACE INTO solve_attempt_terminal_records SELECT * FROM solve_attempt_terminal_records LIMIT 1")));

    QVERIFY(!query.exec(QStringLiteral(
        "INSERT INTO solve_attempt_instances "
        "(attempt_instance_id,instance_schema,puzzle_id,puzzle_record_id,solver_id,session_id,started_at_utc,puzzle_snapshot_json,genesis_hash) "
        "VALUES ('parlawl-attempt-instance-v1:22222222-2222-4222-8222-222222222222',"
        "'parlawl/puzzle-attempt-instance/v1','missing','missing-record',"
        "'parlawl-solver-v1:aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee',"
        "'parlawl-session-v1:12345678-1234-4234-8234-123456789abc',"
        "'2026-08-16T16:00:00Z','{}','genesis')")));
    QVERIFY(!query.exec(QStringLiteral(
        "INSERT INTO solve_attempt_events "
        "(event_id,event_schema,attempt_instance_id,event_index,event_kind,occurred_at_utc,elapsed_milliseconds,payload_json,terminal_kind,previous_hash,event_hash) "
        "VALUES ('x','parlawl/puzzle-attempt-event/v1','missing-instance',0,'attempt_started',"
        "'2026-08-16T16:00:00Z',0,'{}','','x','x')")));

    QVERIFY(query.exec(QStringLiteral(
        "SELECT event_hash FROM solve_attempt_events WHERE attempt_instance_id = '%1' ORDER BY event_index DESC LIMIT 1")
                           .arg(batch.attemptInstanceId)));
    QVERIFY(query.next());
    const QString previousHash = query.value(0).toString();
    const QString eventPrefix = QStringLiteral(
        "INSERT INTO solve_attempt_events "
        "(event_id,event_schema,attempt_instance_id,event_index,event_kind,occurred_at_utc,elapsed_milliseconds,payload_json,terminal_kind,previous_hash,event_hash) VALUES ");
    QVERIFY(!query.exec(eventPrefix + QStringLiteral(
        "('gap','parlawl/puzzle-attempt-event/v1','%1',9,'retry_requested','2026-08-16T16:01:00Z',60000,'{}','','%2','gap')")
                                           .arg(batch.attemptInstanceId, previousHash)));
    QVERIFY(!query.exec(eventPrefix + QStringLiteral(
        "('bad-prev','parlawl/puzzle-attempt-event/v1','%1',2,'retry_requested','2026-08-16T16:01:00Z',60000,'{}','','wrong','bad-prev')")
                                           .arg(batch.attemptInstanceId)));
    QVERIFY(!query.exec(eventPrefix + QStringLiteral(
        "('second-start','parlawl/puzzle-attempt-event/v1','%1',2,'attempt_started','2026-08-16T16:01:00Z',60000,'{}','','%2','second-start')")
                                           .arg(batch.attemptInstanceId, previousHash)));
    QVERIFY(!query.exec(eventPrefix + QStringLiteral(
        "('second-terminal','parlawl/puzzle-attempt-event/v1','%1',2,'attempt_failed_wrong_move','2026-08-16T16:01:00Z',60000,'{}','failed_wrong_move','%2','second-terminal')")
                                           .arg(batch.attemptInstanceId, previousHash)));

    AttemptAppendBatch whitespace = solvedBatch(fixtureRecord());
    whitespace.newAttempt->attemptInstanceId = QStringLiteral("parlawl-attempt-instance-v1:33333333-2222-4333-8444-555555555555");
    whitespace.attemptInstanceId = whitespace.newAttempt->attemptInstanceId;
    whitespace.newAttempt->solverId.append(QLatin1Char(' '));
    QVERIFY(!repository.appendBatch(whitespace, nullptr, &error));

    AttemptAppendBatch metadata = solvedBatch(fixtureRecord());
    metadata.newAttempt->attemptInstanceId = QStringLiteral("parlawl-attempt-instance-v1:44444444-2222-4333-8444-555555555555");
    metadata.attemptInstanceId = metadata.newAttempt->attemptInstanceId;
    metadata.terminalAttempt->metadata.insert(QStringLiteral("email"), QStringLiteral("secret@example.test"));
    QVERIFY(!repository.appendBatch(metadata, nullptr, &error));

    AttemptAppendBatch observed = solvedBatch(fixtureRecord());
    observed.newAttempt->attemptInstanceId = QStringLiteral("parlawl-attempt-instance-v1:55555555-2222-4333-8444-555555555555");
    observed.attemptInstanceId = observed.newAttempt->attemptInstanceId;
    observed.terminalAttempt->observedAtUtc = observed.terminalAttempt->observedAtUtc.addMSecs(1);
    QVERIFY(!repository.appendBatch(observed, nullptr, &error));

    AttemptAppendBatch duration = solvedBatch(fixtureRecord());
    duration.newAttempt->attemptInstanceId = QStringLiteral("parlawl-attempt-instance-v1:66666666-2222-4333-8444-555555555555");
    duration.attemptInstanceId = duration.newAttempt->attemptInstanceId;
    duration.terminalAttempt->durationMilliseconds = 42334;
    QVERIFY(!repository.appendBatch(duration, nullptr, &error));
}

void TestPuzzleAttemptRepository::rejectsPoisonChronologyBeforeCommit()
{
    QTemporaryDir directory;
    DatabaseManager manager(nullptr, QStringLiteral("attempt-chronology-test"));
    QVERIFY(manager.initialize(directory.filePath(QStringLiteral("ledger.sqlite3"))).ok);
    PuzzleAttemptRepository repository(manager.database());
    QString error;

    AttemptAppendBatch badStart = solvedBatch(fixtureRecord());
    badStart.events[0].elapsedMilliseconds = 1;
    QVERIFY(!repository.appendBatch(badStart, nullptr, &error));

    AttemptAppendBatch decreasing = solvedBatch(fixtureRecord());
    AttemptEventInput hint;
    hint.kind = QStringLiteral("hint_granted");
    hint.occurredAtUtc = decreasing.events.last().occurredAtUtc;
    hint.elapsedMilliseconds = decreasing.events.last().elapsedMilliseconds + 1;
    hint.payload = {{QStringLiteral("hint_move_uci"), QStringLiteral("e2e4")}};
    decreasing.events.insert(1, hint);
    QVERIFY(!repository.appendBatch(decreasing, nullptr, &error));

    QSqlQuery counts(manager.database());
    for (const QString &table : {
             QStringLiteral("attempt_puzzle_records"),
             QStringLiteral("solve_attempt_instances"),
             QStringLiteral("solve_attempt_events"),
             QStringLiteral("solve_attempt_terminal_records")}) {
        QVERIFY(counts.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table)));
        QVERIFY(counts.next());
        QCOMPARE(counts.value(0).toInt(), 0);
    }
}

void TestPuzzleAttemptRepository::rejectsPersistedStartTimestampMismatch()
{
    QTemporaryDir directory;
    DatabaseManager manager(nullptr, QStringLiteral("attempt-start-timestamp-test"));
    QVERIFY(manager.initialize(directory.filePath(QStringLiteral("ledger.sqlite3"))).ok);
    PuzzleAttemptRepository repository(manager.database());
    QString error;

    AttemptAppendBatch seed = solvedBatch(fixtureRecord());
    seed.events.resize(1);
    seed.terminalAttempt.reset();
    QVERIFY2(repository.appendBatch(seed, nullptr, &error), qPrintable(error));

    AttemptInstance poisoned = *seed.newAttempt;
    poisoned.attemptInstanceId = QStringLiteral(
        "parlawl-attempt-instance-v1:99999999-7777-4888-8999-aaaaaaaaaaaa");
    const QString genesisHash = PuzzleAttemptRepository::semanticId(
        QStringLiteral("parlawl-attempt-genesis-v1"), instanceIdentityForTest(poisoned));

    QSqlQuery insertInstance(manager.database());
    insertInstance.prepare(QStringLiteral(
        "INSERT INTO solve_attempt_instances "
        "(attempt_instance_id,instance_schema,puzzle_id,puzzle_record_id,solver_id,session_id,"
        "started_at_utc,puzzle_snapshot_json,genesis_hash) VALUES (?,?,?,?,?,?,?,?,?)"));
    insertInstance.addBindValue(poisoned.attemptInstanceId);
    insertInstance.addBindValue(QString::fromLatin1(kLocalAttemptSchema));
    insertInstance.addBindValue(poisoned.puzzleId);
    insertInstance.addBindValue(poisoned.puzzleRecordId);
    insertInstance.addBindValue(poisoned.solverId);
    insertInstance.addBindValue(poisoned.sessionId);
    insertInstance.addBindValue(PuzzleAttemptRepository::pythonUtc(poisoned.startedAtUtc));
    insertInstance.addBindValue(QString::fromUtf8(
        PuzzleAttemptRepository::canonicalJson(poisoned.puzzleSnapshot)));
    insertInstance.addBindValue(genesisHash);
    QVERIFY2(insertInstance.exec(), qPrintable(insertInstance.lastError().text()));

    AttemptEventInput mismatchedStart = seed.events.first();
    mismatchedStart.occurredAtUtc = poisoned.startedAtUtc.addSecs(1);
    const QString eventHash = PuzzleAttemptRepository::semanticId(
        QStringLiteral("parlawl-attempt-event-v1"),
        eventIdentityForTest(poisoned.attemptInstanceId, 0, mismatchedStart, genesisHash));
    QString poisonedInsertError;
    auto insertPoisonedEvent = [&]() {
        QSqlQuery insertEvent(manager.database());
        insertEvent.prepare(QStringLiteral(
            "INSERT INTO solve_attempt_events "
            "(event_id,event_schema,attempt_instance_id,event_index,event_kind,occurred_at_utc,"
            "elapsed_milliseconds,payload_json,terminal_kind,previous_hash,event_hash) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?)"));
        insertEvent.addBindValue(eventHash);
        insertEvent.addBindValue(QString::fromLatin1(kLocalEventSchema));
        insertEvent.addBindValue(poisoned.attemptInstanceId);
        insertEvent.addBindValue(0);
        insertEvent.addBindValue(mismatchedStart.kind);
        insertEvent.addBindValue(PuzzleAttemptRepository::pythonUtc(mismatchedStart.occurredAtUtc));
        insertEvent.addBindValue(0);
        insertEvent.addBindValue(QString::fromUtf8(
            PuzzleAttemptRepository::canonicalJson(mismatchedStart.payload)));
        insertEvent.addBindValue(QStringLiteral(""));
        insertEvent.addBindValue(genesisHash);
        insertEvent.addBindValue(eventHash);
        const bool inserted = insertEvent.exec();
        poisonedInsertError = insertEvent.lastError().text();
        return inserted;
    };

    QVERIFY(!insertPoisonedEvent());
    QSqlQuery dropTrigger(manager.database());
    QVERIFY(dropTrigger.exec(QStringLiteral("DROP TRIGGER solve_attempt_events_require_start")));
    QVERIFY2(insertPoisonedEvent(), qPrintable(poisonedInsertError));
    QVERIFY(!repository.verifyAttempt(poisoned.attemptInstanceId, &error));
    QVERIFY(error.contains(QStringLiteral("chronology")));
}

void TestPuzzleAttemptRepository::retainsOpenAndClockRollbackAttemptsWithoutExport()
{
    QTemporaryDir directory;
    DatabaseManager manager(nullptr, QStringLiteral("attempt-open-test"));
    QVERIFY(manager.initialize(directory.filePath(QStringLiteral("ledger.sqlite3"))).ok);
    PuzzleAttemptRepository repository(manager.database());
    AttemptAppendBatch batch = solvedBatch(fixtureRecord());
    batch.events.resize(1);
    batch.terminalAttempt.reset();
    QString error;
    QVERIFY2(repository.appendBatch(batch, nullptr, &error), qPrintable(error));
    QVERIFY2(repository.verifyAttempt(batch.attemptInstanceId, &error), qPrintable(error));
    QVERIFY(!repository.exportTerminalAttempts(directory.filePath(QStringLiteral("none.jsonl")), nullptr, &error));
    QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("none.jsonl"))));

    AttemptAppendBatch invalidation;
    invalidation.attemptInstanceId = batch.attemptInstanceId;
    invalidation.expectedNextEventIndex = 1;
    QSqlQuery previous(manager.database());
    QVERIFY(previous.exec(QStringLiteral("SELECT event_hash FROM solve_attempt_events ORDER BY event_index DESC LIMIT 1")));
    QVERIFY(previous.next());
    invalidation.expectedPreviousHash = previous.value(0).toString();
    AttemptEventInput invalidated;
    invalidated.kind = QStringLiteral("attempt_invalidated");
    invalidated.terminalKind = QStringLiteral("invalidated");
    invalidated.occurredAtUtc = batch.newAttempt->startedAtUtc.addSecs(-10);
    invalidated.elapsedMilliseconds = 500;
    invalidated.payload = {
        {QStringLiteral("reason"), QStringLiteral("wall_clock_rollback")},
        {QStringLiteral("solver_outcome"), QStringLiteral("solved")},
    };
    invalidation.events.append(invalidated);
    QVERIFY2(repository.appendBatch(invalidation, nullptr, &error), qPrintable(error));
    QVERIFY2(repository.verifyAttempt(batch.attemptInstanceId, &error), qPrintable(error));
}

void TestPuzzleAttemptRepository::failsClosedWhenTerminalRecordIsTampered()
{
    QTemporaryDir directory;
    DatabaseManager manager(nullptr, QStringLiteral("attempt-tamper-test"));
    QVERIFY(manager.initialize(directory.filePath(QStringLiteral("ledger.sqlite3"))).ok);
    PuzzleAttemptRepository repository(manager.database());
    const AttemptAppendBatch batch = solvedBatch(fixtureRecord());
    QString error;
    QVERIFY2(repository.appendBatch(batch, nullptr, &error), qPrintable(error));

    QSqlQuery query(manager.database());
    QVERIFY(query.exec(QStringLiteral("DROP TRIGGER solve_attempt_terminal_records_no_update")));
    QVERIFY(query.exec(QStringLiteral("UPDATE solve_attempt_terminal_records SET wrong_move_count = 7")));
    QVERIFY(!repository.verifyAttempt(batch.attemptInstanceId, &error));
    QVERIFY(!repository.exportTerminalAttempts(directory.filePath(QStringLiteral("tampered.jsonl")), nullptr, &error));
    QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("tampered.jsonl"))));
}

void TestPuzzleAttemptRepository::canonicalizesUnicodeAndPythonUtc()
{
    const QJsonObject object{
        {QStringLiteral("z"), QString::fromUtf8("caf\xC3\xA9")},
        {QStringLiteral("a"), QStringLiteral("line\nfeed")},
    };
    QCOMPARE(
        PuzzleAttemptRepository::canonicalJson(object),
        QByteArray::fromStdString("{\"a\":\"line\\nfeed\",\"z\":\"caf\xC3\xA9\"}"));
    QCOMPARE(
        PuzzleAttemptRepository::pythonUtc(QDateTime::fromString(QStringLiteral("2026-08-16T16:00:00.123Z"), Qt::ISODate)),
        QStringLiteral("2026-08-16T16:00:00.123000Z"));
    QCOMPARE(
        PuzzleAttemptRepository::pythonUtc(QDateTime::fromString(QStringLiteral("2026-08-16T16:00:00Z"), Qt::ISODate)),
        QStringLiteral("2026-08-16T16:00:00Z"));
}

void TestPuzzleAttemptRepository::controllerLifecycleCommitsThroughRealRepository()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DatabaseManager manager(nullptr, QStringLiteral("attempt-controller-integration-test"));
    QVERIFY(manager.initialize(directory.filePath(QStringLiteral("ledger.sqlite3"))).ok);
    PuzzleAttemptRepository repository(manager.database());

    QString error;
    const auto pack = parlawl::puzzle_runner::EngineValidatedPuzzlePack::fromJsonLines(
        fixtureRecord() + '\n', &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    parlawl::puzzle_runner::SessionController controller;
    const QDateTime fixedUtc = QDateTime::fromString(
        QStringLiteral("2026-08-16T18:00:00.123Z"), Qt::ISODate);
    controller.setAttemptUtcNowProviderForTesting([fixedUtc]() { return fixedUtc; });
    controller.configurePuzzleAttemptLedger(
        &repository,
        QStringLiteral("parlawl-solver-v1:aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"),
        QStringLiteral("parlawl-session-v1:12345678-1234-4234-8234-123456789abc"));
    QVERIFY2(controller.replacePuzzles(pack->puzzles(), &error), qPrintable(error));

    controller.requestHint();
    controller.submitUserMove(QStringLiteral("d2d4"));
    QSqlQuery instances(manager.database());
    QVERIFY(instances.exec(QStringLiteral(
        "SELECT attempt_instance_id, session_id FROM solve_attempt_instances ORDER BY started_at_utc")));
    QVERIFY(instances.next());
    const QString firstInstance = instances.value(0).toString();
    const QString sharedSession = instances.value(1).toString();
    QVERIFY2(repository.verifyAttempt(firstInstance, &error), qPrintable(error));

    controller.retryPuzzle();
    controller.revealSolution();
    QVERIFY(instances.exec(QStringLiteral(
        "SELECT attempt_instance_id, session_id FROM solve_attempt_instances ORDER BY started_at_utc")));
    QStringList instanceIds;
    while (instances.next()) {
        instanceIds.append(instances.value(0).toString());
        QCOMPARE(instances.value(1).toString(), sharedSession);
    }
    QCOMPARE(instanceIds.size(), 2);
    QVERIFY(instanceIds.at(0) != instanceIds.at(1));
    for (const QString &instanceId : instanceIds) {
        QVERIFY2(repository.verifyAttempt(instanceId, &error), qPrintable(error));
    }

    const QString exportPath = QFileInfo(directory.path()).canonicalFilePath()
        + QStringLiteral("/controller-attempts.jsonl");
    int exported = 0;
    QVERIFY2(repository.exportTerminalAttempts(exportPath, &exported, &error), qPrintable(error));
    QCOMPARE(exported, 2);
}

void TestPuzzleAttemptRepository::exportsAcceptanceFixtureWhenRequested()
{
    const QString requestedPath = QString::fromLocal8Bit(qgetenv("PARLAWL_ATTEMPT_EXPORT_PATH"));
    if (requestedPath.isEmpty()) {
        QSKIP("set PARLAWL_ATTEMPT_EXPORT_PATH to an absent output path for cross-language acceptance");
    }
    QTemporaryDir directory;
    DatabaseManager manager(nullptr, QStringLiteral("attempt-acceptance-test"));
    QVERIFY(manager.initialize(directory.filePath(QStringLiteral("ledger.sqlite3"))).ok);
    PuzzleAttemptRepository repository(manager.database());
    const QByteArray record = fixtureRecord();
    const AttemptAppendBatch batch = solvedBatch(record);
    const AttemptAppendBatch revealed = revealedFailedBatch(record);
    QString error;
    QVERIFY2(repository.appendBatch(batch, nullptr, &error), qPrintable(error));
    QVERIFY2(repository.appendBatch(revealed, nullptr, &error), qPrintable(error));
    int exported = 0;
    QVERIFY2(repository.exportTerminalAttempts(requestedPath, &exported, &error), qPrintable(error));
    QCOMPARE(exported, 2);
}

QTEST_GUILESS_MAIN(TestPuzzleAttemptRepository)

#include "test_unit_puzzle_attempt_repository.moc"
