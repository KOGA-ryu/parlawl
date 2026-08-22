#pragma once

// The market queue and its solve lifecycle.
//
// The queue mechanics, the exposure bookkeeping and `parlawl-attempt-clock-v1`
// are the ones `SessionController` already holds for chess, re-expressed over
// the market puzzle type: `SessionController` is bound to `PuzzleDefinition`,
// `ChessPosition` and `PuzzleEngine` at every seam, so the honest move was to
// keep the rules and not the class.
//
// The controller owns the vault. No widget does.

#include <functional>
#include <memory>
#include <optional>

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>

#include "market_puzzle_pack.h"
#include "market_rating.h"
#include "market_reveal.h"
#include "market_types.h"
#include "puzzle_attempt_ledger.h"
#include "reveal_ticket.h"

namespace parlawl::market {

enum class MarketMode {
    Rush,
    Study,
};

QString marketModeText(MarketMode mode);

//! Implemented by the market attempt repository, which is the only thing that
//! can mint a `RevealTicket`.
class MarketRevealAuthority
{
public:
    virtual ~MarketRevealAuthority() = default;
    virtual RevealTicket ticketForCommittedTerminal(
        const QString &attemptInstanceId,
        QString *errorMessage) const = 0;
};

class MarketSessionController : public QObject
{
    Q_OBJECT

public:
    explicit MarketSessionController(QObject *parent = nullptr);
    ~MarketSessionController() override;

    bool loadPack(
        const QByteArray &visibleJsonLines,
        const QByteArray &sealedJsonLines,
        QString *errorMessage = nullptr);

    void configureJournal(
        parlawl::attempts::PuzzleAttemptSink *sink,
        const MarketRevealAuthority *revealAuthority,
        const parlawl::market::TerminalEventVerifier *verifier,
        const QString &solverId,
        const QString &sessionId);
    void setUtcNowProviderForTesting(std::function<QDateTime()> provider);

    void setMode(MarketMode mode);
    [[nodiscard]] MarketMode mode() const { return m_mode; }
    void setPlyDeadlineMilliseconds(qint64 milliseconds);
    [[nodiscard]] qint64 plyDeadlineMilliseconds() const { return m_plyDeadlineMs; }

    [[nodiscard]] bool hasPack() const { return m_hasPack; }
    [[nodiscard]] const MarketPackHeader &header() const { return m_header; }
    [[nodiscard]] int puzzleCount() const { return static_cast<int>(m_puzzles.size()); }
    [[nodiscard]] int currentPuzzleIndex() const { return m_currentIndex; }
    [[nodiscard]] const MarketPuzzleVisible *currentPuzzle() const;
    [[nodiscard]] const MarketPlySpec *currentPly() const;
    [[nodiscard]] int currentPlyPosition() const { return m_plyPosition; }
    [[nodiscard]] bool awaitingCalibration() const { return m_awaitingCalibration; }
    [[nodiscard]] bool isTerminal() const { return m_attempt.terminal; }
    [[nodiscard]] bool isRevealed() const { return m_reveal.has_value(); }
    [[nodiscard]] const MarketReveal *reveal() const { return m_reveal.has_value() ? &*m_reveal : nullptr; }
    [[nodiscard]] const MarketAttemptAnswers &answers() const { return m_answers; }
    [[nodiscard]] int priorExposureCount() const;
    [[nodiscard]] int streak() const { return m_streak; }
    [[nodiscard]] const MarketSolverRating &solverRating() const { return m_rating; }
    [[nodiscard]] qint64 elapsedMilliseconds() const;
    [[nodiscard]] qint64 remainingDeadlineMilliseconds() const;
    [[nodiscard]] bool clockInvalidated() const { return m_attempt.clockInvalidated; }
    [[nodiscard]] QString promptText() const;

