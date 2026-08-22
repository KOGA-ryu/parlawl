#include "market_session_controller.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>

#include "strict_json.h"

using namespace parlawl::attempts;

namespace parlawl::market {

namespace {

constexpr qint64 kAttemptClockDriftToleranceMilliseconds = 5000;

void setError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
}

QJsonValue optionalNumber(const std::optional<double> &value)
{
    return value.has_value() ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

} // namespace

QString marketModeText(MarketMode mode)
{
    return mode == MarketMode::Rush ? QStringLiteral("rush") : QStringLiteral("study");
}

MarketSessionController::MarketSessionController(QObject *parent)
    : QObject(parent)
{
}

MarketSessionController::~MarketSessionController() = default;

bool MarketSessionController::loadPack(
    const QByteArray &visibleJsonLines,
    const QByteArray &sealedJsonLines,
    QString *errorMessage)
{
    if (m_attempt.exists && !m_attempt.terminal) {
        setError(errorMessage, QStringLiteral("finish or abandon the open rep before replacing the pack"));
        return false;
    }
    auto pack = MarketPuzzlePack::fromJsonLines(visibleJsonLines, sealedJsonLines, errorMessage);
    if (!pack.has_value()) {
        // The queue is untouched: a pack that fails any check never replaces it.
        return false;
    }
    m_header = pack->header();
    m_puzzles = pack->puzzles();
    m_vault = pack->takeVault();
    m_vault->setTerminalEventVerifier(m_verifier);
    m_hasPack = true;
    m_currentIndex = m_puzzles.isEmpty() ? -1 : 0;
    m_lastAttemptStartByPuzzleId.clear();
    m_exposureCountByPuzzleId.clear();
    m_streak = 0;
    m_rating = {};
    resetForCurrentPuzzle();
    emit sessionChanged();
    emit promptChanged(promptText());
    return true;
}

void MarketSessionController::configureJournal(
    PuzzleAttemptSink *sink,
    const MarketRevealAuthority *revealAuthority,
    const TerminalEventVerifier *verifier,
    const QString &solverId,
    const QString &sessionId)
{
    if (m_attempt.exists && !m_attempt.terminal) {
        emit errorRaised(QStringLiteral("cannot replace the market journal during an open rep"));
        return;
    }
    m_sink = sink;
    m_revealAuthority = revealAuthority;
    m_verifier = verifier;
    m_solverId = solverId;
    m_sessionId = sessionId;
    if (m_vault != nullptr) {
        m_vault->setTerminalEventVerifier(m_verifier);
    }
    if (m_currentIndex >= 0) {
        m_exposureStartedAtUtc = utcNow();
        m_elapsed.start();
    }
}

void MarketSessionController::setUtcNowProviderForTesting(std::function<QDateTime()> provider)
{
    if (m_attempt.exists && !m_attempt.terminal) {
        emit errorRaised(QStringLiteral("cannot replace the market clock during an open rep"));
        return;
    }
    m_utcNowProvider = std::move(provider);
}

void MarketSessionController::setMode(MarketMode mode)
{
    if (mode == m_mode) {
        return;
    }
    if (m_attempt.exists && !m_attempt.terminal) {
        // v1 has no pause/resume state, and a paused latency measurement is not
        // a latency measurement.
        QString abandonError;
        if (!abandonAttempt(QStringLiteral("mode_switch"), &abandonError)) {
            emit errorRaised(abandonError);
            return;
        }
    }
    m_mode = mode;
    resetForCurrentPuzzle();
    emit sessionChanged();
    emit promptChanged(promptText());
}

void MarketSessionController::setPlyDeadlineMilliseconds(qint64 milliseconds)
{
    m_plyDeadlineMs = std::max<qint64>(0, milliseconds);
}

const MarketPuzzleVisible *MarketSessionController::currentPuzzle() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_puzzles.size()) {
        return nullptr;
    }
    return &m_puzzles.at(m_currentIndex);
}

const MarketPlySpec *MarketSessionController::currentPly() const
{
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    if (puzzle == nullptr || m_awaitingCalibration || m_attempt.terminal
        || m_plyPosition < 0 || m_plyPosition >= puzzle->plies.size()) {
        return nullptr;
    }
    return &puzzle->plies.at(m_plyPosition);
}

int MarketSessionController::priorExposureCount() const
{
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    if (puzzle == nullptr) {
        return 0;
    }
    return m_exposureCountByPuzzleId.value(puzzle->puzzleId, 0);
}

qint64 MarketSessionController::elapsedMilliseconds() const
{
    return m_elapsed.isValid() ? std::max<qint64>(0, m_elapsed.elapsed()) : 0;
}

