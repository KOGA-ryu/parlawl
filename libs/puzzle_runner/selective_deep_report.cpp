#include "selective_deep_report.h"

#include <algorithm>
#include <numeric>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace parlawl::puzzle_runner {

namespace {

constexpr qint64 kMaximumReportBytes = 16 * 1024 * 1024;
constexpr qint64 kMaximumCatalogBytes = 128 * 1024 * 1024;
constexpr int kMaximumReports = 100;
constexpr int kMaximumPlies = 700;
constexpr int kMaximumMomentsPerGame = 3;
constexpr int kMaximumPvPlies = 256;

const QRegularExpression kSemanticIdPattern(
    QStringLiteral("^[a-z0-9][a-z0-9_.-]*-v[0-9]+:[0-9a-f]{64}$"));
const QRegularExpression kUciPattern(
    QStringLiteral("^[a-h][1-8][a-h][1-8][qrbn]?$"));
const QRegularExpression kShaPattern(QStringLiteral("^[0-9a-f]{64}$"));

void setError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
}

bool boundedText(
    const QJsonValue &value,
    QString *output,
    const QString &label,
    QString *errorMessage,
    int maximumBytes = 1024)
{
    if (!value.isString()) {
        setError(errorMessage, label + QStringLiteral(" must be text"));
        return false;
    }
    const QString text = value.toString();
    if (text.isEmpty() || text != text.trimmed()
        || text.toUtf8().size() > maximumBytes || text.contains(QChar::Null)) {
        setError(errorMessage, label + QStringLiteral(" is empty or outside its text bound"));
        return false;
    }
    *output = text;
    return true;
}

bool requiredObject(
    const QJsonObject &parent,
    const QString &key,
    QJsonObject *output,
    const QString &label,
    QString *errorMessage)
{
    if (!parent.value(key).isObject()) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" must be an object"));
        return false;
    }
    *output = parent.value(key).toObject();
    return true;
}

bool requiredArray(
    const QJsonObject &parent,
    const QString &key,
    QJsonArray *output,
    const QString &label,
    QString *errorMessage)
{
    if (!parent.value(key).isArray()) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" must be an array"));
        return false;
    }
    *output = parent.value(key).toArray();
    return true;
}

bool requiredInteger(
    const QJsonObject &parent,
    const QString &key,
    qint64 minimum,
    qint64 maximum,
    qint64 *output,
    const QString &label,
    QString *errorMessage)
{
    const QJsonValue value = parent.value(key);
    if (!value.isDouble()) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" must be an integer"));
        return false;
    }
    const double number = value.toDouble();
    const qint64 integer = static_cast<qint64>(number);
    if (number != static_cast<double>(integer)
        || integer < minimum || integer > maximum) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" is outside its integer bound"));
        return false;
    }
    *output = integer;
    return true;
}

bool nullableInteger(
    const QJsonObject &parent,
    const QString &key,
    qint64 minimum,
    qint64 maximum,
    std::optional<qint64> *output,
    const QString &label,
    QString *errorMessage)
{
    if (parent.value(key).isNull()) {
        output->reset();
        return true;
    }
    qint64 integer = 0;
    if (!requiredInteger(parent, key, minimum, maximum, &integer, label, errorMessage)) {
        return false;
    }
    *output = integer;
    return true;
}

bool requiredSemanticId(
    const QJsonObject &parent,
    const QString &key,
    QString *output,
    const QString &label,
    QString *errorMessage)
{
    if (!boundedText(parent.value(key), output,
            label + QLatin1Char('.') + key, errorMessage)) {
        return false;
    }
    if (!kSemanticIdPattern.match(*output).hasMatch()) {
        setError(errorMessage,
            label + QLatin1Char('.') + key + QStringLiteral(" is not a semantic ID"));
        return false;
    }
    return true;
}

