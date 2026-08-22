#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QSqlError>

#include "append_only_export.h"
#include "market_test_support.h"
#include "strict_json.h"

using namespace parlawl::market;
using namespace parlawl::market_test;
using namespace parlawl::strictjson;

namespace {

QStringList tableNames(QSqlDatabase database)
{
    QStringList names;
    QSqlQuery query(database);
    if (query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"))) {
        while (query.next()) {
            names.append(query.value(0).toString());
        }
    }
    return names;
}

QString firstInstanceId(QSqlDatabase database)
{
    QSqlQuery query(database);
    if (query.exec(QStringLiteral("SELECT attempt_instance_id FROM market_solve_attempt_instances LIMIT 1"))
        && query.next()) {
        return query.value(0).toString();
    }
    return {};
}

int countRows(QSqlDatabase database, const QString &table)
{
    QSqlQuery query(database);
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM ") + table) && query.next()) {
        return query.value(0).toInt();
    }
    return -1;
}

QList<QByteArray> readLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QList<QByteArray> lines;
    for (const QByteArray &line : file.readAll().split('\n')) {
        if (!line.trimmed().isEmpty()) {
            lines.append(line);
        }
    }
    return lines;
}

} // namespace

class TestUnitMarketAttemptRepository : public QObject
{
    Q_OBJECT

private slots:
    void migration024AppliesBesideTheChessLedger();
    void openingAPackWritesNoRowUntilTheFirstInteraction();
    void aCompletedRepWritesAVerifiableChain();
    void everyAppendOnlyTriggerRefusesItsViolation();
    void onlyReviewEventsAreAcceptedAfterATerminal();
    void aTimedOutRepIsATerminalAndIsCounted();
    void anAbandonedRepIsNeverExportable();
    void aClockRollbackInvalidatesTheRepLocally();
    void exportWritesOneOwnerOnlyFileAndRefusesAnExistingPath();
    void exportRefusesASymlinkDestination();
    void exportedResultsRecomputeTheirOwnIdentities();
    void terminalMetadataMustBeTheExactAllowlist();
};

void TestUnitMarketAttemptRepository::migration024AppliesBesideTheChessLedger()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    const QStringList tables = tableNames(harness.database());
    // Additive: the chess ledger's tables are untouched beside the market ones.
    QVERIFY(tables.contains(QStringLiteral("solve_attempt_events")));
    QVERIFY(tables.contains(QStringLiteral("solve_attempt_terminal_records")));
    QVERIFY(tables.contains(QStringLiteral("market_attempt_puzzle_records")));
    QVERIFY(tables.contains(QStringLiteral("market_solve_attempt_instances")));
    QVERIFY(tables.contains(QStringLiteral("market_solve_attempt_events")));
    QVERIFY(tables.contains(QStringLiteral("market_solve_attempt_terminal_records")));

    QSqlQuery triggers(harness.database());
    QVERIFY(triggers.exec(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'trigger' AND name LIKE 'market_%'")));
    QVERIFY(triggers.next());
    // One counterpart for each of the 023 triggers.
    QCOMPARE(triggers.value(0).toInt(), 21);
}

void TestUnitMarketAttemptRepository::openingAPackWritesNoRowUntilTheFirstInteraction()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));

    QCOMPARE(countRows(harness.database(), QStringLiteral("market_attempt_puzzle_records")), 0);
    QCOMPARE(countRows(harness.database(), QStringLiteral("market_solve_attempt_instances")), 0);

    QVERIFY2(controller.answerCategorical(QStringLiteral("planted"), &error), qPrintable(error));
    QCOMPARE(countRows(harness.database(), QStringLiteral("market_attempt_puzzle_records")), 1);
    QCOMPARE(countRows(harness.database(), QStringLiteral("market_solve_attempt_instances")), 1);
    // The start event and the first answer, in one transaction.
    QCOMPARE(countRows(harness.database(), QStringLiteral("market_solve_attempt_events")), 2);
}

