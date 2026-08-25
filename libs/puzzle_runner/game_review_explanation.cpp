#include "game_review_explanation.h"

#include <cmath>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

namespace parlawl::puzzle_runner {

namespace {

constexpr qint64 kMaximumSidecarBytes = 2 * 1024 * 1024;
constexpr qint64 kMaximumCatalogBytes = 64 * 1024 * 1024;
constexpr int kMaximumSidecars = 200;
constexpr int kMaximumMoments = 128;
constexpr int kMaximumPly = 700;
constexpr int kMaximumFactsPerMoment = 32;
constexpr int kMaximumValues = 32;
constexpr int kMaximumValueArrayItems = 32;

const QRegularExpression kSidecarNamePattern(
    QStringLiteral("^game-review-mechanical-explanation-v1-([0-9a-f]{64})\\.json$"));
const QRegularExpression kSemanticIdPattern(
    QStringLiteral("^[a-z0-9][a-z0-9_.-]*-v[0-9]+:[0-9a-f]{64}$"));
const QRegularExpression kTokenPattern(
    QStringLiteral("^[a-z][a-z0-9_]{0,127}$"));

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

bool textField(
    const QJsonObject &parent,
    const QString &key,
    QString *output,
    const QString &label,
    QString *errorMessage,
    int maximumBytes)
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

bool integerField(
    const QJsonObject &parent,
    const QString &key,
    int minimum,
    int maximum,
    int *output,
    const QString &label,
    QString *errorMessage)
{
    const QJsonValue value = parent.value(key);
    if (!value.isDouble()) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" must be an integer"));
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < minimum || number > maximum) {
        setError(errorMessage, label + QLatin1Char('.') + key + QStringLiteral(" is outside its integer bound"));
        return false;
    }
    *output = static_cast<int>(number);
    return true;
}

bool boolField(
    const QJsonObject &parent,
    const QString &key,
    bool expected,
    QString *errorMessage)
{
    const QJsonValue value = parent.value(key);
    if (!value.isBool() || value.toBool() != expected) {
        setError(errorMessage, QStringLiteral("claim boundary is unsupported"));
        return false;
    }
    return true;
}

bool tokenField(
    const QJsonObject &parent,
    const QString &key,
    QString *output,
    const QString &label,
    QString *errorMessage)
{
    return textField(parent, key, output, label, errorMessage, 128)
        && (kTokenPattern.match(*output).hasMatch()
            ? true
            : (setError(errorMessage,
                   label + QLatin1Char('.') + key + QStringLiteral(" must be a stable token")),
               false));
}

bool boundedValues(
    const QJsonObject &object,
    const QString &label,
    QString *errorMessage)
{
    if (object.size() > kMaximumValues) {
        setError(errorMessage, label + QStringLiteral(" exceeds its value count bound"));
        return false;
    }
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (!kTokenPattern.match(iterator.key()).hasMatch()) {
            setError(errorMessage, label + QStringLiteral(" contains an invalid value key"));
            return false;
        }
        const QJsonValue value = iterator.value();
        if (value.isString()) {
            const QString text = value.toString();
            if (text.isEmpty() || text != text.trimmed() || text.contains(QChar::Null)
                || text.toUtf8().size() > 512) {
                setError(errorMessage, label + QStringLiteral(" contains invalid value text"));
                return false;
            }
            continue;
        }
        if (value.isDouble()) {
            const double number = value.toDouble();
            if (!std::isfinite(number) || std::floor(number) != number
                || std::abs(number) > 1'000'000'000.0) {
                setError(errorMessage, label + QStringLiteral(" contains an invalid integer value"));
                return false;
            }
            continue;
        }
        if (value.isArray()) {
            const QJsonArray array = value.toArray();
            if (array.size() > kMaximumValueArrayItems) {
                setError(errorMessage, label + QStringLiteral(" contains an oversized value array"));
                return false;
            }
            for (const QJsonValue &item : array) {
                if (!item.isString() || item.toString().isEmpty()
                    || item.toString() != item.toString().trimmed()
                    || item.toString().contains(QChar::Null)
                    || item.toString().toUtf8().size() > 128) {
                    setError(errorMessage, label + QStringLiteral(" contains an invalid value array"));
                    return false;
                }
            }
            continue;
        }
        setError(errorMessage, label + QStringLiteral(" contains an unsupported value type"));
        return false;
    }
    return true;
}

