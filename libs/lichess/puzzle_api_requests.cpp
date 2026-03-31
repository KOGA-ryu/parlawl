#include "puzzle_api_requests.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace puzzle_api {

namespace {

bool assign_error(QString *error_message, const QString &message)
{
    if (error_message != nullptr) {
        *error_message = message;
    }
    return false;
}

bool validate_non_empty_path_component(const QString &value, const QString &field_name, QString *error_message)
{
    if (value.trimmed().isEmpty()) {
        return assign_error(error_message, QStringLiteral("%1 is required").arg(field_name));
    }
    return true;
}

void add_query_item(QList<QPair<QString, QString>> *items, const QString &key, const QString &value)
{
    if (items == nullptr || key.trimmed().isEmpty() || value.trimmed().isEmpty()) {
        return;
    }
    items->append({key, value});
}

} // namespace

QString serialize_url(const QString &base_url, const request_spec &request)
{
    QUrl url(base_url + request.path);
    QUrlQuery query;
    for (const auto &item : request.query_items) {
        if (!item.first.trimmed().isEmpty() && !item.second.trimmed().isEmpty()) {
            query.addQueryItem(item.first, item.second);
        }
    }
    if (!query.isEmpty()) {
        url.setQuery(query);
    }
    return url.toString();
}

QJsonDocument to_json_document(const puzzle_batch_solve_request &request)
{
    QJsonArray solutions;
    for (const puzzle_batch_solution &solution : request.solutions) {
        solutions.append(QJsonObject{
            {QStringLiteral("id"), solution.id},
            {QStringLiteral("win"), solution.win},
            {QStringLiteral("rated"), solution.rated},
        });
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("solutions"), solutions}});
}

bool build_puzzle_id_request(const puzzle_id_request &request, request_spec *spec, QString *error_message)
{
    if (spec == nullptr) {
        return assign_error(error_message, QStringLiteral("request spec output is required"));
    }
    if (!validate_non_empty_path_component(request.id, QStringLiteral("puzzle id"), error_message)) {
        return false;
    }
    spec->path = QStringLiteral("/api/puzzle/%1").arg(request.id.trimmed());
    spec->query_items.clear();
    spec->json_body.clear();
    spec->content_type.clear();
    return true;
}

bool build_puzzle_daily_request(const puzzle_daily_request &, request_spec *spec, QString *error_message)
{
    if (spec == nullptr) {
        return assign_error(error_message, QStringLiteral("request spec output is required"));
    }
    spec->path = QStringLiteral("/api/puzzle/daily");
    spec->query_items.clear();
    spec->json_body.clear();
    spec->content_type.clear();
    return true;
}

bool build_puzzle_next_request(const puzzle_next_options &options, request_spec *spec, QString *error_message)
{
    if (spec == nullptr) {
        return assign_error(error_message, QStringLiteral("request spec output is required"));
    }
    spec->path = QStringLiteral("/api/puzzle/next");
    spec->query_items.clear();
    add_query_item(&spec->query_items, QStringLiteral("angle"), options.angle.trimmed());
    if (options.difficulty.has_value()) {
        add_query_item(&spec->query_items, QStringLiteral("difficulty"), to_string(*options.difficulty));
    }
    if (options.color.has_value()) {
        add_query_item(&spec->query_items, QStringLiteral("color"), to_string(*options.color));
    }
    spec->json_body.clear();
    spec->content_type.clear();
    return true;
}

bool build_puzzle_batch_fetch_request(const puzzle_batch_fetch_options &options, request_spec *spec, QString *error_message)
{
    if (spec == nullptr) {
        return assign_error(error_message, QStringLiteral("request spec output is required"));
    }
    if (!validate_non_empty_path_component(options.angle, QStringLiteral("batch angle"), error_message)) {
        return false;
    }
    if (options.nb < 1 || options.nb > 50) {
        return assign_error(error_message, QStringLiteral("batch fetch nb must be between 1 and 50"));
    }
    if (options.color.has_value() && options.nb != 1) {
        return assign_error(error_message, QStringLiteral("batch fetch color is only valid when nb == 1"));
    }

    spec->path = QStringLiteral("/api/puzzle/batch/%1").arg(options.angle.trimmed());
    spec->query_items.clear();
    add_query_item(&spec->query_items, QStringLiteral("difficulty"), options.difficulty.has_value() ? to_string(*options.difficulty) : QString());
    add_query_item(&spec->query_items, QStringLiteral("nb"), QString::number(options.nb));
    if (options.color.has_value()) {
        add_query_item(&spec->query_items, QStringLiteral("color"), to_string(*options.color));
    }
    spec->json_body.clear();
    spec->content_type.clear();
    return true;
}

