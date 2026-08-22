#include "puzzle_attempt_repository.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include "engine_validated_puzzle_pack.h"

using namespace parlawl::attempts;

namespace {

constexpr qsizetype kMaximumLineBytes = 1024 * 1024;
constexpr qsizetype kMaximumExportBytes = 64 * 1024 * 1024;
constexpr int kMaximumExportRecords = 100000;

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

bool exactOpaqueUuid(const QString &value, const QString &prefix)
{
    static const QRegularExpression suffixPattern(QStringLiteral(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    return value.startsWith(prefix)
        && suffixPattern.match(value.mid(prefix.size())).hasMatch();
}

QJsonObject exactExportMetadata()
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

QJsonObject exactPuzzleSnapshot(const parlawl::puzzle_runner::PuzzleDefinition &puzzle)
{
    return {
        {QStringLiteral("difficulty"), puzzle.metadata.difficulty},
        {QStringLiteral("initial_fen"), puzzle.fenStart},
        {QStringLiteral("opening_name"), puzzle.analysisSeed.openingName},
        {QStringLiteral("puzzle_id"), puzzle.id},
        {QStringLiteral("puzzle_record_id"), puzzle.analysisSeed.sourceRecordId},
        {QStringLiteral("puzzle_record_schema"), puzzle.analysisSeed.sourceRecordSchema},
        {QStringLiteral("solution_uci"), textArray(puzzle.solutionMoves)},
        {QStringLiteral("source_game_id"), puzzle.analysisSeed.sourceGameId},
        {QStringLiteral("source_provider"), puzzle.analysisSeed.sourceProvider},
        {QStringLiteral("themes"), textArray(puzzle.metadata.themes)},
    };
}

bool eventKindAndTerminalKindMatch(const QString &eventKind, const QString &terminalKind)
{
    static const QSet<QString> nonTerminalKinds{
        QStringLiteral("attempt_started"),
        QStringLiteral("move_correct"),
        QStringLiteral("move_rejected"),
        QStringLiteral("hint_granted"),
        QStringLiteral("solution_revealed_review"),
        QStringLiteral("retry_requested"),
    };
    if (nonTerminalKinds.contains(eventKind)) {
        return terminalKind.isEmpty();
    }
    return (eventKind == QStringLiteral("attempt_solved") && terminalKind == QStringLiteral("solved"))
        || (eventKind == QStringLiteral("attempt_failed_wrong_move")
            && terminalKind == QStringLiteral("failed_wrong_move"))
        || (eventKind == QStringLiteral("solution_revealed")
            && terminalKind == QStringLiteral("revealed_failed"))
        || (eventKind == QStringLiteral("attempt_invalidated")
            && terminalKind == QStringLiteral("invalidated"))
        || (eventKind == QStringLiteral("attempt_abandoned")
            && terminalKind == QStringLiteral("abandoned"));
}

bool isExportableTerminalKind(const QString &terminalKind)
{
    return terminalKind == QStringLiteral("solved")
        || terminalKind == QStringLiteral("failed_wrong_move")
        || terminalKind == QStringLiteral("revealed_failed");
}

QString systemError(const QString &prefix)
{
    return QStringLiteral("%1: %2").arg(prefix, QString::fromLocal8Bit(std::strerror(errno)));
}

void appendJsonString(QByteArray *output, const QString &value)
{
    output->append('"');
    for (int index = 0; index < value.size(); ++index) {
        const ushort code = value.at(index).unicode();
        switch (code) {
        case '"': output->append("\\\""); continue;
        case '\\': output->append("\\\\"); continue;
        case '\b': output->append("\\b"); continue;
        case '\f': output->append("\\f"); continue;
        case '\n': output->append("\\n"); continue;
        case '\r': output->append("\\r"); continue;
        case '\t': output->append("\\t"); continue;
        default: break;
        }
        if (code < 0x20) {
            output->append(QStringLiteral("\\u%1").arg(code, 4, 16, QLatin1Char('0')).toLatin1());
            continue;
        }
        if (QChar::isHighSurrogate(code) && index + 1 < value.size()
            && QChar::isLowSurrogate(value.at(index + 1).unicode())) {
            output->append(value.mid(index, 2).toUtf8());
            ++index;
            continue;
        }
        output->append(QString(value.at(index)).toUtf8());
    }
    output->append('"');
}

bool pythonStringLess(const QString &left, const QString &right)
{
    const auto leftCodePoints = left.toUcs4();
    const auto rightCodePoints = right.toUcs4();
    return std::lexicographical_compare(
        leftCodePoints.cbegin(), leftCodePoints.cend(), rightCodePoints.cbegin(), rightCodePoints.cend());
}

void appendCanonical(QByteArray *output, const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        output->append("null");
        return;
    case QJsonValue::Bool:
        output->append(value.toBool() ? "true" : "false");
        return;
    case QJsonValue::Double: {
        const double number = value.toDouble();
        if (std::isfinite(number) && std::trunc(number) == number
            && number >= static_cast<double>(std::numeric_limits<qint64>::min())
            && number <= static_cast<double>(std::numeric_limits<qint64>::max())) {
            output->append(QByteArray::number(static_cast<qint64>(number)));
        } else {
            output->append(QByteArray::number(number, 'g', 17));
        }
        return;
    }
    case QJsonValue::String:
        appendJsonString(output, value.toString());
        return;
    case QJsonValue::Array: {
        output->append('[');
        const QJsonArray array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            if (index > 0) {
                output->append(',');
            }
            appendCanonical(output, array.at(index));
        }
        output->append(']');
        return;
    }
    case QJsonValue::Object:
        break;
    }

    output->append('{');
    const QJsonObject object = value.toObject();
    QStringList keys = object.keys();
    std::sort(keys.begin(), keys.end(), pythonStringLess);
    for (qsizetype index = 0; index < keys.size(); ++index) {
        if (index > 0) {
            output->append(',');
        }
        appendJsonString(output, keys.at(index));
        output->append(':');
        appendCanonical(output, object.value(keys.at(index)));
    }
    output->append('}');
}

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool parseCanonicalObject(const QByteArray &bytes, QJsonObject *object, QString *errorMessage, const QString &label)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(errorMessage, QStringLiteral("%1 is not a JSON object: %2").arg(label, parseError.errorString()));
    }
    if (PuzzleAttemptRepository::canonicalJson(document.object()) != bytes) {
        return fail(errorMessage, QStringLiteral("%1 is not canonical JSON").arg(label));
    }
    if (object != nullptr) {
        *object = document.object();
    }
    return true;
}

QJsonObject instanceIdentityObject(const AttemptInstance &instance)
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
        {QStringLiteral("event_schema"), QString::fromLatin1(kLocalEventSchema)},
        {QStringLiteral("occurred_at_utc"), PuzzleAttemptRepository::pythonUtc(event.occurredAtUtc)},
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

