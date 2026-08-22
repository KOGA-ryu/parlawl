#pragma once

// Layer two of continuation segregation (contract D1).
//
// This header is safe for a widget to include, and that is the whole point of
// its shape: the sealed records live behind an opaque `Impl`, so including this
// file does not put `MarketContinuation` into scope. The only way out is
// `open()`, which needs a minted `RevealTicket` and a journal that still agrees
// the ticket's terminal event happened.

#include <memory>
#include <optional>

#include <QString>
#include <QVector>

#include "market_reveal.h"
#include "market_types.h"
#include "reveal_ticket.h"

namespace parlawl::market {

class MarketPuzzlePack;

class SealedContinuationVault
{
public:
    SealedContinuationVault();
    ~SealedContinuationVault();
    SealedContinuationVault(SealedContinuationVault &&other) noexcept;
    SealedContinuationVault &operator=(SealedContinuationVault &&other) noexcept;
    SealedContinuationVault(const SealedContinuationVault &) = delete;
    SealedContinuationVault &operator=(const SealedContinuationVault &) = delete;

    //! The verifier is owned by the session, never by a widget.
    void setTerminalEventVerifier(const TerminalEventVerifier *verifier);

    [[nodiscard]] int size() const;
    [[nodiscard]] bool holds(const QString &puzzleId) const;
    [[nodiscard]] QString continuationPackId() const;

    //! Refuses unless the ticket was minted, names this puzzle, and the journal
    //! still confirms its terminal event. Never partially discloses.
    [[nodiscard]] std::optional<MarketReveal> open(
        const MarketPuzzleVisible &visible,
        const MarketAttemptAnswers &answers,
        const RevealTicket &ticket,
        QString *errorMessage = nullptr) const;

private:
    friend class MarketPuzzlePack;

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace parlawl::market