qint64 MarketSessionController::remainingDeadlineMilliseconds() const
{
    if (m_mode != MarketMode::Rush || m_attempt.terminal || currentPuzzle() == nullptr) {
        return -1;
    }
    const qint64 spent = elapsedMilliseconds() - m_attempt.lastExposureMs;
    return std::max<qint64>(0, m_plyDeadlineMs - spent);
}

QString MarketSessionController::promptText() const
{
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    if (puzzle == nullptr) {
        return QStringLiteral("No market pack loaded.");
    }
    if (m_reveal.has_value()) {
        return QStringLiteral("Revealed: %1 at %2. %3")
            .arg(m_reveal->ticker, m_reveal->decisionTimeUtc, scoringKeyDisclaimer());
    }
    if (m_attempt.terminal) {
        return QStringLiteral("Rep complete. Open the reveal for the history lesson.");
    }
    if (m_awaitingCalibration) {
        return QStringLiteral("Give an %1%% interval for %2 (%3).")
            .arg(QString::number(qRound(puzzle->calibrationQuestion.intervalLevel * 100.0)),
                 puzzle->calibrationQuestion.quantity,
                 puzzle->calibrationQuestion.unit);
    }
    const MarketPlySpec *ply = currentPly();
    if (ply == nullptr) {
        return QStringLiteral("%1 — nothing further to answer.").arg(puzzle->displaySymbol);
    }
    return QStringLiteral("%1 — %2").arg(puzzle->displaySymbol, plyKindText(ply->kind));
}

QDateTime MarketSessionController::utcNow() const
{
    return m_utcNowProvider ? m_utcNowProvider().toUTC() : QDateTime::currentDateTimeUtc();
}

MarketSessionController::AttemptRuntimeState MarketSessionController::attemptCandidate(
    const QDateTime &nowUtc) const
{
    if (m_attempt.exists) {
        return m_attempt;
    }
    AttemptRuntimeState candidate;
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    if (m_sink == nullptr || puzzle == nullptr || m_solverId.isEmpty() || m_sessionId.isEmpty()) {
        return candidate;
    }
    candidate.exists = true;
    candidate.attemptInstanceId = QStringLiteral("parlawl-attempt-instance-v1:")
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    candidate.wallClockBaselineUtc = m_exposureStartedAtUtc.isValid()
        ? m_exposureStartedAtUtc.toUTC()
        : nowUtc.toUTC();
    candidate.startedAtUtc = candidate.wallClockBaselineUtc;
    const QDateTime priorStart = m_lastAttemptStartByPuzzleId.value(puzzle->puzzleId);
    if (priorStart.isValid() && candidate.startedAtUtc <= priorStart) {
        candidate.startedAtUtc = priorStart.addMSecs(1);
    }
    return candidate;
}

QDateTime MarketSessionController::eventTimeForAttempt(
    const AttemptRuntimeState &candidate,
    const QDateTime &wallUtc) const
{
    if (!candidate.startedAtUtc.isValid() || wallUtc.toUTC() >= candidate.startedAtUtc.toUTC()) {
        return wallUtc.toUTC();
    }
    // The one-millisecond start adjustment that disambiguates a same-tick retry
    // is not a clock anomaly; clamp only that synthetic gap.
    if (!wallClockRolledBack(candidate, wallUtc)) {
        return candidate.startedAtUtc.toUTC();
    }
    return wallUtc.toUTC();
}

bool MarketSessionController::wallClockRolledBack(
    const AttemptRuntimeState &candidate,
    const QDateTime &wallUtc) const
{
    const QDateTime baseline = candidate.wallClockBaselineUtc.isValid()
        ? candidate.wallClockBaselineUtc.toUTC()
        : candidate.startedAtUtc.toUTC();
    if (candidate.lastObservedWallUtc.isValid()
        && wallUtc.toUTC() < candidate.lastObservedWallUtc.toUTC()) {
        return true;
    }
    return baseline.isValid() && wallUtc.toUTC() < baseline;
}

bool MarketSessionController::clockDrifted(
    const AttemptRuntimeState &candidate,
    const QDateTime &wallUtc,
    qint64 monotonicElapsedMilliseconds) const
{
    const QDateTime baseline = candidate.wallClockBaselineUtc.isValid()
        ? candidate.wallClockBaselineUtc.toUTC()
        : candidate.startedAtUtc.toUTC();
    if (!baseline.isValid()) {
        return false;
    }
    if (wallClockRolledBack(candidate, wallUtc)) {
        return true;
    }
    const qint64 wallElapsed = baseline.msecsTo(wallUtc.toUTC());
    return std::abs(wallElapsed - monotonicElapsedMilliseconds)
        > kAttemptClockDriftToleranceMilliseconds;
}

