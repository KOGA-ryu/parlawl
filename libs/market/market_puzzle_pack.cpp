#include "market_puzzle_pack.h"

#include <algorithm>
#include <cmath>

#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#include "market_continuation.h"
#include "market_hud.h"
#include "sealed_continuation_vault_p.h"
#include "strict_json.h"

namespace parlawl::market {

using parlawl::strictjson::JsonValue;
using parlawl::strictjson::StrictJsonParser;
using parlawl::strictjson::canonicalJson;
using parlawl::strictjson::exactKeys;
using parlawl::strictjson::isSha256Hex;
using parlawl::strictjson::kMaximumExactInteger;
using parlawl::strictjson::makeInteger;
using parlawl::strictjson::makeString;
using parlawl::strictjson::member;
using parlawl::strictjson::pythonStringLess;
using parlawl::strictjson::removeMember;
using parlawl::strictjson::requiredBoolean;
using parlawl::strictjson::requiredFloat;
using parlawl::strictjson::requiredInteger;
using parlawl::strictjson::requiredString;
using parlawl::strictjson::semanticId;
using parlawl::strictjson::setError;
using parlawl::strictjson::sha256Hex;
using parlawl::strictjson::validateUtc;

namespace {

const QString kPackHeaderSchema = QStringLiteral("arc/market-puzzle-pack/v1");
const QString kPackHeaderRecordType = QStringLiteral("market_puzzle_pack_header");
const QString kSealedHeaderRecordType = QStringLiteral("market_continuation_pack_header");
const QString kSealedHeaderSchema = QStringLiteral("arc/market-continuation-pack/v1");
const QString kPuzzleSchema = QStringLiteral("arc/market-puzzle/v1");
const QString kPuzzleRecordType = QStringLiteral("market_puzzle");
const QString kContinuationSchema = QStringLiteral("arc/market-continuation/v1");
const QString kContinuationRecordType = QStringLiteral("market_continuation");

const QString kPackIdPrefix = QStringLiteral("market-puzzle-pack-v1");
const QString kContinuationPackIdPrefix = QStringLiteral("market-continuation-pack-v1");
const QString kPuzzleIdPrefix = QStringLiteral("market-puzzle-v1");
const QString kPuzzleRecordIdPrefix = QStringLiteral("market-puzzle-record-v1");
const QString kCommitmentPrefix = QStringLiteral("market-continuation-v1");
const QString kContinuationRecordIdPrefix = QStringLiteral("market-continuation-record-v1");

bool prefixedSha(const QString &value, const QString &prefix)
{
    return value.startsWith(prefix + QLatin1Char(':'))
        && isSha256Hex(value.mid(prefix.size() + 1));
}

//! Permitted rating-seed bases. Every one is a function of the visible window
//! or of pack constants alone; a basis whose inputs include the continuation
//! would announce a violent future before the first bar was read.
bool permittedRatingBasis(const QString &basisId)
{
    return basisId == QStringLiteral("flat_seed_v1")
        || basisId == QStringLiteral("atr_percentile_v1");
}

bool stringArray(
    const JsonValue &value,
    int maximum,
    QStringList *output,
    const QString &context,
    QString *errorMessage)
{
    if (value.kind != JsonValue::Kind::Array || value.array.size() > maximum) {
        setError(errorMessage, context + QStringLiteral(" must be a bounded array of text"));
        return false;
    }
    QSet<QString> seen;
    for (const JsonValue &item : value.array) {
        if (item.kind != JsonValue::Kind::String || item.string.trimmed().isEmpty()
            || item.string != item.string.trimmed() || seen.contains(item.string)) {
            setError(errorMessage, context + QStringLiteral(" must be normalized, unique text"));
            return false;
        }
        seen.insert(item.string);
        output->append(item.string);
    }
    return true;
}

bool floatArray(
    const JsonValue &value,
    int maximum,
    QVector<double> *output,
    const QString &context,
    QString *errorMessage)
{
    if (value.kind != JsonValue::Kind::Array || value.array.isEmpty()
        || value.array.size() > maximum) {
        setError(errorMessage, context + QStringLiteral(" must be a bounded array of floats"));
        return false;
    }
    for (const JsonValue &item : value.array) {
        if (item.kind != JsonValue::Kind::Number || !std::isfinite(item.number)) {
            setError(errorMessage, context + QStringLiteral(" must contain finite floats"));
            return false;
        }
        output->append(item.number);
    }
    return true;
}

bool optionalNumberField(const JsonValue &value, std::optional<double> *output)
{
    if (value.kind == JsonValue::Kind::Null) {
        // Absent is masked, never zero and never carried forward.
        output->reset();
        return true;
    }
    if (value.kind != JsonValue::Kind::Number || !std::isfinite(value.number)) {
        return false;
    }
    *output = value.number;
    return true;
}

bool parseBars(
    const JsonValue &bars,
    int expectedCount,
    QVector<MarketBar> *output,
    const QString &context,
    QString *errorMessage)
{
    if (bars.kind != JsonValue::Kind::Array || bars.array.size() != expectedCount
        || bars.array.size() > kMaximumBars) {
        setError(
            errorMessage,
            QStringLiteral("%1 must contain exactly %2 bars within the consumer bound")
                .arg(context)
                .arg(expectedCount));
        return false;
    }
    for (int index = 0; index < bars.array.size(); ++index) {
        const JsonValue &row = bars.array.at(index);
        if (row.kind != JsonValue::Kind::Array || row.array.size() != 7) {
            setError(
                errorMessage,
                QStringLiteral("%1[%2] must be the fixed 7-tuple").arg(context).arg(index));
            return false;
        }
        const JsonValue &barIndex = row.array.at(0);
        if (barIndex.kind != JsonValue::Kind::Integer || barIndex.integer != index) {
            setError(
                errorMessage,
                QStringLiteral("%1[%2] bar_index must equal its position").arg(context).arg(index));
            return false;
        }
        MarketBar bar;
        bar.barIndex = index;
        if (!optionalNumberField(row.array.at(1), &bar.open)
            || !optionalNumberField(row.array.at(2), &bar.high)
            || !optionalNumberField(row.array.at(3), &bar.low)
            || !optionalNumberField(row.array.at(4), &bar.close)
            || !optionalNumberField(row.array.at(5), &bar.volume)
            || !optionalNumberField(row.array.at(6), &bar.tradeCount)) {
            setError(
                errorMessage,
                QStringLiteral("%1[%2] fields must be floats or null, never integers or zero fills")
                    .arg(context)
                    .arg(index));
            return false;
        }
        if (bar.hasOhlc()
            && (*bar.high < *bar.low || *bar.open > *bar.high || *bar.open < *bar.low
                || *bar.close > *bar.high || *bar.close < *bar.low)) {
            setError(
                errorMessage,
                QStringLiteral("%1[%2] open/close fall outside their own high/low")
                    .arg(context)
                    .arg(index));
            return false;
        }
        output->append(bar);
    }
    return true;
}

bool parseSessionBreaks(
    const JsonValue &value,
    int barCount,
    bool disclosed,
    QVector<bool> *output,
    const QString &context,
    QString *errorMessage)
{
    if (value.kind != JsonValue::Kind::Array) {
        setError(errorMessage, context + QStringLiteral(" must be an array"));
        return false;
    }
    const int expected = disclosed ? barCount : 0;
    if (value.array.size() != expected) {
        setError(
            errorMessage,
            context
                + QStringLiteral(" length must follow the header's session_breaks_disclosed flag"));
        return false;
    }
    for (const JsonValue &item : value.array) {
        if (item.kind != JsonValue::Kind::Boolean) {
            setError(errorMessage, context + QStringLiteral(" must contain booleans"));
            return false;
        }
        output->append(item.boolean);
    }
    return true;
}

bool parseCalibrationQuestion(
    const JsonValue &value,
    MarketCalibrationQuestion *output,
    const QString &context,
    QString *errorMessage)
{
    if (!exactKeys(value, {"bounds", "interval_level", "quantity", "question_id", "unit"}, context, errorMessage)) {
        return false;
    }
    const JsonValue *bounds = member(value, QStringLiteral("bounds"));
    if (bounds == nullptr
        || !exactKeys(*bounds, {"lower", "upper"}, context + QStringLiteral(".bounds"), errorMessage)
        || !requiredString(value, QStringLiteral("question_id"), &output->questionId, context, errorMessage)
        || !requiredString(value, QStringLiteral("quantity"), &output->quantity, context, errorMessage)
        || !requiredString(value, QStringLiteral("unit"), &output->unit, context, errorMessage)
        || !requiredFloat(value, QStringLiteral("interval_level"), &output->intervalLevel, context, errorMessage)
        || !requiredFloat(*bounds, QStringLiteral("lower"), &output->lowerBound, context + QStringLiteral(".bounds"), errorMessage)
        || !requiredFloat(*bounds, QStringLiteral("upper"), &output->upperBound, context + QStringLiteral(".bounds"), errorMessage)) {
        return false;
    }
    if (output->intervalLevel <= 0.0 || output->intervalLevel >= 1.0
        || output->lowerBound >= output->upperBound) {
        setError(errorMessage, context + QStringLiteral(" bounds or interval level are degenerate"));
        return false;
    }
    return true;
}

bool parsePackSource(
    const JsonValue &value,
    bool withCitation,
    MarketPackSource *output,
    const QString &context,
    QString *errorMessage)
{
    const bool keysOk = withCitation
        ? exactKeys(
            value,
            {"adjustment_table_sha256", "corpus_dataset_version", "scan_citation", "scan_manifest_id"},
            context,
            errorMessage)
        : exactKeys(
            value,
            {"adjustment_table_sha256", "corpus_dataset_version", "grain", "scan_manifest_id"},
            context,
            errorMessage);
    if (!keysOk) {
        return false;
    }
    if (!requiredString(value, QStringLiteral("scan_manifest_id"), &output->scanManifestId, context, errorMessage)
        || !requiredString(value, QStringLiteral("corpus_dataset_version"), &output->corpusDatasetVersion, context, errorMessage)
        || !requiredString(value, QStringLiteral("adjustment_table_sha256"), &output->adjustmentTableSha256, context, errorMessage)) {
        return false;
    }
    if (!isSha256Hex(output->adjustmentTableSha256)) {
        setError(errorMessage, context + QStringLiteral(".adjustment_table_sha256 must be 64 lowercase hex"));
        return false;
    }
    if (withCitation) {
        return requiredString(value, QStringLiteral("scan_citation"), &output->scanCitation, context, errorMessage);
    }
    return requiredString(value, QStringLiteral("grain"), &output->grain, context, errorMessage);
}

bool parsePackHeader(
    const JsonValue &root,
    MarketPackHeader *header,
    QString *errorMessage)
{
    const QString context = QStringLiteral("market pack header");
    if (!exactKeys(
            root,
            {"anonymization", "calibration", "compiler", "continuation_bar_count",
             "continuation_pack_id", "counts", "evidence_grade", "grain", "hud_spec",
             "normalization", "pack_id", "rating", "record_type", "response_horizon_bars",
             "schema", "source", "task_spec", "themes", "visible_bar_count"},
            context,
            errorMessage)) {
        return false;
    }

    QString schema;
    QString recordType;
    if (!requiredString(root, QStringLiteral("schema"), &schema, context, errorMessage)
        || !requiredString(root, QStringLiteral("record_type"), &recordType, context, errorMessage)
        || !requiredString(root, QStringLiteral("pack_id"), &header->packId, context, errorMessage)
        || !requiredString(root, QStringLiteral("continuation_pack_id"), &header->continuationPackId, context, errorMessage)
        || !requiredString(root, QStringLiteral("grain"), &header->grain, context, errorMessage)
        || !requiredString(root, QStringLiteral("evidence_grade"), &header->evidenceGrade, context, errorMessage)) {
        return false;
    }
    if (schema != kPackHeaderSchema || recordType != kPackHeaderRecordType) {
        setError(errorMessage, context + QStringLiteral(" uses an unsupported schema or record_type"));
        return false;
    }
    if (!prefixedSha(header->packId, kPackIdPrefix)
        || !prefixedSha(header->continuationPackId, kContinuationPackIdPrefix)) {
        setError(errorMessage, context + QStringLiteral(" identities have invalid syntax"));
        return false;
    }
    if (header->evidenceGrade != marketPackEvidenceGrade()) {
        setError(
            errorMessage,
            context + QStringLiteral(" must carry the v1 evidence_grade sentence verbatim"));
        return false;
    }

    const JsonValue *compiler = member(root, QStringLiteral("compiler"));
    if (compiler == nullptr
        || !exactKeys(
            *compiler,
            {"built_at_utc", "compiler_id", "compiler_version", "git_commit", "git_dirty"},
            context + QStringLiteral(".compiler"),
            errorMessage)
        || !requiredString(*compiler, QStringLiteral("compiler_id"), &header->compilerId, context, errorMessage)
        || !requiredString(*compiler, QStringLiteral("compiler_version"), &header->compilerVersion, context, errorMessage)
        || !requiredString(*compiler, QStringLiteral("git_commit"), &header->gitCommit, context, errorMessage)
        || !requiredBoolean(*compiler, QStringLiteral("git_dirty"), &header->gitDirty, context, errorMessage)
        || !requiredString(*compiler, QStringLiteral("built_at_utc"), &header->builtAtUtc, context, errorMessage)) {
        return false;
    }
    static const QRegularExpression commitPattern(QStringLiteral("^[0-9a-f]{40}$"));
    const JsonValue *builtAt = member(*compiler, QStringLiteral("built_at_utc"));
    if (!commitPattern.match(header->gitCommit).hasMatch()
        || builtAt == nullptr
        || !validateUtc(*builtAt, context + QStringLiteral(".compiler.built_at_utc"), false, errorMessage)) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = context + QStringLiteral(".compiler.git_commit must be 40 lowercase hex");
        }
        return false;
    }

