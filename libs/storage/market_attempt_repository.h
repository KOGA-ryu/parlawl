#pragma once

// The market solve journal: the `024` tables, written through the same
// append-only discipline as the chess ledger and the same extracted
// publication unit.
//
// It is also the only thing in the tree that can mint a `RevealTicket`, and it
// mints one only from a committed, exportable terminal event.

#include <QJsonObject>
#include <QSqlDatabase>

#include "market_session_controller.h"
#include "puzzle_attempt_ledger.h"
#include "reveal_ticket.h"

class MarketAttemptRepository final
    : public parlawl::attempts::PuzzleAttemptSink
    , public parlawl::market::MarketRevealAuthority
    , public parlawl::market::TerminalEventVerifier
{
public:
    explicit MarketAttemptRepository(const QSqlDatabase &database);

    bool appendBatch(
        const parlawl::attempts::AttemptAppendBatch &batch,
        parlawl::attempts::AttemptAppendReceipt *receipt,
        QString *errorMessage = nullptr) override;

    //! Refuses unless the attempt's latest event is an exportable terminal.
    parlawl::market::RevealTicket ticketForCommittedTerminal(
        const QString &attemptInstanceId,
        QString *errorMessage) const override;

    bool verifyTerminalEvent(
        const QString &attemptInstanceId,
        const QString &puzzleId,
        const QString &terminalEventHash,
        QString *errorMessage) const override;

    bool verifyAttempt(const QString &attemptInstanceId, QString *errorMessage = nullptr) const;

    //! `market-solve-results-v1`: header line plus one result per completed or
    //! timed-out attempt, sorted by `result_id`. Open, abandoned and
    //! invalidated attempts never appear.
    bool exportMarketSolveResults(
        const QString &path,
        int *exportedCount = nullptr,
        QString *errorMessage = nullptr) const;

    static QJsonObject exactMarketExportMetadata();

private:
    bool verifyAttemptInCurrentSnapshot(const QString &attemptInstanceId, QString *errorMessage) const;
    bool composeResultRecord(
        const QString &attemptInstanceId,
        QJsonObject *record,
        QString *errorMessage) const;

    mutable QSqlDatabase m_database;
};