bool parseSidecar(
    const QByteArray &raw,
    const QString &fileName,
    GameReviewMechanicalExplanation *explanation,
    QString *errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("companion must contain one JSON object"));
        return false;
    }
    const QJsonObject root = document.object();
    if (!exactKeys(root,
            {"claim_boundary", "contract_version", "moments", "source_game_id",
             "source_report_id"},
            QStringLiteral("companion"), errorMessage)
        || !textField(root, QStringLiteral("contract_version"),
            &explanation->contractVersion, QStringLiteral("companion"), errorMessage, 64)
        || explanation->contractVersion
            != QStringLiteral("chess-game-review-mechanical-explanation-v1")
        || !textField(root, QStringLiteral("source_game_id"),
            &explanation->sourceGameId, QStringLiteral("companion"), errorMessage, 256)
        || !textField(root, QStringLiteral("source_report_id"),
            &explanation->sourceReportId, QStringLiteral("companion"), errorMessage, 256)
        || !kSemanticIdPattern.match(explanation->sourceGameId).hasMatch()
        || !kSemanticIdPattern.match(explanation->sourceReportId).hasMatch()) {
        if (errorMessage != nullptr && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("companion contract or semantic IDs are unsupported");
        }
        return false;
    }
    const QRegularExpressionMatch nameMatch = kSidecarNamePattern.match(fileName);
    if (!nameMatch.hasMatch()
        || !explanation->sourceReportId.endsWith(QLatin1Char(':') + nameMatch.captured(1))) {
        setError(errorMessage, QStringLiteral("companion filename does not match source_report_id"));
        return false;
    }

    if (!root.value(QStringLiteral("claim_boundary")).isObject()) {
        setError(errorMessage, QStringLiteral("companion.claim_boundary must be an object"));
        return false;
    }
    explanation->claimBoundary = root.value(QStringLiteral("claim_boundary")).toObject();
    if (!exactKeys(explanation->claimBoundary,
            {"causal_or_intent_explanation", "engine_evaluation_recomputed",
             "line_is_forced_or_complete", "mechanical_board_facts_only",
             "new_engine_network_database_or_write_work", "objective_chess_truth",
             "published_selective_lines_only", "quiet_engine_preferences_fully_explained",
             "source_report_replay_reperformed"},
            QStringLiteral("claim boundary"), errorMessage)
        || !boolField(explanation->claimBoundary,
            QStringLiteral("causal_or_intent_explanation"), false, errorMessage)
        || !boolField(explanation->claimBoundary,
            QStringLiteral("engine_evaluation_recomputed"), false, errorMessage)
        || !boolField(explanation->claimBoundary,
            QStringLiteral("line_is_forced_or_complete"), false, errorMessage)
        || !boolField(explanation->claimBoundary,
            QStringLiteral("mechanical_board_facts_only"), true, errorMessage)
        || !boolField(explanation->claimBoundary,
            QStringLiteral("new_engine_network_database_or_write_work"), false, errorMessage)
        || !boolField(explanation->claimBoundary,
            QStringLiteral("objective_chess_truth"), false, errorMessage)
        || !boolField(explanation->claimBoundary,
            QStringLiteral("published_selective_lines_only"), true, errorMessage)
        || !boolField(explanation->claimBoundary,
            QStringLiteral("quiet_engine_preferences_fully_explained"), false, errorMessage)
        || !boolField(explanation->claimBoundary,
            QStringLiteral("source_report_replay_reperformed"), false, errorMessage)) {
        return false;
    }

    if (!root.value(QStringLiteral("moments")).isArray()) {
        setError(errorMessage, QStringLiteral("companion.moments must be an array"));
        return false;
    }
    const QJsonArray moments = root.value(QStringLiteral("moments")).toArray();
    if (moments.isEmpty() || moments.size() > kMaximumMoments) {
        setError(errorMessage, QStringLiteral("companion.moments is outside its item bound"));
        return false;
    }
    for (int index = 0; index < moments.size(); ++index) {
        if (!moments.at(index).isObject()) {
            setError(errorMessage, QStringLiteral("mechanical moment must be an object"));
            return false;
        }
        const QJsonObject object = moments.at(index).toObject();
        GameReviewMechanicalMoment moment;
        if (!exactKeys(object,
                {"comparison_status", "facts", "headline", "mechanical_fact_status",
                 "ply", "review_index"},
                QStringLiteral("mechanical moment"), errorMessage)
            || !integerField(object, QStringLiteral("review_index"), 1,
                kMaximumMoments, &moment.reviewIndex,
                QStringLiteral("mechanical moment"), errorMessage)
            || moment.reviewIndex != index + 1
            || !integerField(object, QStringLiteral("ply"), 1, kMaximumPly,
                &moment.ply, QStringLiteral("mechanical moment"), errorMessage)
            || !textField(object, QStringLiteral("headline"), &moment.headline,
                QStringLiteral("mechanical moment"), errorMessage, 1024)
            || !tokenField(object, QStringLiteral("comparison_status"),
                &moment.comparisonStatus, QStringLiteral("mechanical moment"), errorMessage)
            || !tokenField(object, QStringLiteral("mechanical_fact_status"),
                &moment.mechanicalFactStatus, QStringLiteral("mechanical moment"), errorMessage)) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("mechanical moment order is invalid");
            }
            return false;
        }
        const QSet<QString> comparisonStatuses {
            QStringLiteral("ambiguous_engine_stability"),
            QStringLiteral("below_policy_threshold"),
            QStringLiteral("confirmed_under_engine_contract"),
            QStringLiteral("played_move_matches_best"),
        };
        const QSet<QString> mechanicalStatuses {
            QStringLiteral("observed"), QStringLiteral("not_observed")};
        if (!comparisonStatuses.contains(moment.comparisonStatus)
            || !mechanicalStatuses.contains(moment.mechanicalFactStatus)) {
            setError(errorMessage, QStringLiteral("mechanical moment status is unsupported"));
            return false;
        }
        if (!object.value(QStringLiteral("facts")).isArray()) {
            setError(errorMessage, QStringLiteral("mechanical moment.facts must be an array"));
            return false;
        }
        const QJsonArray facts = object.value(QStringLiteral("facts")).toArray();
        if (facts.size() > kMaximumFactsPerMoment) {
            setError(errorMessage, QStringLiteral("mechanical moment.facts exceeds its item bound"));
            return false;
        }
        QSet<QString> factIdentities;
        for (const QJsonValue &value : facts) {
            if (!value.isObject()) {
                setError(errorMessage, QStringLiteral("mechanical fact must be an object"));
                return false;
            }
            const QJsonObject factObject = value.toObject();
            GameReviewMechanicalFact fact;
            if (!exactKeys(factObject, {"code", "scope", "text", "values"},
                    QStringLiteral("mechanical fact"), errorMessage)
                || !tokenField(factObject, QStringLiteral("code"), &fact.code,
                    QStringLiteral("mechanical fact"), errorMessage)
                || !tokenField(factObject, QStringLiteral("scope"), &fact.scope,
                    QStringLiteral("mechanical fact"), errorMessage)
                || !textField(factObject, QStringLiteral("text"), &fact.text,
                    QStringLiteral("mechanical fact"), errorMessage, 2048)
                || !factObject.value(QStringLiteral("values")).isObject()) {
                if (errorMessage != nullptr && errorMessage->isEmpty()) {
                    *errorMessage = QStringLiteral("mechanical fact.values must be an object");
                }
                return false;
            }
            const QSet<QString> scopes {
                QStringLiteral("policy"), QStringLiteral("best_move"),
                QStringLiteral("played_move"), QStringLiteral("comparison"),
                QStringLiteral("context")};
            fact.values = factObject.value(QStringLiteral("values")).toObject();
            const QString identity = fact.scope + QLatin1Char(':') + fact.code;
            if (!scopes.contains(fact.scope)
                || factIdentities.contains(identity)
                || !boundedValues(fact.values, QStringLiteral("mechanical fact.values"), errorMessage)) {
                if (!scopes.contains(fact.scope)) {
                    setError(errorMessage, QStringLiteral("mechanical fact.scope is unsupported"));
                } else if (factIdentities.contains(identity)) {
                    setError(errorMessage, QStringLiteral("mechanical moment repeats a scoped fact code"));
                }
                return false;
            }
            factIdentities.insert(identity);
            moment.facts.append(fact);
        }
        explanation->moments.append(moment);
    }
    return true;
}

} // namespace