    const JsonValue *counts = member(root, QStringLiteral("counts"));
    const JsonValue *byTaskKind = counts == nullptr
        ? nullptr
        : member(*counts, QStringLiteral("by_task_kind"));
    qint64 records = 0;
    qint64 anomaly = 0;
    qint64 pattern = 0;
    qint64 trade = 0;
    if (counts == nullptr || byTaskKind == nullptr
        || !exactKeys(*counts, {"by_task_kind", "records"}, context + QStringLiteral(".counts"), errorMessage)
        || !exactKeys(*byTaskKind, {"anomaly_flag", "pattern_call", "trade_line"}, context + QStringLiteral(".counts.by_task_kind"), errorMessage)
        || !requiredInteger(*counts, QStringLiteral("records"), 1, kMaximumMarketRecords, &records, context, errorMessage)
        || !requiredInteger(*byTaskKind, QStringLiteral("anomaly_flag"), 0, kMaximumMarketRecords, &anomaly, context, errorMessage)
        || !requiredInteger(*byTaskKind, QStringLiteral("pattern_call"), 0, kMaximumMarketRecords, &pattern, context, errorMessage)
        || !requiredInteger(*byTaskKind, QStringLiteral("trade_line"), 0, kMaximumMarketRecords, &trade, context, errorMessage)) {
        return false;
    }
    if (anomaly + pattern + trade != records) {
        setError(errorMessage, context + QStringLiteral(".counts.by_task_kind does not sum to records"));
        return false;
    }
    header->recordCount = static_cast<int>(records);
    header->anomalyFlagCount = static_cast<int>(anomaly);
    header->patternCallCount = static_cast<int>(pattern);
    header->tradeLineCount = static_cast<int>(trade);

    qint64 visibleBars = 0;
    qint64 continuationBars = 0;
    qint64 horizon = 0;
    if (!requiredInteger(root, QStringLiteral("visible_bar_count"), kMinimumVisibleBars, kMaximumBars, &visibleBars, context, errorMessage)
        || !requiredInteger(root, QStringLiteral("continuation_bar_count"), 1, kMaximumBars, &continuationBars, context, errorMessage)
        || !requiredInteger(root, QStringLiteral("response_horizon_bars"), 1, kMaximumPlies, &horizon, context, errorMessage)) {
        return false;
    }
    if (continuationBars < horizon) {
        setError(
            errorMessage,
            context + QStringLiteral(" continuation_bar_count is shorter than the response horizon"));
        return false;
    }
    header->visibleBarCount = static_cast<int>(visibleBars);
    header->continuationBarCount = static_cast<int>(continuationBars);
    header->responseHorizonBars = static_cast<int>(horizon);

    const JsonValue *normalization = member(root, QStringLiteral("normalization"));
    qint64 priceDecimals = 0;
    qint64 volumeDecimals = 0;
    if (normalization == nullptr
        || !exactKeys(*normalization, {"price_anchor", "price_decimals", "volume_basis", "volume_decimals"}, context + QStringLiteral(".normalization"), errorMessage)
        || !requiredFloat(*normalization, QStringLiteral("price_anchor"), &header->priceAnchor, context, errorMessage)
        || !requiredInteger(*normalization, QStringLiteral("price_decimals"), 0, 12, &priceDecimals, context, errorMessage)
        || !requiredString(*normalization, QStringLiteral("volume_basis"), &header->volumeBasis, context, errorMessage)
        || !requiredInteger(*normalization, QStringLiteral("volume_decimals"), 0, 12, &volumeDecimals, context, errorMessage)) {
        return false;
    }
    header->priceDecimals = static_cast<int>(priceDecimals);
    header->volumeDecimals = static_cast<int>(volumeDecimals);
    if (header->priceAnchor != 100.0) {
        setError(errorMessage, context + QStringLiteral(".normalization.price_anchor must be 100.0 in v1"));
        return false;
    }

    const JsonValue *anonymization = member(root, QStringLiteral("anonymization"));
    if (anonymization == nullptr
        || !exactKeys(*anonymization, {"calendar_disclosed", "session_breaks_disclosed", "symbol_scheme"}, context + QStringLiteral(".anonymization"), errorMessage)
        || !requiredString(*anonymization, QStringLiteral("symbol_scheme"), &header->symbolScheme, context, errorMessage)
        || !requiredBoolean(*anonymization, QStringLiteral("calendar_disclosed"), &header->calendarDisclosed, context, errorMessage)
        || !requiredBoolean(*anonymization, QStringLiteral("session_breaks_disclosed"), &header->sessionBreaksDisclosed, context, errorMessage)) {
        return false;
    }
    if (header->calendarDisclosed) {
        setError(
            errorMessage,
            context + QStringLiteral(" declares a disclosed calendar, which v1 refuses: absolute dates "
                                     "appear nowhere in the visible file"));
        return false;
    }

