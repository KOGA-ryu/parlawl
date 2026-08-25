#include "game_review_display.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include "chess_position.h"

namespace parlawl::puzzle_runner {

namespace {

constexpr qint64 kMaximumSidecarBytes = 8 * 1024 * 1024;
constexpr qint64 kMaximumCatalogBytes = 128 * 1024 * 1024;
constexpr int kMaximumSidecars = 200;
constexpr int kMaximumMoves = 700;
constexpr int kMaximumMoments = 128;
constexpr int kMaximumLinePlies = 64;

const QRegularExpression kSidecarNamePattern(
    QStringLiteral("^game-review-display-v1-([0-9a-f]{64})\\.json$"));
const QRegularExpression kSemanticIdPattern(
    QStringLiteral("^[a-z0-9][a-z0-9_.-]*-v[0-9]+:[0-9a-f]{64}$"));
const QRegularExpression kUciPattern(
    QStringLiteral("^[a-h][1-8][a-h][1-8][qrbn]?$"));

void setError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
}

bool exactKeys(
    const QJsonObject &object,
    std::initializer_list<const char *> keys,
    const QString &label,
    QString *errorMessage)
{
    QSet<QString> expected;
    for (const char *key : keys) {
        expected.insert(QString::fromLatin1(key));
    }
    QSet<QString> actual;
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        actual.insert(iterator.key());
    }
    if (actual != expected) {
        setError(errorMessage, label + QStringLiteral(" has unexpected or missing fields"));
        return false;
    }
    return true;
}

bool objectField(
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

bool arrayField(
    const QJsonObject &parent,
    const QString &key,
    int maximum,
    QJsonArray *output,
    const QString &label,
    QString *errorMessage)
{
    if (!parent.value(key).isArray()) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" must be an array"));
        return false;
    }
    const QJsonArray values = parent.value(key).toArray();
    if (values.size() > maximum) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" exceeds its item bound"));
        return false;
    }
    *output = values;
    return true;
}

bool textField(
    const QJsonObject &parent,
    const QString &key,
    QString *output,
    const QString &label,
    QString *errorMessage,
    int maximumBytes = 2048)
{
    const QJsonValue value = parent.value(key);
    if (!value.isString()) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" must be text"));
        return false;
    }
    const QString text = value.toString();
    if (text.isEmpty() || text != text.trimmed() || text.contains(QChar::Null)
        || text.toUtf8().size() > maximumBytes) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" is outside its text bound"));
        return false;
    }
    *output = text;
    return true;
}

bool optionalTextField(
    const QJsonObject &parent,
    const QString &key,
    std::optional<QString> *output,
    const QString &label,
    QString *errorMessage,
    int maximumBytes = 2048)
{
    if (parent.value(key).isNull()) {
        output->reset();
        return true;
    }
    QString value;
    if (!textField(parent, key, &value, label, errorMessage, maximumBytes)) {
        return false;
    }
    *output = value;
    return true;
}

bool integerField(
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
    if (number != static_cast<double>(integer) || integer < minimum || integer > maximum) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" is outside its integer bound"));
        return false;
    }
    *output = integer;
    return true;
}

bool optionalIntegerField(
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
    qint64 value = 0;
    if (!integerField(parent, key, minimum, maximum, &value, label, errorMessage)) {
        return false;
    }
    *output = value;
    return true;
}

bool boolField(
    const QJsonObject &parent,
    const QString &key,
    bool *output,
    const QString &label,
    QString *errorMessage)
{
    if (!parent.value(key).isBool()) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" must be boolean"));
        return false;
    }
    *output = parent.value(key).toBool();
    return true;
}

bool uciText(const QString &uci, const QString &label, QString *errorMessage)
{
    if (!kUciPattern.match(uci).hasMatch()) {
        setError(errorMessage, label + QStringLiteral(" must be UCI notation"));
        return false;
    }
    return true;
}

bool uciArray(
    const QJsonObject &parent,
    const QString &key,
    QStringList *output,
    const QString &label,
    QString *errorMessage)
{
    QJsonArray values;
    if (!arrayField(parent, key, kMaximumLinePlies, &values, label, errorMessage)) {
        return false;
    }
    output->clear();
    for (const QJsonValue &value : values) {
        if (!value.isString() || !uciText(value.toString(), label + QLatin1Char('.') + key, errorMessage)) {
            return false;
        }
        output->append(value.toString());
    }
    return true;
}