const GameReviewMechanicalMoment *GameReviewMechanicalExplanation::moment(
    int reviewIndex,
    int ply) const
{
    if (reviewIndex < 1 || reviewIndex > moments.size()) {
        return nullptr;
    }
    const GameReviewMechanicalMoment &candidate = moments.at(reviewIndex - 1);
    return candidate.reviewIndex == reviewIndex && candidate.ply == ply
        ? &candidate : nullptr;
}

bool GameReviewMechanicalExplanation::matchesDisplay(
    const GameReviewDisplay &display,
    QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (sourceGameId != display.sourceGameId || sourceReportId != display.sourceReportId) {
        setError(errorMessage,
            QStringLiteral("mechanical explanation IDs do not match the Coach Review display"));
        return false;
    }
    if (moments.size() != display.criticalMoments.size()) {
        setError(errorMessage,
            QStringLiteral("mechanical explanation moment count does not match Coach Review"));
        return false;
    }
    for (const GameReviewDisplayMoment &displayMoment : display.criticalMoments) {
        const GameReviewMechanicalMoment *mechanicalMoment =
            moment(displayMoment.reviewIndex, displayMoment.ply);
        if (mechanicalMoment == nullptr) {
            setError(errorMessage,
                QStringLiteral("mechanical explanation review_index/ply join does not match Coach Review"));
            return false;
        }
        QString expectedComparisonStatus;
        if (displayMoment.status == QStringLiteral("ambiguous_engine_instability")) {
            expectedComparisonStatus = QStringLiteral("ambiguous_engine_stability");
        } else if (displayMoment.status == QStringLiteral("below_confirmation_threshold")) {
            expectedComparisonStatus = QStringLiteral("below_policy_threshold");
        } else if (displayMoment.status == QStringLiteral("confirmed_severe_error")
            || displayMoment.status == QStringLiteral("confirmed_missed_opportunity")) {
            expectedComparisonStatus = QStringLiteral("confirmed_under_engine_contract");
        } else if (displayMoment.status == QStringLiteral("played_move_matches_best")) {
            expectedComparisonStatus = QStringLiteral("played_move_matches_best");
        } else {
            setError(errorMessage,
                QStringLiteral("Coach Review status has no frozen explanation-status join"));
            return false;
        }
        if (mechanicalMoment->comparisonStatus != expectedComparisonStatus) {
            setError(errorMessage,
                QStringLiteral("mechanical explanation comparison status contradicts Coach Review"));
            return false;
        }
    }
    return true;
}