    const JsonValue *themes = member(root, QStringLiteral("themes"));
    if (themes == nullptr || themes->kind != JsonValue::Kind::Array || themes->array.isEmpty()
        || themes->array.size() > kMaximumThemes) {
        setError(errorMessage, context + QStringLiteral(".themes must be a bounded non-empty array"));
        return false;
    }
    for (const JsonValue &entry : themes->array) {
        QString theme;
        bool knowable = false;
        if (!exactKeys(entry, {"knowable_at_t", "theme"}, context + QStringLiteral(".themes[]"), errorMessage)
            || !requiredString(entry, QStringLiteral("theme"), &theme, context, errorMessage)
            || !requiredBoolean(entry, QStringLiteral("knowable_at_t"), &knowable, context, errorMessage)) {
            return false;
        }
        if (!knowable) {
            setError(
                errorMessage,
                QStringLiteral("%1.themes declares '%2' with knowable_at_t false; a theme that names "
                               "the outcome hands the operator the answer in a word")
                    .arg(context, theme));
            return false;
        }
        if (header->themes.contains(theme)) {
            setError(errorMessage, context + QStringLiteral(".themes contains a duplicate"));
            return false;
        }
        header->themes.append(theme);
    }

    const JsonValue *taskSpec = member(root, QStringLiteral("task_spec"));
    const JsonValue *patternSpec = taskSpec == nullptr ? nullptr : member(*taskSpec, QStringLiteral("pattern_call"));
    const JsonValue *tradeSpec = taskSpec == nullptr ? nullptr : member(*taskSpec, QStringLiteral("trade_line"));
    const JsonValue *anomalySpec = taskSpec == nullptr ? nullptr : member(*taskSpec, QStringLiteral("anomaly_flag"));
    if (taskSpec == nullptr || patternSpec == nullptr || tradeSpec == nullptr || anomalySpec == nullptr
        || !exactKeys(*taskSpec, {"anomaly_flag", "pattern_call", "trade_line"}, context + QStringLiteral(".task_spec"), errorMessage)
        || !exactKeys(*patternSpec, {"labels"}, context + QStringLiteral(".task_spec.pattern_call"), errorMessage)
        || !exactKeys(*tradeSpec, {"entries", "follow_up_actions", "size_bands", "stop_atr_multiples", "target_atr_multiples"}, context + QStringLiteral(".task_spec.trade_line"), errorMessage)
        || !exactKeys(*anomalySpec, {"artifact_classes"}, context + QStringLiteral(".task_spec.anomaly_flag"), errorMessage)) {
        return false;
    }
    MarketTaskSpec &spec = header->taskSpec;
    if (!stringArray(*member(*patternSpec, QStringLiteral("labels")), 64, &spec.patternLabels, context + QStringLiteral(".task_spec.pattern_call.labels"), errorMessage)
        || !stringArray(*member(*tradeSpec, QStringLiteral("entries")), 64, &spec.entries, context + QStringLiteral(".task_spec.trade_line.entries"), errorMessage)
        || !stringArray(*member(*tradeSpec, QStringLiteral("size_bands")), 64, &spec.sizeBands, context + QStringLiteral(".task_spec.trade_line.size_bands"), errorMessage)
        || !stringArray(*member(*tradeSpec, QStringLiteral("follow_up_actions")), 64, &spec.followUpActions, context + QStringLiteral(".task_spec.trade_line.follow_up_actions"), errorMessage)
        || !stringArray(*member(*anomalySpec, QStringLiteral("artifact_classes")), 64, &spec.artifactClasses, context + QStringLiteral(".task_spec.anomaly_flag.artifact_classes"), errorMessage)
        || !floatArray(*member(*tradeSpec, QStringLiteral("stop_atr_multiples")), 64, &spec.stopAtrMultiples, context + QStringLiteral(".task_spec.trade_line.stop_atr_multiples"), errorMessage)
        || !floatArray(*member(*tradeSpec, QStringLiteral("target_atr_multiples")), 64, &spec.targetAtrMultiples, context + QStringLiteral(".task_spec.trade_line.target_atr_multiples"), errorMessage)) {
        return false;
    }
    if (!spec.entries.contains(QStringLiteral("pass"))) {
        setError(
            errorMessage,
            context + QStringLiteral(".task_spec.trade_line.entries must include 'pass'; a rep that "
                                     "cannot decline a fight teaches the opposite of discipline"));
        return false;
    }

    const JsonValue *hudSpec = member(root, QStringLiteral("hud_spec"));
    if (hudSpec == nullptr
        || !exactKeys(*hudSpec, {"stats", "verified_stats"}, context + QStringLiteral(".hud_spec"), errorMessage)
        || !stringArray(*member(*hudSpec, QStringLiteral("stats")), kMaximumHudStats, &header->hudStats, context + QStringLiteral(".hud_spec.stats"), errorMessage)
        || !stringArray(*member(*hudSpec, QStringLiteral("verified_stats")), kMaximumHudStats, &header->verifiedHudStats, context + QStringLiteral(".hud_spec.verified_stats"), errorMessage)) {
        return false;
    }
    const QStringList derivable = verifiedHudStatIds();
    for (const QString &statId : header->verifiedHudStats) {
        if (!header->hudStats.contains(statId) || !derivable.contains(statId)) {
            setError(
                errorMessage,
                QStringLiteral("%1.hud_spec.verified_stats names '%2', which ParlAWL cannot derive "
                               "from the visible bars")
                    .arg(context, statId));
            return false;
        }
    }

    const JsonValue *calibration = member(root, QStringLiteral("calibration"));
    if (calibration == nullptr
        || !exactKeys(*calibration, {"bounds", "interval_level", "quantity", "question_template", "unit"}, context + QStringLiteral(".calibration"), errorMessage)) {
        return false;
    }
    {
        // The header spells the question with `question_template`; the record
        // spells the same thing with `question_id`. Normalize before comparing.
        JsonValue rewritten = *calibration;
        QString templateId;
        if (!requiredString(*calibration, QStringLiteral("question_template"), &templateId, context, errorMessage)) {
            return false;
        }
        removeMember(&rewritten, QStringLiteral("question_template"));
        rewritten.objectKeys.append(QStringLiteral("question_id"));
        rewritten.objectValues.append(makeString(templateId));
        if (!parseCalibrationQuestion(rewritten, &header->calibration, context + QStringLiteral(".calibration"), errorMessage)) {
            return false;
        }
    }

    const JsonValue *rating = member(root, QStringLiteral("rating"));
    const JsonValue *band = rating == nullptr ? nullptr : member(*rating, QStringLiteral("band"));
    qint64 minimum = 0;
    qint64 maximum = 0;
    if (rating == nullptr || band == nullptr
        || !exactKeys(*rating, {"band", "basis_id"}, context + QStringLiteral(".rating"), errorMessage)
        || !exactKeys(*band, {"max", "min"}, context + QStringLiteral(".rating.band"), errorMessage)
        || !requiredString(*rating, QStringLiteral("basis_id"), &header->ratingBasisId, context, errorMessage)
        || !requiredInteger(*band, QStringLiteral("min"), kMinimumRatingSeed, kMaximumRatingSeed, &minimum, context, errorMessage)
        || !requiredInteger(*band, QStringLiteral("max"), kMinimumRatingSeed, kMaximumRatingSeed, &maximum, context, errorMessage)) {
        return false;
    }
    if (minimum >= maximum || !permittedRatingBasis(header->ratingBasisId)) {
        setError(
            errorMessage,
            context + QStringLiteral(".rating band is degenerate or its basis is not permitted in v1"));
        return false;
    }
    header->ratingBandMinimum = static_cast<int>(minimum);
    header->ratingBandMaximum = static_cast<int>(maximum);

    const JsonValue *source = member(root, QStringLiteral("source"));
    if (source == nullptr
        || !parsePackSource(*source, true, &header->source, context + QStringLiteral(".source"), errorMessage)) {
        return false;
    }
    header->source.grain = header->grain;

    JsonValue identity = root;
    removeMember(&identity, QStringLiteral("pack_id"));
    if (semanticId(kPackIdPrefix, identity) != header->packId) {
        setError(errorMessage, context + QStringLiteral(".pack_id does not match its canonical content"));
        return false;
    }
    return true;
}

bool parseSealedHeader(
    const JsonValue &root,
    const MarketPackHeader &visibleHeader,
    int *recordCount,
    QString *errorMessage)
{
    const QString context = QStringLiteral("market continuation pack header");
    if (!exactKeys(
            root,
            {"built_at_utc", "compiler", "continuation_pack_id", "evidence_grade", "record_count",
             "record_type", "schema"},
            context,
            errorMessage)) {
        return false;
    }
    QString schema;
    QString recordType;
    QString continuationPackId;
    QString evidenceGrade;
    QString builtAt;
    qint64 count = 0;
    if (!requiredString(root, QStringLiteral("schema"), &schema, context, errorMessage)
        || !requiredString(root, QStringLiteral("record_type"), &recordType, context, errorMessage)
        || !requiredString(root, QStringLiteral("continuation_pack_id"), &continuationPackId, context, errorMessage)
        || !requiredString(root, QStringLiteral("evidence_grade"), &evidenceGrade, context, errorMessage)
        || !requiredString(root, QStringLiteral("built_at_utc"), &builtAt, context, errorMessage)
        || !requiredInteger(root, QStringLiteral("record_count"), 1, kMaximumMarketRecords, &count, context, errorMessage)) {
        return false;
    }
    if (schema != kSealedHeaderSchema || recordType != kSealedHeaderRecordType) {
        setError(errorMessage, context + QStringLiteral(" uses an unsupported schema or record_type"));
        return false;
    }
    if (evidenceGrade != marketPackEvidenceGrade()) {
        setError(errorMessage, context + QStringLiteral(" must carry the v1 evidence_grade sentence verbatim"));
        return false;
    }
    const JsonValue *builtAtValue = member(root, QStringLiteral("built_at_utc"));
    if (builtAtValue == nullptr
        || !validateUtc(*builtAtValue, context + QStringLiteral(".built_at_utc"), false, errorMessage)) {
        return false;
    }
    const JsonValue *compiler = member(root, QStringLiteral("compiler"));
    if (compiler == nullptr
        || !exactKeys(
            *compiler,
            {"built_at_utc", "compiler_id", "compiler_version", "git_commit", "git_dirty"},
            context + QStringLiteral(".compiler"),
            errorMessage)) {
        return false;
    }
    JsonValue identity = root;
    removeMember(&identity, QStringLiteral("continuation_pack_id"));
    if (semanticId(kContinuationPackIdPrefix, identity) != continuationPackId) {
        setError(errorMessage, context + QStringLiteral(".continuation_pack_id does not match its canonical content"));
        return false;
    }
    if (continuationPackId != visibleHeader.continuationPackId) {
        setError(
            errorMessage,
            QStringLiteral("the visible header names continuation pack %1 but the sealed file is %2")
                .arg(visibleHeader.continuationPackId, continuationPackId));
        return false;
    }
    if (static_cast<int>(count) != visibleHeader.recordCount) {
        setError(errorMessage, context + QStringLiteral(".record_count disagrees with the visible header"));
        return false;
    }
    *recordCount = static_cast<int>(count);
    return true;
}