bool parseMove(
    const QJsonObject &object,
    int expectedPly,
    GameReviewDisplayMove *move,
    QString *errorMessage)
{
    if (!exactKeys(object,
            {"after_fen", "before_fen", "capture", "castling", "check", "checkmate",
             "clock_remaining_ms", "decision_start_clock_ms", "elapsed_move_ms",
             "elapsed_status", "move_number", "mover", "phase", "piece",
             "player_username", "ply", "pressure_thresholds_met_ms", "san",
             "selected_moment_indexes", "uci"},
            QStringLiteral("move"), errorMessage)) {
        return false;
    }
    qint64 ply = 0;
    qint64 moveNumber = 0;
    std::optional<qint64> elapsed;
    std::optional<qint64> decisionStart;
    std::optional<qint64> clockRemaining;
    std::optional<QString> castling;
    if (!integerField(object, QStringLiteral("ply"), 1, kMaximumMoves, &ply,
            QStringLiteral("move"), errorMessage)
        || ply != expectedPly
        || !integerField(object, QStringLiteral("move_number"), 1, kMaximumMoves,
            &moveNumber, QStringLiteral("move"), errorMessage)
        || moveNumber != (expectedPly + 1) / 2
        || !textField(object, QStringLiteral("mover"), &move->mover,
            QStringLiteral("move"), errorMessage, 16)
        || !textField(object, QStringLiteral("player_username"), &move->playerUsername,
            QStringLiteral("move"), errorMessage, 256)
        || !textField(object, QStringLiteral("san"), &move->san,
            QStringLiteral("move"), errorMessage, 64)
        || !textField(object, QStringLiteral("uci"), &move->uci,
            QStringLiteral("move"), errorMessage, 5)
        || !uciText(move->uci, QStringLiteral("move.uci"), errorMessage)
        || !textField(object, QStringLiteral("phase"), &move->phase,
            QStringLiteral("move"), errorMessage, 32)
        || !textField(object, QStringLiteral("before_fen"), &move->beforeFen,
            QStringLiteral("move"), errorMessage, 256)
        || !textField(object, QStringLiteral("after_fen"), &move->afterFen,
            QStringLiteral("move"), errorMessage, 256)
        || !ChessPosition::fromFen(move->beforeFen, errorMessage).has_value()
        || !ChessPosition::fromFen(move->afterFen, errorMessage).has_value()
        || !optionalIntegerField(object, QStringLiteral("elapsed_move_ms"), 0, 86'400'000,
            &elapsed, QStringLiteral("move"), errorMessage)
        || !textField(object, QStringLiteral("elapsed_status"), &move->elapsedStatus,
            QStringLiteral("move"), errorMessage, 64)
        || !optionalIntegerField(object, QStringLiteral("decision_start_clock_ms"), 0,
            86'400'000, &decisionStart, QStringLiteral("move"), errorMessage)
        || !optionalIntegerField(object, QStringLiteral("clock_remaining_ms"), 0,
            86'400'000, &clockRemaining, QStringLiteral("move"), errorMessage)
        || !textField(object, QStringLiteral("piece"), &move->piece,
            QStringLiteral("move"), errorMessage, 16)
        || !boolField(object, QStringLiteral("capture"), &move->capture,
            QStringLiteral("move"), errorMessage)
        || !boolField(object, QStringLiteral("check"), &move->check,
            QStringLiteral("move"), errorMessage)
        || !boolField(object, QStringLiteral("checkmate"), &move->checkmate,
            QStringLiteral("move"), errorMessage)
        || !optionalTextField(object, QStringLiteral("castling"), &castling,
            QStringLiteral("move"), errorMessage, 16)) {
        return false;
    }
    move->ply = static_cast<int>(ply);
    move->moveNumber = static_cast<int>(moveNumber);
    move->elapsedMoveMs = elapsed;
    move->decisionStartClockMs = decisionStart;
    move->clockRemainingMs = clockRemaining;
    move->castling = castling;

    QJsonArray pressure;
    QJsonArray indexes;
    if (!arrayField(object, QStringLiteral("pressure_thresholds_met_ms"), 16,
            &pressure, QStringLiteral("move"), errorMessage)
        || !arrayField(object, QStringLiteral("selected_moment_indexes"), kMaximumMoments,
            &indexes, QStringLiteral("move"), errorMessage)) {
        return false;
    }
    for (const QJsonValue &value : pressure) {
        if (!value.isDouble() || value.toDouble() != static_cast<qint64>(value.toDouble())
            || value.toDouble() < 0 || value.toDouble() > 86'400'000) {
            setError(errorMessage, QStringLiteral("move pressure threshold must be an integer"));
            return false;
        }
        move->pressureThresholdsMetMs.append(static_cast<qint64>(value.toDouble()));
    }
    QSet<int> uniqueIndexes;
    for (const QJsonValue &value : indexes) {
        if (!value.isDouble() || value.toDouble() != static_cast<int>(value.toDouble())
            || value.toInt() < 1 || value.toInt() > kMaximumMoments
            || uniqueIndexes.contains(value.toInt())) {
            setError(errorMessage, QStringLiteral("move selected moment indexes are invalid"));
            return false;
        }
        uniqueIndexes.insert(value.toInt());
        move->selectedMomentIndexes.append(value.toInt());
    }
    return true;
}

