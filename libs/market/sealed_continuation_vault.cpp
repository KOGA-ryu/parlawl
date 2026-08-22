#include "sealed_continuation_vault_p.h"

#include "market_grader.h"

namespace parlawl::market {

namespace {

void setError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
}

} // namespace

SealedContinuationVault::SealedContinuationVault()
    : m_impl(std::make_unique<Impl>())
{
}

SealedContinuationVault::~SealedContinuationVault() = default;

SealedContinuationVault::SealedContinuationVault(SealedContinuationVault &&other) noexcept = default;

SealedContinuationVault &SealedContinuationVault::operator=(
    SealedContinuationVault &&other) noexcept = default;

void SealedContinuationVault::setTerminalEventVerifier(const TerminalEventVerifier *verifier)
{
    m_impl->verifier = verifier;
}

int SealedContinuationVault::size() const
{
    return static_cast<int>(m_impl->continuations.size());
}

bool SealedContinuationVault::holds(const QString &puzzleId) const
{
    return m_impl->continuations.contains(puzzleId);
}

QString SealedContinuationVault::continuationPackId() const
{
    return m_impl->continuationPackId;
}

std::optional<MarketReveal> SealedContinuationVault::open(
    const MarketPuzzleVisible &visible,
    const MarketAttemptAnswers &answers,
    const RevealTicket &ticket,
    QString *errorMessage) const
{
    if (!ticket.isValid()) {
        setError(
            errorMessage,
            QStringLiteral("the reveal vault requires a ticket minted by a committed terminal event"));
        return std::nullopt;
    }
    if (ticket.puzzleId() != visible.puzzleId) {
        setError(errorMessage, QStringLiteral("reveal ticket names a different puzzle"));
        return std::nullopt;
    }
    if (m_impl->verifier == nullptr) {
        setError(errorMessage, QStringLiteral("the reveal vault has no journal to verify against"));
        return std::nullopt;
    }
    QString verificationError;
    if (!m_impl->verifier->verifyTerminalEvent(
            ticket.attemptInstanceId(),
            ticket.puzzleId(),
            ticket.terminalEventHash(),
            &verificationError)) {
        setError(
            errorMessage,
            QStringLiteral("reveal ticket failed journal verification: %1").arg(verificationError));
        return std::nullopt;
    }
    const auto found = m_impl->continuations.constFind(visible.puzzleId);
    if (found == m_impl->continuations.cend()) {
        setError(errorMessage, QStringLiteral("the reveal vault holds no continuation for this puzzle"));
        return std::nullopt;
    }
    if (found->continuationCommitment != visible.continuationCommitment) {
        setError(errorMessage, QStringLiteral("sealed commitment no longer matches the visible record"));
        return std::nullopt;
    }
    return buildReveal(visible, *found, answers);
}

} // namespace parlawl::market