bool expectedPliesFor(
    TaskKind taskKind,
    int horizonBars,
    QVector<MarketPlySpec> *expected,
    QString *errorMessage)
{
    switch (taskKind) {
    case TaskKind::PatternCall:
        expected->append({0, PlyKind::Label, std::nullopt});
        expected->append({1, PlyKind::Confidence, std::nullopt});
        return true;
    case TaskKind::AnomalyFlag:
        expected->append({0, PlyKind::Verdict, std::nullopt});
        expected->append({1, PlyKind::ArtifactClass, std::nullopt});
        expected->append({2, PlyKind::Confidence, std::nullopt});
        return true;
    case TaskKind::TradeLine:
        expected->append({0, PlyKind::Entry, std::nullopt});
        expected->append({1, PlyKind::SizeBand, std::nullopt});
        expected->append({2, PlyKind::Bracket, std::nullopt});
        for (int offset = 1; offset <= horizonBars; ++offset) {
            expected->append({2 + offset, PlyKind::FollowUp, offset});
        }
        if (expected->size() > kMaximumPlies) {
            setError(errorMessage, QStringLiteral("response_spec exceeds the 32-ply consumer bound"));
            return false;
        }
        return true;
    }
    return false;
}

bool parseVisibleRecord(
    const QByteArray &line,
    int lineNumber,
    const MarketPackHeader &header,
    MarketPuzzleVisible *puzzle,
    QString *errorMessage)
{
    const QString context = QStringLiteral("market puzzle line %1").arg(lineNumber);
    JsonValue root;
    StrictJsonParser parser(line);
    if (!parser.parse(&root, errorMessage)) {
        setError(errorMessage, QStringLiteral("%1: %2").arg(context, errorMessage == nullptr ? QString() : *errorMessage));
        return false;
    }
    if (!exactKeys(
            root,
            {"calibration_question", "continuation_commitment", "display_symbol", "hud", "puzzle_id",
             "rating_seed", "rating_seed_basis", "record_id", "record_type", "response_spec",
             "schema", "source", "task_kind", "theme", "window"},
            context,
            errorMessage)) {
        return false;
    }

    QString schema;
    QString recordType;
    QString taskKindText;
    if (!requiredString(root, QStringLiteral("schema"), &schema, context, errorMessage)
        || !requiredString(root, QStringLiteral("record_type"), &recordType, context, errorMessage)
        || !requiredString(root, QStringLiteral("puzzle_id"), &puzzle->puzzleId, context, errorMessage)
        || !requiredString(root, QStringLiteral("record_id"), &puzzle->recordId, context, errorMessage)
        || !requiredString(root, QStringLiteral("continuation_commitment"), &puzzle->continuationCommitment, context, errorMessage)
        || !requiredString(root, QStringLiteral("task_kind"), &taskKindText, context, errorMessage)
        || !requiredString(root, QStringLiteral("theme"), &puzzle->theme, context, errorMessage)
        || !requiredString(root, QStringLiteral("rating_seed_basis"), &puzzle->ratingSeedBasis, context, errorMessage)
        || !requiredString(root, QStringLiteral("display_symbol"), &puzzle->displaySymbol, context, errorMessage)) {
        return false;
    }
    if (schema != kPuzzleSchema || recordType != kPuzzleRecordType) {
        setError(errorMessage, context + QStringLiteral(" uses an unsupported schema or record_type"));
        return false;
    }
    if (!prefixedSha(puzzle->puzzleId, kPuzzleIdPrefix)
        || !prefixedSha(puzzle->recordId, kPuzzleRecordIdPrefix)
        || !prefixedSha(puzzle->continuationCommitment, kCommitmentPrefix)) {
        setError(errorMessage, context + QStringLiteral(" identities have invalid syntax"));
        return false;
    }
    const auto taskKind = taskKindFromText(taskKindText);
    if (!taskKind.has_value()) {
        setError(errorMessage, context + QStringLiteral(" declares an unknown task_kind"));
        return false;
    }
    puzzle->taskKind = *taskKind;
    if (!header.themes.contains(puzzle->theme)) {
        setError(
            errorMessage,
            QStringLiteral("%1 declares theme '%2', which the pack header does not list")
                .arg(context, puzzle->theme));
        return false;
    }
    static const QRegularExpression symbolPattern(QStringLiteral("^SYM-[0-9]{4}$"));
    if (!symbolPattern.match(puzzle->displaySymbol).hasMatch()) {
        setError(errorMessage, context + QStringLiteral(" display_symbol is not an opaque pack-local symbol"));
        return false;
    }

    qint64 ratingSeed = 0;
    if (!requiredInteger(root, QStringLiteral("rating_seed"), header.ratingBandMinimum, header.ratingBandMaximum, &ratingSeed, context, errorMessage)) {
        return false;
    }
    puzzle->ratingSeed = static_cast<int>(ratingSeed);
    if (puzzle->ratingSeedBasis != header.ratingBasisId || !permittedRatingBasis(puzzle->ratingSeedBasis)) {
        setError(errorMessage, context + QStringLiteral(" rating_seed_basis is not the header's permitted basis"));
        return false;
    }

    const JsonValue *window = member(root, QStringLiteral("window"));
    if (window == nullptr
        || !exactKeys(*window, {"bar_count", "bars", "grain", "session_break_after", "window_digest"}, context + QStringLiteral(".window"), errorMessage)
        || !requiredString(*window, QStringLiteral("grain"), &puzzle->window.grain, context, errorMessage)
        || !requiredString(*window, QStringLiteral("window_digest"), &puzzle->window.windowDigest, context, errorMessage)) {
        return false;
    }
    qint64 barCount = 0;
    if (!requiredInteger(*window, QStringLiteral("bar_count"), kMinimumVisibleBars, kMaximumBars, &barCount, context, errorMessage)) {
        return false;
    }
    if (static_cast<int>(barCount) != header.visibleBarCount || puzzle->window.grain != header.grain) {
        setError(
            errorMessage,
            context + QStringLiteral(".window bar_count or grain differs from the pack constants; any "
                                     "per-record variation is a channel"));
        return false;
    }
    puzzle->window.barCount = static_cast<int>(barCount);
    const JsonValue *bars = member(*window, QStringLiteral("bars"));
    if (bars == nullptr
        || !parseBars(*bars, puzzle->window.barCount, &puzzle->window.bars, context + QStringLiteral(".window.bars"), errorMessage)
        || !parseSessionBreaks(
            *member(*window, QStringLiteral("session_break_after")),
            puzzle->window.barCount,
            header.sessionBreaksDisclosed,
            &puzzle->window.sessionBreakAfter,
            context + QStringLiteral(".window.session_break_after"),
            errorMessage)) {
        return false;
    }
    if (!isSha256Hex(puzzle->window.windowDigest)
        || sha256Hex(canonicalJson(*bars)) != puzzle->window.windowDigest) {
        setError(errorMessage, context + QStringLiteral(".window.window_digest does not cover the shipped bars"));
        return false;
    }
    const auto lastClose = puzzle->window.bars.isEmpty()
        ? std::optional<double>()
        : puzzle->window.bars.last().close;
    if (!lastClose.has_value() || *lastClose != header.priceAnchor) {
        setError(
            errorMessage,
            context + QStringLiteral(".window is not normalized: the close at T must be exactly 100.0"));
        return false;
    }

    const JsonValue *hud = member(root, QStringLiteral("hud"));
    if (hud == nullptr || hud->kind != JsonValue::Kind::Array || hud->array.size() > kMaximumHudStats) {
        setError(errorMessage, context + QStringLiteral(".hud must be a bounded array"));
        return false;
    }
    QSet<QString> seenStats;
    for (const JsonValue &entry : hud->array) {
        MarketHudStat stat;
        if (!exactKeys(entry, {"stat_id", "unit", "value"}, context + QStringLiteral(".hud[]"), errorMessage)
            || !requiredString(entry, QStringLiteral("stat_id"), &stat.statId, context, errorMessage)
            || !requiredString(entry, QStringLiteral("unit"), &stat.unit, context, errorMessage)
            || !requiredFloat(entry, QStringLiteral("value"), &stat.value, context, errorMessage)) {
            return false;
        }
        if (!header.hudStats.contains(stat.statId) || seenStats.contains(stat.statId)) {
            setError(
                errorMessage,
                QStringLiteral("%1.hud carries '%2', which is unknown or repeated; an unrecognized HUD "
                               "number trains a reflex against a number nobody checked")
                    .arg(context, stat.statId));
            return false;
        }
        seenStats.insert(stat.statId);
        puzzle->hud.append(stat);
    }
    for (const QString &statId : header.verifiedHudStats) {
        if (!seenStats.contains(statId)) {
            setError(
                errorMessage,
                QStringLiteral("%1.hud omits verified stat '%2'").arg(context, statId));
            return false;
        }
        const auto recomputed = computeVerifiedHudStat(statId, puzzle->window.bars);
        double supplied = 0.0;
        for (const MarketHudStat &stat : puzzle->hud) {
            if (stat.statId == statId) {
                supplied = stat.value;
            }
        }
        if (!recomputed.has_value()
            || std::fabs(*recomputed - supplied) > kHudVerificationTolerance) {
            setError(
                errorMessage,
                QStringLiteral("%1.hud '%2' disagrees with ParlAWL's own derivation from the visible bars")
                    .arg(context, statId));
            return false;
        }
    }

    const JsonValue *responseSpec = member(root, QStringLiteral("response_spec"));
    qint64 horizon = 0;
    if (responseSpec == nullptr
        || !exactKeys(*responseSpec, {"horizon_bars", "plies"}, context + QStringLiteral(".response_spec"), errorMessage)
        || !requiredInteger(*responseSpec, QStringLiteral("horizon_bars"), 1, kMaximumPlies, &horizon, context, errorMessage)) {
        return false;
    }
    if (static_cast<int>(horizon) != header.responseHorizonBars) {
        setError(
            errorMessage,
            context + QStringLiteral(".response_spec.horizon_bars is not the pack constant; a varying "
                                     "ply count leaks how long the rule's trade lasts"));
        return false;
    }
    puzzle->responseHorizonBars = static_cast<int>(horizon);

    const JsonValue *plies = member(*responseSpec, QStringLiteral("plies"));
    if (plies == nullptr || plies->kind != JsonValue::Kind::Array
        || plies->array.size() > kMaximumPlies) {
        setError(errorMessage, context + QStringLiteral(".response_spec.plies must be a bounded array"));
        return false;
    }
    for (int index = 0; index < plies->array.size(); ++index) {
        const JsonValue &entry = plies->array.at(index);
        const QString plyContext = QStringLiteral("%1.response_spec.plies[%2]").arg(context).arg(index);
        const bool withOffset = member(entry, QStringLiteral("bar_offset")) != nullptr;
        const bool keysOk = withOffset
            ? exactKeys(entry, {"bar_offset", "ply_index", "ply_kind"}, plyContext, errorMessage)
            : exactKeys(entry, {"ply_index", "ply_kind"}, plyContext, errorMessage);
        QString plyKindTextValue;
        qint64 plyIndex = 0;
        if (!keysOk
            || !requiredInteger(entry, QStringLiteral("ply_index"), 0, kMaximumPlies, &plyIndex, plyContext, errorMessage)
            || !requiredString(entry, QStringLiteral("ply_kind"), &plyKindTextValue, plyContext, errorMessage)) {
            return false;
        }
        const auto plyKind = plyKindFromText(plyKindTextValue);
        if (!plyKind.has_value()) {
            setError(errorMessage, plyContext + QStringLiteral(" declares an unknown ply_kind"));
            return false;
        }
        MarketPlySpec spec;
        spec.plyIndex = static_cast<int>(plyIndex);
        spec.kind = *plyKind;
        if (withOffset) {
            qint64 offset = 0;
            if (!requiredInteger(entry, QStringLiteral("bar_offset"), 1, kMaximumPlies, &offset, plyContext, errorMessage)) {
                return false;
            }
            spec.barOffset = static_cast<int>(offset);
        }
        puzzle->plies.append(spec);
    }
    QVector<MarketPlySpec> expected;
    if (!expectedPliesFor(puzzle->taskKind, puzzle->responseHorizonBars, &expected, errorMessage)) {
        return false;
    }
    if (expected.size() != puzzle->plies.size()) {
        setError(errorMessage, context + QStringLiteral(".response_spec.plies does not match its task kind"));
        return false;
    }
    for (int index = 0; index < expected.size(); ++index) {
        const MarketPlySpec &want = expected.at(index);
        const MarketPlySpec &got = puzzle->plies.at(index);
        if (want.plyIndex != got.plyIndex || want.kind != got.kind || want.barOffset != got.barOffset) {
            setError(errorMessage, context + QStringLiteral(".response_spec.plies does not match its task kind"));
            return false;
        }
    }

    const JsonValue *calibration = member(root, QStringLiteral("calibration_question"));
    if (calibration == nullptr
        || !parseCalibrationQuestion(*calibration, &puzzle->calibrationQuestion, context + QStringLiteral(".calibration_question"), errorMessage)) {
        return false;
    }
    if (puzzle->calibrationQuestion.questionId != header.calibration.questionId
        || puzzle->calibrationQuestion.quantity != header.calibration.quantity
        || puzzle->calibrationQuestion.unit != header.calibration.unit
        || puzzle->calibrationQuestion.intervalLevel != header.calibration.intervalLevel
        || puzzle->calibrationQuestion.lowerBound != header.calibration.lowerBound
        || puzzle->calibrationQuestion.upperBound != header.calibration.upperBound) {
        setError(errorMessage, context + QStringLiteral(".calibration_question differs from the pack constant"));
        return false;
    }

    const JsonValue *source = member(root, QStringLiteral("source"));
    if (source == nullptr
        || !parsePackSource(*source, false, &puzzle->source, context + QStringLiteral(".source"), errorMessage)) {
        return false;
    }
    if (puzzle->source.scanManifestId != header.source.scanManifestId
        || puzzle->source.corpusDatasetVersion != header.source.corpusDatasetVersion
        || puzzle->source.adjustmentTableSha256 != header.source.adjustmentTableSha256
        || puzzle->source.grain != header.grain) {
        setError(errorMessage, context + QStringLiteral(".source disagrees with the pack header citation"));
        return false;
    }
    puzzle->source.scanCitation = header.source.scanCitation;

    JsonValue identity;
    identity.kind = JsonValue::Kind::Object;
    identity.objectKeys = {
        QStringLiteral("continuation_commitment"),
        QStringLiteral("grain"),
        QStringLiteral("response_horizon_bars"),
        QStringLiteral("task_kind"),
        QStringLiteral("window_digest"),
    };
    identity.objectValues = {
        makeString(puzzle->continuationCommitment),
        makeString(puzzle->window.grain),
        makeInteger(puzzle->responseHorizonBars),
        makeString(taskKindText),
        makeString(puzzle->window.windowDigest),
    };
    if (semanticId(kPuzzleIdPrefix, identity) != puzzle->puzzleId) {
        setError(
            errorMessage,
            context + QStringLiteral(".puzzle_id does not match its canonical position and commitment"));
        return false;
    }

    JsonValue recordContent = root;
    removeMember(&recordContent, QStringLiteral("record_id"));
    removeMember(&recordContent, QStringLiteral("record_type"));
    removeMember(&recordContent, QStringLiteral("schema"));
    if (semanticId(kPuzzleRecordIdPrefix, recordContent) != puzzle->recordId) {
        setError(errorMessage, context + QStringLiteral(".record_id does not match its canonical content"));
        return false;
    }
    if (canonicalJson(root) != line) {
        setError(errorMessage, context + QStringLiteral(" is not canonical JSONL emitted by the v1 contract"));
        return false;
    }
    puzzle->canonicalLine = line;
    return true;
}