bool parseMoment(
    const QJsonObject &object,
    int expectedIndex,
    int moveCount,
    GameReviewDisplayMoment *moment,
    QString *errorMessage)
{
    if (!exactKeys(object,
            {"best_centipawns_mover", "best_line_uci", "best_move_san", "best_move_uci",
             "centipawn_loss", "confidence", "elapsed_move_ms",
             "expectation_loss_millionths", "move_number", "mover", "phase",
             "played_centipawns_mover", "played_line_uci", "played_san", "played_uci",
             "player_username", "ply", "review_index", "severity", "status", "summary",
             "title"},
            QStringLiteral("critical moment"), errorMessage)) {
        return false;
    }
    qint64 reviewIndex = 0;
    qint64 ply = 0;
    qint64 moveNumber = 0;
    if (!integerField(object, QStringLiteral("review_index"), 1, kMaximumMoments,
            &reviewIndex, QStringLiteral("critical moment"), errorMessage)
        || reviewIndex != expectedIndex
        || !integerField(object, QStringLiteral("ply"), 1, moveCount, &ply,
            QStringLiteral("critical moment"), errorMessage)
        || !integerField(object, QStringLiteral("move_number"), 1, moveCount,
            &moveNumber, QStringLiteral("critical moment"), errorMessage)
        || moveNumber != (ply + 1) / 2
        || !textField(object, QStringLiteral("mover"), &moment->mover,
            QStringLiteral("critical moment"), errorMessage, 16)
        || !textField(object, QStringLiteral("player_username"), &moment->playerUsername,
            QStringLiteral("critical moment"), errorMessage, 256)
        || !textField(object, QStringLiteral("phase"), &moment->phase,
            QStringLiteral("critical moment"), errorMessage, 32)
        || !textField(object, QStringLiteral("played_san"), &moment->playedSan,
            QStringLiteral("critical moment"), errorMessage, 64)
        || !textField(object, QStringLiteral("played_uci"), &moment->playedUci,
            QStringLiteral("critical moment"), errorMessage, 5)
        || !uciText(moment->playedUci, QStringLiteral("critical moment.played_uci"), errorMessage)
        || !optionalTextField(object, QStringLiteral("best_move_san"), &moment->bestMoveSan,
            QStringLiteral("critical moment"), errorMessage, 64)
        || !textField(object, QStringLiteral("best_move_uci"), &moment->bestMoveUci,
            QStringLiteral("critical moment"), errorMessage, 5)
        || !uciText(moment->bestMoveUci, QStringLiteral("critical moment.best_move_uci"), errorMessage)
        || !textField(object, QStringLiteral("status"), &moment->status,
            QStringLiteral("critical moment"), errorMessage, 128)
        || !optionalTextField(object, QStringLiteral("severity"), &moment->severity,
            QStringLiteral("critical moment"), errorMessage, 64)
        || !textField(object, QStringLiteral("confidence"), &moment->confidence,
            QStringLiteral("critical moment"), errorMessage, 128)
        || !textField(object, QStringLiteral("title"), &moment->title,
            QStringLiteral("critical moment"), errorMessage, 256)
        || !textField(object, QStringLiteral("summary"), &moment->summary,
            QStringLiteral("critical moment"), errorMessage, 2048)
        || !optionalIntegerField(object, QStringLiteral("centipawn_loss"), 0, 1'000'000,
            &moment->centipawnLoss, QStringLiteral("critical moment"), errorMessage)
        || !optionalIntegerField(object, QStringLiteral("best_centipawns_mover"), -1'000'000,
            1'000'000, &moment->bestCentipawnsMover, QStringLiteral("critical moment"), errorMessage)
        || !optionalIntegerField(object, QStringLiteral("played_centipawns_mover"), -1'000'000,
            1'000'000, &moment->playedCentipawnsMover, QStringLiteral("critical moment"), errorMessage)
        || !optionalIntegerField(object, QStringLiteral("expectation_loss_millionths"), 0,
            1'000'000, &moment->expectationLossMillionths, QStringLiteral("critical moment"), errorMessage)
        || !optionalIntegerField(object, QStringLiteral("elapsed_move_ms"), 0, 86'400'000,
            &moment->elapsedMoveMs, QStringLiteral("critical moment"), errorMessage)
        || !uciArray(object, QStringLiteral("best_line_uci"), &moment->bestLineUci,
            QStringLiteral("critical moment"), errorMessage)
        || !uciArray(object, QStringLiteral("played_line_uci"), &moment->playedLineUci,
            QStringLiteral("critical moment"), errorMessage)) {
        return false;
    }
    moment->reviewIndex = static_cast<int>(reviewIndex);
    moment->ply = static_cast<int>(ply);
    moment->moveNumber = static_cast<int>(moveNumber);
    if (moment->bestLineUci.isEmpty() || moment->bestLineUci.first() != moment->bestMoveUci
        || (!moment->playedLineUci.isEmpty()
            && moment->playedLineUci.first() != moment->playedUci)) {
        setError(errorMessage, QStringLiteral("critical moment retained lines do not match their root moves"));
        return false;
    }
    return true;
}

