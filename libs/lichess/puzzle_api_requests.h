#pragma once

#include <optional>

#include <QByteArray>
#include <QJsonDocument>
#include <QList>
#include <QString>

#include "puzzle_api_types.h"

namespace puzzle_api {

struct puzzle_id_request
{
    QString id;
};

struct puzzle_daily_request
{
};

struct puzzle_next_options
{
    QString angle;
    std::optional<puzzle_difficulty> difficulty;
    std::optional<puzzle_color> color;
};

struct puzzle_batch_fetch_options
{
    QString angle = QStringLiteral("mix");
    std::optional<puzzle_difficulty> difficulty;
    int nb = 15;
    std::optional<puzzle_color> color;
};

struct puzzle_batch_solution
{
    QString id;
    bool win = false;
    bool rated = false;
};

struct puzzle_batch_solve_options
{
    QString angle = QStringLiteral("mix");
    int nb = 0;
};

struct puzzle_batch_solve_request
{
    QList<puzzle_batch_solution> solutions;
};

struct puzzle_activity_options
{
    std::optional<int> max;
    std::optional<int> before;
    std::optional<int> since;
};

struct puzzle_replay_request
{
    int days = 0;
    QString theme;
};

struct puzzle_dashboard_request
{
    int days = 0;
};

struct request_spec
{
    QString path;
    QList<QPair<QString, QString>> query_items;
    QByteArray json_body;
    QString content_type;
};

QString serialize_url(const QString &base_url, const request_spec &request);
QJsonDocument to_json_document(const puzzle_batch_solve_request &request);

bool build_puzzle_id_request(const puzzle_id_request &request, request_spec *spec, QString *error_message = nullptr);
bool build_puzzle_daily_request(const puzzle_daily_request &request, request_spec *spec, QString *error_message = nullptr);
bool build_puzzle_next_request(const puzzle_next_options &options, request_spec *spec, QString *error_message = nullptr);
bool build_puzzle_batch_fetch_request(const puzzle_batch_fetch_options &options, request_spec *spec, QString *error_message = nullptr);
bool build_puzzle_batch_solve_request(
    const puzzle_batch_solve_options &options,
    const puzzle_batch_solve_request &request,
    request_spec *spec,
    QString *error_message = nullptr);
bool build_puzzle_activity_request(const puzzle_activity_options &options, request_spec *spec, QString *error_message = nullptr);
bool build_puzzle_replay_request(const puzzle_replay_request &request, request_spec *spec, QString *error_message = nullptr);
bool build_puzzle_dashboard_request(const puzzle_dashboard_request &request, request_spec *spec, QString *error_message = nullptr);

} // namespace puzzle_api