bool loadInstance(QSqlDatabase database, const QString &attemptInstanceId, InstanceRow *row, QString *errorMessage)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT puzzle_id, puzzle_record_id, solver_id, session_id, started_at_utc, puzzle_snapshot_json, genesis_hash "
        "FROM solve_attempt_instances WHERE attempt_instance_id = ?"));
    query.addBindValue(attemptInstanceId);
    if (!query.exec()) {
        return fail(errorMessage, QStringLiteral("failed to read solve attempt instance: %1").arg(query.lastError().text()));
    }
    if (!query.next()) {
        return fail(errorMessage, QStringLiteral("solve attempt instance '%1' does not exist").arg(attemptInstanceId));
    }
    QJsonObject snapshot;
    const QByteArray snapshotJson = query.value(5).toString().toUtf8();
    if (snapshotJson.size() < 2 || snapshotJson.size() > kMaximumLineBytes) {
        return fail(errorMessage, QStringLiteral("puzzle snapshot exceeds its bounded cell profile"));
    }
    if (!parseCanonicalObject(snapshotJson, &snapshot, errorMessage, QStringLiteral("puzzle snapshot"))) {
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
        || PuzzleAttemptRepository::pythonUtc(row->value.startedAtUtc) != storedStartedAt) {
        return fail(errorMessage, QStringLiteral("solve attempt instance identity or timestamp is invalid"));
    }
    return true;
}

bool terminalPayloadMatches(const QJsonObject &payload, const TerminalAttemptInput &terminal)
{
    const QJsonValue durationValue = terminal.durationMilliseconds.has_value()
        ? QJsonValue(*terminal.durationMilliseconds)
        : QJsonValue(QJsonValue::Null);
    return payload.value(QStringLiteral("outcome")) == QJsonValue(terminal.outcome)
        && payload.value(QStringLiteral("duration_milliseconds")) == durationValue
        && payload.value(QStringLiteral("wrong_move_count")) == QJsonValue(terminal.wrongMoveCount)
        && payload.value(QStringLiteral("hints_used")) == QJsonValue(terminal.hintsUsed)
        && payload.value(QStringLiteral("solution_revealed")) == QJsonValue(terminal.solutionRevealed);
}

bool validateTerminalInput(
    const InstanceRow &instance,
    const AttemptEventInput &lastEvent,
    const TerminalAttemptInput &terminal,
    QString *errorMessage)
{
    if (terminal.outcome != QStringLiteral("solved") && terminal.outcome != QStringLiteral("failed")) {
        return fail(errorMessage, QStringLiteral("terminal attempt outcome must be solved or failed"));
    }
    if (terminal.solutionRevealed && terminal.outcome == QStringLiteral("solved")) {
        return fail(errorMessage, QStringLiteral("a revealed terminal attempt cannot be solved"));
    }
    if (!terminal.observedAtUtc.isValid() || terminal.observedAtUtc.toUTC() < instance.value.startedAtUtc.toUTC()) {
        return fail(errorMessage, QStringLiteral("terminal attempt observation precedes its start"));
    }
    if (PuzzleAttemptRepository::pythonUtc(terminal.observedAtUtc)
        != PuzzleAttemptRepository::pythonUtc(lastEvent.occurredAtUtc)) {
        return fail(errorMessage, QStringLiteral("terminal observation must equal the terminal journal event time"));
    }
    const std::optional<qint64> expectedDuration = lastEvent.elapsedMilliseconds > 0
        ? std::optional<qint64>(lastEvent.elapsedMilliseconds)
        : std::nullopt;
    if (terminal.durationMilliseconds != expectedDuration) {
        return fail(errorMessage, QStringLiteral("terminal duration must equal the monotonic terminal event elapsed time"));
    }
    if (terminal.wrongMoveCount < 0 || terminal.hintsUsed < 0) {
        return fail(errorMessage, QStringLiteral("terminal attempt counters cannot be negative"));
    }
    if (terminal.metadata != exactExportMetadata()) {
        return fail(errorMessage, QStringLiteral("terminal attempt metadata must use the exact privacy-safe allowlist"));
    }
    const QString expectedTerminalKind = terminal.outcome == QStringLiteral("solved")
        ? QStringLiteral("solved")
        : (terminal.solutionRevealed ? QStringLiteral("revealed_failed") : QStringLiteral("failed_wrong_move"));
    if (lastEvent.terminalKind != expectedTerminalKind || !terminalPayloadMatches(lastEvent.payload, terminal)) {
        return fail(errorMessage, QStringLiteral("terminal attempt does not match the latest terminal journal event"));
    }
    return true;
}

QJsonObject logicalAttemptContent(const InstanceRow &instance)
{
    return {
        {QStringLiteral("puzzle_id"), instance.value.puzzleId},
        {QStringLiteral("session_id"), instance.value.sessionId},
        {QStringLiteral("solver_id"), instance.value.solverId},
        {QStringLiteral("started_at_utc"), PuzzleAttemptRepository::pythonUtc(instance.value.startedAtUtc)},
    };
}

QJsonObject terminalContent(
    const InstanceRow &instance,
    const TerminalAttemptInput &terminal,
    const QString &logicalAttemptId)
{
    QJsonObject content{
        {QStringLiteral("duration_milliseconds"), terminal.durationMilliseconds.has_value()
             ? QJsonValue(*terminal.durationMilliseconds)
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("hints_used"), terminal.hintsUsed},
        {QStringLiteral("logical_attempt_id"), logicalAttemptId},
        {QStringLiteral("metadata"), terminal.metadata},
        {QStringLiteral("observed_at_utc"), PuzzleAttemptRepository::pythonUtc(terminal.observedAtUtc)},
        {QStringLiteral("outcome"), terminal.outcome},
        {QStringLiteral("puzzle_id"), instance.value.puzzleId},
        {QStringLiteral("puzzle_record_id"), instance.value.puzzleRecordId},
        {QStringLiteral("session_id"), instance.value.sessionId},
        {QStringLiteral("solution_revealed"), terminal.solutionRevealed},
        {QStringLiteral("solver_id"), instance.value.solverId},
        {QStringLiteral("started_at_utc"), PuzzleAttemptRepository::pythonUtc(instance.value.startedAtUtc)},
        {QStringLiteral("wrong_move_count"), terminal.wrongMoveCount},
    };
    return content;
}