void MarketSessionController::resetForCurrentPuzzle()
{
    m_answers = {};
    m_plyPosition = 0;
    m_awaitingCalibration = false;
    m_revealedContinuationBars = 0;
    m_reveal.reset();
    m_attempt = {};
    m_exposureStartedAtUtc = utcNow();
    m_elapsed.start();
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    if (puzzle != nullptr) {
        m_answers.calibration.questionId = puzzle->calibrationQuestion.questionId;
        m_answers.calibration.intervalLevel = puzzle->calibrationQuestion.intervalLevel;
    }
}

QJsonObject MarketSessionController::displayedBlock() const
{
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    if (puzzle == nullptr) {
        return {};
    }
    return {
        {QStringLiteral("bar_count"), puzzle->window.barCount},
        {QStringLiteral("deadline_milliseconds"),
         m_mode == MarketMode::Rush ? QJsonValue(m_plyDeadlineMs) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("hud_digest"), marketHudDigest(puzzle->hud)},
        {QStringLiteral("record_sha256"),
         parlawl::strictjson::sha256Hex(puzzle->canonicalLine)},
        {QStringLiteral("window_digest"), marketBarsDigest(puzzle->window.bars)},
    };
}

QJsonObject MarketSessionController::responsesPayload() const
{
    QJsonArray responses;
    for (const MarketResponse &response : m_answers.responses) {
        QJsonObject row{
            {QStringLiteral("answered_at_ms"), response.answeredAtMs},
            {QStringLiteral("exposed_at_ms"), response.exposedAtMs},
            {QStringLiteral("latency_ms"), response.latencyMs()},
            {QStringLiteral("ply_index"), response.plyIndex},
            {QStringLiteral("ply_kind"), plyKindText(response.kind)},
            {QStringLiteral("timed_out"), response.timedOut},
        };
        if (response.barOffset.has_value()) {
            row.insert(QStringLiteral("bar_offset"), *response.barOffset);
        }
        if (response.bracket.has_value()) {
            row.insert(
                QStringLiteral("response"),
                QJsonObject{
                    {QStringLiteral("stop_atr"), response.bracket->stopAtr},
                    {QStringLiteral("target_atr"), response.bracket->targetAtr},
                });
        } else if (response.confidence.has_value()) {
            row.insert(QStringLiteral("response"), *response.confidence);
        } else if (response.timedOut) {
            row.insert(QStringLiteral("response"), QJsonValue(QJsonValue::Null));
        } else {
            row.insert(QStringLiteral("response"), response.choice);
        }
        responses.append(row);
    }

    QJsonValue calibration(QJsonValue::Null);
    if (m_answers.calibration.answered) {
        calibration = QJsonObject{
            {QStringLiteral("answered_at_ms"), m_answers.calibration.answeredAtMs},
            {QStringLiteral("exposed_at_ms"), m_answers.calibration.exposedAtMs},
            {QStringLiteral("interval_level"), m_answers.calibration.intervalLevel},
            {QStringLiteral("latency_ms"), m_answers.calibration.latencyMs()},
            {QStringLiteral("lower"), m_answers.calibration.lower},
            {QStringLiteral("question_id"), m_answers.calibration.questionId},
            {QStringLiteral("upper"), m_answers.calibration.upper},
        };
    }
    return {
        {QStringLiteral("calibration"), calibration},
        {QStringLiteral("responses"), responses},
    };
}

