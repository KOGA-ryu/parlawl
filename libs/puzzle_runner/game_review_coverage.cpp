#include "game_review_coverage.h"

#include <cmath>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

namespace parlawl::puzzle_runner {

namespace {

constexpr qint64 kMaximumIndexBytes = 2 * 1024 * 1024;
constexpr int kMaximumEntries = 2'000;

const QRegularExpression kSemanticIdPattern(
    QStringLiteral("^[a-z0-9][a-z0-9_.-]*-v[0-9]+:[0-9a-f]{64}$"));
const QRegularExpression kSha256Pattern(QStringLiteral("^[0-9a-f]{64}$"));
const QRegularExpression kDisplayFilePattern(
    QStringLiteral("^game-review-display-v1-([0-9a-f]{64})\\.json$"));
const QRegularExpression kExplanationFilePattern(
    QStringLiteral("^game-review-mechanical-explanation-v1-([0-9a-f]{64})\\.json$"));

void setError(QString *target, const QString &message)
{
    if (target != nullptr) *target = message;
}

bool exactKeys(
    const QJsonObject &object,
    std::initializer_list<const char *> keys,
    const QString &label,
    QString *errorMessage)
{
    QSet<QString> expected;
    for (const char *key : keys) expected.insert(QString::fromLatin1(key));
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

bool textField(
    const QJsonObject &object,
    const QString &key,
    QString *output,
    const QString &label,
    QString *errorMessage,
    int maximumBytes)
{
    if (!object.value(key).isString()) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" must be text"));
        return false;
    }
    const QString text = object.value(key).toString();
    if (text.isEmpty() || text != text.trimmed() || text.contains(QChar::Null)
        || text.toUtf8().size() > maximumBytes) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" is outside its text bound"));
        return false;
    }
    *output = text;
    return true;
}

bool optionalTextField(
    const QJsonObject &object,
    const QString &key,
    std::optional<QString> *output,
    const QString &label,
    QString *errorMessage,
    int maximumBytes)
{
    if (object.value(key).isNull()) {
        output->reset();
        return true;
    }
    QString value;
    if (!textField(object, key, &value, label, errorMessage, maximumBytes)) return false;
    *output = value;
    return true;
}

bool integerField(
    const QJsonObject &object,
    const QString &key,
    int minimum,
    int maximum,
    int *output,
    const QString &label,
    QString *errorMessage)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble())
        || std::floor(value.toDouble()) != value.toDouble()
        || value.toDouble() < minimum || value.toDouble() > maximum) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" is outside its integer bound"));
        return false;
    }
    *output = static_cast<int>(value.toDouble());
    return true;
}

bool optionalIntegerField(
    const QJsonObject &object,
    const QString &key,
    std::optional<int> *output,
    const QString &label,
    QString *errorMessage)
{
    if (object.value(key).isNull()) {
        output->reset();
        return true;
    }
    int value = 0;
    if (!integerField(object, key, 1, 128, &value, label, errorMessage)) return false;
    *output = value;
    return true;
}

bool booleanField(
    const QJsonObject &object,
    const QString &key,
    bool *output,
    const QString &label,
    QString *errorMessage)
{
    if (!object.value(key).isBool()) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" must be boolean"));
        return false;
    }
    *output = object.value(key).toBool();
    return true;
}

bool expectedBoolean(
    const QJsonObject &object,
    const QString &key,
    bool expected,
    QString *errorMessage)
{
    bool actual = false;
    return booleanField(object, key, &actual, QStringLiteral("claim boundary"), errorMessage)
        && (actual == expected
            ? true
            : (setError(errorMessage, QStringLiteral("claim boundary is unsupported")), false));
}

