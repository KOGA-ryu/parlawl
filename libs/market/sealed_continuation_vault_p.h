#pragma once

// Private to `libs/market/`. Names the continuation, so it is on the sealed
// side of the wall like `market_continuation.h` itself.

#include <QHash>

#include "market_continuation.h"
#include "sealed_continuation_vault.h"

namespace parlawl::market {

class SealedContinuationVault::Impl
{
public:
    QHash<QString, MarketContinuation> continuations;
    QString continuationPackId;
    const TerminalEventVerifier *verifier = nullptr;
};

} // namespace parlawl::market