bool MarketSessionController::persist(
    const AttemptRuntimeState &candidate,
    const QList<AttemptEventInput> &events,
    const std::optional<TerminalAttemptInput> &terminal,
    QString *errorMessage)
{
    if (!candidate.exists || m_sink == nullptr) {
        return true;
    }
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    if (puzzle == nullptr) {
        setError(errorMessage, QStringLiteral("no market puzzle is exposed"));
        return false;
    }
    const bool startsAttempt = !m_attempt.exists;
    AttemptAppendBatch batch;
    batch.attemptInstanceId = candidate.attemptInstanceId;
    batch.expectedNextEventIndex = startsAttempt ? 0 : m_attempt.nextEventIndex;
    batch.expectedPreviousHash = startsAttempt ? QString() : m_attempt.previousHash;

    if (startsAttempt) {
        RetainedPuzzleRecord retained;
        retained.puzzleRecordId = puzzle->recordId;
        retained.puzzleId = puzzle->puzzleId;
        retained.recordSchema = QString::fromLatin1(kMarketPuzzleRecordSchema);
        retained.canonicalJson = puzzle->canonicalLine;
        // Opening a pack creates no row. The exact visible line is retained only
        // now, in the first synchronous interaction transaction.
        retained.retainedAtUtc = utcNow();
        batch.retainedPuzzleRecord = retained;

        AttemptInstance instance;
        instance.attemptInstanceId = candidate.attemptInstanceId;
        instance.puzzleId = puzzle->puzzleId;
        instance.puzzleRecordId = puzzle->recordId;
        instance.solverId = m_solverId;
        instance.sessionId = m_sessionId;
        instance.startedAtUtc = candidate.startedAtUtc;
        instance.puzzleSnapshot = QJsonObject{
            {QStringLiteral("display_symbol"), puzzle->displaySymbol},
            {QStringLiteral("grain"), puzzle->window.grain},
            {QStringLiteral("hud_digest"), marketHudDigest(puzzle->hud)},
            // Which pack the rep came out of. The record carries its own ids but
            // not its pack's, and `market-solve-results-v1` §2.1 makes the pack
            // id the join key Arc's ingest regrades against — a results file
            // that cannot name its pack is ingested as unregradable. Journaling
            // it here rather than at export time puts it inside the attempt's
            // genesis identity, so it is as tamper-evident as the rest.
            {QStringLiteral("pack_id"), m_header.packId},
            {QStringLiteral("puzzle_id"), puzzle->puzzleId},
            {QStringLiteral("puzzle_record_id"), puzzle->recordId},
            {QStringLiteral("puzzle_record_schema"), QString::fromLatin1(kMarketPuzzleRecordSchema)},
            {QStringLiteral("response_horizon_bars"), puzzle->responseHorizonBars},
            {QStringLiteral("task_kind"), taskKindText(puzzle->taskKind)},
            {QStringLiteral("theme"), puzzle->theme},
            {QStringLiteral("window_digest"), puzzle->window.windowDigest},
        };
        batch.newAttempt = instance;

        AttemptEventInput started;
        started.kind = QStringLiteral("attempt_started");
        started.occurredAtUtc = candidate.startedAtUtc;
        started.payload = QJsonObject{
            {QStringLiteral("mode"), marketModeText(m_mode)},
            {QStringLiteral("prior_exposure_count"), priorExposureCount()},
        };
        batch.events.append(started);
    }
    batch.events.append(events);
    batch.terminalAttempt = terminal;

    AttemptAppendReceipt receipt;
    if (!m_sink->appendBatch(batch, &receipt, errorMessage)) {
        return false;
    }
    AttemptRuntimeState committed = candidate;
    committed.nextEventIndex = receipt.nextEventIndex;
    committed.previousHash = receipt.previousHash;
    if (terminal.has_value()) {
        committed.terminal = true;
        committed.terminalEventHash = receipt.previousHash;
    }
    m_attempt = committed;
    if (startsAttempt) {
        m_lastAttemptStartByPuzzleId.insert(puzzle->puzzleId, committed.startedAtUtc.toUTC());
    }
    return true;
}

bool MarketSessionController::invalidateForClock(QString *errorMessage)
{
    if (!m_attempt.exists || m_attempt.terminal) {
        m_attempt.clockInvalidated = true;
        return true;
    }
    const QDateTime wall = utcNow();
    AttemptEventInput invalidated;
    invalidated.kind = QStringLiteral("attempt_invalidated");
    invalidated.terminalKind = QStringLiteral("invalidated");
    invalidated.occurredAtUtc = eventTimeForAttempt(m_attempt, wall);
    invalidated.elapsedMilliseconds = elapsedMilliseconds();
    invalidated.payload = QJsonObject{
        {QStringLiteral("clock_policy"), QStringLiteral("parlawl-attempt-clock-v1")},
        {QStringLiteral("clock_tolerance_milliseconds"), kAttemptClockDriftToleranceMilliseconds},
        {QStringLiteral("reason"), QStringLiteral("attempt_clock_anomaly")},
    };
    AttemptRuntimeState candidate = m_attempt;
    const bool ok = persist(candidate, {invalidated}, std::nullopt, errorMessage);
    m_attempt.terminal = true;
    m_attempt.clockInvalidated = true;
    emit sessionChanged();
    return ok;
}