bool parseTiming(
    const QJsonObject &object,
    int moveCount,
    GameReviewDisplay *review,
    QString *errorMessage)
{
    if (!exactKeys(object,
            {"by_player_and_phase", "clock_semantics", "decision_count",
             "elapsed_missing_count", "elapsed_observed_count",
             "longest_observed_by_player", "observed_elapsed_sum_ms"},
            QStringLiteral("timing"), errorMessage)
        || !textField(object, QStringLiteral("clock_semantics"), &review->clockSemantics,
            QStringLiteral("timing"), errorMessage, 128)
        || review->clockSemantics != QStringLiteral("server-accounted-not-cognitive-time")) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("timing clock semantics are unsupported");
        }
        return false;
    }
    qint64 decisionCount = 0;
    qint64 observed = 0;
    qint64 missing = 0;
    if (!integerField(object, QStringLiteral("decision_count"), 0, kMaximumMoves,
            &decisionCount, QStringLiteral("timing"), errorMessage)
        || decisionCount != moveCount
        || !integerField(object, QStringLiteral("elapsed_observed_count"), 0, moveCount,
            &observed, QStringLiteral("timing"), errorMessage)
        || !integerField(object, QStringLiteral("elapsed_missing_count"), 0, moveCount,
            &missing, QStringLiteral("timing"), errorMessage)
        || observed + missing != decisionCount
        || !optionalIntegerField(object, QStringLiteral("observed_elapsed_sum_ms"), 0,
            86'400'000'000, &review->observedElapsedSumMs,
            QStringLiteral("timing"), errorMessage)) {
        return false;
    }
    review->decisionCount = static_cast<int>(decisionCount);
    review->elapsedObservedCount = static_cast<int>(observed);
    review->elapsedMissingCount = static_cast<int>(missing);

    QJsonArray buckets;
    QJsonArray longest;
    if (!arrayField(object, QStringLiteral("by_player_and_phase"), 32, &buckets,
            QStringLiteral("timing"), errorMessage)
        || !arrayField(object, QStringLiteral("longest_observed_by_player"), 2, &longest,
            QStringLiteral("timing"), errorMessage)) {
        return false;
    }
    for (const QJsonValue &value : buckets) {
        if (!value.isObject()) {
            setError(errorMessage, QStringLiteral("timing bucket must be an object"));
            return false;
        }
        const QJsonObject item = value.toObject();
        if (!exactKeys(item,
                {"decision_count", "elapsed_missing_count", "elapsed_observed_count",
                 "maximum_observed_elapsed_ms", "mean_observed_elapsed_ms",
                 "observed_elapsed_sum_ms", "phase", "player_color"},
                QStringLiteral("timing bucket"), errorMessage)) {
            return false;
        }
        GameReviewDisplayTimingBucket bucket;
        qint64 decisions = 0;
        qint64 bucketObserved = 0;
        qint64 bucketMissing = 0;
        if (!textField(item, QStringLiteral("player_color"), &bucket.playerColor,
                QStringLiteral("timing bucket"), errorMessage, 16)
            || !textField(item, QStringLiteral("phase"), &bucket.phase,
                QStringLiteral("timing bucket"), errorMessage, 32)
            || !integerField(item, QStringLiteral("decision_count"), 0, moveCount,
                &decisions, QStringLiteral("timing bucket"), errorMessage)
            || !integerField(item, QStringLiteral("elapsed_observed_count"), 0, moveCount,
                &bucketObserved, QStringLiteral("timing bucket"), errorMessage)
            || !integerField(item, QStringLiteral("elapsed_missing_count"), 0, moveCount,
                &bucketMissing, QStringLiteral("timing bucket"), errorMessage)
            || bucketObserved + bucketMissing != decisions
            || !optionalIntegerField(item, QStringLiteral("observed_elapsed_sum_ms"), 0,
                86'400'000'000, &bucket.observedElapsedSumMs,
                QStringLiteral("timing bucket"), errorMessage)
            || !optionalIntegerField(item, QStringLiteral("mean_observed_elapsed_ms"), 0,
                86'400'000, &bucket.meanObservedElapsedMs,
                QStringLiteral("timing bucket"), errorMessage)
            || !optionalIntegerField(item, QStringLiteral("maximum_observed_elapsed_ms"), 0,
                86'400'000, &bucket.maximumObservedElapsedMs,
                QStringLiteral("timing bucket"), errorMessage)) {
            return false;
        }
        bucket.decisionCount = static_cast<int>(decisions);
        bucket.elapsedObservedCount = static_cast<int>(bucketObserved);
        bucket.elapsedMissingCount = static_cast<int>(bucketMissing);
        review->timingBuckets.append(bucket);
    }
    for (const QJsonValue &value : longest) {
        if (!value.isObject()) {
            setError(errorMessage, QStringLiteral("longest observed move must be an object"));
            return false;
        }
        const QJsonObject item = value.toObject();
        if (!exactKeys(item,
                {"elapsed_move_ms", "move_number", "phase", "player_color",
                 "player_username", "ply", "san", "uci"},
                QStringLiteral("longest observed move"), errorMessage)) {
            return false;
        }
        GameReviewDisplayLongestMove longestMove;
        qint64 ply = 0;
        qint64 moveNumber = 0;
        qint64 elapsed = 0;
        if (!textField(item, QStringLiteral("player_color"), &longestMove.playerColor,
                QStringLiteral("longest observed move"), errorMessage, 16)
            || !textField(item, QStringLiteral("player_username"), &longestMove.playerUsername,
                QStringLiteral("longest observed move"), errorMessage, 256)
            || !integerField(item, QStringLiteral("ply"), 1, moveCount, &ply,
                QStringLiteral("longest observed move"), errorMessage)
            || !integerField(item, QStringLiteral("move_number"), 1, moveCount, &moveNumber,
                QStringLiteral("longest observed move"), errorMessage)
            || !textField(item, QStringLiteral("san"), &longestMove.san,
                QStringLiteral("longest observed move"), errorMessage, 64)
            || !textField(item, QStringLiteral("uci"), &longestMove.uci,
                QStringLiteral("longest observed move"), errorMessage, 5)
            || !uciText(longestMove.uci, QStringLiteral("longest observed move.uci"), errorMessage)
            || !textField(item, QStringLiteral("phase"), &longestMove.phase,
                QStringLiteral("longest observed move"), errorMessage, 32)
            || !integerField(item, QStringLiteral("elapsed_move_ms"), 0, 86'400'000,
                &elapsed, QStringLiteral("longest observed move"), errorMessage)) {
            return false;
        }
        longestMove.ply = static_cast<int>(ply);
        longestMove.moveNumber = static_cast<int>(moveNumber);
        longestMove.elapsedMoveMs = elapsed;
        review->longestObservedMoves.append(longestMove);
    }
    return true;
}

