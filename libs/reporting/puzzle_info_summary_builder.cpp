#include "puzzle_info_summary_builder.h"

#include "opening_resolution.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

QString cleanText(const QString &value)
{
    return value.trimmed();
}

QString sentence(const QString &value, const QString &fallback = QStringLiteral("unclear from current packet"))
{
    const QString cleaned = cleanText(value);
    if (cleaned.isEmpty()) {
        return fallback;
    }
    return cleaned;
}

QString humanizeToken(QString token)
{
    token = token.trimmed();
    if (token.isEmpty()) {
        return token;
    }
    token.replace(QLatin1Char('_'), QLatin1Char(' '));
    token.replace(QLatin1Char('-'), QLatin1Char(' '));
    token.replace(QRegularExpression(QStringLiteral("([a-z])([A-Z])")), QStringLiteral("\\1 \\2"));
    const QStringList parts = token.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QStringList titled;
    for (const QString &part : parts) {
        QString lower = part.toLower();
        if (lower == QStringLiteral("cp")) {
            titled.append(QStringLiteral("CP"));
        } else if (lower == QStringLiteral("wdl")) {
            titled.append(QStringLiteral("WDL"));
        } else {
            lower[0] = lower[0].toUpper();
            titled.append(lower);
        }
    }
    return titled.join(QStringLiteral(" "));
}

QString sanForMove(const CriticalMove &move, const QString &uci)
{
    const QJsonDocument document = QJsonDocument::fromJson(move.candidateMovesJson.toUtf8());
    if (!document.isArray()) {
        return QString();
    }
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("move_uci")).toString() == uci) {
            return object.value(QStringLiteral("move_san")).toString().trimmed();
        }
    }
    return QString();
}

QString displayMove(const CriticalMove &move, const QString &uci)
{
    const QString san = sanForMove(move, uci);
    return san.isEmpty() ? uci : san;
}

bool hasMoveLabelConflict(const TacticalEvent &event, const QList<CriticalMove> &moves)
{
    if (!event.retainedBreakPlayedMove.isEmpty()
        && !event.retainedBreakBestMove.isEmpty()
        && event.retainedBreakPlayedMove == event.retainedBreakBestMove) {
        return true;
    }

    for (const CriticalMove &move : moves) {
        if (!move.playedMove.isEmpty()
            && !move.bestMove.isEmpty()
            && move.playedMove == move.bestMove
            && move.playedContinuationCompact != move.bestContinuationCompact) {
            return true;
        }
    }
    return false;
}

QString joinNonEmpty(const QStringList &parts)
{
    QStringList filtered;
    for (const QString &part : parts) {
        if (!cleanText(part).isEmpty()) {
            filtered.append(cleanText(part));
        }
    }
    return filtered.join(QStringLiteral("; "));
}

QString strategicErrorText(const TacticalEvent &event)
{
    QStringList parts;
    if (!event.keyWeakness.isEmpty()) {
        parts.append(QStringLiteral("The long-term weakness was %1").arg(humanizeToken(event.keyWeakness).toLower()));
    }
    if (!event.kingSafetyState.isEmpty()) {
        parts.append(QStringLiteral("king safety had already degraded into %1").arg(humanizeToken(event.kingSafetyState).toLower()));
    }
    if (!event.structuralFeatureSummary.isEmpty()) {
        parts.append(QStringLiteral("the structure around the tactic showed %1").arg(humanizeToken(event.structuralFeatureSummary).toLower()));
    } else if (!event.localTargetSummary.isEmpty()) {
        parts.append(QStringLiteral("the local target picture was %1").arg(event.localTargetSummary));
    }
    if (!event.materialBalance.isEmpty() && event.materialBalance != QStringLiteral("equal")) {
        parts.append(QStringLiteral("material balance was %1").arg(humanizeToken(event.materialBalance).toLower()));
    }
    return sentence(joinNonEmpty(parts));
}