bool MarketSessionController::recordAnswer(const MarketResponse &response, QString *errorMessage)
{
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    if (puzzle == nullptr) {
        setError(errorMessage, QStringLiteral("no market puzzle is exposed"));
        return false;
    }
    if (m_attempt.terminal) {
        setError(errorMessage, QStringLiteral("this rep already reached a terminal"));
        return false;
    }

    const QDateTime wall = utcNow();
    const qint64 monotonic = elapsedMilliseconds();
    AttemptRuntimeState candidate = attemptCandidate(wall);
    if (candidate.exists && clockDrifted(candidate, wall, monotonic)) {
        invalidateForClock(nullptr);
        setError(
            errorMessage,
            QStringLiteral("the attempt clock moved backwards or drifted past the 5,000 ms tolerance; "
                           "this rep is invalidated locally and is not exportable"));
        return false;
    }

    MarketResponse stored = response;
    stored.exposedAtMs = m_attempt.lastExposureMs;
    stored.answeredAtMs = monotonic;
    m_answers.responses.append(stored);

    AttemptEventInput event;
    event.kind = stored.timedOut ? QStringLiteral("ply_timed_out") : QStringLiteral("ply_answered");
    event.occurredAtUtc = eventTimeForAttempt(candidate, wall);
    event.elapsedMilliseconds = monotonic;
    QJsonObject payload{
        {QStringLiteral("answered_at_ms"), stored.answeredAtMs},
        {QStringLiteral("exposed_at_ms"), stored.exposedAtMs},
        {QStringLiteral("latency_ms"), stored.latencyMs()},
        {QStringLiteral("ply_index"), stored.plyIndex},
        {QStringLiteral("ply_kind"), plyKindText(stored.kind)},
    };
    if (stored.barOffset.has_value()) {
        payload.insert(QStringLiteral("bar_offset"), *stored.barOffset);
    }
    if (stored.timedOut) {
        payload.insert(QStringLiteral("response"), QJsonValue(QJsonValue::Null));
    } else if (stored.bracket.has_value()) {
        payload.insert(
            QStringLiteral("response"),
            QJsonObject{
                {QStringLiteral("stop_atr"), stored.bracket->stopAtr},
                {QStringLiteral("target_atr"), stored.bracket->targetAtr},
            });
    } else if (stored.confidence.has_value()) {
        payload.insert(QStringLiteral("response"), *stored.confidence);
    } else {
        payload.insert(QStringLiteral("response"), stored.choice);
    }
    event.payload = payload;

    // One user action is one transaction, committed before the next question is
    // exposed.
    if (!persist(candidate, {event}, std::nullopt, errorMessage)) {
        m_answers.responses.removeLast();
        return false;
    }
    m_attempt.lastObservedWallUtc = wall;
    m_attempt.lastExposureMs = monotonic;
    ++m_plyPosition;
    if (m_plyPosition >= puzzle->plies.size()) {
        m_awaitingCalibration = true;
        m_answers.calibration.exposedAtMs = monotonic;
    }
    emit sessionChanged();
    emit promptChanged(promptText());
    return true;
}

bool MarketSessionController::answerCategorical(const QString &choice, QString *errorMessage)
{
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    const MarketPlySpec *ply = currentPly();
    if (puzzle == nullptr || ply == nullptr) {
        setError(errorMessage, QStringLiteral("there is no open question to answer"));
        return false;
    }
    const MarketTaskSpec &spec = m_header.taskSpec;
    bool permitted = false;
    switch (ply->kind) {
    case PlyKind::Entry:
        permitted = spec.entries.contains(choice);
        break;
    case PlyKind::SizeBand:
        permitted = spec.sizeBands.contains(choice);
        break;
    case PlyKind::FollowUp:
        permitted = spec.followUpActions.contains(choice);
        break;
    case PlyKind::Label:
        permitted = spec.patternLabels.contains(choice);
        break;
    case PlyKind::Verdict:
        permitted = choice == QStringLiteral("planted") || choice == QStringLiteral("clean");
        break;
    case PlyKind::ArtifactClass:
        permitted = spec.artifactClasses.contains(choice);
        break;
    default:
        permitted = false;
        break;
    }
    if (!permitted) {
        setError(
            errorMessage,
            QStringLiteral("'%1' is not in the declared enumeration for a %2 ply")
                .arg(choice, plyKindText(ply->kind)));
        return false;
    }
    MarketResponse response;
    response.plyIndex = ply->plyIndex;
    response.kind = ply->kind;
    response.barOffset = ply->barOffset;
    response.choice = choice;
    return recordAnswer(response, errorMessage);
}

bool MarketSessionController::answerBracket(const BracketChoice &bracket, QString *errorMessage)
{
    const MarketPlySpec *ply = currentPly();
    if (ply == nullptr || ply->kind != PlyKind::Bracket) {
        setError(errorMessage, QStringLiteral("the open question is not a bracket"));
        return false;
    }
    if (!m_header.taskSpec.stopAtrMultiples.contains(bracket.stopAtr)
        || !m_header.taskSpec.targetAtrMultiples.contains(bracket.targetAtr)) {
        setError(errorMessage, QStringLiteral("the bracket is off the pack's declared ATR grids"));
        return false;
    }
    MarketResponse response;
    response.plyIndex = ply->plyIndex;
    response.kind = ply->kind;
    response.bracket = bracket;
    return recordAnswer(response, errorMessage);
}