bool parseEntry(
    const QJsonObject &object,
    int expectedOrdinal,
    GameReviewCoverageEntry *entry,
    QString *errorMessage)
{
    if (!exactKeys(object,
            {"canonical_game_url", "display_filename", "display_review_available",
             "mechanical_explanation_available", "mechanical_explanation_filename",
             "report_available", "review_status", "screening_status",
             "selected_moment_count", "source_game_id", "source_ordinal",
             "source_report_id", "status_detail", "status_label"},
            QStringLiteral("coverage entry"), errorMessage)
        || !textField(object, QStringLiteral("canonical_game_url"),
            &entry->canonicalGameUrl, QStringLiteral("coverage entry"), errorMessage, 2048)
        || !optionalTextField(object, QStringLiteral("display_filename"),
            &entry->displayFileName, QStringLiteral("coverage entry"), errorMessage, 256)
        || !booleanField(object, QStringLiteral("display_review_available"),
            &entry->displayReviewAvailable, QStringLiteral("coverage entry"), errorMessage)
        || !booleanField(object, QStringLiteral("mechanical_explanation_available"),
            &entry->mechanicalExplanationAvailable, QStringLiteral("coverage entry"), errorMessage)
        || !optionalTextField(object, QStringLiteral("mechanical_explanation_filename"),
            &entry->mechanicalExplanationFileName, QStringLiteral("coverage entry"), errorMessage, 256)
        || !booleanField(object, QStringLiteral("report_available"),
            &entry->reportAvailable, QStringLiteral("coverage entry"), errorMessage)
        || !textField(object, QStringLiteral("review_status"), &entry->reviewStatus,
            QStringLiteral("coverage entry"), errorMessage, 64)
        || !textField(object, QStringLiteral("screening_status"), &entry->screeningStatus,
            QStringLiteral("coverage entry"), errorMessage, 64)
        || !optionalIntegerField(object, QStringLiteral("selected_moment_count"),
            &entry->selectedMomentCount, QStringLiteral("coverage entry"), errorMessage)
        || !textField(object, QStringLiteral("source_game_id"), &entry->sourceGameId,
            QStringLiteral("coverage entry"), errorMessage, 256)
        || !integerField(object, QStringLiteral("source_ordinal"), 1, kMaximumEntries,
            &entry->sourceOrdinal, QStringLiteral("coverage entry"), errorMessage)
        || entry->sourceOrdinal != expectedOrdinal
        || !optionalTextField(object, QStringLiteral("source_report_id"),
            &entry->sourceReportId, QStringLiteral("coverage entry"), errorMessage, 256)
        || !textField(object, QStringLiteral("status_detail"), &entry->statusDetail,
            QStringLiteral("coverage entry"), errorMessage, 2048)
        || !textField(object, QStringLiteral("status_label"), &entry->statusLabel,
            QStringLiteral("coverage entry"), errorMessage, 256)
        || !entry->canonicalGameUrl.startsWith(
            QStringLiteral("https://www.chess.com/game/"))
        || !kSemanticIdPattern.match(entry->sourceGameId).hasMatch()
        || (entry->sourceReportId.has_value()
            && !kSemanticIdPattern.match(*entry->sourceReportId).hasMatch())) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("coverage entry identity or ordinal is unsupported");
        }
        return false;
    }

    const QSet<QString> statuses {
        QStringLiteral("review_available"), QStringLiteral("analysis_incomplete"),
        QStringLiteral("no_selected_report_available")};
    if (!statuses.contains(entry->reviewStatus)) {
        setError(errorMessage, QStringLiteral("coverage entry.review_status is unsupported"));
        return false;
    }
    if (entry->displayReviewAvailable != entry->displayFileName.has_value()
        || entry->mechanicalExplanationAvailable
            != entry->mechanicalExplanationFileName.has_value()
        || entry->reportAvailable != entry->sourceReportId.has_value()
        || entry->reportAvailable != entry->selectedMomentCount.has_value()) {
        setError(errorMessage, QStringLiteral("coverage entry availability and filename differ"));
        return false;
    }
    QRegularExpressionMatch displayMatch;
    QRegularExpressionMatch explanationMatch;
    if (entry->displayFileName.has_value()) {
        displayMatch = kDisplayFilePattern.match(*entry->displayFileName);
        if (!displayMatch.hasMatch()) {
            setError(errorMessage, QStringLiteral("coverage entry display filename is unsupported"));
            return false;
        }
    }
    if (entry->mechanicalExplanationFileName.has_value()) {
        explanationMatch = kExplanationFilePattern.match(*entry->mechanicalExplanationFileName);
        if (!explanationMatch.hasMatch()) {
            setError(errorMessage, QStringLiteral("coverage entry explanation filename is unsupported"));
            return false;
        }
    }
    if (entry->sourceReportId.has_value()) {
        const QString digest = entry->sourceReportId->section(QLatin1Char(':'), -1);
        if ((entry->displayFileName.has_value() && displayMatch.captured(1) != digest)
            || (entry->mechanicalExplanationFileName.has_value()
                && explanationMatch.captured(1) != digest)) {
            setError(errorMessage, QStringLiteral("coverage entry locator differs from source_report_id"));
            return false;
        }
    }
    if ((entry->displayReviewAvailable || entry->mechanicalExplanationAvailable)
        && !entry->reportAvailable) {
        setError(errorMessage, QStringLiteral("coverage delivery has no source report"));
        return false;
    }
    const QString expectedStatus = entry->displayReviewAvailable
        ? QStringLiteral("review_available")
        : entry->reportAvailable
            ? QStringLiteral("analysis_incomplete")
            : QStringLiteral("no_selected_report_available");
    if (entry->reviewStatus != expectedStatus) {
        setError(errorMessage, QStringLiteral("coverage status disagrees with available files"));
        return false;
    }
    if (entry->reportAvailable) {
        if (entry->screeningStatus != QStringLiteral("selected_report_available")) {
            setError(errorMessage, QStringLiteral("selected report coverage status is inconsistent"));
            return false;
        }
    } else if (entry->screeningStatus
               != QStringLiteral("not_established_by_delivery_index")) {
        setError(errorMessage, QStringLiteral("absent report overstates screening evidence"));
        return false;
    }
    QString expectedLabel;
    QString expectedDetail;
    if (entry->reviewStatus == QStringLiteral("review_available")) {
        expectedLabel = QStringLiteral("Review available");
        expectedDetail = QStringLiteral("The exact display review is available.");
    } else if (entry->reviewStatus == QStringLiteral("analysis_incomplete")) {
        expectedLabel = QStringLiteral("Analysis incomplete");
        expectedDetail = QStringLiteral(
            "A selected source report exists, but its display review is missing.");
    } else {
        expectedLabel = QStringLiteral("No deep review available");
        expectedDetail = QStringLiteral(
            "No selected Report-v2 file was delivered for this game; this does not mean the game was mistake-free.");
    }
    if (entry->statusLabel != expectedLabel || entry->statusDetail != expectedDetail) {
        setError(errorMessage, QStringLiteral("coverage status wording differs from the frozen contract"));
        return false;
    }
    return true;
}

} // namespace

