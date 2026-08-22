#include "replay_session.h"

#include <algorithm>

#include <QStringList>
#include <QtGlobal>

namespace parlawl::puzzle_runner {

namespace {

void setError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
}

QString canonicalFen(const ChessPosition &position)
{
    QStringList fields = position.toFen().split(QLatin1Char(' '));
    if (fields.size() != 6 || fields.at(3) == QStringLiteral("-")) {
        return position.toFen();
    }
    const int target = ChessPosition::squareFromName(fields.at(3));
    bool legalCapture = false;
    for (const Move &move : position.legalMoves()) {
        const Piece piece = position.pieceAt(move.from);
        if (move.to == target && piece.type == PieceType::Pawn
            && ChessPosition::fileOf(move.from) != ChessPosition::fileOf(move.to)) {
            legalCapture = true;
            break;
        }
    }
    if (!legalCapture) {
        fields[3] = QStringLiteral("-");
    }
    return fields.join(QLatin1Char(' '));
}

QString canonicalFenAfterMove(
    const ChessPosition &before,
    const Move &move,
    const ChessPosition &after)
{
    QStringList fields = canonicalFen(after).split(QLatin1Char(' '));
    const QStringList beforeFields = canonicalFen(before).split(QLatin1Char(' '));
    if (fields.size() != 6 || beforeFields.size() != 6) {
        return canonicalFen(after);
    }
    const Piece moving = before.pieceAt(move.from);
    const Piece captured = before.pieceAt(move.to);
    const bool enPassantCapture = moving.type == PieceType::Pawn && captured.isEmpty()
        && ChessPosition::fileOf(move.from) != ChessPosition::fileOf(move.to);
    bool ok = false;
    const int previousHalfmove = beforeFields.at(4).toInt(&ok);
    if (ok) {
        fields[4] = QString::number(
            moving.type == PieceType::Pawn || !captured.isEmpty() || enPassantCapture
                ? 0 : previousHalfmove + 1);
    }
    return fields.join(QLatin1Char(' '));
}

} // namespace

void ReplaySession::load(const AnnotatedReplayPack &pack)
{
    m_pack = pack;
    m_mainlinePly = 0;
    m_variation.reset();
}

bool ReplaySession::seekMainlinePly(int ply)
{
    if (!m_pack.has_value() || m_variation.has_value()
        || ply < 0 || ply >= m_pack->mainlinePositions().size()) {
        return false;
    }
    m_mainlinePly = ply;
    return true;
}

bool ReplaySession::stepBackward()
{
    if (!m_pack.has_value()) {
        return false;
    }
    if (m_variation.has_value()) {
        if (m_variation->localPly <= 0) {
            return false;
        }
        --m_variation->localPly;
        return true;
    }
    if (m_mainlinePly <= 0) {
        return false;
    }
    --m_mainlinePly;
    return true;
}

bool ReplaySession::stepForward()
{
    if (!m_pack.has_value()) {
        return false;
    }
    if (m_variation.has_value()) {
        if (m_variation->localPly >= m_variation->positions.size() - 1) {
            return false;
        }
        ++m_variation->localPly;
        return true;
    }
    if (m_mainlinePly >= m_pack->mainlinePositions().size() - 1) {
        return false;
    }
    ++m_mainlinePly;
    return true;
}

bool ReplaySession::enterPreferredVariation(int anchorPly, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!m_pack.has_value() || m_variation.has_value()) {
        setError(errorMessage, QStringLiteral("replay session is unavailable or already in a variation"));
        return false;
    }
    const ReplayPreferredVariation *variation = m_pack->preferredVariation(anchorPly);
    if (variation == nullptr || anchorPly < 1
        || anchorPly >= m_pack->mainlinePositions().size()) {
        setError(errorMessage, QStringLiteral("no available preferred variation exists at that ply"));
        return false;
    }
    const ChessPosition &checkpoint = m_pack->mainlinePositions().at(anchorPly - 1);
    if (canonicalFen(checkpoint) != variation->checkpointFen) {
        setError(errorMessage, QStringLiteral("variation checkpoint differs from immutable mainline"));
        return false;
    }
    ActiveVariation active;
    active.anchorPly = anchorPly;
    active.definition = variation;
    active.positions.append(checkpoint);
    ChessPosition current = checkpoint;
    for (const ReplayVariationStep &step : variation->displayedSteps) {
        const auto move = Move::fromUci(step.uci);
        if (!move.has_value() || !current.isLegalMove(*move)) {
            setError(errorMessage, QStringLiteral("variation became illegal while entering the session"));
            return false;
        }
        ChessPosition next = current;
        if (!next.applyMove(*move)
            || canonicalFenAfterMove(current, *move, next) != step.afterFen) {
            setError(errorMessage, QStringLiteral("variation state differs while entering the session"));
            return false;
        }
        QString fenError;
        const auto exact = ChessPosition::fromFen(step.afterFen, &fenError);
        if (!exact.has_value()) {
            setError(errorMessage, QStringLiteral("variation recorded state is invalid: ") + fenError);
            return false;
        }
        active.positions.append(*exact);
        current = *exact;
    }
    m_mainlinePly = anchorPly - 1;
    m_variation = active;
    return true;
}

bool ReplaySession::exitVariation(QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!m_pack.has_value() || !m_variation.has_value()
        || m_variation->definition == nullptr) {
        setError(errorMessage, QStringLiteral("replay session is not in a variation"));
        return false;
    }
    const int anchorPly = m_variation->anchorPly;
    const ReplayPreferredVariation &variation = *m_variation->definition;
    const ChessPosition checkpoint = m_pack->mainlinePositions().at(anchorPly - 1);
    const auto played = Move::fromUci(variation.restorePlayedUci);
    ChessPosition restored = checkpoint;
    if (!played.has_value() || !restored.applyMove(*played)
        || canonicalFenAfterMove(checkpoint, *played, restored) != variation.restoreAfterFen
        || canonicalFen(m_pack->mainlinePositions().at(anchorPly))
            != variation.restoreAfterFen) {
        setError(errorMessage, QStringLiteral("variation restore no longer matches immutable mainline"));
        return false;
    }
    m_variation.reset();
    m_mainlinePly = anchorPly;
    return true;
}

const ChessPosition &ReplaySession::currentPosition() const
{
    Q_ASSERT(m_pack.has_value());
    if (m_variation.has_value()) {
        return m_variation->positions.at(m_variation->localPly);
    }
    return m_pack->mainlinePositions().at(m_mainlinePly);
}

int ReplaySession::currentVariationPly() const
{
    return m_variation.has_value() ? m_variation->localPly : 0;
}

} // namespace parlawl::puzzle_runner