bool MarketSessionController::answerConfidence(double probability, QString *errorMessage)
{
    const MarketPlySpec *ply = currentPly();
    if (ply == nullptr || ply->kind != PlyKind::Confidence) {
        setError(errorMessage, QStringLiteral("the open question is not a confidence"));
        return false;
    }
    if (!std::isfinite(probability) || probability < 0.0 || probability > 1.0) {
        setError(errorMessage, QStringLiteral("confidence must be a probability in [0, 1]"));
        return false;
    }
    MarketResponse response;
    response.plyIndex = ply->plyIndex;
    response.kind = ply->kind;
    response.confidence = probability;
    return recordAnswer(response, errorMessage);
}

bool MarketSessionController::expireCurrentPly(QString *errorMessage)
{
    const MarketPlySpec *ply = currentPly();
    if (ply == nullptr) {
        setError(errorMessage, QStringLiteral("there is no open question to expire"));
        return false;
    }
    MarketResponse response;
    response.plyIndex = ply->plyIndex;
    response.kind = ply->kind;
    response.barOffset = ply->barOffset;
    response.timedOut = true;
    if (!recordAnswer(response, errorMessage)) {
        return false;
    }
    m_streak = 0;
    // A timeout says the operator froze. That is data, and it terminates the rep
    // rather than being discarded.
    return finishAttempt(true, errorMessage);
}

bool MarketSessionController::answerCalibration(double lower, double upper, QString *errorMessage)
{
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    if (puzzle == nullptr || !m_awaitingCalibration || m_attempt.terminal) {
        setError(errorMessage, QStringLiteral("the calibration question is not open"));
        return false;
    }
    if (!std::isfinite(lower) || !std::isfinite(upper) || lower > upper
        || lower < puzzle->calibrationQuestion.lowerBound
        || upper > puzzle->calibrationQuestion.upperBound) {
        setError(errorMessage, QStringLiteral("the interval is inverted or outside the pack's bounds"));
        return false;
    }
    const QDateTime wall = utcNow();
    const qint64 monotonic = elapsedMilliseconds();
    AttemptRuntimeState candidate = attemptCandidate(wall);
    if (candidate.exists && clockDrifted(candidate, wall, monotonic)) {
        invalidateForClock(nullptr);
        setError(errorMessage, QStringLiteral("the attempt clock failed its policy; this rep is invalidated"));
        return false;
    }

    MarketCalibrationAnswer previous = m_answers.calibration;
    m_answers.calibration.questionId = puzzle->calibrationQuestion.questionId;
    m_answers.calibration.intervalLevel = puzzle->calibrationQuestion.intervalLevel;
    m_answers.calibration.lower = lower;
    m_answers.calibration.upper = upper;
    m_answers.calibration.answeredAtMs = monotonic;
    m_answers.calibration.answered = true;

    AttemptEventInput event;
    event.kind = QStringLiteral("calibration_answered");
    event.occurredAtUtc = eventTimeForAttempt(candidate, wall);
    event.elapsedMilliseconds = monotonic;
    event.payload = QJsonObject{
        {QStringLiteral("answered_at_ms"), m_answers.calibration.answeredAtMs},
        {QStringLiteral("exposed_at_ms"), m_answers.calibration.exposedAtMs},
        {QStringLiteral("interval_level"), m_answers.calibration.intervalLevel},
        {QStringLiteral("latency_ms"), m_answers.calibration.latencyMs()},
        {QStringLiteral("lower"), lower},
        {QStringLiteral("question_id"), m_answers.calibration.questionId},
        {QStringLiteral("upper"), upper},
    };
    if (!persist(candidate, {event}, std::nullopt, errorMessage)) {
        m_answers.calibration = previous;
        return false;
    }
    m_attempt.lastObservedWallUtc = wall;
    m_attempt.lastExposureMs = monotonic;
    m_awaitingCalibration = false;
    return finishAttempt(false, errorMessage);
}