bool parseScoringKey(
    const JsonValue &value,
    TaskKind taskKind,
    const MarketPackHeader &header,
    MarketScoringKey *key,
    const QString &context,
    QString *errorMessage)
{
    QString declaredTaskKind;
    if (!requiredString(value, QStringLiteral("task_kind"), &declaredTaskKind, context, errorMessage)) {
        return false;
    }
    const auto declared = taskKindFromText(declaredTaskKind);
    if (!declared.has_value() || *declared != taskKind) {
        setError(errorMessage, context + QStringLiteral(".task_kind disagrees with its visible record"));
        return false;
    }
    key->taskKind = taskKind;

    if (taskKind == TaskKind::PatternCall) {
        if (!exactKeys(value, {"correct_label", "label_derivation", "task_kind"}, context, errorMessage)
            || !requiredString(value, QStringLiteral("correct_label"), &key->correctLabel, context, errorMessage)
            || !requiredString(value, QStringLiteral("label_derivation"), &key->labelDerivation, context, errorMessage)) {
            return false;
        }
        if (!header.taskSpec.patternLabels.contains(key->correctLabel)) {
            setError(errorMessage, context + QStringLiteral(".correct_label is outside the declared label set"));
            return false;
        }
        return true;
    }

    if (taskKind == TaskKind::AnomalyFlag) {
        if (!exactKeys(
                value,
                {"artifact_class", "clean_provenance", "injection_spec", "planted", "task_kind"},
                context,
                errorMessage)
            || !requiredBoolean(value, QStringLiteral("planted"), &key->planted, context, errorMessage)) {
            return false;
        }
        const JsonValue *artifactClass = member(value, QStringLiteral("artifact_class"));
        const JsonValue *injection = member(value, QStringLiteral("injection_spec"));
        const JsonValue *cleanProvenance = member(value, QStringLiteral("clean_provenance"));
        if (artifactClass == nullptr || injection == nullptr || cleanProvenance == nullptr) {
            return false;
        }
        if (key->planted) {
            if (artifactClass->kind != JsonValue::Kind::String
                || !header.taskSpec.artifactClasses.contains(artifactClass->string)
                || cleanProvenance->kind != JsonValue::Kind::Null) {
                setError(errorMessage, context + QStringLiteral(" planted shape is invalid"));
                return false;
            }
            key->artifactClass = artifactClass->string;
            QString transform;
            if (!exactKeys(*injection, {"bars", "magnitude", "transform"}, context + QStringLiteral(".injection_spec"), errorMessage)
                || !requiredString(*injection, QStringLiteral("transform"), &transform, context, errorMessage)) {
                return false;
            }
            key->injectionTransform = transform;
            const JsonValue *injectionBars = member(*injection, QStringLiteral("bars"));
            if (injectionBars == nullptr || injectionBars->kind != JsonValue::Kind::Array
                || injectionBars->array.size() > kMaximumBars) {
                setError(errorMessage, context + QStringLiteral(".injection_spec.bars must be a bounded array"));
                return false;
            }
            for (const JsonValue &bar : injectionBars->array) {
                if (bar.kind != JsonValue::Kind::Integer || bar.integer < 0
                    || bar.integer >= header.visibleBarCount) {
                    setError(errorMessage, context + QStringLiteral(".injection_spec.bars is out of range"));
                    return false;
                }
                key->injectionBars.append(QString::number(bar.integer));
            }
            const JsonValue *magnitude = member(*injection, QStringLiteral("magnitude"));
            if (magnitude == nullptr || !optionalNumberField(*magnitude, &key->injectionMagnitude)) {
                setError(errorMessage, context + QStringLiteral(".injection_spec.magnitude must be a float or null"));
                return false;
            }
            return true;
        }
        if (artifactClass->kind != JsonValue::Kind::Null || injection->kind != JsonValue::Kind::Null) {
            setError(errorMessage, context + QStringLiteral(" clean shape must null its planted fields"));
            return false;
        }
        // `{screen_id, limit, checks}` — see `MarketScoringKey::cleanLimit` for
        // why the producer answers with an object and not a list of strings.
        const QString where = context + QStringLiteral(".clean_provenance");
        if (cleanProvenance->kind != JsonValue::Kind::Object
            || !exactKeys(*cleanProvenance, {"checks", "limit", "screen_id"}, where, errorMessage)
            || !requiredString(*cleanProvenance, QStringLiteral("screen_id"), &key->cleanScreenId, where, errorMessage)
            || !requiredString(*cleanProvenance, QStringLiteral("limit"), &key->cleanLimit, where, errorMessage)) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                setError(errorMessage, where + QStringLiteral(" must name the screen that asserted cleanliness"));
            }
            return false;
        }
        const JsonValue *checks = member(*cleanProvenance, QStringLiteral("checks"));
        if (checks == nullptr || checks->kind != JsonValue::Kind::Object) {
            setError(errorMessage, where + QStringLiteral(".checks must be an object of the screen's terms"));
            return false;
        }
        const QByteArray canonicalChecks = canonicalJson(*checks);
        if (canonicalChecks.size() > kMaximumMarketLineBytes) {
            setError(errorMessage, where + QStringLiteral(".checks is not bounded"));
            return false;
        }
        key->cleanChecksJson = QString::fromUtf8(canonicalChecks);
        return true;
    }

    if (!exactKeys(
            value,
            {"line", "line_outcome", "perfect_outcome", "rule_declaration_digest", "rule_id", "task_kind"},
            context,
            errorMessage)
        || !requiredString(value, QStringLiteral("rule_id"), &key->ruleId, context, errorMessage)
        || !requiredString(value, QStringLiteral("rule_declaration_digest"), &key->ruleDeclarationDigest, context, errorMessage)) {
        return false;
    }
    if (!isSha256Hex(key->ruleDeclarationDigest)) {
        setError(errorMessage, context + QStringLiteral(".rule_declaration_digest must be 64 lowercase hex"));
        return false;
    }
    const JsonValue *line = member(value, QStringLiteral("line"));
    if (line == nullptr || line->kind != JsonValue::Kind::Array || line->array.isEmpty()
        || line->array.size() > kMaximumPlies) {
        setError(errorMessage, context + QStringLiteral(".line must be a bounded non-empty array"));
        return false;
    }
    for (int index = 0; index < line->array.size(); ++index) {
        const JsonValue &entry = line->array.at(index);
        const QString plyContext = QStringLiteral("%1.line[%2]").arg(context).arg(index);
        const bool withOffset = member(entry, QStringLiteral("bar_offset")) != nullptr;
        const bool keysOk = withOffset
            ? exactKeys(entry, {"bar_offset", "key", "ply_index", "ply_kind"}, plyContext, errorMessage)
            : exactKeys(entry, {"key", "ply_index", "ply_kind"}, plyContext, errorMessage);
        qint64 plyIndex = 0;
        QString plyKindTextValue;
        if (!keysOk
            || !requiredInteger(entry, QStringLiteral("ply_index"), 0, kMaximumPlies, &plyIndex, plyContext, errorMessage)
            || !requiredString(entry, QStringLiteral("ply_kind"), &plyKindTextValue, plyContext, errorMessage)) {
            return false;
        }
        const auto plyKind = plyKindFromText(plyKindTextValue);
        if (!plyKind.has_value()) {
            setError(errorMessage, plyContext + QStringLiteral(" declares an unknown ply_kind"));
            return false;
        }
        TradeLineKeyPly ply;
        ply.plyIndex = static_cast<int>(plyIndex);
        ply.kind = *plyKind;
        if (withOffset) {
            qint64 offset = 0;
            if (!requiredInteger(entry, QStringLiteral("bar_offset"), 1, kMaximumPlies, &offset, plyContext, errorMessage)) {
                return false;
            }
            ply.barOffset = static_cast<int>(offset);
        }
        const JsonValue *keyValue = member(entry, QStringLiteral("key"));
        if (keyValue == nullptr) {
            return false;
        }
        if (ply.kind == PlyKind::Bracket) {
            BracketChoice bracket;
            if (!exactKeys(*keyValue, {"stop_atr", "target_atr"}, plyContext + QStringLiteral(".key"), errorMessage)
                || !requiredFloat(*keyValue, QStringLiteral("stop_atr"), &bracket.stopAtr, plyContext, errorMessage)
                || !requiredFloat(*keyValue, QStringLiteral("target_atr"), &bracket.targetAtr, plyContext, errorMessage)) {
                return false;
            }
            if (!header.taskSpec.stopAtrMultiples.contains(bracket.stopAtr)
                || !header.taskSpec.targetAtrMultiples.contains(bracket.targetAtr)) {
                setError(errorMessage, plyContext + QStringLiteral(".key is off the declared ATR grids"));
                return false;
            }
            ply.bracketKey = bracket;
        } else {
            if (keyValue->kind != JsonValue::Kind::String) {
                setError(errorMessage, plyContext + QStringLiteral(".key must be text"));
                return false;
            }
            ply.categoricalKey = keyValue->string;
            const bool known = (ply.kind == PlyKind::Entry && header.taskSpec.entries.contains(ply.categoricalKey))
                || (ply.kind == PlyKind::SizeBand && header.taskSpec.sizeBands.contains(ply.categoricalKey))
                || (ply.kind == PlyKind::FollowUp && header.taskSpec.followUpActions.contains(ply.categoricalKey));
            if (!known) {
                setError(errorMessage, plyContext + QStringLiteral(".key is outside its declared enumeration"));
                return false;
            }
        }
        key->line.append(ply);
    }

    const JsonValue *lineOutcome = member(value, QStringLiteral("line_outcome"));
    if (lineOutcome == nullptr) {
        return false;
    }
    if (lineOutcome->kind != JsonValue::Kind::Null) {
        TradeLineOutcome outcome;
        qint64 exitBarOffset = 0;
        if (!exactKeys(*lineOutcome, {"exit_bar_offset", "exit_reason", "r_multiple"}, context + QStringLiteral(".line_outcome"), errorMessage)
            || !requiredFloat(*lineOutcome, QStringLiteral("r_multiple"), &outcome.rMultiple, context, errorMessage)
            || !requiredString(*lineOutcome, QStringLiteral("exit_reason"), &outcome.exitReason, context, errorMessage)
            || !requiredInteger(*lineOutcome, QStringLiteral("exit_bar_offset"), 0, kMaximumBars, &exitBarOffset, context, errorMessage)) {
            return false;
        }
        outcome.exitBarOffset = static_cast<int>(exitBarOffset);
        key->lineOutcome = outcome;
    }

    const JsonValue *perfect = member(value, QStringLiteral("perfect_outcome"));
    if (perfect == nullptr) {
        return false;
    }
    if (perfect->kind != JsonValue::Kind::Null) {
        double perfectR = 0.0;
        if (!exactKeys(*perfect, {"note", "r_multiple"}, context + QStringLiteral(".perfect_outcome"), errorMessage)
            || !requiredFloat(*perfect, QStringLiteral("r_multiple"), &perfectR, context, errorMessage)
            || !requiredString(*perfect, QStringLiteral("note"), &key->perfectNote, context, errorMessage)) {
            return false;
        }
        key->perfectRMultiple = perfectR;
    }
    return true;
}

