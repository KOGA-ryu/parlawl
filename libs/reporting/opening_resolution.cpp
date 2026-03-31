#include "opening_resolution.h"

#include "pgn_utils.h"

#include <QRegularExpression>

namespace {

QString cleanText(const QString &value)
{
    return value.trimmed();
}

QString humanizeToken(QString token)
{
    token = cleanText(token);
    if (token.isEmpty()) {
        return token;
    }

    token.replace(QLatin1Char('_'), QLatin1Char(' '));
    token.replace(QLatin1Char('-'), QLatin1Char(' '));
    token.replace(QRegularExpression(QStringLiteral("([a-z])([A-Z])")), QStringLiteral("\\1 \\2"));

    const QStringList parts = token.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QStringList titled;
    titled.reserve(parts.size());
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

} // namespace

OpeningResolution resolveOpeningDisplay(
    const SourceGame &sourceGame,
    const TacticalEvent &tacticalEvent
)
{
    OpeningResolution resolution;

    const QString pgnOpening = parlawl::puzzle_runner::pgnDisplayOpening(sourceGame.pgnText);
    const QString storedOpening = cleanText(sourceGame.openingName);

    if (!pgnOpening.isEmpty()) {
        resolution.display = pgnOpening;
        if (!storedOpening.isEmpty() && storedOpening != pgnOpening) {
            resolution.warnings.append(QStringLiteral("Stored opening disagrees with PGN header."));
        }
        return resolution;
    }

    if (!storedOpening.isEmpty()) {
        resolution.display = storedOpening;
        return resolution;
    }

    if (!tacticalEvent.openingFamily.isEmpty()) {
        resolution.display = humanizeToken(tacticalEvent.openingFamily);
        resolution.warnings.append(QStringLiteral("Opening inferred from fallback field."));
        return resolution;
    }

    resolution.display = QStringLiteral("Unknown");
    return resolution;
}