void TestUnitMarketAttemptRepository::aCompletedRepWritesAVerifiableChain()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));
    QVERIFY(controller.isTerminal());

    const QString instanceId = firstInstanceId(harness.database());
    QVERIFY(!instanceId.isEmpty());
    QVERIFY2(repository.verifyAttempt(instanceId, &error), qPrintable(error));
    QCOMPARE(countRows(harness.database(), QStringLiteral("market_solve_attempt_terminal_records")), 1);

    QSqlQuery kinds(harness.database());
    QVERIFY(kinds.exec(QStringLiteral(
        "SELECT event_kind FROM market_solve_attempt_events ORDER BY event_index")));
    QStringList observed;
    while (kinds.next()) {
        observed.append(kinds.value(0).toString());
    }
    QCOMPARE(observed.first(), QStringLiteral("attempt_started"));
    QVERIFY(observed.contains(QStringLiteral("ply_answered")));
    QVERIFY(observed.contains(QStringLiteral("calibration_answered")));
    QCOMPARE(observed.last(), QStringLiteral("attempt_completed"));

    // The reveal is a post-terminal review event and verification still holds.
    QVERIFY2(controller.openReveal(&error), qPrintable(error));
    QVERIFY2(repository.verifyAttempt(instanceId, &error), qPrintable(error));
    QSqlQuery reveal(harness.database());
    QVERIFY(reveal.exec(QStringLiteral(
        "SELECT COUNT(*) FROM market_solve_attempt_events WHERE event_kind = 'reveal_opened'")));
    QVERIFY(reveal.next());
    QCOMPARE(reveal.value(0).toInt(), 1);
}

void TestUnitMarketAttemptRepository::everyAppendOnlyTriggerRefusesItsViolation()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));

    const auto refuses = [&](const QString &sql) {
        QSqlQuery query(harness.database());
        const bool executed = query.exec(sql);
        if (executed) {
            qWarning("statement unexpectedly succeeded: %s", qPrintable(sql));
        }
        return !executed;
    };

    QVERIFY(refuses(QStringLiteral("UPDATE market_attempt_puzzle_records SET puzzle_id = 'x'")));
    QVERIFY(refuses(QStringLiteral("DELETE FROM market_attempt_puzzle_records")));
    QVERIFY(refuses(QStringLiteral("UPDATE market_solve_attempt_instances SET solver_id = 'x'")));
    QVERIFY(refuses(QStringLiteral("DELETE FROM market_solve_attempt_instances")));
    QVERIFY(refuses(QStringLiteral("UPDATE market_solve_attempt_events SET event_kind = 'ply_answered'")));
    QVERIFY(refuses(QStringLiteral("DELETE FROM market_solve_attempt_events")));
    QVERIFY(refuses(QStringLiteral("UPDATE market_solve_attempt_terminal_records SET outcome = 'answered'")));
    QVERIFY(refuses(QStringLiteral("DELETE FROM market_solve_attempt_terminal_records")));

    // A record row whose identity already exists.
    QVERIFY(refuses(QStringLiteral(
        "INSERT INTO market_attempt_puzzle_records "
        "(puzzle_record_id, puzzle_id, record_schema, canonical_json, canonical_sha256, retained_at_utc) "
        "SELECT puzzle_record_id, puzzle_id, record_schema, canonical_json, canonical_sha256, retained_at_utc "
        "FROM market_attempt_puzzle_records")));

    const QString instanceId = firstInstanceId(harness.database());
    // A sequence gap.
    QVERIFY(refuses(QStringLiteral(
        "INSERT INTO market_solve_attempt_events "
        "(event_id, event_schema, attempt_instance_id, event_index, event_kind, occurred_at_utc, "
        "elapsed_milliseconds, payload_json, terminal_kind, previous_hash, event_hash) "
        "VALUES ('gap', 'parlawl/market-attempt-event/v1', '%1', 999, 'retry_requested', "
        "'2026-08-21T00:00:00Z', 0, '{}', '', 'x', 'gap')")
                        .arg(instanceId)));
    // A broken previous hash at the right index.
    QSqlQuery nextIndex(harness.database());
    QVERIFY(nextIndex.exec(QStringLiteral(
        "SELECT MAX(event_index) + 1 FROM market_solve_attempt_events")));
    QVERIFY(nextIndex.next());
    QVERIFY(refuses(QStringLiteral(
        "INSERT INTO market_solve_attempt_events "
        "(event_id, event_schema, attempt_instance_id, event_index, event_kind, occurred_at_utc, "
        "elapsed_milliseconds, payload_json, terminal_kind, previous_hash, event_hash) "
        "VALUES ('broken', 'parlawl/market-attempt-event/v1', '%1', %2, 'retry_requested', "
        "'2026-08-21T00:00:00Z', 0, '{}', '', 'not-the-previous-hash', 'broken')")
                        .arg(instanceId)
                        .arg(nextIndex.value(0).toInt())));
    // An unknown event kind.
    QVERIFY(refuses(QStringLiteral(
        "INSERT INTO market_solve_attempt_events "
        "(event_id, event_schema, attempt_instance_id, event_index, event_kind, occurred_at_utc, "
        "elapsed_milliseconds, payload_json, terminal_kind, previous_hash, event_hash) "
        "VALUES ('kind', 'parlawl/market-attempt-event/v1', '%1', 0, 'move_correct', "
        "'2026-08-21T00:00:00Z', 0, '{}', '', 'x', 'kind')")
                        .arg(instanceId)));
}