bool MarketSessionController::finishAttempt(bool timedOut, QString *errorMessage)
{
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    if (puzzle == nullptr || m_attempt.terminal) {
        return true;
    }
    const QDateTime wall = utcNow();
    const qint64 monotonic = elapsedMilliseconds();
    AttemptRuntimeState candidate = attemptCandidate(wall);
    if (candidate.exists && clockDrifted(candidate, wall, monotonic)) {
        invalidateForClock(nullptr);
        setError(errorMessage, QStringLiteral("the attempt clock failed its policy; this rep is invalidated"));
        return false;
    }

    QJsonObject payload = responsesPayload();
    payload.insert(QStringLiteral("displayed"), displayedBlock());
    payload.insert(
        QStringLiteral("duration_milliseconds"),
        monotonic > 0 ? QJsonValue(monotonic) : QJsonValue(QJsonValue::Null));
    payload.insert(QStringLiteral("mode"), marketModeText(m_mode));
    payload.insert(
        QStringLiteral("outcome"),
        timedOut ? QStringLiteral("timed_out") : QStringLiteral("answered"));
    payload.insert(QStringLiteral("prior_exposure_count"), priorExposureCount());
    payload.insert(QStringLiteral("task_kind"), taskKindText(puzzle->taskKind));

    AttemptEventInput terminalEvent;
    terminalEvent.kind = timedOut
        ? QStringLiteral("attempt_timed_out")
        : QStringLiteral("attempt_completed");
    terminalEvent.terminalKind = timedOut
        ? QStringLiteral("timed_out")
        : QStringLiteral("completed");
    terminalEvent.occurredAtUtc = eventTimeForAttempt(candidate, wall);
    terminalEvent.elapsedMilliseconds = monotonic;
    terminalEvent.payload = payload;

    TerminalAttemptInput terminal;
    terminal.outcome = timedOut ? QStringLiteral("timed_out") : QStringLiteral("answered");
    terminal.observedAtUtc = terminalEvent.occurredAtUtc;
    terminal.durationMilliseconds = monotonic > 0 ? std::optional<qint64>(monotonic) : std::nullopt;
    terminal.metadata = QJsonObject{
        {QStringLiteral("attempt_policy"), QStringLiteral("parlawl-terminal-attempt-v1")},
        {QStringLiteral("calibration_policy"), marketCalibrationPolicyId()},
        {QStringLiteral("clock_policy"), QStringLiteral("parlawl-attempt-clock-v1")},
        {QStringLiteral("interface"), QStringLiteral("ParlAWL")},
        {QStringLiteral("scoring_policy"), marketScoringPolicyId()},
    };

    if (!persist(candidate, {terminalEvent}, terminal, errorMessage)) {
        return false;
    }
    m_attempt.lastObservedWallUtc = wall;
    m_attempt.observedAtUtc = terminalEvent.occurredAtUtc;
    m_attempt.durationMs = monotonic;
    m_attempt.terminal = true;
    m_exposureCountByPuzzleId[puzzle->puzzleId] = m_exposureCountByPuzzleId.value(puzzle->puzzleId, 0) + 1;
    emit sessionChanged();
    emit promptChanged(promptText());
    return true;
}

bool MarketSessionController::abandonAttempt(const QString &reason, QString *errorMessage)
{
    if (!m_attempt.exists || m_attempt.terminal) {
        m_attempt.terminal = true;
        return true;
    }
    const QDateTime wall = utcNow();
    AttemptEventInput abandoned;
    abandoned.kind = QStringLiteral("attempt_abandoned");
    abandoned.terminalKind = QStringLiteral("abandoned");
    abandoned.occurredAtUtc = eventTimeForAttempt(m_attempt, wall);
    abandoned.elapsedMilliseconds = elapsedMilliseconds();
    abandoned.payload = QJsonObject{{QStringLiteral("reason"), reason}};
    AttemptRuntimeState candidate = m_attempt;
    if (!persist(candidate, {abandoned}, std::nullopt, errorMessage)) {
        return false;
    }
    m_attempt.terminal = true;
    emit sessionChanged();
    return true;
}

