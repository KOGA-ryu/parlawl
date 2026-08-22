#pragma once

#include <optional>

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace parlawl::attempts {

inline constexpr auto kPuzzleRecordSchema = "esports-probability-lab/puzzle-candidate/v1";
inline constexpr auto kAttemptRecordSchema = "esports-probability-lab/puzzle-attempt/v1";
inline constexpr auto kLocalAttemptSchema = "parlawl/puzzle-attempt-instance/v1";
inline constexpr auto kLocalEventSchema = "parlawl/puzzle-attempt-event/v1";

struct RetainedPuzzleRecord
{
    QString puzzleRecordId;
    QString puzzleId;
    QString recordSchema;
    QByteArray canonicalJson;
    QDateTime retainedAtUtc;
};

struct AttemptInstance
{
    QString attemptInstanceId;
    QString puzzleId;
    QString puzzleRecordId;
    QString solverId;
    QString sessionId;
    QDateTime startedAtUtc;
    QJsonObject puzzleSnapshot;
};

struct AttemptEventInput
{
    QString kind;
    QDateTime occurredAtUtc;
    qint64 elapsedMilliseconds = 0;
    QJsonObject payload;
    QString terminalKind;
};

struct TerminalAttemptInput
{
    QString outcome;
    QDateTime observedAtUtc;
    std::optional<qint64> durationMilliseconds;
    int wrongMoveCount = 0;
    int hintsUsed = 0;
    bool solutionRevealed = false;
    QJsonObject metadata;
};

struct AttemptAppendBatch
{
    std::optional<RetainedPuzzleRecord> retainedPuzzleRecord;
    std::optional<AttemptInstance> newAttempt;
    QString attemptInstanceId;
    int expectedNextEventIndex = 0;
    QString expectedPreviousHash;
    QList<AttemptEventInput> events;
    std::optional<TerminalAttemptInput> terminalAttempt;
};

struct AttemptAppendReceipt
{
    int nextEventIndex = 0;
    QString previousHash;
    QString terminalAttemptId;
};

class PuzzleAttemptSink
{
public:
    virtual ~PuzzleAttemptSink() = default;
    virtual bool appendBatch(
        const AttemptAppendBatch &batch,
        AttemptAppendReceipt *receipt,
        QString *errorMessage = nullptr) = 0;
};

} // namespace parlawl::attempts