std::optional<GameReviewMechanicalExplanationCatalog>
GameReviewMechanicalExplanationCatalog::fromDirectory(
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
        setError(errorMessage,
            QStringLiteral("game review explanation directory must be a direct canonical directory"));
        return std::nullopt;
    }
    const QFileInfoList files = QDir(directoryInfo.absoluteFilePath()).entryInfoList(
        {QStringLiteral("game-review-mechanical-explanation-v1-*.json")},
        QDir::Files | QDir::Readable | QDir::NoSymLinks,
        QDir::Name);
    if (files.isEmpty() || files.size() > kMaximumSidecars) {
        setError(errorMessage,
            QStringLiteral("game review explanation directory must contain 1..200 v1 companions"));
        return std::nullopt;
    }

    GameReviewMechanicalExplanationCatalog catalog;
    catalog.m_directoryPath = directoryInfo.absoluteFilePath();
    qint64 totalBytes = 0;
    for (const QFileInfo &fileInfo : files) {
        if (!kSidecarNamePattern.match(fileInfo.fileName()).hasMatch()
            || fileInfo.isSymLink() || !fileInfo.isFile()
            || fileInfo.canonicalFilePath() != fileInfo.absoluteFilePath()
            || fileInfo.size() < 1 || fileInfo.size() > kMaximumSidecarBytes
            || totalBytes > kMaximumCatalogBytes - fileInfo.size()) {
            setError(errorMessage,
                QStringLiteral("game review explanation name, path, or size is invalid"));
            return std::nullopt;
        }
        totalBytes += fileInfo.size();
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            setError(errorMessage,
                QStringLiteral("game review explanation companion could not be read"));
            return std::nullopt;
        }
        const QByteArray raw = file.read(kMaximumSidecarBytes + 1);
        if (raw.size() != fileInfo.size()) {
            setError(errorMessage,
                QStringLiteral("game review explanation companion changed while it was read"));
            return std::nullopt;
        }
        GameReviewMechanicalExplanation explanation;
        QString detail;
        if (!parseSidecar(raw, fileInfo.fileName(), &explanation, &detail)) {
            setError(errorMessage, QStringLiteral("%1: %2").arg(fileInfo.fileName(), detail));
            return std::nullopt;
        }
        if (catalog.m_explanationsByGame.contains(explanation.sourceGameId)) {
            setError(errorMessage,
                QStringLiteral("game review explanation directory repeats one source game"));
            return std::nullopt;
        }
        catalog.m_explanationsByGame.insert(explanation.sourceGameId, explanation);
        catalog.m_fileNamesByGame.insert(explanation.sourceGameId, fileInfo.fileName());
    }
    return catalog;
}

const GameReviewMechanicalExplanation *
GameReviewMechanicalExplanationCatalog::explanationForGame(
    const QString &sourceGameId) const
{
    const auto iterator = m_explanationsByGame.constFind(sourceGameId);
    return iterator == m_explanationsByGame.cend() ? nullptr : &iterator.value();
}

int GameReviewMechanicalExplanationCatalog::momentCount() const
{
    int count = 0;
    for (const GameReviewMechanicalExplanation &explanation : m_explanationsByGame) {
        count += explanation.moments.size();
    }
    return count;
}

} // namespace parlawl::puzzle_runner