bool parseSealedRecord(
    const QByteArray &line,
    int lineNumber,
    const MarketPackHeader &header,
    const MarketPuzzleVisible &visible,
    MarketContinuation *continuation,
    QString *errorMessage)
{
    const QString context = QStringLiteral("market continuation line %1").arg(lineNumber);
    JsonValue root;
    StrictJsonParser parser(line);
    if (!parser.parse(&root, errorMessage)) {
        setError(errorMessage, QStringLiteral("%1: %2").arg(context, errorMessage == nullptr ? QString() : *errorMessage));
        return false;
    }
    if (!exactKeys(root, {"content", "puzzle_id", "record_id", "record_type", "schema"}, context, errorMessage)) {
        return false;
    }
    QString schema;
    QString recordType;
    if (!requiredString(root, QStringLiteral("schema"), &schema, context, errorMessage)
        || !requiredString(root, QStringLiteral("record_type"), &recordType, context, errorMessage)
        || !requiredString(root, QStringLiteral("puzzle_id"), &continuation->puzzleId, context, errorMessage)
        || !requiredString(root, QStringLiteral("record_id"), &continuation->recordId, context, errorMessage)) {
        return false;
    }
    if (schema != kContinuationSchema || recordType != kContinuationRecordType) {
        setError(errorMessage, context + QStringLiteral(" uses an unsupported schema or record_type"));
        return false;
    }
    if (!prefixedSha(continuation->puzzleId, kPuzzleIdPrefix)
        || !prefixedSha(continuation->recordId, kContinuationRecordIdPrefix)) {
        setError(errorMessage, context + QStringLiteral(" identities have invalid syntax"));
        return false;
    }
    if (continuation->puzzleId != visible.puzzleId) {
        setError(
            errorMessage,
            QStringLiteral("%1 pairs with puzzle %2 but the visible file has %3 at this position")
                .arg(context, continuation->puzzleId, visible.puzzleId));
        return false;
    }

    const JsonValue *content = member(root, QStringLiteral("content"));
    if (content == nullptr
        || !exactKeys(
            *content,
            {"calibration_key", "continuation", "difficulty_note", "outcome_theme", "reveal_identity",
             "scoring_key", "source_identity"},
            context + QStringLiteral(".content"),
            errorMessage)) {
        return false;
    }

    const JsonValue *identity = member(*content, QStringLiteral("reveal_identity"));
    if (identity == nullptr
        || !exactKeys(*identity, {"decision_time_utc", "exchange", "instrument_class", "ticker"}, context + QStringLiteral(".content.reveal_identity"), errorMessage)
        || !requiredString(*identity, QStringLiteral("ticker"), &continuation->identity.ticker, context, errorMessage)
        || !requiredString(*identity, QStringLiteral("decision_time_utc"), &continuation->identity.decisionTimeUtc, context, errorMessage)
        || !requiredString(*identity, QStringLiteral("instrument_class"), &continuation->identity.instrumentClass, context, errorMessage)) {
        return false;
    }
    // `exchange` is text OR null, and null is the honest answer rather than a
    // defect: the bar corpus records no listing venue, so `dojo/corpus.py`
    // writes null instead of inventing `XNAS` for every row. Demanding text
    // here would force the producer to launder a guess into reveal material,
    // which this contract forbids everywhere else. The KEY must still be
    // present — §5.2's rule that a record carries the same key set with nulls,
    // so record length never sorts one record from another — and the
    // `exactKeys` check above is what enforces that. A null arrives as an empty
    // string, which no reveal surface may spell as a real venue.
    const JsonValue *exchange = member(*identity, QStringLiteral("exchange"));
    if (exchange == nullptr) {
        setError(
            errorMessage,
            context + QStringLiteral(".content.reveal_identity.exchange is missing"));
        return false;
    }
    if (exchange->kind == JsonValue::Kind::Null) {
        continuation->identity.exchange.clear();
    } else if (!requiredString(
                   *identity,
                   QStringLiteral("exchange"),
                   &continuation->identity.exchange,
                   context,
                   errorMessage)) {
        return false;
    }
    const JsonValue *decisionTime = member(*identity, QStringLiteral("decision_time_utc"));
    if (decisionTime == nullptr
        || !validateUtc(*decisionTime, context + QStringLiteral(".content.reveal_identity.decision_time_utc"), false, errorMessage)) {
        return false;
    }

    const JsonValue *tail = member(*content, QStringLiteral("continuation"));
    qint64 continuationBarCount = 0;
    if (tail == nullptr
        || !exactKeys(*tail, {"bar_count", "bars", "grain", "session_break_after"}, context + QStringLiteral(".content.continuation"), errorMessage)
        || !requiredString(*tail, QStringLiteral("grain"), &continuation->continuation.grain, context, errorMessage)
        || !requiredInteger(*tail, QStringLiteral("bar_count"), 1, kMaximumBars, &continuationBarCount, context, errorMessage)) {
        return false;
    }
    if (static_cast<int>(continuationBarCount) != header.continuationBarCount
        || continuation->continuation.grain != header.grain) {
        setError(errorMessage, context + QStringLiteral(".content.continuation differs from the pack constants"));
        return false;
    }
    continuation->continuation.barCount = static_cast<int>(continuationBarCount);
    if (!parseBars(*member(*tail, QStringLiteral("bars")), continuation->continuation.barCount, &continuation->continuation.bars, context + QStringLiteral(".content.continuation.bars"), errorMessage)
        || !parseSessionBreaks(
            *member(*tail, QStringLiteral("session_break_after")),
            continuation->continuation.barCount,
            header.sessionBreaksDisclosed,
            &continuation->continuation.sessionBreakAfter,
            context + QStringLiteral(".content.continuation.session_break_after"),
            errorMessage)) {
        return false;
    }

    const JsonValue *scoringKey = member(*content, QStringLiteral("scoring_key"));
    if (scoringKey == nullptr
        || !parseScoringKey(*scoringKey, visible.taskKind, header, &continuation->scoringKey, context + QStringLiteral(".content.scoring_key"), errorMessage)) {
        return false;
    }

    const JsonValue *calibrationKey = member(*content, QStringLiteral("calibration_key"));
    if (calibrationKey == nullptr
        || !exactKeys(*calibrationKey, {"derivation", "question_id", "realized_value"}, context + QStringLiteral(".content.calibration_key"), errorMessage)
        || !requiredString(*calibrationKey, QStringLiteral("question_id"), &continuation->calibrationKey.questionId, context, errorMessage)
        || !requiredString(*calibrationKey, QStringLiteral("derivation"), &continuation->calibrationKey.derivation, context, errorMessage)
        || !requiredFloat(*calibrationKey, QStringLiteral("realized_value"), &continuation->calibrationKey.realizedValue, context, errorMessage)) {
        return false;
    }
    if (continuation->calibrationKey.questionId != visible.calibrationQuestion.questionId) {
        setError(errorMessage, context + QStringLiteral(".content.calibration_key names a different question"));
        return false;
    }

    if (!requiredString(*content, QStringLiteral("outcome_theme"), &continuation->outcomeTheme, context, errorMessage)) {
        return false;
    }
    const JsonValue *difficultyNote = member(*content, QStringLiteral("difficulty_note"));
    if (difficultyNote == nullptr) {
        return false;
    }
    if (difficultyNote->kind != JsonValue::Kind::Null) {
        double noteValue = 0.0;
        if (!exactKeys(*difficultyNote, {"basis_id", "value"}, context + QStringLiteral(".content.difficulty_note"), errorMessage)
            || !requiredString(*difficultyNote, QStringLiteral("basis_id"), &continuation->difficultyNoteBasis, context, errorMessage)
            || !requiredFloat(*difficultyNote, QStringLiteral("value"), &noteValue, context, errorMessage)) {
            return false;
        }
        continuation->difficultyNoteValue = noteValue;
    }

    const JsonValue *sourceIdentity = member(*content, QStringLiteral("source_identity"));
    if (sourceIdentity == nullptr
        || !exactKeys(*sourceIdentity, {"bars_root_id", "partition_paths", "source_continuation_digest", "source_window_digest"}, context + QStringLiteral(".content.source_identity"), errorMessage)
        || !requiredString(*sourceIdentity, QStringLiteral("source_window_digest"), &continuation->sourceIdentity.sourceWindowDigest, context, errorMessage)
        || !requiredString(*sourceIdentity, QStringLiteral("source_continuation_digest"), &continuation->sourceIdentity.sourceContinuationDigest, context, errorMessage)
        || !requiredString(*sourceIdentity, QStringLiteral("bars_root_id"), &continuation->sourceIdentity.barsRootId, context, errorMessage)
        || !stringArray(*member(*sourceIdentity, QStringLiteral("partition_paths")), kMaximumBars, &continuation->sourceIdentity.partitionPaths, context + QStringLiteral(".content.source_identity.partition_paths"), errorMessage)) {
        return false;
    }
    if (!isSha256Hex(continuation->sourceIdentity.sourceWindowDigest)
        || !isSha256Hex(continuation->sourceIdentity.sourceContinuationDigest)) {
        setError(errorMessage, context + QStringLiteral(".content.source_identity digests must be 64 lowercase hex"));
        return false;
    }

    continuation->continuationCommitment = semanticId(kCommitmentPrefix, *content);
    if (continuation->continuationCommitment != visible.continuationCommitment) {
        setError(
            errorMessage,
            context + QStringLiteral(" recomputed commitment does not equal the visible record's "
                                     "continuation_commitment"));
        return false;
    }

    JsonValue recordContent = root;
    removeMember(&recordContent, QStringLiteral("record_id"));
    removeMember(&recordContent, QStringLiteral("record_type"));
    removeMember(&recordContent, QStringLiteral("schema"));
    if (semanticId(kContinuationRecordIdPrefix, recordContent) != continuation->recordId) {
        setError(errorMessage, context + QStringLiteral(".record_id does not match its canonical content"));
        return false;
    }
    if (canonicalJson(root) != line) {
        setError(errorMessage, context + QStringLiteral(" is not canonical JSONL emitted by the v1 contract"));
        return false;
    }
    return true;
}