QString planText(const TacticalEvent &event)
{
    QStringList parts;
    if (!event.solutionSummary.isEmpty()) {
        parts.append(event.solutionSummary);
    }
    if (!event.attackerCoordinationSummary.isEmpty()) {
        parts.append(QStringLiteral("The winning plan coordinated %1").arg(event.attackerCoordinationSummary));
    } else if (!event.pressureLaneTargetSummary.isEmpty()) {
        parts.append(QStringLiteral("The practical plan was to increase pressure on %1").arg(event.pressureLaneTargetSummary));
    }
    if (!event.defenderRemovalExposureSummary.isEmpty()) {
        parts.append(QStringLiteral("A key idea was %1").arg(event.defenderRemovalExposureSummary));
    } else if (!event.structuralV4Summary.isEmpty()) {
        parts.append(QStringLiteral("The attack built through %1").arg(humanizeToken(event.structuralV4Summary).toLower()));
    }
    return sentence(joinNonEmpty(parts));
}

QString criticalMistakeText(const TacticalEvent &event, const QList<CriticalMove> &moves, QStringList *warnings)
{
    const CriticalMove *move = nullptr;
    for (const CriticalMove &candidate : moves) {
        if (candidate.role == QStringLiteral("decisive_blunder")
            || candidate.criticalReasonType.contains(QStringLiteral("collapse"))
            || candidate.criticalReasonType.contains(QStringLiteral("decisive"))) {
            move = &candidate;
            break;
        }
    }
    if (move == nullptr && !moves.isEmpty()) {
        move = &moves.last();
    }
    if (hasMoveLabelConflict(event, moves)) {
        if (warnings != nullptr) {
            warnings->append(QStringLiteral("Primary break packet contains conflicting move labels."));
        }
        return sentence(
            QStringLiteral("The collapse begins around ply %1, but the packet has conflicting move labels in the decisive window.")
                .arg(event.primaryBreakPly > 0 ? QString::number(event.primaryBreakPly) : QStringLiteral("unknown")),
            QStringLiteral("packet inconsistency detected"));
    }

    if (move != nullptr && !move->playedMove.isEmpty()) {
        const QString played = displayMove(*move, move->playedMove);
        const QString best = move->bestMove.isEmpty() ? QString() : displayMove(*move, move->bestMove);
        QString summary = QStringLiteral("%1 was the critical mistake").arg(played);
        if (!best.isEmpty() && best != played) {
            summary += QStringLiteral("; %1 was the cleaner move").arg(best);
        }
        if (!move->criticalReasonCompactSummary.isEmpty()) {
            summary += QStringLiteral(". It failed because %1").arg(move->criticalReasonCompactSummary);
        } else if (!event.divergenceCompactSummary.isEmpty()) {
            summary += QStringLiteral(". The packet marks this as %1").arg(event.divergenceCompactSummary);
        }
        return summary;
    }

    if (!event.retainedBreakSummary.isEmpty()) {
        return event.retainedBreakSummary;
    }
    return QStringLiteral("unclear from current packet");
}

QString lastPracticalMistakeText(const QList<CriticalMove> &moves, QStringList *warnings)
{
    const CriticalMove *candidate = nullptr;
    for (auto it = moves.crbegin(); it != moves.crend(); ++it) {
        const bool strongerAlternative = it->whyCritical.contains(QStringLiteral("stronger_alternative_missed"))
            || it->candidateRankingType.contains(QStringLiteral("stronger_alternative_missed"))
            || it->evidenceNoteType.contains(QStringLiteral("stronger_alternative_missed"))
            || it->whyCritical.contains(QStringLiteral("missed_counterplay"));
        if (strongerAlternative) {
            candidate = &(*it);
            break;
        }
    }
    if (candidate == nullptr) {
        if (warnings != nullptr) {
            warnings->append(QStringLiteral("Last practical mistake unavailable."));
        }
        return QStringLiteral("unclear from current packet");
    }

    const QString played = displayMove(*candidate, candidate->playedMove);
    const QString best = candidate->bestMove.isEmpty() ? QString() : displayMove(*candidate, candidate->bestMove);
    QString summary = QStringLiteral("%1 was the last practical miss").arg(played);
    if (!best.isEmpty() && best != played) {
        summary += QStringLiteral("; %1 was the better practical try").arg(best);
    }
    if (candidate->hasEvalDeltaCp) {
        summary += QStringLiteral(". The swing was about %1 cp").arg(candidate->evalDeltaCp);
    }
    return summary;
}