QJsonObject terminalRecordObject(
    const InstanceRow &instance,
    const TerminalAttemptInput &terminal,
    QString *logicalAttemptId,
    QString *attemptId)
{
    *logicalAttemptId = PuzzleAttemptRepository::semanticId(
        QStringLiteral("puzzle-attempt-logical-v1"), logicalAttemptContent(instance));
    const QJsonObject content = terminalContent(instance, terminal, *logicalAttemptId);
    *attemptId = PuzzleAttemptRepository::semanticId(QStringLiteral("puzzle-attempt-v1"), content);
    QJsonObject record = content;
    record.insert(QStringLiteral("attempt_id"), *attemptId);
    record.insert(QStringLiteral("record_type"), QStringLiteral("puzzle_attempt"));
    record.insert(QStringLiteral("schema"), QString::fromLatin1(kAttemptRecordSchema));
    return record;
}

int openDirectoryWithoutSymlinks(const QString &absolutePath, QString *errorMessage)
{
    if (!QDir::isAbsolutePath(absolutePath)) {
        fail(errorMessage, QStringLiteral("export parent path must be absolute"));
        return -1;
    }
    int directoryFd = ::open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directoryFd < 0) {
        fail(errorMessage, systemError(QStringLiteral("failed to open filesystem root")));
        return -1;
    }
    const QStringList components = QDir::cleanPath(absolutePath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &component : components) {
        if (component == QStringLiteral(".") || component == QStringLiteral("..")) {
            ::close(directoryFd);
            fail(errorMessage, QStringLiteral("export parent contains an unsafe path component"));
            return -1;
        }
        const QByteArray encoded = QFile::encodeName(component);
        const int nextFd = ::openat(
            directoryFd, encoded.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (nextFd < 0) {
            const QString message = systemError(QStringLiteral("export parent contains a missing or symlink component"));
            ::close(directoryFd);
            fail(errorMessage, message);
            return -1;
        }
        ::close(directoryFd);
        directoryFd = nextFd;
    }
    struct stat directoryStat {};
    if (::fstat(directoryFd, &directoryStat) != 0 || !S_ISDIR(directoryStat.st_mode)) {
        const QString message = systemError(QStringLiteral("failed to verify export parent directory"));
        ::close(directoryFd);
        fail(errorMessage, message);
        return -1;
    }
    return directoryFd;
}