bool nullableUci(
    const QJsonObject &parent,
    const QString &key,
    std::optional<QString> *output,
    const QString &label,
    QString *errorMessage)
{
    if (parent.value(key).isNull()) {
        output->reset();
        return true;
    }
    QString value;
    if (!boundedText(parent.value(key), &value, label + QLatin1Char('.') + key, errorMessage, 5)
        || !kUciPattern.match(value).hasMatch()) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" is not UCI"));
        return false;
    }
    *output = value;
    return true;
}

bool parseEngineLine(
    const QJsonObject &wrapper,
    const QString &expectedRole,
    SelectiveDeepEngineLine *output,
    const QString &label,
    QString *errorMessage)
{
    QString role;
    QJsonObject observation;
    qint64 depth = 0;
    qint64 selectiveDepth = 0;
    qint64 nodes = 0;
    qint64 lineRank = 0;
    QJsonArray wdl;
    QJsonArray pv;
    if (!boundedText(wrapper.value(QStringLiteral("role")), &role,
            label + QStringLiteral(".role"), errorMessage)
        || role != expectedRole
        || !requiredObject(wrapper, QStringLiteral("engine_observation"), &observation, label, errorMessage)
        || !requiredSemanticId(observation, QStringLiteral("observation_id"), &output->observationId, label, errorMessage)
        || !requiredSemanticId(observation, QStringLiteral("engine_contract_id"),
            &output->engineContractId, label, errorMessage)
        || !requiredSemanticId(observation, QStringLiteral("transition_id"),
            &output->transitionId, label, errorMessage)
        || !boundedText(observation.value(QStringLiteral("fen")), &output->fen,
            label + QStringLiteral(".fen"), errorMessage, 256)
        || !boundedText(observation.value(QStringLiteral("side_to_move")), &output->sideToMove,
            label + QStringLiteral(".side_to_move"), errorMessage, 8)
        || (output->sideToMove != QStringLiteral("white")
            && output->sideToMove != QStringLiteral("black"))
        || !boundedText(observation.value(QStringLiteral("score_kind")), &output->scoreKind,
            label + QStringLiteral(".score_kind"), errorMessage, 16)
        || (output->scoreKind != QStringLiteral("cp")
            && output->scoreKind != QStringLiteral("mate"))
        || !nullableInteger(observation, QStringLiteral("centipawns_white"), -1'000'000, 1'000'000,
            &output->centipawnsWhite, label, errorMessage)
        || !nullableInteger(observation, QStringLiteral("mate_for_white"), -1'000'000, 1'000'000,
            &output->mateForWhite, label, errorMessage)
        || (output->scoreKind == QStringLiteral("cp")
            && (!output->centipawnsWhite.has_value() || output->mateForWhite.has_value()))
        || (output->scoreKind == QStringLiteral("mate")
            && (output->centipawnsWhite.has_value()
                || !output->mateForWhite.has_value() || *output->mateForWhite == 0))
        || !boundedText(observation.value(QStringLiteral("root_move_uci")), &output->rootMoveUci,
            label + QStringLiteral(".root_move_uci"), errorMessage, 5)
        || !kUciPattern.match(output->rootMoveUci).hasMatch()
        || !requiredInteger(observation, QStringLiteral("line_rank"), 1, 10,
            &lineRank, label, errorMessage)
        || !requiredInteger(observation, QStringLiteral("depth"), 1, 1'000'000,
            &depth, label, errorMessage)
        || !requiredInteger(observation, QStringLiteral("selective_depth"), 0, 1'000'000,
            &selectiveDepth, label, errorMessage)
        || !requiredInteger(observation, QStringLiteral("nodes"), 1, 1'000'000'000,
            &nodes, label, errorMessage)
        || !requiredArray(observation, QStringLiteral("wdl_white"), &wdl, label, errorMessage)
        || wdl.size() != 3
        || !requiredArray(observation, QStringLiteral("pv_uci"), &pv, label, errorMessage)
        || pv.isEmpty() || pv.size() > kMaximumPvPlies) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = label + QStringLiteral(" is malformed");
        }
        return false;
    }
    output->depth = static_cast<int>(depth);
    output->selectiveDepth = static_cast<int>(selectiveDepth);
    output->nodes = nodes;
    output->lineRank = static_cast<int>(lineRank);
    for (const QJsonValue &item : wdl) {
        if (!item.isDouble() || item.toInt(-1) < 0 || item.toInt(-1) > 1'000) {
            setError(errorMessage, label + QStringLiteral(" has malformed WDL"));
            return false;
        }
        output->wdlWhite.append(item.toInt());
    }
    if (std::accumulate(output->wdlWhite.cbegin(), output->wdlWhite.cend(), 0) != 1'000) {
        setError(errorMessage, label + QStringLiteral(" WDL does not conserve 1000"));
        return false;
    }
    for (const QJsonValue &item : pv) {
        QString move;
        if (!boundedText(item, &move, label + QStringLiteral(".pv_uci"), errorMessage, 5)
            || !kUciPattern.match(move).hasMatch()) {
            setError(errorMessage, label + QStringLiteral(" contains malformed PV UCI"));
            return false;
        }
        output->pvUci.append(move);
    }
    if (output->pvUci.first() != output->rootMoveUci) {
        setError(errorMessage, label + QStringLiteral(" PV does not start with its root move"));
        return false;
    }
    return true;
}