QStringList themeNames(const PuzzleRound &round, const TacticalEvent &event)
{
    QStringList result;
    const QJsonDocument themesDoc = QJsonDocument::fromJson(round.themesJson.toUtf8());
    if (themesDoc.isArray()) {
        for (const QJsonValue &value : themesDoc.array()) {
            if (value.isString()) {
                result.append(humanizeToken(value.toString()));
            }
        }
    }
    const QJsonDocument tacticalDoc = QJsonDocument::fromJson(event.tacticalCandidatesJson.toUtf8());
    if (tacticalDoc.isArray()) {
        for (const QJsonValue &value : tacticalDoc.array()) {
            if (value.isString()) {
                result.append(humanizeToken(value.toString()));
            }
        }
    }
    result.removeDuplicates();
    while (result.size() > 3) {
        result.removeLast();
    }
    return result;
}

QString tacticalThemeText(const PuzzleRound &round, const TacticalEvent &event)
{
    const QStringList themes = themeNames(round, event);
    if (themes.isEmpty()) {
        return QStringLiteral("unclear from current packet");
    }
    return themes.join(QStringLiteral(", "));
}

QString rawEvidenceText(const PuzzleRound &round, const SourceGame &game, const TacticalEvent &event, const QList<CriticalMove> &moves)
{
    QStringList lines;
    lines << QStringLiteral("puzzle id: %1").arg(round.puzzleId);
    lines << QStringLiteral("source game id: %1").arg(game.sourceGameId);
    lines << QStringLiteral("opening family: %1").arg(event.openingFamily.isEmpty() ? QStringLiteral("unknown") : event.openingFamily);
    lines << QStringLiteral("key weakness: %1").arg(event.keyWeakness.isEmpty() ? QStringLiteral("unknown") : event.keyWeakness);
    lines << QStringLiteral("structural summary: %1").arg(event.structuralFeatureSummary.isEmpty() ? QStringLiteral("none") : event.structuralFeatureSummary);
    lines << QStringLiteral("local target: %1").arg(event.localTargetSummary.isEmpty() ? QStringLiteral("none") : event.localTargetSummary);
    lines << QStringLiteral("solution summary: %1").arg(event.solutionSummary.isEmpty() ? QStringLiteral("none") : event.solutionSummary);
    lines << QStringLiteral("primary break: %1").arg(event.retainedBreakSummary.isEmpty() ? QStringLiteral("none") : event.retainedBreakSummary);
    if (!moves.isEmpty()) {
        const CriticalMove &move = moves.first();
        lines << QStringLiteral("first critical move: played %1 | best %2 | why %3")
                     .arg(move.playedMove.isEmpty() ? QStringLiteral("unknown") : move.playedMove,
                          move.bestMove.isEmpty() ? QStringLiteral("unknown") : move.bestMove,
                          move.criticalReasonCompactSummary.isEmpty() ? QStringLiteral("unknown") : move.criticalReasonCompactSummary);
    }
    lines << QStringLiteral("tactical candidates: %1").arg(event.tacticalCandidatesJson.isEmpty() ? QStringLiteral("[]") : event.tacticalCandidatesJson);
    return lines.join(QLatin1Char('\n'));
}

} // namespace

PuzzleInfoSummary PuzzleInfoSummaryBuilder::build(
    const PuzzleRound &puzzleRound,
    const SourceGame &sourceGame,
    const TacticalEvent &tacticalEvent,
    const QList<CriticalMove> &criticalMoves
)
{
    PuzzleInfoSummary summary;
    summary.available = true;
    const OpeningResolution opening = resolveOpeningDisplay(sourceGame, tacticalEvent);
    summary.opening = opening.display;
    summary.warnings.append(opening.warnings);
    summary.strategicError = strategicErrorText(tacticalEvent);
    summary.plan = planText(tacticalEvent);
    summary.criticalMistake = criticalMistakeText(tacticalEvent, criticalMoves, &summary.warnings);
    summary.lastPracticalMistake = lastPracticalMistakeText(criticalMoves, &summary.warnings);
    summary.tacticalTheme = tacticalThemeText(puzzleRound, tacticalEvent);
    summary.rawEvidenceText = rawEvidenceText(puzzleRound, sourceGame, tacticalEvent, criticalMoves);
    return summary;
}