bool MarketSessionController::openReveal(QString *errorMessage)
{
    const MarketPuzzleVisible *puzzle = currentPuzzle();
    if (puzzle == nullptr) {
        setError(errorMessage, QStringLiteral("no market puzzle is exposed"));
        return false;
    }
    if (m_reveal.has_value()) {
        return true;
    }
    if (!m_attempt.exists || !m_attempt.terminal || m_attempt.clockInvalidated) {
        setError(
            errorMessage,
            QStringLiteral("the reveal opens only after a committed terminal event; there is no peek "
                           "and no setting"));
        return false;
    }
    if (m_vault == nullptr || m_revealAuthority == nullptr) {
        setError(errorMessage, QStringLiteral("this session has no sealed vault to open"));
        return false;
    }
    QString ticketError;
    const RevealTicket ticket =
        m_revealAuthority->ticketForCommittedTerminal(m_attempt.attemptInstanceId, &ticketError);
    if (!ticket.isValid()) {
        setError(errorMessage, ticketError);
        return false;
    }
    auto opened = m_vault->open(*puzzle, m_answers, ticket, errorMessage);
    if (!opened.has_value()) {
        return false;
    }

    const QDateTime wall = utcNow();
    const qint64 monotonic = elapsedMilliseconds();
    AttemptEventInput revealEvent;
    revealEvent.kind = QStringLiteral("reveal_opened");
    revealEvent.occurredAtUtc = eventTimeForAttempt(m_attempt, wall);
    revealEvent.elapsedMilliseconds = monotonic;
    QJsonArray scores;
    for (const PlyScore &score : opened->scoreCard.plyScores) {
        scores.append(QJsonObject{
            {QStringLiteral("key"), score.keyText},
            {QStringLiteral("match"), plyMatchText(score.match)},
            {QStringLiteral("ply_index"), score.plyIndex},
            {QStringLiteral("score"), score.score},
        });
    }
    const LineScore &line = opened->scoreCard.lineScore;
    revealEvent.payload = QJsonObject{
        {QStringLiteral("brier_score"), optionalNumber(opened->scoreCard.brierScore)},
        {QStringLiteral("calibration"),
         opened->scoreCard.calibration.answered
             ? QJsonValue(QJsonObject{
                 {QStringLiteral("covered"), opened->scoreCard.calibration.covered},
                 {QStringLiteral("realized_value"), opened->scoreCard.calibration.realizedValue},
                 {QStringLiteral("winkler_score"), opened->scoreCard.calibration.winklerScore},
             })
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("line_score"),
         QJsonObject{
             {QStringLiteral("human_r_multiple"), optionalNumber(line.humanRMultiple)},
             {QStringLiteral("perfect_r_multiple"), optionalNumber(line.perfectRMultiple)},
             {QStringLiteral("plies_exact"), line.pliesExact},
             {QStringLiteral("plies_total"), line.pliesTotal},
             {QStringLiteral("rule_r_multiple"), optionalNumber(line.ruleRMultiple)},
             {QStringLiteral("shortfall_vs_perfect"), optionalNumber(line.shortfallVsPerfect)},
             {QStringLiteral("shortfall_vs_rule"), optionalNumber(line.shortfallVsRule)},
         }},
        {QStringLiteral("scores"), scores},
        {QStringLiteral("scoring_policy"), marketScoringPolicyId()},
        {QStringLiteral("terminal_event_hash"), m_attempt.terminalEventHash},
        {QStringLiteral("ticket_verified"), true},
    };
    AttemptRuntimeState candidate = m_attempt;
    if (!persist(candidate, {revealEvent}, std::nullopt, errorMessage)) {
        return false;
    }

    m_reveal = std::move(opened);
    m_revealedContinuationBars = 0;
    const double repScore = repScoreFromCard(m_reveal->scoreCard);
    m_rating = updateSolverRating(m_rating, puzzle->ratingSeed, repScore);
    m_streak = repScore >= 1.0 ? m_streak + 1 : 0;
    emit revealChanged();
    emit sessionChanged();
    emit promptChanged(promptText());
    return true;
}

bool MarketSessionController::stepContinuation()
{
    if (!m_reveal.has_value() || m_mode != MarketMode::Study) {
        return false;
    }
    if (m_revealedContinuationBars >= m_reveal->continuationBars.size()) {
        return false;
    }
    ++m_revealedContinuationBars;
    emit sessionChanged();
    return true;
}

bool MarketSessionController::goToPuzzle(int index, QString *errorMessage)
{
    if (index < 0 || index >= m_puzzles.size()) {
        setError(errorMessage, QStringLiteral("the market queue has no puzzle at that position"));
        return false;
    }
    if (m_attempt.exists && !m_attempt.terminal) {
        if (!abandonAttempt(QStringLiteral("navigation"), errorMessage)) {
            return false;
        }
    }
    m_currentIndex = index;
    resetForCurrentPuzzle();
    emit sessionChanged();
    emit promptChanged(promptText());
    return true;
}

bool MarketSessionController::nextPuzzle(QString *errorMessage)
{
    // The queue prefers a puzzle this solver has not seen; once revealed, the
    // identity is known and a second look is a different measurement.
    for (int offset = 1; offset <= m_puzzles.size(); ++offset) {
        const int index = (m_currentIndex + offset) % m_puzzles.size();
        if (m_exposureCountByPuzzleId.value(m_puzzles.at(index).puzzleId, 0) == 0) {
            return goToPuzzle(index, errorMessage);
        }
    }
    if (m_puzzles.isEmpty()) {
        setError(errorMessage, QStringLiteral("the market queue is empty"));
        return false;
    }
    return goToPuzzle((m_currentIndex + 1) % m_puzzles.size(), errorMessage);
}

bool MarketSessionController::previousPuzzle(QString *errorMessage)
{
    if (m_currentIndex <= 0) {
        setError(errorMessage, QStringLiteral("already at the first rep in the queue"));
        return false;
    }
    return goToPuzzle(m_currentIndex - 1, errorMessage);
}

} // namespace parlawl::market
