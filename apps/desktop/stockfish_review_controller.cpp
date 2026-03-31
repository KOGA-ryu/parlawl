#include "stockfish_review_controller.h"

#include <QtMath>

namespace {

QString trimmedPv(const QString &pv)
{
    return pv.simplified();
}

} // namespace

StockfishReviewController::StockfishReviewController(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    connect(m_process, &QProcess::started, this, &StockfishReviewController::onProcessStarted);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &StockfishReviewController::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &StockfishReviewController::onReadyReadStandardError);
    connect(m_process, &QProcess::finished, this, &StockfishReviewController::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &StockfishReviewController::onProcessError);
    finalizeUnavailable(QStringLiteral("configure a valid stockfish path to review the current position"));
}

void StockfishReviewController::setEnginePath(const QString &enginePath)
{
    const QString normalized = enginePath.trimmed();
    if (m_enginePath == normalized) {
        return;
    }
    m_enginePath = normalized;
    m_cache.clear();
    resetProcess();
    finalizeFromCacheOrUnavailable(m_currentFen);
}

void StockfishReviewController::setAutoRefreshEnabled(bool enabled)
{
    m_autoRefreshEnabled = enabled;
}

void StockfishReviewController::clearCache()
{
    m_cache.clear();
    if (!m_currentFen.isEmpty()) {
        finalizeFromCacheOrUnavailable(m_currentFen);
    }
}

void StockfishReviewController::resetCurrentReview(const QString &message)
{
    resetProcess();
    m_currentFen.clear();
    m_snapshot.fen.clear();
    m_snapshot.statusText = message;
    m_snapshot.evaluationText = QStringLiteral("n/a");
    m_snapshot.bestMove = QStringLiteral("n/a");
    m_snapshot.pvLine = QStringLiteral("n/a");
    m_snapshot.whiteExpectation = 0.5;
    m_snapshot.available = false;
    m_snapshot.inProgress = false;
    emit reviewUpdated();
}

void StockfishReviewController::requestReview(const QString &fen, bool forceRefresh)
{
    m_currentFen = fen.trimmed();
    if (m_currentFen.isEmpty()) {
        finalizeUnavailable(QStringLiteral("no board position is available for review"));
        return;
    }

    if (m_enginePath.isEmpty()) {
        finalizeUnavailable(QStringLiteral("configure a valid stockfish path to review the current position"));
        return;
    }

    const QString key = cacheKeyForFen(m_currentFen);
    if (!forceRefresh && m_cache.contains(key)) {
        m_snapshot = m_cache.value(key);
        m_snapshot.inProgress = false;
        emit reviewUpdated();
        return;
    }

    if (m_process->state() != QProcess::NotRunning) {
        resetProcess();
    }

    startEngineRequest(m_currentFen);
}

void StockfishReviewController::refreshCurrent()
{
    if (m_currentFen.isEmpty()) {
        finalizeUnavailable(QStringLiteral("no board position is available for review"));
        return;
    }
    requestReview(m_currentFen, true);
}

void StockfishReviewController::onProcessStarted()
{
    m_state = ProcessState::WaitingForUciOk;
    m_process->write("uci\n");
}

void StockfishReviewController::onReadyReadStandardOutput()
{
    parseOutputChunk(m_process->readAllStandardOutput());
}

void StockfishReviewController::onReadyReadStandardError()
{
    const QString stderrText = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
    if (!stderrText.isEmpty()) {
        m_snapshot.statusText = QStringLiteral("engine stderr: %1").arg(stderrText);
        emit reviewUpdated();
    }
}

void StockfishReviewController::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    if (m_state == ProcessState::Idle) {
        return;
    }

    if (exitStatus != QProcess::NormalExit) {
        finalizeUnavailable(QStringLiteral("stockfish review process did not exit cleanly"));
        return;
    }

    if (m_requestedFen.isEmpty()) {
        finalizeUnavailable(QStringLiteral("stockfish review ended without a position"));
        return;
    }

    finalizeSuccessfulReview();
}