bool parseSidecar(
    const QByteArray &raw,
    const QString &fileName,
    GameReviewDisplay *review,
    QString *errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("sidecar must contain one JSON object"));
        return false;
    }
    const QJsonObject root = document.object();
    if (!exactKeys(root,
            {"claim_boundary", "critical_moments", "display_schema", "game", "moves",
             "opening", "outcome", "source_report_id", "timing"},
            QStringLiteral("sidecar"), errorMessage)
        || !textField(root, QStringLiteral("display_schema"), &review->displaySchema,
            QStringLiteral("sidecar"), errorMessage, 64)
        || review->displaySchema != QStringLiteral("chess-game-review-display-v1")
        || !textField(root, QStringLiteral("source_report_id"), &review->sourceReportId,
            QStringLiteral("sidecar"), errorMessage, 256)
        || !kSemanticIdPattern.match(review->sourceReportId).hasMatch()) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("sidecar schema or source report ID is unsupported");
        }
        return false;
    }
    const QRegularExpressionMatch nameMatch = kSidecarNamePattern.match(fileName);
    if (!nameMatch.hasMatch()
        || !review->sourceReportId.endsWith(QLatin1Char(':') + nameMatch.captured(1))) {
        setError(errorMessage, QStringLiteral("sidecar filename does not match source_report_id"));
        return false;
    }

    QJsonObject boundary;
    if (!objectField(root, QStringLiteral("claim_boundary"), &boundary,
            QStringLiteral("sidecar"), errorMessage)
        || !exactKeys(boundary,
            {"causal_or_intent_explanation", "complete_game_error_coverage",
             "display_projection_only", "engine_or_network_work_performed",
             "objective_chess_truth", "source_report_content_address_validated",
             "source_report_replay_reperformed", "unselected_moves_claimed_safe"},
            QStringLiteral("claim boundary"), errorMessage)) {
        return false;
    }
    const QHash<QString, bool> expectedBoundary {
        {QStringLiteral("causal_or_intent_explanation"), false},
        {QStringLiteral("complete_game_error_coverage"), false},
        {QStringLiteral("display_projection_only"), true},
        {QStringLiteral("engine_or_network_work_performed"), false},
        {QStringLiteral("objective_chess_truth"), false},
        {QStringLiteral("source_report_content_address_validated"), true},
        {QStringLiteral("source_report_replay_reperformed"), false},
        {QStringLiteral("unselected_moves_claimed_safe"), false},
    };
    for (auto iterator = expectedBoundary.constBegin(); iterator != expectedBoundary.constEnd(); ++iterator) {
        bool value = false;
        if (!boolField(boundary, iterator.key(), &value,
                QStringLiteral("claim boundary"), errorMessage)
            || value != iterator.value()) {
            setError(errorMessage, QStringLiteral("claim boundary is unsupported"));
            return false;
        }
    }

    QJsonObject game;
    QJsonObject opening;
    QJsonObject outcome;
    QJsonObject timing;
    if (!objectField(root, QStringLiteral("game"), &game, QStringLiteral("sidecar"), errorMessage)
        || !objectField(root, QStringLiteral("opening"), &opening, QStringLiteral("sidecar"), errorMessage)
        || !objectField(root, QStringLiteral("outcome"), &outcome, QStringLiteral("sidecar"), errorMessage)
        || !objectField(root, QStringLiteral("timing"), &timing, QStringLiteral("sidecar"), errorMessage)
        || !exactKeys(game,
            {"black_postgame_rating_observed", "black_username", "canonical_game_url",
             "end_time_utc", "move_count", "rated", "rules", "selected_moment_count",
             "source_game_id", "start_time_utc", "time_class", "time_control",
             "white_postgame_rating_observed", "white_username"},
            QStringLiteral("game"), errorMessage)
        || !exactKeys(opening,
            {"deepest_exact_match_position_index", "eco", "first_move_after_book_san",
             "first_ply_after_book", "name", "status"},
            QStringLiteral("opening"), errorMessage)
        || !exactKeys(outcome,
            {"result", "summary", "termination_claim", "termination_status", "winner_username"},
            QStringLiteral("outcome"), errorMessage)) {
        return false;
    }
    qint64 moveCount = 0;
    qint64 momentCount = 0;
    std::optional<qint64> whiteRating;
    std::optional<qint64> blackRating;
    if (!textField(game, QStringLiteral("source_game_id"), &review->sourceGameId,
            QStringLiteral("game"), errorMessage, 256)
        || !kSemanticIdPattern.match(review->sourceGameId).hasMatch()
        || !textField(game, QStringLiteral("canonical_game_url"), &review->canonicalGameUrl,
            QStringLiteral("game"), errorMessage, 2048)
        || !textField(game, QStringLiteral("start_time_utc"), &review->startTimeUtc,
            QStringLiteral("game"), errorMessage, 64)
        || !textField(game, QStringLiteral("end_time_utc"), &review->endTimeUtc,
            QStringLiteral("game"), errorMessage, 64)
        || !textField(game, QStringLiteral("white_username"), &review->whiteUsername,
            QStringLiteral("game"), errorMessage, 256)
        || !textField(game, QStringLiteral("black_username"), &review->blackUsername,
            QStringLiteral("game"), errorMessage, 256)
        || !optionalIntegerField(game, QStringLiteral("white_postgame_rating_observed"), 0,
            100'000, &whiteRating, QStringLiteral("game"), errorMessage)
        || !optionalIntegerField(game, QStringLiteral("black_postgame_rating_observed"), 0,
            100'000, &blackRating, QStringLiteral("game"), errorMessage)
        || !boolField(game, QStringLiteral("rated"), &review->rated,
            QStringLiteral("game"), errorMessage)
        || !textField(game, QStringLiteral("rules"), &review->rules,
            QStringLiteral("game"), errorMessage, 64)
        || !textField(game, QStringLiteral("time_class"), &review->timeClass,
            QStringLiteral("game"), errorMessage, 64)
        || !textField(game, QStringLiteral("time_control"), &review->timeControl,
            QStringLiteral("game"), errorMessage, 64)
        || !integerField(game, QStringLiteral("move_count"), 1, kMaximumMoves, &moveCount,
            QStringLiteral("game"), errorMessage)
        || !integerField(game, QStringLiteral("selected_moment_count"), 1, kMaximumMoments,
            &momentCount, QStringLiteral("game"), errorMessage)) {
        return false;
    }
    if (whiteRating.has_value()) review->whiteRating = static_cast<int>(*whiteRating);
    if (blackRating.has_value()) review->blackRating = static_cast<int>(*blackRating);
    review->moveCount = static_cast<int>(moveCount);
    review->selectedMomentCount = static_cast<int>(momentCount);

    std::optional<qint64> openingIndex;
    std::optional<qint64> firstPly;
    if (!optionalTextField(opening, QStringLiteral("eco"), &review->openingEco,
            QStringLiteral("opening"), errorMessage, 16)
        || !optionalTextField(opening, QStringLiteral("name"), &review->openingName,
            QStringLiteral("opening"), errorMessage, 256)
        || !textField(opening, QStringLiteral("status"), &review->openingStatus,
            QStringLiteral("opening"), errorMessage, 64)
        || !optionalIntegerField(opening, QStringLiteral("deepest_exact_match_position_index"),
            0, moveCount, &openingIndex, QStringLiteral("opening"), errorMessage)
        || !optionalIntegerField(opening, QStringLiteral("first_ply_after_book"), 1,
            moveCount, &firstPly, QStringLiteral("opening"), errorMessage)
        || !optionalTextField(opening, QStringLiteral("first_move_after_book_san"),
            &review->firstMoveAfterBookSan, QStringLiteral("opening"), errorMessage, 64)) {
        return false;
    }
    if (openingIndex.has_value()) review->deepestExactMatchPositionIndex = static_cast<int>(*openingIndex);
    if (firstPly.has_value()) review->firstPlyAfterBook = static_cast<int>(*firstPly);

    if (!textField(outcome, QStringLiteral("result"), &review->result,
            QStringLiteral("outcome"), errorMessage, 16)
        || !optionalTextField(outcome, QStringLiteral("winner_username"),
            &review->winnerUsername, QStringLiteral("outcome"), errorMessage, 256)
        || !textField(outcome, QStringLiteral("termination_status"), &review->terminationStatus,
            QStringLiteral("outcome"), errorMessage, 128)
        || !optionalTextField(outcome, QStringLiteral("termination_claim"),
            &review->terminationClaim, QStringLiteral("outcome"), errorMessage, 512)
        || !textField(outcome, QStringLiteral("summary"), &review->outcomeSummary,
            QStringLiteral("outcome"), errorMessage, 1024)) {
        return false;
    }

    QJsonArray moves;
    QJsonArray moments;
    if (!arrayField(root, QStringLiteral("moves"), kMaximumMoves, &moves,
            QStringLiteral("sidecar"), errorMessage)
        || moves.size() != moveCount
        || !arrayField(root, QStringLiteral("critical_moments"), kMaximumMoments, &moments,
            QStringLiteral("sidecar"), errorMessage)
        || moments.size() != momentCount) {
        setError(errorMessage, QStringLiteral("game counts do not match sidecar arrays"));
        return false;
    }
    for (int index = 0; index < moves.size(); ++index) {
        if (!moves.at(index).isObject()) {
            setError(errorMessage, QStringLiteral("move must be an object"));
            return false;
        }
        GameReviewDisplayMove move;
        if (!parseMove(moves.at(index).toObject(), index + 1, &move, errorMessage)) {
            return false;
        }
        review->moves.append(move);
    }
    for (int index = 0; index < moments.size(); ++index) {
        if (!moments.at(index).isObject()) {
            setError(errorMessage, QStringLiteral("critical moment must be an object"));
            return false;
        }
        GameReviewDisplayMoment moment;
        if (!parseMoment(moments.at(index).toObject(), index + 1, moveCount,
                &moment, errorMessage)) {
            return false;
        }
        const GameReviewDisplayMove &move = review->moves.at(moment.ply - 1);
        if (move.san != moment.playedSan || move.uci != moment.playedUci
            || !move.selectedMomentIndexes.contains(moment.reviewIndex)) {
            setError(errorMessage, QStringLiteral("critical moment differs from its attached move"));
            return false;
        }
        review->criticalMoments.append(moment);
    }
    for (const GameReviewDisplayMove &move : review->moves) {
        for (int reviewIndex : move.selectedMomentIndexes) {
            const GameReviewDisplayMoment *moment = review->momentAtReviewIndex(reviewIndex);
            if (moment == nullptr || moment->ply != move.ply) {
                setError(errorMessage, QStringLiteral("move selected moment attachment is inconsistent"));
                return false;
            }
        }
    }
    return parseTiming(timing, moveCount, review, errorMessage);
}

} // namespace