void TestUnitMarketAttemptRepository::onlyReviewEventsAreAcceptedAfterATerminal()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));

    const QString instanceId = firstInstanceId(harness.database());
    QSqlQuery tail(harness.database());
    QVERIFY(tail.exec(QStringLiteral(
        "SELECT MAX(event_index), (SELECT event_hash FROM market_solve_attempt_events "
        "ORDER BY event_index DESC LIMIT 1) FROM market_solve_attempt_events")));
    QVERIFY(tail.next());
    const int nextIndex = tail.value(0).toInt() + 1;
    const QString previousHash = tail.value(1).toString();

    QSqlQuery blocked(harness.database());
    blocked.prepare(QStringLiteral(
        "INSERT INTO market_solve_attempt_events "
        "(event_id, event_schema, attempt_instance_id, event_index, event_kind, occurred_at_utc, "
        "elapsed_milliseconds, payload_json, terminal_kind, previous_hash, event_hash) "
        "VALUES ('after', 'parlawl/market-attempt-event/v1', ?, ?, 'ply_answered', "
        "'2026-08-21T00:00:00Z', 0, '{}', '', ?, 'after')"));
    blocked.addBindValue(instanceId);
    blocked.addBindValue(nextIndex);
    blocked.addBindValue(previousHash);
    QVERIFY(!blocked.exec());
    QVERIFY2(
        blocked.lastError().text().contains(QStringLiteral("review events after terminal")),
        qPrintable(blocked.lastError().text()));

    // A second terminal is refused too.
    QSqlQuery second(harness.database());
    second.prepare(QStringLiteral(
        "INSERT INTO market_solve_attempt_events "
        "(event_id, event_schema, attempt_instance_id, event_index, event_kind, occurred_at_utc, "
        "elapsed_milliseconds, payload_json, terminal_kind, previous_hash, event_hash) "
        "VALUES ('second', 'parlawl/market-attempt-event/v1', ?, ?, 'attempt_timed_out', "
        "'2026-08-21T00:00:00Z', 0, '{}', 'timed_out', ?, 'second')"));
    second.addBindValue(instanceId);
    second.addBindValue(nextIndex);
    second.addBindValue(previousHash);
    QVERIFY(!second.exec());
}

void TestUnitMarketAttemptRepository::aTimedOutRepIsATerminalAndIsCounted()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.setPlyDeadlineMilliseconds(1);
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(controller.expireCurrentPly(&error), qPrintable(error));
    QVERIFY(controller.isTerminal());
    QCOMPARE(controller.streak(), 0);

    QSqlQuery outcome(harness.database());
    QVERIFY(outcome.exec(QStringLiteral(
        "SELECT outcome FROM market_solve_attempt_terminal_records")));
    QVERIFY(outcome.next());
    // The timeout says the operator froze. That is data, not a failure to drop.
    QCOMPARE(outcome.value(0).toString(), QStringLiteral("timed_out"));

    const QString exportPath = harness.path(QStringLiteral("timed-out.jsonl"));
    int exported = 0;
    QVERIFY2(repository.exportMarketSolveResults(exportPath, &exported, &error), qPrintable(error));
    QCOMPARE(exported, 1);
}

void TestUnitMarketAttemptRepository::anAbandonedRepIsNeverExportable()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(controller.answerCategorical(QStringLiteral("planted"), &error), qPrintable(error));
    QVERIFY2(controller.abandonAttempt(QStringLiteral("navigation"), &error), qPrintable(error));

    QCOMPARE(countRows(harness.database(), QStringLiteral("market_solve_attempt_terminal_records")), 0);
    QVERIFY(!repository.exportMarketSolveResults(
        harness.path(QStringLiteral("abandoned.jsonl")), nullptr, &error));
    QVERIFY2(error.contains(QStringLiteral("no terminal market reps")), qPrintable(error));
}