bool splitFramedLines(
    const QByteArray &raw,
    const QString &label,
    QVector<QByteArray> *lines,
    QString *errorMessage)
{
    if (raw.size() > kMaximumMarketPackBytes) {
        setError(errorMessage, QStringLiteral("%1 exceeds 64 MiB").arg(label));
        return false;
    }
    if (raw.startsWith("\xEF\xBB\xBF") || raw.contains('\0')) {
        setError(errorMessage, QStringLiteral("%1 must be UTF-8 without BOM or NUL").arg(label));
        return false;
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder.decode(raw);
    if (decoder.hasError()) {
        setError(errorMessage, QStringLiteral("%1 is not strict UTF-8").arg(label));
        return false;
    }
    qsizetype offset = 0;
    int lineNumber = 0;
    while (offset < raw.size()) {
        ++lineNumber;
        if (lineNumber > kMaximumMarketRecords + 1) {
            setError(errorMessage, QStringLiteral("%1 exceeds 100000 JSONL rows").arg(label));
            return false;
        }
        qsizetype newline = raw.indexOf('\n', offset);
        if (newline < 0) {
            newline = raw.size();
        }
        const qsizetype rawLength = newline - offset;
        const qsizetype physicalLength = rawLength + (newline < raw.size() ? 1 : 0);
        if (physicalLength > kMaximumMarketLineBytes) {
            setError(errorMessage, QStringLiteral("%1 line %2 exceeds 1 MiB").arg(label).arg(lineNumber));
            return false;
        }
        QByteArray line = raw.mid(offset, rawLength);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        offset = newline < raw.size() ? newline + 1 : newline;
        if (line.trimmed().isEmpty()) {
            continue;
        }
        lines->append(line);
    }
    return true;
}

} // namespace

