#include "market_types.h"

#include "strict_json.h"

namespace parlawl::market {

QString taskKindText(TaskKind kind)
{
    switch (kind) {
    case TaskKind::AnomalyFlag:
        return QStringLiteral("anomaly_flag");
    case TaskKind::PatternCall:
        return QStringLiteral("pattern_call");
    case TaskKind::TradeLine:
        return QStringLiteral("trade_line");
    }
    return {};
}

std::optional<TaskKind> taskKindFromText(const QString &text)
{
    if (text == QStringLiteral("anomaly_flag")) {
        return TaskKind::AnomalyFlag;
    }
    if (text == QStringLiteral("pattern_call")) {
        return TaskKind::PatternCall;
    }
    if (text == QStringLiteral("trade_line")) {
        return TaskKind::TradeLine;
    }
    return std::nullopt;
}

QString plyKindText(PlyKind kind)
{
    switch (kind) {
    case PlyKind::ArtifactClass:
        return QStringLiteral("artifact_class");
    case PlyKind::Bracket:
        return QStringLiteral("bracket");
    case PlyKind::Confidence:
        return QStringLiteral("confidence");
    case PlyKind::Entry:
        return QStringLiteral("entry");
    case PlyKind::FollowUp:
        return QStringLiteral("follow_up");
    case PlyKind::Label:
        return QStringLiteral("label");
    case PlyKind::SizeBand:
        return QStringLiteral("size_band");
    case PlyKind::Verdict:
        return QStringLiteral("verdict");
    }
    return {};
}

std::optional<PlyKind> plyKindFromText(const QString &text)
{
    if (text == QStringLiteral("artifact_class")) {
        return PlyKind::ArtifactClass;
    }
    if (text == QStringLiteral("bracket")) {
        return PlyKind::Bracket;
    }
    if (text == QStringLiteral("confidence")) {
        return PlyKind::Confidence;
    }
    if (text == QStringLiteral("entry")) {
        return PlyKind::Entry;
    }
    if (text == QStringLiteral("follow_up")) {
        return PlyKind::FollowUp;
    }
    if (text == QStringLiteral("label")) {
        return PlyKind::Label;
    }
    if (text == QStringLiteral("size_band")) {
        return PlyKind::SizeBand;
    }
    if (text == QStringLiteral("verdict")) {
        return PlyKind::Verdict;
    }
    return std::nullopt;
}

namespace {

parlawl::strictjson::JsonValue numberOrNull(const std::optional<double> &value)
{
    parlawl::strictjson::JsonValue result;
    if (!value.has_value()) {
        result.kind = parlawl::strictjson::JsonValue::Kind::Null;
        return result;
    }
    result.kind = parlawl::strictjson::JsonValue::Kind::Number;
    result.number = *value;
    return result;
}

} // namespace

QString marketBarsDigest(const QVector<MarketBar> &bars)
{
    parlawl::strictjson::JsonValue array;
    array.kind = parlawl::strictjson::JsonValue::Kind::Array;
    for (const MarketBar &bar : bars) {
        parlawl::strictjson::JsonValue row;
        row.kind = parlawl::strictjson::JsonValue::Kind::Array;
        row.array.append(parlawl::strictjson::makeInteger(bar.barIndex));
        row.array.append(numberOrNull(bar.open));
        row.array.append(numberOrNull(bar.high));
        row.array.append(numberOrNull(bar.low));
        row.array.append(numberOrNull(bar.close));
        row.array.append(numberOrNull(bar.volume));
        row.array.append(numberOrNull(bar.tradeCount));
        array.array.append(row);
    }
    return parlawl::strictjson::sha256Hex(parlawl::strictjson::canonicalJson(array));
}

QString marketHudDigest(const QVector<MarketHudStat> &hud)
{
    parlawl::strictjson::JsonValue array;
    array.kind = parlawl::strictjson::JsonValue::Kind::Array;
    for (const MarketHudStat &stat : hud) {
        parlawl::strictjson::JsonValue value;
        value.kind = parlawl::strictjson::JsonValue::Kind::Number;
        value.number = stat.value;
        array.array.append(parlawl::strictjson::makeObject({
            {QStringLiteral("stat_id"), parlawl::strictjson::makeString(stat.statId)},
            {QStringLiteral("unit"), parlawl::strictjson::makeString(stat.unit)},
            {QStringLiteral("value"), value},
        }));
    }
    return parlawl::strictjson::sha256Hex(parlawl::strictjson::canonicalJson(array));
}

} // namespace parlawl::market