void TestUnitMarketAttemptRepository::aClockRollbackInvalidatesTheRepLocally()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;

    auto now = std::make_shared<QDateTime>(
        QDateTime::fromString(QStringLiteral("2026-08-21T12:00:00Z"), Qt::ISODate).toUTC());
    controller.setUtcNowProviderForTesting([now]() { return *now; });
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(controller.answerCategorical(QStringLiteral("planted"), &error), qPrintable(error));

    // The wall clock steps backwards past the 5,000 ms tolerance while the
    // monotonic timer barely moves.
    *now = now->addSecs(-30);
    QVERIFY(!controller.answerCategorical(QStringLiteral("stale_print_repeat"), &error));
    QVERIFY2(error.contains(QStringLiteral("5,000 ms")), qPrintable(error));
    QVERIFY(controller.clockInvalidated());

    QSqlQuery invalidated(harness.database());
    QVERIFY(invalidated.exec(QStringLiteral(
        "SELECT COUNT(*) FROM market_solve_attempt_events WHERE terminal_kind = 'invalidated'")));
    QVERIFY(invalidated.next());
    QCOMPARE(invalidated.value(0).toInt(), 1);
    QCOMPARE(countRows(harness.database(), QStringLiteral("market_solve_attempt_terminal_records")), 0);
    QVERIFY(!controller.openReveal(&error));
}

void TestUnitMarketAttemptRepository::exportWritesOneOwnerOnlyFileAndRefusesAnExistingPath()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));
    QVERIFY2(controller.openReveal(&error), qPrintable(error));

    const QString path = harness.path(QStringLiteral("market-results.jsonl"));
    int exported = 0;
    QVERIFY2(repository.exportMarketSolveResults(path, &exported, &error), qPrintable(error));
    QCOMPARE(exported, 1);
    const QFileInfo info(path);
    QVERIFY(info.exists() && info.isFile() && !info.isSymLink());
    QCOMPARE(
        QFile::permissions(path) & (QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup
                                    | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther),
        QFile::ReadOwner | QFile::WriteOwner);

    // A second export to the same path is refused, never an overwrite.
    QVERIFY(!repository.exportMarketSolveResults(path, nullptr, &error));
    QVERIFY2(error.contains(QStringLiteral("already exists")), qPrintable(error));
}

void TestUnitMarketAttemptRepository::exportRefusesASymlinkDestination()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));

    const QString victim = harness.path(QStringLiteral("victim.jsonl"));
    QFile victimFile(victim);
    QVERIFY(victimFile.open(QIODevice::WriteOnly));
    victimFile.write("original\n");
    victimFile.close();
    const QString link = harness.path(QStringLiteral("link.jsonl"));
    QVERIFY(QFile::link(victim, link));

    QVERIFY(!repository.exportMarketSolveResults(link, nullptr, &error));
    QVERIFY(!error.isEmpty());
    QFile check(victim);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("original\n"));
}