bool writeNewPrivateFile(const QString &path, const QByteArray &bytes, QString *errorMessage)
{
    const QFileInfo targetInfo(path);
    const QString fileName = targetInfo.fileName();
    if (fileName.isEmpty() || fileName == QStringLiteral(".") || fileName == QStringLiteral("..")
        || fileName.contains(QLatin1Char('/'))) {
        return fail(errorMessage, QStringLiteral("export destination must name a direct file"));
    }
    const int parentFd = openDirectoryWithoutSymlinks(targetInfo.absoluteDir().absolutePath(), errorMessage);
    if (parentFd < 0) {
        return false;
    }
    const QByteArray encodedName = QFile::encodeName(fileName);
    struct stat targetStat {};
    if (::fstatat(parentFd, encodedName.constData(), &targetStat, AT_SYMLINK_NOFOLLOW) == 0) {
        ::close(parentFd);
        return fail(errorMessage, QStringLiteral("export destination already exists; choose a new file name"));
    }
    if (errno != ENOENT) {
        const QString message = systemError(QStringLiteral("failed to inspect export destination"));
        ::close(parentFd);
        return fail(errorMessage, message);
    }

    const QByteArray temporaryName = QByteArrayLiteral(".parlawl-attempt-export-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1() + QByteArrayLiteral(".tmp");
    const int fileFd = ::openat(
        parentFd, temporaryName.constData(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fileFd < 0) {
        const QString message = systemError(QStringLiteral("failed to create private export staging file"));
        ::close(parentFd);
        return fail(errorMessage, message);
    }

    bool ok = ::fchmod(fileFd, S_IRUSR | S_IWUSR) == 0;
    qsizetype offset = 0;
    while (ok && offset < bytes.size()) {
        const ssize_t written = ::write(fileFd, bytes.constData() + offset, static_cast<size_t>(bytes.size() - offset));
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok) {
        ok = ::fsync(fileFd) == 0;
    }
    const int savedErrno = errno;
    if (::close(fileFd) != 0 && ok) {
        ok = false;
    }
    if (!ok) {
        errno = savedErrno;
        ::unlinkat(parentFd, temporaryName.constData(), 0);
        const QString message = systemError(QStringLiteral("failed to publish complete export"));
        ::close(parentFd);
        return fail(errorMessage, message);
    }

    if (::linkat(parentFd, temporaryName.constData(), parentFd, encodedName.constData(), 0) != 0) {
        const QString message = errno == EEXIST
            ? QStringLiteral("export destination already exists; choose a new file name")
            : systemError(QStringLiteral("failed to atomically publish export"));
        ::unlinkat(parentFd, temporaryName.constData(), 0);
        ::close(parentFd);
        return fail(errorMessage, message);
    }
    if (::fsync(parentFd) != 0) {
        const int publicationErrno = errno;
        const bool removedFinal = ::unlinkat(parentFd, encodedName.constData(), 0) == 0;
        struct stat stagedFinal {};
        struct stat remainingFinal {};
        const bool completeFinalRemains = !removedFinal
            && ::fstatat(parentFd, temporaryName.constData(), &stagedFinal, AT_SYMLINK_NOFOLLOW) == 0
            && ::fstatat(parentFd, encodedName.constData(), &remainingFinal, AT_SYMLINK_NOFOLLOW) == 0
            && S_ISREG(remainingFinal.st_mode)
            && stagedFinal.st_dev == remainingFinal.st_dev
            && stagedFinal.st_ino == remainingFinal.st_ino;
        ::unlinkat(parentFd, temporaryName.constData(), 0);
        ::fsync(parentFd);
        ::close(parentFd);
        if (completeFinalRemains) {
            // The complete, fsynced inode is already published. Reporting a
            // failure would leave a successful-looking no-overwrite target
            // that the caller cannot retry, so publication is success here.
            return true;
        }
        errno = publicationErrno;
        const QString message = systemError(QStringLiteral("failed to sync published export directory"));
        return fail(errorMessage, message);
    }
    if (::unlinkat(parentFd, temporaryName.constData(), 0) == 0) {
        // The final link was already made durable above. A failure to persist
        // staging-name cleanup does not turn that complete publication into a
        // reported failure.
        ::fsync(parentFd);
    }
    ::close(parentFd);
    return true;
}

} // namespace

PuzzleAttemptRepository::PuzzleAttemptRepository(const QSqlDatabase &database)
    : m_database(database)
{
}

QByteArray PuzzleAttemptRepository::canonicalJson(const QJsonValue &value)
{
    QByteArray result;
    appendCanonical(&result, value);
    return result;
}

QString PuzzleAttemptRepository::semanticId(const QString &prefix, const QJsonObject &value)
{
    return prefix + QLatin1Char(':') + sha256(canonicalJson(value));
}

QString PuzzleAttemptRepository::pythonUtc(const QDateTime &value)
{
    if (!value.isValid()) {
        return {};
    }
    const QDateTime utc = value.toUTC();
    QString result = utc.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"));
    if (utc.time().msec() != 0) {
        result += QLatin1Char('.') + QStringLiteral("%1").arg(utc.time().msec(), 3, 10, QLatin1Char('0'))
            + QStringLiteral("000");
    }
    return result + QLatin1Char('Z');
}

bool PuzzleAttemptRepository::appendBatch(
    const AttemptAppendBatch &batch,
    AttemptAppendReceipt *receipt,
    QString *errorMessage)
{
    if (!m_database.isOpen()) {
        return fail(errorMessage, QStringLiteral("solve attempt database is not open"));
    }
    if (batch.attemptInstanceId.trimmed().isEmpty() || batch.events.isEmpty()) {
        return fail(errorMessage, QStringLiteral("solve attempt batch requires an instance and at least one event"));
    }
    int exportableTerminalIndex = -1;
    for (int index = 0; index < batch.events.size(); ++index) {
        if (!isExportableTerminalKind(batch.events.at(index).terminalKind)) {
            continue;
        }
        if (exportableTerminalIndex >= 0) {
            return fail(errorMessage, QStringLiteral("solve attempt batch contains multiple exportable terminal events"));
        }
        exportableTerminalIndex = index;
    }
    if (exportableTerminalIndex >= 0
        && (exportableTerminalIndex != batch.events.size() - 1 || !batch.terminalAttempt.has_value())) {
        return fail(errorMessage, QStringLiteral(
            "an exportable terminal event must be last and include its atomic terminal record"));
    }
    if (exportableTerminalIndex < 0 && batch.terminalAttempt.has_value()) {
        return fail(errorMessage, QStringLiteral("terminal record has no matching exportable terminal event"));
    }

    QSqlQuery beginQuery(m_database);
    if (!beginQuery.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        return fail(errorMessage, QStringLiteral("failed to start solve attempt transaction: %1").arg(beginQuery.lastError().text()));
    }
    auto rollback = [&]() { m_database.rollback(); };

    if (batch.retainedPuzzleRecord.has_value()) {
        const RetainedPuzzleRecord &record = *batch.retainedPuzzleRecord;
        QJsonParseError retainedParseError;
        const QJsonDocument retainedDocument = QJsonDocument::fromJson(record.canonicalJson, &retainedParseError);
        const QJsonObject recordObject = retainedDocument.object();
        if (record.puzzleRecordId.isEmpty() || record.puzzleId.isEmpty()
            || record.recordSchema != QString::fromLatin1(kPuzzleRecordSchema)
            || !record.retainedAtUtc.isValid()
            || retainedParseError.error != QJsonParseError::NoError || !retainedDocument.isObject()
            || recordObject.value(QStringLiteral("record_id")).toString() != record.puzzleRecordId
            || recordObject.value(QStringLiteral("puzzle_id")).toString() != record.puzzleId
            || recordObject.value(QStringLiteral("schema")).toString() != record.recordSchema
            || recordObject.value(QStringLiteral("record_type")).toString() != QStringLiteral("puzzle_candidate")
            || recordObject.value(QStringLiteral("status")).toString() != QStringLiteral("engine_validated")) {
            rollback();
            return fail(errorMessage, QStringLiteral("retained puzzle record identity or status is invalid"));
        }
        QString strictParseError;
        const auto strictPack = parlawl::puzzle_runner::EngineValidatedPuzzlePack::fromJsonLines(
            record.canonicalJson + '\n', &strictParseError);
        if (!strictPack.has_value() || strictPack->puzzles().size() != 1
            || strictPack->puzzles().first().id != record.puzzleId
            || strictPack->puzzles().first().analysisSeed.sourceRecordId != record.puzzleRecordId
            || strictPack->puzzles().first().analysisSeed.rawSourceRecordJson.toUtf8() != record.canonicalJson) {
            rollback();
            return fail(
                errorMessage,
                QStringLiteral("retained puzzle record failed strict engine-line validation: %1").arg(strictParseError));
        }
        const QString contentHash = sha256(record.canonicalJson);
        QSqlQuery existing(m_database);
        existing.prepare(QStringLiteral(
            "SELECT puzzle_id, record_schema, canonical_json, canonical_sha256 FROM attempt_puzzle_records "
            "WHERE puzzle_record_id = ?"));
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
                return fail(errorMessage, QStringLiteral("retained puzzle record identity conflicts with stored content"));
            }
        } else {
            QSqlQuery insert(m_database);
            insert.prepare(QStringLiteral(
                "INSERT INTO attempt_puzzle_records "
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
                return fail(errorMessage, QStringLiteral("failed to retain puzzle record: %1").arg(insert.lastError().text()));
            }
        }
    }

    if (batch.newAttempt.has_value()) {
        const AttemptInstance &instance = *batch.newAttempt;
        if (instance.attemptInstanceId != batch.attemptInstanceId || instance.puzzleRecordId.isEmpty()
            || instance.puzzleId.isEmpty() || instance.solverId.trimmed().isEmpty()
            || instance.sessionId.trimmed().isEmpty() || !instance.startedAtUtc.isValid()
            || !exactOpaqueUuid(instance.attemptInstanceId, QStringLiteral("parlawl-attempt-instance-v1:"))
            || !exactOpaqueUuid(instance.solverId, QStringLiteral("parlawl-solver-v1:"))
            || !exactOpaqueUuid(instance.sessionId, QStringLiteral("parlawl-session-v1:"))) {
            rollback();
            return fail(errorMessage, QStringLiteral("new solve attempt identity is incomplete"));
        }
        const QByteArray snapshotJson = canonicalJson(instance.puzzleSnapshot);
        if (snapshotJson.size() < 2 || snapshotJson.size() > kMaximumLineBytes) {
            rollback();
            return fail(errorMessage, QStringLiteral("puzzle snapshot exceeds its bounded write profile"));
        }
        QSqlQuery sourceRecord(m_database);
        sourceRecord.prepare(QStringLiteral(
            "SELECT canonical_json FROM attempt_puzzle_records WHERE puzzle_record_id = ? AND puzzle_id = ?"));
        sourceRecord.addBindValue(instance.puzzleRecordId);
        sourceRecord.addBindValue(instance.puzzleId);
        if (!sourceRecord.exec() || !sourceRecord.next()) {
            rollback();
            return fail(errorMessage, QStringLiteral("new solve attempt exact puzzle record is missing"));
        }
        QString snapshotSourceError;
        const auto snapshotSourcePack = parlawl::puzzle_runner::EngineValidatedPuzzlePack::fromJsonLines(
            sourceRecord.value(0).toByteArray() + '\n', &snapshotSourceError);
        if (!snapshotSourcePack.has_value() || snapshotSourcePack->puzzles().size() != 1
            || instance.puzzleSnapshot != exactPuzzleSnapshot(snapshotSourcePack->puzzles().first())) {
            rollback();
            return fail(errorMessage, QStringLiteral(
                "puzzle snapshot does not exactly match its retained source record: %1")
                                          .arg(snapshotSourceError));
        }
        const QString genesisHash = semanticId(QStringLiteral("parlawl-attempt-genesis-v1"), instanceIdentityObject(instance));
        QSqlQuery existing(m_database);
        existing.prepare(QStringLiteral(
            "SELECT puzzle_id, puzzle_record_id, solver_id, session_id, started_at_utc, puzzle_snapshot_json, genesis_hash "
            "FROM solve_attempt_instances WHERE attempt_instance_id = ?"));
        existing.addBindValue(instance.attemptInstanceId);
        if (!existing.exec()) {
            rollback();
            return fail(errorMessage, existing.lastError().text());
        }
        if (existing.next()) {
            if (existing.value(0).toString() != instance.puzzleId
                || existing.value(1).toString() != instance.puzzleRecordId
                || existing.value(2).toString() != instance.solverId
                || existing.value(3).toString() != instance.sessionId
                || existing.value(4).toString() != pythonUtc(instance.startedAtUtc)
                || existing.value(5).toString().toUtf8() != snapshotJson
                || existing.value(6).toString() != genesisHash) {
                rollback();
                return fail(errorMessage, QStringLiteral("solve attempt instance identity conflicts with stored content"));
            }
        } else {
            QSqlQuery insert(m_database);
            insert.prepare(QStringLiteral(
                "INSERT INTO solve_attempt_instances "
                "(attempt_instance_id, instance_schema, puzzle_id, puzzle_record_id, solver_id, session_id, "
                "started_at_utc, puzzle_snapshot_json, genesis_hash) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
            insert.addBindValue(instance.attemptInstanceId);
            insert.addBindValue(QString::fromLatin1(kLocalAttemptSchema));
            insert.addBindValue(instance.puzzleId);
            insert.addBindValue(instance.puzzleRecordId);
            insert.addBindValue(instance.solverId);
            insert.addBindValue(instance.sessionId);
            insert.addBindValue(pythonUtc(instance.startedAtUtc));
            insert.addBindValue(QString::fromUtf8(snapshotJson));
            insert.addBindValue(genesisHash);
            if (!insert.exec()) {
                rollback();
                return fail(errorMessage, QStringLiteral("failed to create solve attempt: %1").arg(insert.lastError().text()));
            }
        }
    }

    InstanceRow instance;
    if (!loadInstance(m_database, batch.attemptInstanceId, &instance, errorMessage)) {
        rollback();
        return false;
    }
    const QString computedGenesis = semanticId(QStringLiteral("parlawl-attempt-genesis-v1"), instanceIdentityObject(instance.value));
    if (computedGenesis != instance.genesisHash) {
        rollback();
        return fail(errorMessage, QStringLiteral("stored solve attempt genesis hash does not match"));
    }

    QSqlQuery existingEventCount(m_database);
    existingEventCount.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM solve_attempt_events WHERE attempt_instance_id = ?"));
    existingEventCount.addBindValue(batch.attemptInstanceId);
    if (!existingEventCount.exec() || !existingEventCount.next()) {
        rollback();
        return fail(errorMessage, QStringLiteral("failed to inspect existing solve attempt events"));
    }
    if (existingEventCount.value(0).toInt() > 0) {
        QString existingVerificationError;
        if (!verifyAttemptInCurrentSnapshot(batch.attemptInstanceId, &existingVerificationError)) {
            rollback();
            return fail(errorMessage, QStringLiteral("stored solve attempt is invalid: %1")
                                          .arg(existingVerificationError));
        }
    }

    if (batch.expectedNextEventIndex < 0) {
        rollback();
        return fail(errorMessage, QStringLiteral("solve attempt expected event index cannot be negative"));
    }
    qint64 preflightElapsed = 0;
    bool preflightTerminalSeen = false;
    if (batch.expectedNextEventIndex > 0) {
        QSqlQuery priorEvent(m_database);
        priorEvent.prepare(QStringLiteral(
            "SELECT elapsed_milliseconds FROM solve_attempt_events "
            "WHERE attempt_instance_id = ? AND event_index = ?"));
        priorEvent.addBindValue(batch.attemptInstanceId);
        priorEvent.addBindValue(batch.expectedNextEventIndex - 1);
        if (!priorEvent.exec() || !priorEvent.next()) {
            rollback();
            return fail(errorMessage, QStringLiteral("solve attempt preflight predecessor is missing"));
        }
        preflightElapsed = priorEvent.value(0).toLongLong();
        QSqlQuery priorTerminal(m_database);
        priorTerminal.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM solve_attempt_events "
            "WHERE attempt_instance_id = ? AND event_index < ? AND terminal_kind != ''"));
        priorTerminal.addBindValue(batch.attemptInstanceId);
        priorTerminal.addBindValue(batch.expectedNextEventIndex);
        if (!priorTerminal.exec() || !priorTerminal.next()) {
            rollback();
            return fail(errorMessage, QStringLiteral("failed to inspect solve attempt terminal lifecycle"));
        }
        preflightTerminalSeen = priorTerminal.value(0).toInt() > 0;
    }
    for (int offset = 0; offset < batch.events.size(); ++offset) {
        const AttemptEventInput &event = batch.events.at(offset);
        const int eventIndex = batch.expectedNextEventIndex + offset;
        const bool afterTerminal = preflightTerminalSeen;
        if (!event.occurredAtUtc.isValid() || event.elapsedMilliseconds < preflightElapsed
            || !eventKindAndTerminalKindMatch(event.kind, event.terminalKind)
            || (eventIndex == 0
                && (event.kind != QStringLiteral("attempt_started")
                    || event.elapsedMilliseconds != 0
                    || pythonUtc(event.occurredAtUtc) != pythonUtc(instance.value.startedAtUtc)))
            || (eventIndex > 0 && event.kind == QStringLiteral("attempt_started"))
            || (afterTerminal
                && event.kind != QStringLiteral("solution_revealed_review")
                && event.kind != QStringLiteral("retry_requested"))) {
            rollback();
            return fail(errorMessage, QStringLiteral("solve attempt event preflight failed"));
        }
        if (!event.terminalKind.isEmpty()) {
            if (preflightTerminalSeen) {
                rollback();
                return fail(errorMessage, QStringLiteral("solve attempt contains more than one terminal event"));
            }
            preflightTerminalSeen = true;
        }
        preflightElapsed = event.elapsedMilliseconds;
    }

    QSqlQuery lastQuery(m_database);
    lastQuery.prepare(QStringLiteral(
        "SELECT event_index, event_hash FROM solve_attempt_events WHERE attempt_instance_id = ? "
        "ORDER BY event_index DESC LIMIT 1"));
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
        QString replayPrevious = expectedPrevious;
        int replayIndex = batch.expectedNextEventIndex;
        bool exactReplay = !replayPrevious.isEmpty();
        for (const AttemptEventInput &event : batch.events) {
            const QString replayHash = semanticId(
                QStringLiteral("parlawl-attempt-event-v1"),
                eventIdentityObject(batch.attemptInstanceId, replayIndex, event, replayPrevious));
            QSqlQuery replayQuery(m_database);
            replayQuery.prepare(QStringLiteral(
                "SELECT event_id, event_kind, occurred_at_utc, elapsed_milliseconds, payload_json, terminal_kind, "
                "previous_hash, event_hash FROM solve_attempt_events WHERE attempt_instance_id = ? AND event_index = ?"));
            replayQuery.addBindValue(batch.attemptInstanceId);
            replayQuery.addBindValue(replayIndex);
            if (!replayQuery.exec() || !replayQuery.next()
                || replayQuery.value(0).toString() != replayHash
                || replayQuery.value(1).toString() != event.kind
                || replayQuery.value(2).toString() != pythonUtc(event.occurredAtUtc)
                || replayQuery.value(3).toLongLong() != event.elapsedMilliseconds
                || replayQuery.value(4).toString().toUtf8() != canonicalJson(event.payload)
                || replayQuery.value(5).toString() != event.terminalKind
                || replayQuery.value(6).toString() != replayPrevious
                || replayQuery.value(7).toString() != replayHash) {
                exactReplay = false;
                break;
            }
            replayPrevious = replayHash;
            ++replayIndex;
        }
        if (exactReplay && replayIndex == nextIndex && replayPrevious == previousHash) {
            QString replayTerminalId;
            if (batch.terminalAttempt.has_value()) {
                QString logicalAttemptId;
                const QByteArray terminalJson = canonicalJson(terminalRecordObject(
                    instance, *batch.terminalAttempt, &logicalAttemptId, &replayTerminalId));
                QSqlQuery terminalReplay(m_database);
                terminalReplay.prepare(QStringLiteral(
                    "SELECT logical_attempt_id, canonical_json FROM solve_attempt_terminal_records "
                    "WHERE attempt_instance_id = ? AND attempt_id = ?"));
                terminalReplay.addBindValue(batch.attemptInstanceId);
                terminalReplay.addBindValue(replayTerminalId);
                exactReplay = terminalReplay.exec() && terminalReplay.next()
                    && terminalReplay.value(0).toString() == logicalAttemptId
                    && terminalReplay.value(1).toByteArray() == terminalJson;
            }
            if (exactReplay && m_database.commit()) {
                if (receipt != nullptr) {
                    receipt->nextEventIndex = nextIndex;
                    receipt->previousHash = previousHash;
                    receipt->terminalAttemptId = replayTerminalId;
                }
                return true;
            }
        }
        rollback();
        return fail(errorMessage, QStringLiteral("solve attempt journal advanced unexpectedly"));
    }

    for (const AttemptEventInput &event : batch.events) {
        if (!event.occurredAtUtc.isValid() || event.elapsedMilliseconds < 0) {
            rollback();
            return fail(errorMessage, QStringLiteral("solve attempt event time is invalid"));
        }
        const QJsonObject identity = eventIdentityObject(batch.attemptInstanceId, nextIndex, event, previousHash);
        const QString eventHash = semanticId(QStringLiteral("parlawl-attempt-event-v1"), identity);
        const QByteArray payloadJson = canonicalJson(event.payload);
        if (payloadJson.size() < 2 || payloadJson.size() > 65536) {
            rollback();
            return fail(errorMessage, QStringLiteral("solve attempt event payload exceeds its bounded write profile"));
        }
        QSqlQuery insert(m_database);
        insert.prepare(QStringLiteral(
            "INSERT INTO solve_attempt_events "
            "(event_id, event_schema, attempt_instance_id, event_index, event_kind, occurred_at_utc, "
            "elapsed_milliseconds, payload_json, terminal_kind, previous_hash, event_hash) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(eventHash);
        insert.addBindValue(QString::fromLatin1(kLocalEventSchema));
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
            return fail(errorMessage, QStringLiteral("failed to append solve attempt event: %1").arg(insert.lastError().text()));
        }
        previousHash = eventHash;
        ++nextIndex;
    }

    QString terminalAttemptId;
    if (batch.terminalAttempt.has_value()) {
        const TerminalAttemptInput &terminal = *batch.terminalAttempt;
        if (!validateTerminalInput(instance, batch.events.last(), terminal, errorMessage)) {
            rollback();
            return false;
        }
        QString logicalAttemptId;
        const QJsonObject terminalRecord = terminalRecordObject(instance, terminal, &logicalAttemptId, &terminalAttemptId);
        const QByteArray terminalJson = canonicalJson(terminalRecord);
        const QByteArray metadataJson = canonicalJson(terminal.metadata);
        if (terminalJson.size() < 2 || terminalJson.size() > kMaximumLineBytes
            || metadataJson.size() < 2 || metadataJson.size() > 4096) {
            rollback();
            return fail(errorMessage, QStringLiteral("terminal attempt exceeds its bounded write profile"));
        }
        QSqlQuery insert(m_database);
        insert.prepare(QStringLiteral(
            "INSERT INTO solve_attempt_terminal_records "
            "(attempt_id, logical_attempt_id, attempt_instance_id, puzzle_record_id, record_schema, outcome, "
            "observed_at_utc, duration_milliseconds, wrong_move_count, hints_used, solution_revealed, "
            "metadata_json, canonical_json, canonical_sha256) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(terminalAttemptId);
        insert.addBindValue(logicalAttemptId);
        insert.addBindValue(batch.attemptInstanceId);
        insert.addBindValue(instance.value.puzzleRecordId);
        insert.addBindValue(QString::fromLatin1(kAttemptRecordSchema));
        insert.addBindValue(terminal.outcome);
        insert.addBindValue(pythonUtc(terminal.observedAtUtc));
        if (terminal.durationMilliseconds.has_value()) {
            insert.addBindValue(*terminal.durationMilliseconds);
        } else {
            insert.addBindValue(QVariant());
        }
        insert.addBindValue(terminal.wrongMoveCount);
        insert.addBindValue(terminal.hintsUsed);
        insert.addBindValue(terminal.solutionRevealed ? 1 : 0);
        insert.addBindValue(QString::fromUtf8(metadataJson));
        insert.addBindValue(terminalJson);
        insert.addBindValue(sha256(terminalJson));
        if (!insert.exec()) {
            rollback();
            return fail(errorMessage, QStringLiteral("failed to append terminal solve attempt: %1").arg(insert.lastError().text()));
        }
    }

    if (!m_database.commit()) {
        rollback();
        return fail(errorMessage, QStringLiteral("failed to commit solve attempt batch: %1").arg(m_database.lastError().text()));
    }
    if (receipt != nullptr) {
        receipt->nextEventIndex = nextIndex;
        receipt->previousHash = previousHash;
        receipt->terminalAttemptId = terminalAttemptId;
    }
    return true;
}

bool PuzzleAttemptRepository::verifyAttempt(const QString &attemptInstanceId, QString *errorMessage) const
{
    QSqlQuery begin(m_database);
    if (!begin.exec(QStringLiteral("BEGIN"))) {
        return fail(errorMessage, QStringLiteral("failed to open solve attempt verification snapshot: %1")
                                      .arg(begin.lastError().text()));
    }
    const bool ok = verifyAttemptInCurrentSnapshot(attemptInstanceId, errorMessage);
    if (!ok) {
        m_database.rollback();
        return false;
    }
    if (!m_database.commit()) {
        m_database.rollback();
        return fail(errorMessage, QStringLiteral("failed to close solve attempt verification snapshot"));
    }
    return true;
}

bool PuzzleAttemptRepository::verifyAttemptInCurrentSnapshot(
    const QString &attemptInstanceId,
    QString *errorMessage) const
{
    InstanceRow instance;
    if (!loadInstance(m_database, attemptInstanceId, &instance, errorMessage)) {
        return false;
    }
    if (semanticId(QStringLiteral("parlawl-attempt-genesis-v1"), instanceIdentityObject(instance.value))
        != instance.genesisHash) {
        return fail(errorMessage, QStringLiteral("solve attempt genesis hash mismatch"));
    }

    QSqlQuery recordQuery(m_database);
    recordQuery.prepare(QStringLiteral(
        "SELECT puzzle_id, record_schema, canonical_json, canonical_sha256, retained_at_utc "
        "FROM attempt_puzzle_records WHERE puzzle_record_id = ?"));
    recordQuery.addBindValue(instance.value.puzzleRecordId);
    if (!recordQuery.exec() || !recordQuery.next()) {
        return fail(errorMessage, QStringLiteral("solve attempt retained puzzle record is missing"));
    }
    const QByteArray recordJson = recordQuery.value(2).toByteArray();
    const QString retainedAtText = recordQuery.value(4).toString();
    const QDateTime retainedAtUtc = QDateTime::fromString(retainedAtText, Qt::ISODate);
    if (recordJson.size() < 2 || recordJson.size() > kMaximumLineBytes
        || recordQuery.value(0).toString() != instance.value.puzzleId
        || recordQuery.value(1).toString() != QString::fromLatin1(kPuzzleRecordSchema)
        || !retainedAtUtc.isValid() || pythonUtc(retainedAtUtc) != retainedAtText) {
        return fail(errorMessage, QStringLiteral("retained puzzle record metadata is invalid"));
    }
    QString strictError;
    const auto strictPack = parlawl::puzzle_runner::EngineValidatedPuzzlePack::fromJsonLines(
        recordJson + '\n', &strictError);
    if (!strictPack.has_value() || strictPack->puzzles().size() != 1
        || recordQuery.value(3).toString() != sha256(recordJson)
        || strictPack->puzzles().first().analysisSeed.sourceRecordId != instance.value.puzzleRecordId
        || strictPack->puzzles().first().id != instance.value.puzzleId
        || strictPack->puzzles().first().analysisSeed.rawSourceRecordJson.toUtf8() != recordJson
        || instance.value.puzzleSnapshot != exactPuzzleSnapshot(strictPack->puzzles().first())) {
        return fail(errorMessage, QStringLiteral("retained puzzle record failed exact verification"));
    }

    QString previousHash = instance.genesisHash;
    int expectedIndex = 0;
    qint64 previousElapsedMilliseconds = 0;
    bool sawTerminalEvent = false;
    QString exportableTerminalKind;
    AttemptEventInput exportableTerminalEvent;
    QSqlQuery events(m_database);
    events.prepare(QStringLiteral(
        "SELECT event_id, event_index, event_kind, occurred_at_utc, elapsed_milliseconds, payload_json, "
        "terminal_kind, previous_hash, event_hash FROM solve_attempt_events "
        "WHERE attempt_instance_id = ? ORDER BY event_index"));
    events.addBindValue(attemptInstanceId);
    if (!events.exec()) {
        return fail(errorMessage, events.lastError().text());
    }
    while (events.next()) {
        QJsonObject payload;
        const QByteArray payloadBytes = events.value(5).toString().toUtf8();
        if (payloadBytes.size() < 2 || payloadBytes.size() > 65536) {
            return fail(errorMessage, QStringLiteral("solve attempt event payload exceeds its bounded cell profile"));
        }
        if (!parseCanonicalObject(
                payloadBytes, &payload, errorMessage, QStringLiteral("solve attempt event payload"))) {
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
            || event.elapsedMilliseconds < 0
            || (expectedIndex == 0 && (event.kind != QStringLiteral("attempt_started")
                                       || event.elapsedMilliseconds != 0
                                       || occurredAtText != pythonUtc(instance.value.startedAtUtc)))
            || (expectedIndex > 0 && event.kind == QStringLiteral("attempt_started"))
            || event.elapsedMilliseconds < previousElapsedMilliseconds
            || !eventKindAndTerminalKindMatch(event.kind, event.terminalKind)
            || (sawTerminalEvent
                && event.kind != QStringLiteral("solution_revealed_review")
                && event.kind != QStringLiteral("retry_requested"))) {
            return fail(errorMessage, QStringLiteral("solve attempt event chronology or kind is invalid"));
        }
        if (!event.terminalKind.isEmpty()) {
            if (sawTerminalEvent) {
                return fail(errorMessage, QStringLiteral("solve attempt has multiple terminal events"));
            }
            sawTerminalEvent = true;
        }
        const QString expectedHash = semanticId(
            QStringLiteral("parlawl-attempt-event-v1"),
            eventIdentityObject(attemptInstanceId, expectedIndex, event, previousHash));
        if (events.value(0).toString() != expectedHash || events.value(1).toInt() != expectedIndex
            || events.value(7).toString() != previousHash || events.value(8).toString() != expectedHash) {
            return fail(errorMessage, QStringLiteral("solve attempt event hash chain mismatch"));
        }
        if (event.terminalKind == QStringLiteral("solved")
            || event.terminalKind == QStringLiteral("failed_wrong_move")
            || event.terminalKind == QStringLiteral("revealed_failed")) {
            if (!exportableTerminalKind.isEmpty()) {
                return fail(errorMessage, QStringLiteral("solve attempt has multiple exportable terminal events"));
            }
            exportableTerminalKind = event.terminalKind;
            exportableTerminalEvent = event;
        }
        previousHash = expectedHash;
        previousElapsedMilliseconds = event.elapsedMilliseconds;
        ++expectedIndex;
    }
    if (expectedIndex == 0) {
        return fail(errorMessage, QStringLiteral("solve attempt has no start event"));
    }

    QSqlQuery terminalQuery(m_database);
    terminalQuery.prepare(QStringLiteral(
        "SELECT attempt_id, logical_attempt_id, puzzle_record_id, record_schema, outcome, observed_at_utc, duration_milliseconds, "
        "wrong_move_count, hints_used, solution_revealed, metadata_json, canonical_json, canonical_sha256 "
        "FROM solve_attempt_terminal_records WHERE attempt_instance_id = ?"));
    terminalQuery.addBindValue(attemptInstanceId);
    if (!terminalQuery.exec()) {
        return fail(errorMessage, terminalQuery.lastError().text());
    }
    if (!terminalQuery.next()) {
        if (!exportableTerminalKind.isEmpty()) {
            return fail(errorMessage, QStringLiteral("exportable terminal journal event has no exact terminal record"));
        }
        return true;
    }
    if (exportableTerminalKind.isEmpty()
        || terminalQuery.value(2).toString() != instance.value.puzzleRecordId
        || terminalQuery.value(3).toString() != QString::fromLatin1(kAttemptRecordSchema)) {
        return fail(errorMessage, QStringLiteral("terminal record is not linked to its journal and puzzle record"));
    }

    const QByteArray metadataBytes = terminalQuery.value(10).toString().toUtf8();
    const QByteArray storedTerminalJson = terminalQuery.value(11).toByteArray();
    if (metadataBytes.size() < 2 || metadataBytes.size() > 4096
        || storedTerminalJson.size() < 2 || storedTerminalJson.size() > kMaximumLineBytes) {
        return fail(errorMessage, QStringLiteral("terminal attempt exceeds its bounded cell profile"));
    }
    QJsonObject metadata;
    if (!parseCanonicalObject(
            metadataBytes, &metadata, errorMessage, QStringLiteral("terminal metadata"))) {
        return false;
    }
    TerminalAttemptInput terminal;
    terminal.outcome = terminalQuery.value(4).toString();
    const QString terminalObservedAtText = terminalQuery.value(5).toString();
    terminal.observedAtUtc = QDateTime::fromString(terminalObservedAtText, Qt::ISODate);
    if (!terminalQuery.value(6).isNull()) {
        terminal.durationMilliseconds = terminalQuery.value(6).toLongLong();
    }
    const qint64 wrongMoveCount = terminalQuery.value(7).toLongLong();
    const qint64 hintsUsed = terminalQuery.value(8).toLongLong();
    if (!terminal.observedAtUtc.isValid() || pythonUtc(terminal.observedAtUtc) != terminalObservedAtText
        || wrongMoveCount < 0 || wrongMoveCount > std::numeric_limits<int>::max()
        || hintsUsed < 0 || hintsUsed > std::numeric_limits<int>::max()) {
        return fail(errorMessage, QStringLiteral("terminal attempt timestamp or counters are invalid"));
    }
    terminal.wrongMoveCount = static_cast<int>(wrongMoveCount);
    terminal.hintsUsed = static_cast<int>(hintsUsed);
    terminal.solutionRevealed = terminalQuery.value(9).toInt() != 0;
    terminal.metadata = metadata;
    QString terminalValidationError;
    if (!validateTerminalInput(instance, exportableTerminalEvent, terminal, &terminalValidationError)) {
        return fail(errorMessage, QStringLiteral("terminal record does not match its journal: %1")
                                      .arg(terminalValidationError));
    }
    QString logicalAttemptId;
    QString attemptId;
    const QByteArray expectedJson = canonicalJson(terminalRecordObject(instance, terminal, &logicalAttemptId, &attemptId));
    if (terminalQuery.value(0).toString() != attemptId || terminalQuery.value(1).toString() != logicalAttemptId
        || storedTerminalJson != expectedJson
        || terminalQuery.value(12).toString() != sha256(expectedJson)) {
        return fail(errorMessage, QStringLiteral("terminal record does not exactly reconstruct from its journal and snapshot"));
    }
    return true;
}

bool PuzzleAttemptRepository::exportTerminalAttempts(
    const QString &path,
    int *exportedCount,
    QString *errorMessage) const
{
    if (exportedCount != nullptr) {
        *exportedCount = 0;
    }
    QSqlQuery begin(m_database);
    if (!begin.exec(QStringLiteral("BEGIN"))) {
        return fail(errorMessage, QStringLiteral("failed to open terminal export read snapshot: %1")
                                      .arg(begin.lastError().text()));
    }
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT attempt_id, attempt_instance_id, canonical_json FROM solve_attempt_terminal_records "
            "ORDER BY attempt_id"))) {
        m_database.rollback();
        return fail(errorMessage, QStringLiteral("failed to read terminal solve attempts: %1").arg(query.lastError().text()));
    }
    QByteArray output;
    int count = 0;
    while (query.next()) {
        if (++count > kMaximumExportRecords) {
            m_database.rollback();
            return fail(errorMessage, QStringLiteral("terminal solve attempt export exceeds 100000 records"));
        }
        QString verifyError;
        if (!verifyAttemptInCurrentSnapshot(query.value(1).toString(), &verifyError)) {
            m_database.rollback();
            return fail(errorMessage, QStringLiteral("terminal solve attempt '%1' failed closed: %2")
                                          .arg(query.value(0).toString(), verifyError));
        }
        const QByteArray line = query.value(2).toByteArray() + '\n';
        if (line.size() > kMaximumLineBytes) {
            m_database.rollback();
            return fail(errorMessage, QStringLiteral("terminal solve attempt line exceeds 1 MiB"));
        }
        if (output.size() + line.size() > kMaximumExportBytes) {
            m_database.rollback();
            return fail(errorMessage, QStringLiteral("terminal solve attempt export exceeds 64 MiB"));
        }
        output.append(line);
    }
    if (count == 0) {
        m_database.rollback();
        return fail(errorMessage, QStringLiteral("no terminal imported-puzzle attempts are available to export"));
    }
    query.finish();
    if (!m_database.commit()) {
        m_database.rollback();
        return fail(errorMessage, QStringLiteral("failed to close terminal export read snapshot"));
    }
    if (!writeNewPrivateFile(path, output, errorMessage)) {
        return false;
    }
    if (exportedCount != nullptr) {
        *exportedCount = count;
    }
    return true;
}