void StockfishReviewController::finalizeSuccessfulReview()
{
    if (m_requestedFen.isEmpty()) {
        finalizeUnavailable(QStringLiteral("stockfish review ended without a position"));
        return;
    }

    StockfishReviewSnapshot snapshot;
    snapshot.fen = m_requestedFen;
    snapshot.available = true;
    snapshot.inProgress = false;
    snapshot.bestMove = m_lastInfo.bestMove.isEmpty() ? QStringLiteral("unknown") : m_lastInfo.bestMove;
    snapshot.pvLine = m_lastInfo.pvLine.isEmpty() ? QStringLiteral("none") : m_lastInfo.pvLine;
    snapshot.whiteExpectation = expectationFromScore();

    if (m_lastInfo.hasMate) {
        const int whiteMate = sideToMoveIsWhite(m_requestedFen) ? m_lastInfo.mate : -m_lastInfo.mate;
        snapshot.evaluationText = whiteMate > 0
            ? QStringLiteral("mate for white in %1").arg(whiteMate)
            : QStringLiteral("mate for black in %1").arg(std::abs(whiteMate));
    } else if (m_lastInfo.hasCp) {
        const double whiteCp = sideToMoveIsWhite(m_requestedFen) ? m_lastInfo.cp : -m_lastInfo.cp;
        snapshot.evaluationText = QStringLiteral("%1%2").arg(whiteCp >= 0.0 ? QLatin1Char('+') : QLatin1Char('-')).arg(QString::number(std::abs(whiteCp) / 100.0, 'f', 2));
    } else {
        snapshot.evaluationText = QStringLiteral("unknown");
    }

    if (m_lastInfo.hasWdl) {
        const int whiteWins = sideToMoveIsWhite(m_requestedFen) ? m_lastInfo.wins : m_lastInfo.losses;
        const int whiteLosses = sideToMoveIsWhite(m_requestedFen) ? m_lastInfo.losses : m_lastInfo.wins;
        snapshot.statusText = QStringLiteral("stockfish ready • wdl %1/%2/%3").arg(
            QString::number(whiteWins),
            QString::number(m_lastInfo.draws),
            QString::number(whiteLosses));
    } else {
        snapshot.statusText = QStringLiteral("stockfish ready");
    }

    m_cache.insert(cacheKeyForFen(m_requestedFen), snapshot);
    m_snapshot = snapshot;
    m_requestedFen.clear();
    m_state = ProcessState::Idle;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->write("quit\n");
        m_process->waitForFinished(250);
    }
    emit reviewUpdated();
}

void StockfishReviewController::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    finalizeUnavailable(QStringLiteral("failed to launch stockfish review"));
}

void StockfishReviewController::resetProcess()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
    m_stdoutBuffer.clear();
    m_requestedFen.clear();
    m_lastInfo = {};
    m_state = ProcessState::Idle;
}

void StockfishReviewController::finalizeUnavailable(const QString &message)
{
    resetProcess();
    m_snapshot.fen = m_currentFen;
    m_snapshot.statusText = message;
    m_snapshot.evaluationText = QStringLiteral("n/a");
    m_snapshot.bestMove = QStringLiteral("n/a");
    m_snapshot.pvLine = QStringLiteral("n/a");
    m_snapshot.whiteExpectation = 0.5;
    m_snapshot.available = false;
    m_snapshot.inProgress = false;
    emit reviewUpdated();
}

void StockfishReviewController::finalizeFromCacheOrUnavailable(const QString &fen)
{
    const QString key = cacheKeyForFen(fen);
    if (!fen.isEmpty() && m_cache.contains(key)) {
        m_snapshot = m_cache.value(key);
        m_snapshot.inProgress = false;
        emit reviewUpdated();
        return;
    }
    finalizeUnavailable(QStringLiteral("configure a valid stockfish path to review the current position"));
}

void StockfishReviewController::parseOutputChunk(const QByteArray &chunk)
{
    m_stdoutBuffer.append(chunk);
    while (true) {
        const int newlineIndex = m_stdoutBuffer.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }
        const QByteArray lineBytes = m_stdoutBuffer.left(newlineIndex).trimmed();
        m_stdoutBuffer.remove(0, newlineIndex + 1);
        if (!lineBytes.isEmpty()) {
            handleOutputLine(QString::fromUtf8(lineBytes));
        }
    }
}