    //! Study mode steps the disclosed continuation one bar at a time. Zero means
    //! "nothing after T is drawn yet", and before the reveal it is always zero.
    [[nodiscard]] int revealedContinuationBars() const { return m_revealedContinuationBars; }

public slots:
    bool answerCategorical(const QString &choice, QString *errorMessage = nullptr);
    bool answerBracket(const BracketChoice &bracket, QString *errorMessage = nullptr);
    bool answerConfidence(double probability, QString *errorMessage = nullptr);
    bool answerCalibration(double lower, double upper, QString *errorMessage = nullptr);
    bool expireCurrentPly(QString *errorMessage = nullptr);
    bool openReveal(QString *errorMessage = nullptr);
    bool stepContinuation();
    bool goToPuzzle(int index, QString *errorMessage = nullptr);
    bool nextPuzzle(QString *errorMessage = nullptr);
    bool previousPuzzle(QString *errorMessage = nullptr);
    bool abandonAttempt(const QString &reason, QString *errorMessage = nullptr);

signals:
    void sessionChanged();
    void promptChanged(const QString &prompt);
    void revealChanged();
    void errorRaised(const QString &message);

private:
    struct AttemptRuntimeState
    {
        bool exists = false;
        bool terminal = false;
        bool clockInvalidated = false;
        QString attemptInstanceId;
        QDateTime startedAtUtc;
        QDateTime wallClockBaselineUtc;
        QDateTime lastObservedWallUtc;
        QDateTime observedAtUtc;
        int nextEventIndex = 0;
        QString previousHash;
        QString terminalEventHash;
        qint64 lastExposureMs = 0;
        qint64 durationMs = 0;
    };

    [[nodiscard]] QDateTime utcNow() const;
    [[nodiscard]] AttemptRuntimeState attemptCandidate(const QDateTime &nowUtc) const;
    [[nodiscard]] QDateTime eventTimeForAttempt(
        const AttemptRuntimeState &candidate, const QDateTime &wallUtc) const;
    [[nodiscard]] bool wallClockRolledBack(
        const AttemptRuntimeState &candidate, const QDateTime &wallUtc) const;
    [[nodiscard]] bool clockDrifted(
        const AttemptRuntimeState &candidate,
        const QDateTime &wallUtc,
        qint64 monotonicElapsedMilliseconds) const;

    bool recordAnswer(const MarketResponse &response, QString *errorMessage);
    bool persist(
        const AttemptRuntimeState &candidate,
        const QList<parlawl::attempts::AttemptEventInput> &events,
        const std::optional<parlawl::attempts::TerminalAttemptInput> &terminal,
        QString *errorMessage);
    bool finishAttempt(bool timedOut, QString *errorMessage);
    bool invalidateForClock(QString *errorMessage);
    void resetForCurrentPuzzle();
    [[nodiscard]] QJsonObject displayedBlock() const;
    [[nodiscard]] QJsonObject responsesPayload() const;

    bool m_hasPack = false;
    MarketPackHeader m_header;
    QVector<MarketPuzzleVisible> m_puzzles;
    std::unique_ptr<SealedContinuationVault> m_vault;

    parlawl::attempts::PuzzleAttemptSink *m_sink = nullptr;
    const MarketRevealAuthority *m_revealAuthority = nullptr;
    const TerminalEventVerifier *m_verifier = nullptr;
    QString m_solverId;
    QString m_sessionId;
    std::function<QDateTime()> m_utcNowProvider;

    MarketMode m_mode = MarketMode::Rush;
    qint64 m_plyDeadlineMs = 45000;
    int m_currentIndex = -1;
    int m_plyPosition = 0;
    bool m_awaitingCalibration = false;
    int m_revealedContinuationBars = 0;
    int m_streak = 0;
    MarketSolverRating m_rating;
    MarketAttemptAnswers m_answers;
    std::optional<MarketReveal> m_reveal;
    AttemptRuntimeState m_attempt;
    QElapsedTimer m_elapsed;
    QDateTime m_exposureStartedAtUtc;
    QHash<QString, QDateTime> m_lastAttemptStartByPuzzleId;
    QHash<QString, int> m_exposureCountByPuzzleId;
};

} // namespace parlawl::market