bool build_puzzle_batch_solve_request(
    const puzzle_batch_solve_options &options,
    const puzzle_batch_solve_request &request,
    request_spec *spec,
    QString *error_message)
{
    if (spec == nullptr) {
        return assign_error(error_message, QStringLiteral("request spec output is required"));
    }
    if (!validate_non_empty_path_component(options.angle, QStringLiteral("batch angle"), error_message)) {
        return false;
    }
    if (options.nb < 0 || options.nb > 50) {
        return assign_error(error_message, QStringLiteral("batch solve nb must be between 0 and 50"));
    }
    if (request.solutions.isEmpty()) {
        return assign_error(error_message, QStringLiteral("batch solve request requires at least one solution"));
    }
    for (const puzzle_batch_solution &solution : request.solutions) {
        if (solution.id.trimmed().isEmpty()) {
            return assign_error(error_message, QStringLiteral("batch solve solution id is required"));
        }
    }

    spec->path = QStringLiteral("/api/puzzle/batch/%1").arg(options.angle.trimmed());
    spec->query_items.clear();
    add_query_item(&spec->query_items, QStringLiteral("nb"), QString::number(options.nb));
    spec->json_body = to_json_document(request).toJson(QJsonDocument::Compact);
    spec->content_type = QStringLiteral("application/json");
    return true;
}

bool build_puzzle_activity_request(const puzzle_activity_options &options, request_spec *spec, QString *error_message)
{
    if (spec == nullptr) {
        return assign_error(error_message, QStringLiteral("request spec output is required"));
    }
    auto validate_timestamp = [&](const std::optional<int> &value, const QString &field_name) {
        if (value.has_value() && *value < 0) {
            return assign_error(error_message, QStringLiteral("%1 must be a non-negative integer timestamp").arg(field_name));
        }
        return true;
    };
    if (!validate_timestamp(options.before, QStringLiteral("before"))
        || !validate_timestamp(options.since, QStringLiteral("since"))) {
        return false;
    }
    if (options.max.has_value() && *options.max < 0) {
        return assign_error(error_message, QStringLiteral("max must be a non-negative integer"));
    }

    spec->path = QStringLiteral("/api/puzzle/activity");
    spec->query_items.clear();
    if (options.max.has_value()) {
        add_query_item(&spec->query_items, QStringLiteral("max"), QString::number(*options.max));
    }
    if (options.before.has_value()) {
        add_query_item(&spec->query_items, QStringLiteral("before"), QString::number(*options.before));
    }
    if (options.since.has_value()) {
        add_query_item(&spec->query_items, QStringLiteral("since"), QString::number(*options.since));
    }
    spec->json_body.clear();
    spec->content_type.clear();
    return true;
}

bool build_puzzle_replay_request(const puzzle_replay_request &request, request_spec *spec, QString *error_message)
{
    if (spec == nullptr) {
        return assign_error(error_message, QStringLiteral("request spec output is required"));
    }
    if (request.days < 0) {
        return assign_error(error_message, QStringLiteral("replay days must be non-negative"));
    }
    if (!validate_non_empty_path_component(request.theme, QStringLiteral("replay theme"), error_message)) {
        return false;
    }
    spec->path = QStringLiteral("/api/puzzle/replay/%1/%2").arg(request.days).arg(request.theme.trimmed());
    spec->query_items.clear();
    spec->json_body.clear();
    spec->content_type.clear();
    return true;
}

bool build_puzzle_dashboard_request(const puzzle_dashboard_request &request, request_spec *spec, QString *error_message)
{
    if (spec == nullptr) {
        return assign_error(error_message, QStringLiteral("request spec output is required"));
    }
    if (request.days < 0) {
        return assign_error(error_message, QStringLiteral("dashboard days must be non-negative"));
    }
    spec->path = QStringLiteral("/api/puzzle/dashboard/%1").arg(request.days);
    spec->query_items.clear();
    spec->json_body.clear();
    spec->content_type.clear();
    return true;
}

} // namespace puzzle_api