std::optional<GameReviewCoverageIndex> GameReviewCoverageIndex::fromFile(
    const QString &absoluteFilePath,
    QString *errorMessage)
{
    if (errorMessage != nullptr) errorMessage->clear();
    const QFileInfo fileInfo(absoluteFilePath);
    if (absoluteFilePath.isEmpty() || !fileInfo.isAbsolute() || fileInfo.isSymLink()
        || !fileInfo.isFile()
        || fileInfo.canonicalFilePath() != fileInfo.absoluteFilePath()
        || fileInfo.size() < 1 || fileInfo.size() > kMaximumIndexBytes) {
        setError(errorMessage, QStringLiteral("coverage index must be one direct canonical file"));
        return std::nullopt;
    }
    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("coverage index could not be read"));
        return std::nullopt;
    }
    const QByteArray raw = file.read(kMaximumIndexBytes + 1);
    if (raw.size() != fileInfo.size()) {
        setError(errorMessage, QStringLiteral("coverage index changed while it was read"));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("coverage index must contain one JSON object"));
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    GameReviewCoverageIndex index;
    index.filePath = fileInfo.absoluteFilePath();
    if (!exactKeys(root,
            {"claim_boundary", "contract_version", "counts", "coverage_id", "entries",
             "source_pgn_sha256"},
            QStringLiteral("coverage index"), errorMessage)
        || !textField(root, QStringLiteral("coverage_id"), &index.coverageId,
            QStringLiteral("coverage index"), errorMessage, 256)
        || !textField(root, QStringLiteral("contract_version"), &index.contractVersion,
            QStringLiteral("coverage index"), errorMessage, 64)
        || index.contractVersion != QStringLiteral("chess-game-review-coverage-index-v1")
        || !textField(root, QStringLiteral("source_pgn_sha256"), &index.sourcePgnSha256,
            QStringLiteral("coverage index"), errorMessage, 64)
        || !kSemanticIdPattern.match(index.coverageId).hasMatch()
        || !index.coverageId.startsWith(QStringLiteral("chess-game-review-coverage-index-v1:"))
        || !kSha256Pattern.match(index.sourcePgnSha256).hasMatch()) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("coverage index contract or identity is unsupported");
        }
        return std::nullopt;
    }
    if (!root.value(QStringLiteral("claim_boundary")).isObject()) {
        setError(errorMessage, QStringLiteral("coverage index claim_boundary must be an object"));
        return std::nullopt;
    }
    index.claimBoundary = root.value(QStringLiteral("claim_boundary")).toObject();
    if (!exactKeys(index.claimBoundary,
            {"absent_report_means_mistake_free", "absent_report_proves_screening_completed",
             "coverage_index_authenticates_source_replay", "delivery_inventory_only",
             "engine_network_or_database_work", "source_unavailable_games_invented"},
            QStringLiteral("claim boundary"), errorMessage)
        || !expectedBoolean(index.claimBoundary,
            QStringLiteral("absent_report_means_mistake_free"), false, errorMessage)
        || !expectedBoolean(index.claimBoundary,
            QStringLiteral("absent_report_proves_screening_completed"), false, errorMessage)
        || !expectedBoolean(index.claimBoundary,
            QStringLiteral("coverage_index_authenticates_source_replay"), false, errorMessage)
        || !expectedBoolean(index.claimBoundary,
            QStringLiteral("delivery_inventory_only"), true, errorMessage)
        || !expectedBoolean(index.claimBoundary,
            QStringLiteral("engine_network_or_database_work"), false, errorMessage)
        || !expectedBoolean(index.claimBoundary,
            QStringLiteral("source_unavailable_games_invented"), false, errorMessage)) {
        return std::nullopt;
    }
    if (!root.value(QStringLiteral("counts")).isObject()) {
        setError(errorMessage, QStringLiteral("coverage index counts must be an object"));
        return std::nullopt;
    }
    const QJsonObject counts = root.value(QStringLiteral("counts")).toObject();
    const QStringList countKeys {
        QStringLiteral("analysis_incomplete"),
        QStringLiteral("mechanical_explanation_available"),
        QStringLiteral("no_selected_report_available"),
        QStringLiteral("report_available"),
        QStringLiteral("review_available"),
        QStringLiteral("selected_moments"),
        QStringLiteral("source_games"),
        QStringLiteral("source_unavailable")};
    if (!exactKeys(counts,
            {"analysis_incomplete", "mechanical_explanation_available",
             "no_selected_report_available", "report_available", "review_available",
             "selected_moments", "source_games", "source_unavailable"},
            QStringLiteral("coverage counts"), errorMessage)) {
        return std::nullopt;
    }
    for (const QString &key : countKeys) {
        int value = 0;
        if (!integerField(counts, key, 0, 100'000, &value,
                QStringLiteral("coverage counts"), errorMessage)) {
            return std::nullopt;
        }
        index.counts.insert(key, value);
    }
    if (!root.value(QStringLiteral("entries")).isArray()) {
        setError(errorMessage, QStringLiteral("coverage index entries must be an array"));
        return std::nullopt;
    }
    const QJsonArray entries = root.value(QStringLiteral("entries")).toArray();
    if (entries.isEmpty() || entries.size() > kMaximumEntries
        || entries.size() != index.counts.value(QStringLiteral("source_games"))) {
        setError(errorMessage, QStringLiteral("coverage entry count differs from counts.source_games"));
        return std::nullopt;
    }
    QSet<QString> sourceGameIds;
    QHash<QString, int> actualCounts;
    for (int arrayIndex = 0; arrayIndex < entries.size(); ++arrayIndex) {
        if (!entries.at(arrayIndex).isObject()) {
            setError(errorMessage, QStringLiteral("coverage entry must be an object"));
            return std::nullopt;
        }
        GameReviewCoverageEntry entry;
        if (!parseEntry(entries.at(arrayIndex).toObject(), arrayIndex + 1,
                &entry, errorMessage)) {
            return std::nullopt;
        }
        if (sourceGameIds.contains(entry.sourceGameId)) {
            setError(errorMessage, QStringLiteral("coverage index repeats one source game"));
            return std::nullopt;
        }
        sourceGameIds.insert(entry.sourceGameId);
        ++actualCounts[entry.reviewStatus];
        if (entry.reportAvailable) ++actualCounts[QStringLiteral("report_available")];
        if (entry.mechanicalExplanationAvailable) {
            ++actualCounts[QStringLiteral("mechanical_explanation_available")];
        }
        actualCounts[QStringLiteral("selected_moments")] +=
            entry.selectedMomentCount.value_or(0);
        index.entries.append(entry);
    }
    const QStringList auditedKeys {
        QStringLiteral("analysis_incomplete"),
        QStringLiteral("mechanical_explanation_available"),
        QStringLiteral("no_selected_report_available"),
        QStringLiteral("report_available"),
        QStringLiteral("review_available"),
        QStringLiteral("selected_moments")};
    for (const QString &key : auditedKeys) {
        if (actualCounts.value(key) != index.counts.value(key)) {
            setError(errorMessage, QStringLiteral("coverage counts do not match entries"));
            return std::nullopt;
        }
    }
    if (index.counts.value(QStringLiteral("source_unavailable")) != 0) {
        setError(errorMessage, QStringLiteral("coverage source_unavailable count is unsupported"));
        return std::nullopt;
    }
    return index;
}

const GameReviewCoverageEntry *GameReviewCoverageIndex::entryForGame(
    const QString &sourceGameId) const
{
    for (const GameReviewCoverageEntry &entry : entries) {
        if (entry.sourceGameId == sourceGameId) return &entry;
    }
    return nullptr;
}

} // namespace parlawl::puzzle_runner
