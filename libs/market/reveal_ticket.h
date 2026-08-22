#pragma once

// Layer three of continuation segregation (contract D1).
//
// A `RevealTicket` cannot be constructed by anything except the market attempt
// repository, and the repository mints one only inside the append receipt of a
// batch that committed a terminal event. A default-constructed ticket is inert
// and opens nothing, so "no terminal yet" and "forged" are the same refusal.

#include <QString>

class MarketAttemptRepository;

namespace parlawl::market {

class RevealTicket
{
public:
    //! An inert ticket. Deliberately public: a caller that has not been through
    //! the journal has one of these and nothing else, and it opens nothing.
    RevealTicket() = default;

    [[nodiscard]] bool isValid() const { return m_minted; }
    [[nodiscard]] const QString &attemptInstanceId() const { return m_attemptInstanceId; }
    [[nodiscard]] const QString &puzzleId() const { return m_puzzleId; }
    [[nodiscard]] const QString &terminalEventHash() const { return m_terminalEventHash; }

private:
    RevealTicket(QString attemptInstanceId, QString puzzleId, QString terminalEventHash)
        : m_attemptInstanceId(std::move(attemptInstanceId))
        , m_puzzleId(std::move(puzzleId))
        , m_terminalEventHash(std::move(terminalEventHash))
        , m_minted(true)
    {
    }

    friend class ::MarketAttemptRepository;

    QString m_attemptInstanceId;
    QString m_puzzleId;
    QString m_terminalEventHash;
    bool m_minted = false;
};

//! The runtime half of the guard. The vault re-asks the journal whether the
//! ticket's attempt really did commit that terminal event for that puzzle, so
//! the refusal is testable at runtime rather than only at compile time.
class TerminalEventVerifier
{
public:
    virtual ~TerminalEventVerifier() = default;

    virtual bool verifyTerminalEvent(
        const QString &attemptInstanceId,
        const QString &puzzleId,
        const QString &terminalEventHash,
        QString *errorMessage) const = 0;
};

} // namespace parlawl::market