void StockfishReviewController::handleOutputLine(const QString &line)
{
    if (m_state == ProcessState::WaitingForUciOk) {
        if (line == QStringLiteral("uciok")) {
            m_process->write("setoption name UCI_ShowWDL value true\n");
            m_process->write("isready\n");
            m_state = ProcessState::WaitingForReadyOk;
        }
        return;
    }

    if (m_state == ProcessState::WaitingForReadyOk) {
        if (line == QStringLiteral("readyok")) {
            m_process->write(QStringLiteral("position fen %1\n").arg(m_requestedFen).toUtf8());
            m_process->write("go movetime 150\n");
            m_state = ProcessState::WaitingForBestMove;
        }
        return;
    }

    if (m_state != ProcessState::WaitingForBestMove) {
        return;
    }

    if (line.startsWith(QStringLiteral("bestmove "))) {
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            m_lastInfo.bestMove = parts.at(1);
        }
        finalizeSuccessfulReview();
        return;
    }

    if (!line.startsWith(QStringLiteral("info "))) {
        return;
    }

    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int index = 0; index < parts.size(); ++index) {
        if (parts.at(index) == QStringLiteral("score") && index + 2 < parts.size()) {
            if (parts.at(index + 1) == QStringLiteral("cp")) {
                m_lastInfo.hasCp = true;
                m_lastInfo.hasMate = false;
                m_lastInfo.cp = parts.at(index + 2).toInt();
            } else if (parts.at(index + 1) == QStringLiteral("mate")) {
                m_lastInfo.hasMate = true;
                m_lastInfo.hasCp = false;
                m_lastInfo.mate = parts.at(index + 2).toInt();
            }
        } else if (parts.at(index) == QStringLiteral("wdl") && index + 3 < parts.size()) {
            m_lastInfo.hasWdl = true;
            m_lastInfo.wins = parts.at(index + 1).toInt();
            m_lastInfo.draws = parts.at(index + 2).toInt();
            m_lastInfo.losses = parts.at(index + 3).toInt();
        } else if (parts.at(index) == QStringLiteral("pv") && index + 1 < parts.size()) {
            m_lastInfo.pvLine = trimmedPv(parts.mid(index + 1).join(QLatin1Char(' ')));
            break;
        }
    }
}

void StockfishReviewController::startEngineRequest(const QString &fen)
{
    m_requestedFen = fen;
    m_stdoutBuffer.clear();
    m_lastInfo = {};
    m_state = ProcessState::Idle;

    m_snapshot.fen = fen;
    m_snapshot.statusText = QStringLiteral("analyzing current position...");
    m_snapshot.evaluationText = QStringLiteral("working...");
    m_snapshot.bestMove = QStringLiteral("working...");
    m_snapshot.pvLine = QStringLiteral("working...");
    m_snapshot.whiteExpectation = 0.5;
    m_snapshot.available = true;
    m_snapshot.inProgress = true;
    emit reviewUpdated();

    m_process->start(m_enginePath);
}

double StockfishReviewController::expectationFromScore() const
{
    if (m_lastInfo.hasWdl) {
        const double total = static_cast<double>(m_lastInfo.wins + m_lastInfo.draws + m_lastInfo.losses);
        if (total > 0.0) {
            const double whiteWins = sideToMoveIsWhite(m_requestedFen) ? m_lastInfo.wins : m_lastInfo.losses;
            const double whiteDraws = m_lastInfo.draws;
            return qBound(0.0, (whiteWins + (0.5 * whiteDraws)) / total, 1.0);
        }
    }

    if (m_lastInfo.hasMate) {
        const int whiteMate = sideToMoveIsWhite(m_requestedFen) ? m_lastInfo.mate : -m_lastInfo.mate;
        return whiteMate > 0 ? 1.0 : 0.0;
    }

    if (m_lastInfo.hasCp) {
        const double whiteCp = sideToMoveIsWhite(m_requestedFen) ? m_lastInfo.cp : -m_lastInfo.cp;
        const double winPercent = 50.0 + 50.0 * (2.0 / (1.0 + qExp(-0.00368208 * whiteCp)) - 1.0);
        return qBound(0.0, winPercent / 100.0, 1.0);
    }

    return 0.5;
}

QString StockfishReviewController::cacheKeyForFen(const QString &fen) const
{
    return m_enginePath + QStringLiteral("||") + fen;
}

bool StockfishReviewController::sideToMoveIsWhite(const QString &fen) const
{
    const QStringList parts = fen.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return parts.size() > 1 ? parts.at(1) == QStringLiteral("w") : true;
}