MarketPuzzlePack::MarketPuzzlePack()
    : m_vault(std::make_unique<SealedContinuationVault>())
{
}

MarketPuzzlePack::~MarketPuzzlePack() = default;
MarketPuzzlePack::MarketPuzzlePack(MarketPuzzlePack &&) noexcept = default;
MarketPuzzlePack &MarketPuzzlePack::operator=(MarketPuzzlePack &&) noexcept = default;

std::optional<MarketPuzzlePack> MarketPuzzlePack::fromJsonLines(
    const QByteArray &visibleJsonLines,
    const QByteArray &sealedJsonLines,
    QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QVector<QByteArray> visibleLines;
    QVector<QByteArray> sealedLines;
    if (!splitFramedLines(visibleJsonLines, QStringLiteral("market puzzle pack"), &visibleLines, errorMessage)
        || !splitFramedLines(sealedJsonLines, QStringLiteral("market continuation pack"), &sealedLines, errorMessage)) {
        return std::nullopt;
    }
    if (visibleLines.isEmpty() || sealedLines.isEmpty()) {
        setError(errorMessage, QStringLiteral("both market pack files must carry a header on line 1"));
        return std::nullopt;
    }

    MarketPuzzlePack pack;

    JsonValue visibleHeaderRoot;
    {
        StrictJsonParser parser(visibleLines.first());
        if (!parser.parse(&visibleHeaderRoot, errorMessage)) {
            return std::nullopt;
        }
    }
    if (!parsePackHeader(visibleHeaderRoot, &pack.m_header, errorMessage)) {
        return std::nullopt;
    }
    if (canonicalJson(visibleHeaderRoot) != visibleLines.first()) {
        setError(errorMessage, QStringLiteral("market pack header is not canonical JSONL"));
        return std::nullopt;
    }

    JsonValue sealedHeaderRoot;
    {
        StrictJsonParser parser(sealedLines.first());
        if (!parser.parse(&sealedHeaderRoot, errorMessage)) {
            return std::nullopt;
        }
    }
    int sealedRecordCount = 0;
    if (!parseSealedHeader(sealedHeaderRoot, pack.m_header, &sealedRecordCount, errorMessage)) {
        return std::nullopt;
    }
    if (canonicalJson(sealedHeaderRoot) != sealedLines.first()) {
        setError(errorMessage, QStringLiteral("market continuation pack header is not canonical JSONL"));
        return std::nullopt;
    }

    const int visibleRecordCount = static_cast<int>(visibleLines.size()) - 1;
    const int sealedBodyCount = static_cast<int>(sealedLines.size()) - 1;
    if (visibleRecordCount != pack.m_header.recordCount) {
        setError(
            errorMessage,
            QStringLiteral("market pack header declares %1 records but the file carries %2")
                .arg(pack.m_header.recordCount)
                .arg(visibleRecordCount));
        return std::nullopt;
    }
    if (sealedBodyCount != visibleRecordCount) {
        setError(
            errorMessage,
            QStringLiteral("the sealed file carries %1 continuations for %2 visible records; both files "
                           "import atomically or not at all")
                .arg(sealedBodyCount)
                .arg(visibleRecordCount));
        return std::nullopt;
    }

    QSet<QString> seenPuzzleIds;
    QSet<QString> seenRecordIds;
    QString previousPuzzleId;
    int anomalyCount = 0;
    int patternCount = 0;
    int tradeCount = 0;

    for (int index = 0; index < visibleRecordCount; ++index) {
        MarketPuzzleVisible puzzle;
        if (!parseVisibleRecord(visibleLines.at(index + 1), index + 2, pack.m_header, &puzzle, errorMessage)) {
            return std::nullopt;
        }
        if (seenPuzzleIds.contains(puzzle.puzzleId) || seenRecordIds.contains(puzzle.recordId)) {
            setError(
                errorMessage,
                QStringLiteral("market pack repeats puzzle_id or record_id %1").arg(puzzle.puzzleId));
            return std::nullopt;
        }
        if (!previousPuzzleId.isEmpty() && !pythonStringLess(previousPuzzleId, puzzle.puzzleId)) {
            setError(
                errorMessage,
                QStringLiteral("market pack records are not sorted by puzzle_id ascending; any other "
                               "order is a channel"));
            return std::nullopt;
        }
        previousPuzzleId = puzzle.puzzleId;
        seenPuzzleIds.insert(puzzle.puzzleId);
        seenRecordIds.insert(puzzle.recordId);
        switch (puzzle.taskKind) {
        case TaskKind::AnomalyFlag:
            ++anomalyCount;
            break;
        case TaskKind::PatternCall:
            ++patternCount;
            break;
        case TaskKind::TradeLine:
            ++tradeCount;
            break;
        }

        MarketContinuation continuation;
        if (!parseSealedRecord(sealedLines.at(index + 1), index + 2, pack.m_header, puzzle, &continuation, errorMessage)) {
            return std::nullopt;
        }
        pack.m_vault->m_impl->continuations.insert(puzzle.puzzleId, continuation);
        pack.m_puzzles.append(puzzle);
    }

    if (anomalyCount != pack.m_header.anomalyFlagCount
        || patternCount != pack.m_header.patternCallCount
        || tradeCount != pack.m_header.tradeLineCount) {
        setError(
            errorMessage,
            QStringLiteral("market pack header counts.by_task_kind disagrees with what was parsed"));
        return std::nullopt;
    }
    pack.m_vault->m_impl->continuationPackId = pack.m_header.continuationPackId;
    return pack;
}

bool verifyRetainedMarketPuzzleLine(
    const QByteArray &canonicalLine,
    const QString &expectedRecordId,
    const QString &expectedPuzzleId,
    QString *errorMessage)
{
    const QString context = QStringLiteral("retained market puzzle record");
    if (canonicalLine.size() < 2 || canonicalLine.size() > kMaximumMarketLineBytes
        || canonicalLine.contains('\n') || canonicalLine.contains('\0')) {
        setError(errorMessage, context + QStringLiteral(" is not one bounded JSONL line"));
        return false;
    }
    JsonValue root;
    StrictJsonParser parser(canonicalLine);
    if (!parser.parse(&root, errorMessage)) {
        return false;
    }
    if (!exactKeys(
            root,
            {"calibration_question", "continuation_commitment", "display_symbol", "hud", "puzzle_id",
             "rating_seed", "rating_seed_basis", "record_id", "record_type", "response_spec",
             "schema", "source", "task_kind", "theme", "window"},
            context,
            errorMessage)) {
        return false;
    }
    QString schema;
    QString recordType;
    QString puzzleId;
    QString recordId;
    if (!requiredString(root, QStringLiteral("schema"), &schema, context, errorMessage)
        || !requiredString(root, QStringLiteral("record_type"), &recordType, context, errorMessage)
        || !requiredString(root, QStringLiteral("puzzle_id"), &puzzleId, context, errorMessage)
        || !requiredString(root, QStringLiteral("record_id"), &recordId, context, errorMessage)) {
        return false;
    }
    if (schema != kPuzzleSchema || recordType != kPuzzleRecordType) {
        setError(errorMessage, context + QStringLiteral(" uses an unsupported schema or record_type"));
        return false;
    }
    if (!prefixedSha(puzzleId, kPuzzleIdPrefix) || !prefixedSha(recordId, kPuzzleRecordIdPrefix)
        || puzzleId != expectedPuzzleId || recordId != expectedRecordId) {
        setError(errorMessage, context + QStringLiteral(" identities do not match the journal row"));
        return false;
    }
    JsonValue recordContent = root;
    removeMember(&recordContent, QStringLiteral("record_id"));
    removeMember(&recordContent, QStringLiteral("record_type"));
    removeMember(&recordContent, QStringLiteral("schema"));
    if (semanticId(kPuzzleRecordIdPrefix, recordContent) != recordId) {
        setError(errorMessage, context + QStringLiteral(" record_id does not match its canonical content"));
        return false;
    }
    if (canonicalJson(root) != canonicalLine) {
        setError(errorMessage, context + QStringLiteral(" is not canonical JSONL"));
        return false;
    }
    return true;
}

} // namespace parlawl::market