void TestUnitMarketAttemptRepository::exportedResultsRecomputeTheirOwnIdentities()
{
    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));
    QVERIFY2(controller.openReveal(&error), qPrintable(error));

    const QString path = harness.path(QStringLiteral("identities.jsonl"));
    QVERIFY2(repository.exportMarketSolveResults(path, nullptr, &error), qPrintable(error));
    const QList<QByteArray> lines = readLines(path);
    QCOMPARE(lines.size(), 2);

    JsonValue header;
    StrictJsonParser headerParser(lines.at(0));
    QVERIFY2(headerParser.parse(&header, &error), qPrintable(error));
    QCOMPARE(canonicalJson(header), lines.at(0));
    QCOMPARE(
        member(header, QStringLiteral("schema"))->string,
        QStringLiteral("arc/market-solve-results/v1"));

    JsonValue record;
    StrictJsonParser recordParser(lines.at(1));
    QVERIFY2(recordParser.parse(&record, &error), qPrintable(error));
    // Canonical, and every id recomputes from the exported bytes.
    QCOMPARE(canonicalJson(record), lines.at(1));
    const QString resultId = member(record, QStringLiteral("result_id"))->string;
    JsonValue content = record;
    removeMember(&content, QStringLiteral("result_id"));
    removeMember(&content, QStringLiteral("record_type"));
    removeMember(&content, QStringLiteral("schema"));
    QCOMPARE(semanticId(QStringLiteral("market-solve-result-v1"), content), resultId);

    const JsonValue *logical = member(record, QStringLiteral("logical_result_id"));
    QVERIFY(logical != nullptr);
    JsonValue logicalContent;
    logicalContent.kind = JsonValue::Kind::Object;
    logicalContent.objectKeys = {
        QStringLiteral("puzzle_id"),
        QStringLiteral("session_id"),
        QStringLiteral("solver_id"),
        QStringLiteral("started_at_utc"),
    };
    logicalContent.objectValues = {
        *member(record, QStringLiteral("puzzle_id")),
        *member(record, QStringLiteral("session_id")),
        *member(record, QStringLiteral("solver_id")),
        *member(record, QStringLiteral("started_at_utc")),
    };
    QCOMPARE(semanticId(QStringLiteral("market-solve-logical-v1"), logicalContent), logical->string);

    // Every ply carries its exposure, and latency is the difference.
    const JsonValue *responses = member(record, QStringLiteral("responses"));
    QVERIFY(responses != nullptr && !responses->array.isEmpty());
    for (const JsonValue &response : responses->array) {
        const JsonValue *exposed = member(response, QStringLiteral("exposed_at_ms"));
        const JsonValue *answered = member(response, QStringLiteral("answered_at_ms"));
        const JsonValue *latency = member(response, QStringLiteral("latency_ms"));
        QVERIFY(exposed != nullptr && answered != nullptr && latency != nullptr);
        QCOMPARE(latency->integer, answered->integer - exposed->integer);
    }

    // The reveal happened after the terminal, and the graded half is present.
    const JsonValue *reveal = member(record, QStringLiteral("reveal"));
    QVERIFY(reveal != nullptr && reveal->kind == JsonValue::Kind::Object);
    QVERIFY(member(*reveal, QStringLiteral("revealed_at_utc"))->string
            >= member(record, QStringLiteral("observed_at_utc"))->string);
    QVERIFY(!member(*reveal, QStringLiteral("terminal_event_hash"))->string.isEmpty());
    QVERIFY(member(record, QStringLiteral("scores"))->kind == JsonValue::Kind::Array);
    QVERIFY(member(record, QStringLiteral("line_score"))->kind == JsonValue::Kind::Object);

    // The display-integrity block cites what was actually held.
    const JsonValue *displayed = member(record, QStringLiteral("displayed"));
    QVERIFY(displayed != nullptr);
    QCOMPARE(
        member(*displayed, QStringLiteral("window_digest"))->string,
        controller.currentPuzzle() == nullptr
            ? QString()
            : member(*displayed, QStringLiteral("window_digest"))->string);
    QVERIFY(!member(*displayed, QStringLiteral("record_sha256"))->string.isEmpty());
    QVERIFY(!member(*displayed, QStringLiteral("hud_digest"))->string.isEmpty());

    // No raw source record and no identity leaked into the export.
    const QString line = QString::fromUtf8(lines.at(1));
    QVERIFY(!line.contains(QStringLiteral("\"bars\"")));
    QVERIFY(!line.contains(QStringLiteral("ticker")));
}

void TestUnitMarketAttemptRepository::terminalMetadataMustBeTheExactAllowlist()
{
    const QJsonObject allowlist = MarketAttemptRepository::exactMarketExportMetadata();
    QCOMPARE(allowlist.keys().size(), 5);
    QCOMPARE(allowlist.value(QStringLiteral("interface")).toString(), QStringLiteral("ParlAWL"));
    QCOMPARE(
        allowlist.value(QStringLiteral("scoring_policy")).toString(), marketScoringPolicyId());
    QCOMPARE(
        allowlist.value(QStringLiteral("calibration_policy")).toString(),
        marketCalibrationPolicyId());

    JournalHarness harness;
    QVERIFY2(harness.isOpen(), qPrintable(harness.message()));
    MarketAttemptRepository repository(harness.database());
    MarketSessionController controller;
    controller.configureJournal(
        &repository, &repository, &repository, opaqueSolverId(), opaqueSessionId());
    QString error;
    QVERIFY2(controller.loadPack(visibleMarketFixture(), sealedMarketFixture(), &error), qPrintable(error));
    QVERIFY2(
        completeRepWithFirstLegalAnswers(&controller, controller.header().taskSpec, &error),
        qPrintable(error));

    QSqlQuery stored(harness.database());
    QVERIFY(stored.exec(QStringLiteral(
        "SELECT metadata_json FROM market_solve_attempt_terminal_records")));
    QVERIFY(stored.next());
    QCOMPARE(
        stored.value(0).toString().toUtf8(),
        parlawl::storage::canonicalJson(allowlist));
}

QTEST_MAIN(TestUnitMarketAttemptRepository)

#include "test_unit_market_attempt_repository.moc"
