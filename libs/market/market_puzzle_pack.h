#pragma once

// The market import boundary. Rides the same strict-JSON discipline as
// `libs/puzzle_runner/engine_validated_puzzle_pack.*`: bounded framing,
// recursive duplicate-key rejection, exact field sets, Python-compatible
// canonical semantic-ID recomputation, and a stricter consumer profile than the
// producer contract permits.
//
// It adds one thing the chess boundary does not need: the two files import
// atomically or not at all, and the continuation half never reaches the type a
// panel is handed. There is no blitz-only mode that runs without the key,
// because a rep whose answer cannot be scored is not a rep.
//
// These checks establish internal consistency, not producer authenticity. The
// pack's scoring key is a declared rule's line replayed on the continuation.

#include <memory>
#include <optional>

#include <QByteArray>
#include <QString>
#include <QVector>

#include "market_types.h"
#include "sealed_continuation_vault.h"

namespace parlawl::market {

class MarketPuzzlePack
{
public:
    MarketPuzzlePack(MarketPuzzlePack &&) noexcept;
    MarketPuzzlePack &operator=(MarketPuzzlePack &&) noexcept;
    MarketPuzzlePack(const MarketPuzzlePack &) = delete;
    MarketPuzzlePack &operator=(const MarketPuzzlePack &) = delete;
    ~MarketPuzzlePack();

    //! Both files or neither. A visible file whose sealed partner is missing,
    //! mismatched, or short one record does not load.
    static std::optional<MarketPuzzlePack> fromJsonLines(
        const QByteArray &visibleJsonLines,
        const QByteArray &sealedJsonLines,
        QString *errorMessage = nullptr);

    [[nodiscard]] const MarketPackHeader &header() const { return m_header; }
    [[nodiscard]] const QVector<MarketPuzzleVisible> &puzzles() const { return m_puzzles; }

    //! Hand the sealed half to whoever owns the session. A widget never calls
    //! this, and after the move the pack holds no continuation at all.
    [[nodiscard]] std::unique_ptr<SealedContinuationVault> takeVault() { return std::move(m_vault); }

private:
    MarketPuzzlePack();

    MarketPackHeader m_header;
    QVector<MarketPuzzleVisible> m_puzzles;
    std::unique_ptr<SealedContinuationVault> m_vault;
};

//! Self-consistency check of one retained visible line, standing alone.
//!
//! A journal row carries the record but not its pack header or its sealed
//! partner, so the cross-file half of the import cannot be re-run from it. What
//! is checkable alone is checked here and nothing is assumed: strict parse,
//! exact field set, canonical round-trip, and `record_id` recomputation.
bool verifyRetainedMarketPuzzleLine(
    const QByteArray &canonicalLine,
    const QString &expectedRecordId,
    const QString &expectedPuzzleId,
    QString *errorMessage = nullptr);

} // namespace parlawl::market