bool parseReport(
    const QByteArray &raw,
    const QString &fileName,
    SelectiveDeepGameReview *output,
    QString *errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("selective report JSON is malformed"));
        return false;
    }
    const QJsonObject root = document.object();
    QString version;
    QJsonObject claim;
    QJsonObject context;
    QJsonObject game;
    QJsonObject white;
    QJsonObject black;
    QJsonObject engineInterpretation;
    QJsonObject engineContract;
    QJsonArray moves;
    QJsonArray moments;
    QJsonObject audit;
    qint64 selectedCount = 0;
    qint64 whiteRating = 0;
    qint64 blackRating = 0;
    qint64 nodeLimit = 0;
    qint64 alternatives = 0;
    qint64 threads = 0;
    if (!boundedText(root.value(QStringLiteral("contract_version")), &version,
            QStringLiteral("report.contract_version"), errorMessage)
        || version != QStringLiteral("chess-selective-game-analysis-report-v2")
        || !requiredSemanticId(root, QStringLiteral("report_id"), &output->reportId,
            QStringLiteral("report"), errorMessage)
        || fileName != QStringLiteral("selective-game-analysis-report-v2-")
                + output->reportId.section(QLatin1Char(':'), 1) + QStringLiteral(".json")
        || !requiredSemanticId(root, QStringLiteral("selection_receipt_id"),
            &output->selectionReceiptId, QStringLiteral("report"), errorMessage)
        || !requiredObject(root, QStringLiteral("claim_boundary"), &claim,
            QStringLiteral("report"), errorMessage)
        || claim.value(QStringLiteral("source_and_interpretation_replayed")) != QJsonValue(true)
        || claim.value(QStringLiteral("outcomes_used_for_selection_or_order")) != QJsonValue(false)
        || claim.value(QStringLiteral("engine_processes_started")).toInt(-1) != 0
        || claim.value(QStringLiteral("network_requests")).toInt(-1) != 0
        || claim.value(QStringLiteral("writes_performed")).toInt(-1) != 0
        || !requiredObject(root, QStringLiteral("presentation_context"), &context,
            QStringLiteral("report"), errorMessage)
        || !requiredObject(context, QStringLiteral("game"), &game,
            QStringLiteral("presentation_context"), errorMessage)
        || !requiredObject(game, QStringLiteral("white"), &white,
            QStringLiteral("game"), errorMessage)
        || !requiredObject(game, QStringLiteral("black"), &black,
            QStringLiteral("game"), errorMessage)
        || !requiredSemanticId(game, QStringLiteral("source_game_id"), &output->sourceGameId,
            QStringLiteral("game"), errorMessage)
        || !boundedText(game.value(QStringLiteral("canonical_game_url")), &output->canonicalGameUrl,
            QStringLiteral("game.canonical_game_url"), errorMessage)
        || !QUrl(output->canonicalGameUrl).isValid()
        || !boundedText(game.value(QStringLiteral("start_time_utc")), &output->eventStartUtc,
            QStringLiteral("game.start_time_utc"), errorMessage, 64)
        || !boundedText(white.value(QStringLiteral("username")), &output->whiteUsername,
            QStringLiteral("game.white.username"), errorMessage, 128)
        || !boundedText(black.value(QStringLiteral("username")), &output->blackUsername,
            QStringLiteral("game.black.username"), errorMessage, 128)
        || !requiredInteger(white, QStringLiteral("postgame_rating_observed"), 100, 5'000,
            &whiteRating, QStringLiteral("game.white"), errorMessage)
        || !requiredInteger(black, QStringLiteral("postgame_rating_observed"), 100, 5'000,
            &blackRating, QStringLiteral("game.black"), errorMessage)
        || !boundedText(game.value(QStringLiteral("result")), &output->result,
            QStringLiteral("game.result"), errorMessage, 16)
        || (output->result != QStringLiteral("1-0")
            && output->result != QStringLiteral("0-1")
            && output->result != QStringLiteral("1/2-1/2"))
        || !requiredArray(context, QStringLiteral("moves"), &moves,
            QStringLiteral("presentation_context"), errorMessage)
        || moves.size() < 2 || moves.size() > kMaximumPlies
        || !requiredObject(root, QStringLiteral("engine_interpretation"), &engineInterpretation,
            QStringLiteral("report"), errorMessage)
        || !requiredSemanticId(engineInterpretation, QStringLiteral("interpretation_id"),
            &output->interpretationId, QStringLiteral("engine_interpretation"), errorMessage)
        || !requiredObject(engineInterpretation, QStringLiteral("engine_contract"), &engineContract,
            QStringLiteral("engine_interpretation"), errorMessage)
        || !requiredSemanticId(engineContract, QStringLiteral("contract_id"), &output->engineContractId,
            QStringLiteral("engine_contract"), errorMessage)
        || !boundedText(engineContract.value(QStringLiteral("engine_name")), &output->engineName,
            QStringLiteral("engine_contract.engine_name"), errorMessage, 256)
        || !boundedText(engineContract.value(QStringLiteral("engine_author")), &output->engineAuthor,
            QStringLiteral("engine_contract.engine_author"), errorMessage, 256)
        || !boundedText(engineContract.value(QStringLiteral("engine_binary_sha256")),
            &output->engineBinarySha256, QStringLiteral("engine_contract.engine_binary_sha256"),
            errorMessage, 64)
        || !kShaPattern.match(output->engineBinarySha256).hasMatch()
        || !requiredInteger(engineContract, QStringLiteral("node_limit"), 1'000, 1'000'000,
            &nodeLimit, QStringLiteral("engine_contract"), errorMessage)
        || !requiredInteger(engineContract, QStringLiteral("alternative_line_count"), 2, 10,
            &alternatives, QStringLiteral("engine_contract"), errorMessage)
        || !requiredInteger(engineContract, QStringLiteral("threads"), 1, 1,
            &threads, QStringLiteral("engine_contract"), errorMessage)
        || !requiredArray(root, QStringLiteral("selected_moments"), &moments,
            QStringLiteral("report"), errorMessage)
        || moments.isEmpty() || moments.size() > kMaximumMomentsPerGame
        || !requiredObject(root, QStringLiteral("audit"), &audit,
            QStringLiteral("report"), errorMessage)
        || !requiredInteger(audit, QStringLiteral("selected_occurrence_count"), 1,
            kMaximumMomentsPerGame, &selectedCount, QStringLiteral("audit"), errorMessage)
        || selectedCount != moments.size()) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("selective report authority or contract fields are inconsistent");
        }
        return false;
    }
    output->whiteRating = static_cast<int>(whiteRating);
    output->blackRating = static_cast<int>(blackRating);
    output->nodeLimit = static_cast<int>(nodeLimit);
    output->alternativeLineCount = static_cast<int>(alternatives);

    output->mainline.reserve(moves.size());
    for (int index = 0; index < moves.size(); ++index) {
        if (!moves.at(index).isObject()) {
            setError(errorMessage, QStringLiteral("selective report mainline contains a non-object"));
            return false;
        }
        const QJsonObject item = moves.at(index).toObject();
        SelectiveDeepMainlineMove move;
        qint64 ply = 0;
        if (!requiredInteger(item, QStringLiteral("ply"), 1, kMaximumPlies,
                &ply, QStringLiteral("mainline move"), errorMessage)
            || ply != index + 1
            || !boundedText(item.value(QStringLiteral("mover")), &move.mover,
                QStringLiteral("mainline move.mover"), errorMessage, 8)
            || move.mover != (index % 2 == 0 ? QStringLiteral("white") : QStringLiteral("black"))
            || !boundedText(item.value(QStringLiteral("phase")), &move.phase,
                QStringLiteral("mainline move.phase"), errorMessage, 16)
            || (move.phase != QStringLiteral("opening")
                && move.phase != QStringLiteral("middlegame")
                && move.phase != QStringLiteral("endgame"))
            || !boundedText(item.value(QStringLiteral("san")), &move.san,
                QStringLiteral("mainline move.san"), errorMessage, 32)
            || !boundedText(item.value(QStringLiteral("uci")), &move.uci,
                QStringLiteral("mainline move.uci"), errorMessage, 5)
            || !kUciPattern.match(move.uci).hasMatch()
            || !boundedText(item.value(QStringLiteral("before_fen")), &move.beforeFen,
                QStringLiteral("mainline move.before_fen"), errorMessage, 256)
            || !boundedText(item.value(QStringLiteral("after_fen")), &move.afterFen,
                QStringLiteral("mainline move.after_fen"), errorMessage, 256)
            || !boundedText(item.value(QStringLiteral("deep_selection_status")),
                &move.selectionStatus, QStringLiteral("mainline move.deep_selection_status"),
                errorMessage, 64)
            || (move.selectionStatus != QStringLiteral("selected_for_deep_assessment")
                && move.selectionStatus != QStringLiteral("not_selected_for_deep_assessment"))) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("mainline move %1 is inconsistent").arg(index + 1);
            }
            return false;
        }
        move.ply = static_cast<int>(ply);
        output->mainline.append(move);
    }

    QSet<QString> occurrenceIds;
    QSet<int> selectedPlies;
    output->moments.reserve(moments.size());
    for (int index = 0; index < moments.size(); ++index) {
        if (!moments.at(index).isObject()) {
            setError(errorMessage, QStringLiteral("selected moment is not an object"));
            return false;
        }
        const QJsonObject item = moments.at(index).toObject();
        QJsonObject assessment;
        QJsonObject occurrence;
        QJsonObject sourceMove;
        QJsonObject engineLines;
        QJsonArray alternativesArray;
        SelectiveDeepMoment moment;
        qint64 order = 0;
        qint64 priority = 0;
        qint64 ply = 0;
        QString occurrenceAssessmentId;
        QString occurrenceMover;
        QString occurrencePhase;
        QString occurrenceUci;
        qint64 occurrencePly = 0;
        if (!requiredInteger(item, QStringLiteral("presentation_order"), 1,
                kMaximumMomentsPerGame, &order, QStringLiteral("selected moment"), errorMessage)
            || order != index + 1
            || !requiredObject(item, QStringLiteral("assessment"), &assessment,
                QStringLiteral("selected moment"), errorMessage)
            || !requiredObject(item, QStringLiteral("occurrence"), &occurrence,
                QStringLiteral("selected moment"), errorMessage)
            || !requiredObject(item, QStringLiteral("source_move"), &sourceMove,
                QStringLiteral("selected moment"), errorMessage)
            || !requiredObject(item, QStringLiteral("engine_lines"), &engineLines,
                QStringLiteral("selected moment"), errorMessage)
            || !requiredSemanticId(assessment, QStringLiteral("assessment_id"), &moment.assessmentId,
                QStringLiteral("assessment"), errorMessage)
            || !requiredSemanticId(occurrence, QStringLiteral("occurrence_id"), &moment.occurrenceId,
                QStringLiteral("occurrence"), errorMessage)
            || !requiredSemanticId(occurrence, QStringLiteral("assessment_id"), &occurrenceAssessmentId,
                QStringLiteral("occurrence"), errorMessage)
            || occurrenceAssessmentId != moment.assessmentId
            || !requiredSemanticId(occurrence, QStringLiteral("episode_id"), &moment.episodeId,
                QStringLiteral("occurrence"), errorMessage)
            || !requiredSemanticId(occurrence, QStringLiteral("transition_id"), &moment.transitionId,
                QStringLiteral("occurrence"), errorMessage)
            || assessment.value(QStringLiteral("transition_id")).toString() != moment.transitionId
            || !requiredInteger(assessment, QStringLiteral("priority_rank"), 1, 512,
                &priority, QStringLiteral("assessment"), errorMessage)
            || !requiredInteger(assessment, QStringLiteral("ply"), 1, output->mainline.size(),
                &ply, QStringLiteral("assessment"), errorMessage)
            || !requiredInteger(occurrence, QStringLiteral("ply"), 1, output->mainline.size(),
                &occurrencePly, QStringLiteral("occurrence"), errorMessage)
            || ply != occurrencePly
            || !boundedText(occurrence.value(QStringLiteral("mover")), &occurrenceMover,
                QStringLiteral("occurrence.mover"), errorMessage, 8)
            || !boundedText(occurrence.value(QStringLiteral("phase")), &occurrencePhase,
                QStringLiteral("occurrence.phase"), errorMessage, 16)
            || !boundedText(occurrence.value(QStringLiteral("played_move_uci")), &occurrenceUci,
                QStringLiteral("occurrence.played_move_uci"), errorMessage, 5)
            || !kUciPattern.match(occurrenceUci).hasMatch()
            || assessment.value(QStringLiteral("mover")).toString() != occurrenceMover
            || assessment.value(QStringLiteral("phase")).toString() != occurrencePhase
            || assessment.value(QStringLiteral("played_move_uci")).toString() != occurrenceUci
            || sourceMove.value(QStringLiteral("ply")).toInt(-1) != ply
            || sourceMove.value(QStringLiteral("mover")).toString() != occurrenceMover
            || sourceMove.value(QStringLiteral("phase")).toString() != occurrencePhase
            || sourceMove.value(QStringLiteral("uci")).toString() != occurrenceUci
            || !boundedText(sourceMove.value(QStringLiteral("san")), &moment.san,
                QStringLiteral("source_move.san"), errorMessage, 32)
            || !boundedText(sourceMove.value(QStringLiteral("before_fen")), &moment.beforeFen,
                QStringLiteral("source_move.before_fen"), errorMessage, 256)
            || !boundedText(assessment.value(QStringLiteral("status")), &moment.status,
                QStringLiteral("assessment.status"), errorMessage, 64)
            || !boundedText(assessment.value(QStringLiteral("mate_comparison")),
                &moment.mateComparison, QStringLiteral("assessment.mate_comparison"),
                errorMessage, 96)
            || !boundedText(assessment.value(QStringLiteral("pair_stability")),
                &moment.pairStability, QStringLiteral("assessment.pair_stability"),
                errorMessage, 96)
            || !nullableUci(assessment, QStringLiteral("best_move_uci"), &moment.bestMoveUci,
                QStringLiteral("assessment"), errorMessage)
            || !nullableInteger(assessment, QStringLiteral("best_expectation_millionths"),
                0, 1'000'000, &moment.bestExpectationMillionths,
                QStringLiteral("assessment"), errorMessage)
            || !nullableInteger(assessment, QStringLiteral("played_expectation_millionths"),
                0, 1'000'000, &moment.playedExpectationMillionths,
                QStringLiteral("assessment"), errorMessage)
            || !nullableInteger(assessment, QStringLiteral("signed_expectation_delta_millionths"),
                -1'000'000, 1'000'000, &moment.signedExpectationDeltaMillionths,
                QStringLiteral("assessment"), errorMessage)
            || !nullableInteger(assessment, QStringLiteral("wdl_loss_millionths"),
                0, 1'000'000, &moment.wdlLossMillionths,
                QStringLiteral("assessment"), errorMessage)
            || !nullableInteger(assessment, QStringLiteral("centipawn_loss"),
                0, 1'000'000, &moment.centipawnLoss,
                QStringLiteral("assessment"), errorMessage)
            || !requiredArray(engineLines, QStringLiteral("alternatives"), &alternativesArray,
                QStringLiteral("engine_lines"), errorMessage)
            || alternativesArray.size() > output->alternativeLineCount) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("selected moment %1 is inconsistent").arg(index + 1);
            }
            return false;
        }
        moment.presentationOrder = static_cast<int>(order);
        moment.priorityRank = static_cast<int>(priority);
        moment.ply = static_cast<int>(ply);
        moment.mover = occurrenceMover;
        moment.phase = occurrencePhase;
        moment.playedMoveUci = occurrenceUci;
        if (assessment.value(QStringLiteral("severity")).isNull()) {
            moment.severity.reset();
        } else {
            QString severity;
            if (!boundedText(assessment.value(QStringLiteral("severity")), &severity,
                    QStringLiteral("assessment.severity"), errorMessage, 32)
                || (severity != QStringLiteral("none")
                    && severity != QStringLiteral("inaccuracy")
                    && severity != QStringLiteral("mistake")
                    && severity != QStringLiteral("severe"))) {
                if (errorMessage != nullptr && errorMessage->isEmpty()) {
                    *errorMessage = QStringLiteral("assessment.severity is inconsistent");
                }
                return false;
            }
            if (severity == QStringLiteral("none")) {
                moment.severity.reset();
            } else {
                moment.severity = severity;
            }
        }
        const SelectiveDeepMainlineMove &mainline = output->mainline.at(moment.ply - 1);
        if (mainline.selectionStatus != QStringLiteral("selected_for_deep_assessment")
            || mainline.mover != moment.mover || mainline.phase != moment.phase
            || mainline.san != moment.san || mainline.uci != moment.playedMoveUci
            || mainline.beforeFen != moment.beforeFen
            || occurrenceIds.contains(moment.occurrenceId)) {
            setError(errorMessage, QStringLiteral("selected moment differs from its exact mainline move"));
            return false;
        }
        occurrenceIds.insert(moment.occurrenceId);
        selectedPlies.insert(moment.ply);

        for (int lineIndex = 0; lineIndex < alternativesArray.size(); ++lineIndex) {
            if (!alternativesArray.at(lineIndex).isObject()) {
                setError(errorMessage, QStringLiteral("deep alternative line is not an object"));
                return false;
            }
            SelectiveDeepEngineLine line;
            if (!parseEngineLine(alternativesArray.at(lineIndex).toObject(),
                    QStringLiteral("root_multipv_alternative"), &line,
                    QStringLiteral("deep alternative line"), errorMessage)
                || line.engineContractId != output->engineContractId
                || line.transitionId != moment.transitionId
                || line.fen != moment.beforeFen || line.sideToMove != moment.mover
                || line.lineRank != lineIndex + 1) {
                if (errorMessage != nullptr && errorMessage->isEmpty()) {
                    *errorMessage = QStringLiteral("deep alternative line authority is inconsistent");
                }
                return false;
            }
            moment.alternativeLines.append(line);
        }
        if (!engineLines.value(QStringLiteral("played_move")).isNull()) {
            if (!engineLines.value(QStringLiteral("played_move")).isObject()) {
                setError(errorMessage, QStringLiteral("deep played line must be an object or null"));
                return false;
            }
            SelectiveDeepEngineLine line;
            if (!parseEngineLine(engineLines.value(QStringLiteral("played_move")).toObject(),
                    QStringLiteral("played_move_constrained"), &line,
                    QStringLiteral("deep played line"), errorMessage)
                || line.rootMoveUci != moment.playedMoveUci
                || line.engineContractId != output->engineContractId
                || line.transitionId != moment.transitionId
                || line.fen != moment.beforeFen || line.sideToMove != moment.mover
                || line.lineRank != 1) {
                setError(errorMessage, QStringLiteral("deep played line differs from the recorded move"));
                return false;
            }
            moment.playedLine = line;
        }
        output->moments.append(moment);
    }

    for (const SelectiveDeepMainlineMove &move : output->mainline) {
        const bool selected = selectedPlies.contains(move.ply);
        if (selected != (move.selectionStatus == QStringLiteral("selected_for_deep_assessment"))) {
            setError(errorMessage, QStringLiteral("deep selection flags do not conserve selected moments"));
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<SelectiveDeepReportCatalog> SelectiveDeepReportCatalog::fromDirectory(
    const QString &absoluteDirectoryPath,
    QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    const QFileInfo directoryInfo(absoluteDirectoryPath);
    if (absoluteDirectoryPath.isEmpty() || !directoryInfo.isAbsolute()
        || directoryInfo.isSymLink() || !directoryInfo.isDir()
        || directoryInfo.canonicalFilePath() != directoryInfo.absoluteFilePath()) {
        setError(errorMessage, QStringLiteral("selective report directory must be a direct canonical directory"));
        return std::nullopt;
    }
    const QFileInfoList files = QDir(directoryInfo.absoluteFilePath()).entryInfoList(
        {QStringLiteral("selective-game-analysis-report-v2-*.json")},
        QDir::Files | QDir::Readable | QDir::NoSymLinks,
        QDir::Name);
    if (files.isEmpty() || files.size() > kMaximumReports) {
        setError(errorMessage, QStringLiteral("selective report directory must contain 1..100 Report-v2 files"));
        return std::nullopt;
    }

    SelectiveDeepReportCatalog catalog;
    catalog.m_directoryPath = directoryInfo.absoluteFilePath();
    qint64 totalBytes = 0;
    for (const QFileInfo &fileInfo : files) {
        if (fileInfo.isSymLink() || !fileInfo.isFile()
            || fileInfo.canonicalFilePath() != fileInfo.absoluteFilePath()
            || fileInfo.size() < 1 || fileInfo.size() > kMaximumReportBytes
            || totalBytes > kMaximumCatalogBytes - fileInfo.size()) {
            setError(errorMessage, QStringLiteral("selective report file is indirect or outside its byte bound"));
            return std::nullopt;
        }
        totalBytes += fileInfo.size();
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            setError(errorMessage, QStringLiteral("selective report file could not be read"));
            return std::nullopt;
        }
        const QByteArray raw = file.read(kMaximumReportBytes + 1);
        if (raw.size() != fileInfo.size()) {
            setError(errorMessage, QStringLiteral("selective report file changed while it was read"));
            return std::nullopt;
        }
        SelectiveDeepGameReview review;
        if (!parseReport(raw, fileInfo.fileName(), &review, errorMessage)) {
            const QString detail = errorMessage == nullptr || errorMessage->isEmpty()
                ? QStringLiteral("report fields are inconsistent")
                : *errorMessage;
            setError(errorMessage,
                QStringLiteral("%1: %2").arg(fileInfo.fileName(), detail));
            return std::nullopt;
        }
        if (catalog.m_reviewsByGame.contains(review.sourceGameId)) {
            setError(errorMessage, QStringLiteral("selective report directory repeats one source game"));
            return std::nullopt;
        }
        catalog.m_reviewsByGame.insert(review.sourceGameId, review);
    }
    return catalog;
}

const SelectiveDeepGameReview *SelectiveDeepReportCatalog::reviewForGame(
    const QString &sourceGameId) const
{
    const auto iterator = m_reviewsByGame.constFind(sourceGameId);
    return iterator == m_reviewsByGame.cend() ? nullptr : &iterator.value();
}

} // namespace parlawl::puzzle_runner