const GameReviewDisplayMoment *GameReviewDisplay::momentAtReviewIndex(int reviewIndex) const
{
    return reviewIndex > 0 && reviewIndex <= criticalMoments.size()
        ? &criticalMoments.at(reviewIndex - 1) : nullptr;
}

const GameReviewDisplayMoment *GameReviewDisplay::firstMomentAtPly(int ply) const
{
    if (ply <= 0 || ply > moves.size() || moves.at(ply - 1).selectedMomentIndexes.isEmpty()) {
        return nullptr;
    }
    return momentAtReviewIndex(moves.at(ply - 1).selectedMomentIndexes.first());
}

std::optional<GameReviewDisplayCatalog> GameReviewDisplayCatalog::fromDirectory(
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
        setError(errorMessage, QStringLiteral("game review directory must be a direct canonical directory"));
        return std::nullopt;
    }
    const QFileInfoList files = QDir(directoryInfo.absoluteFilePath()).entryInfoList(
        {QStringLiteral("game-review-display-v1-*.json")},
        QDir::Files | QDir::Readable | QDir::NoSymLinks,
        QDir::Name);
    if (files.isEmpty() || files.size() > kMaximumSidecars) {
        setError(errorMessage, QStringLiteral("game review directory must contain 1..200 display-v1 sidecars"));
        return std::nullopt;
    }

    GameReviewDisplayCatalog catalog;
    catalog.m_directoryPath = directoryInfo.absoluteFilePath();
    qint64 totalBytes = 0;
    for (const QFileInfo &fileInfo : files) {
        if (!kSidecarNamePattern.match(fileInfo.fileName()).hasMatch()
            || fileInfo.isSymLink() || !fileInfo.isFile()
            || fileInfo.canonicalFilePath() != fileInfo.absoluteFilePath()
            || fileInfo.size() < 1 || fileInfo.size() > kMaximumSidecarBytes
            || totalBytes > kMaximumCatalogBytes - fileInfo.size()) {
            setError(errorMessage, QStringLiteral("game review sidecar name, path, or size is invalid"));
            return std::nullopt;
        }
        totalBytes += fileInfo.size();
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            setError(errorMessage, QStringLiteral("game review sidecar could not be read"));
            return std::nullopt;
        }
        const QByteArray raw = file.read(kMaximumSidecarBytes + 1);
        if (raw.size() != fileInfo.size()) {
            setError(errorMessage, QStringLiteral("game review sidecar changed while it was read"));
            return std::nullopt;
        }
        GameReviewDisplay review;
        QString detail;
        if (!parseSidecar(raw, fileInfo.fileName(), &review, &detail)) {
            setError(errorMessage, QStringLiteral("%1: %2").arg(fileInfo.fileName(), detail));
            return std::nullopt;
        }
        if (catalog.m_reviewsByGame.contains(review.sourceGameId)) {
            setError(errorMessage, QStringLiteral("game review directory repeats one source game"));
            return std::nullopt;
        }
        catalog.m_reviewsByGame.insert(review.sourceGameId, review);
    }
    return catalog;
}

const GameReviewDisplay *GameReviewDisplayCatalog::reviewForGame(
    const QString &sourceGameId) const
{
    const auto iterator = m_reviewsByGame.constFind(sourceGameId);
    return iterator == m_reviewsByGame.cend() ? nullptr : &iterator.value();
}

} // namespace parlawl::puzzle_runner
