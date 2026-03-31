#include "pgn_utils.h"

#include <QRegularExpression>

namespace parlawl::puzzle_runner {

namespace {

QString stripCommentsAndVariations(const QString &pgnText)
{
    QString out;
    out.reserve(pgnText.size());

    bool inBraceComment = false;
    bool inSemicolonComment = false;
    int variationDepth = 0;

    for (const QChar ch : pgnText) {
        if (inSemicolonComment) {
            if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) {
                inSemicolonComment = false;
                out.append(QLatin1Char(' '));
            }
            continue;
        }

        if (inBraceComment) {
            if (ch == QLatin1Char('}')) {
                inBraceComment = false;
            }
            continue;
        }

        if (variationDepth > 0) {
            if (ch == QLatin1Char('(')) {
                ++variationDepth;
            } else if (ch == QLatin1Char(')')) {
                --variationDepth;
            }
            continue;
        }

        if (ch == QLatin1Char('{')) {
            inBraceComment = true;
            continue;
        }
        if (ch == QLatin1Char(';')) {
            inSemicolonComment = true;
            continue;
        }
        if (ch == QLatin1Char('(')) {
            variationDepth = 1;
            continue;
        }

        out.append(ch);
    }

    return out;
}

bool isMoveNumberToken(const QString &token)
{
    static const QRegularExpression moveNumberRegex(QStringLiteral("^\\d+\\.(\\.\\.)?$"));
    return moveNumberRegex.match(token).hasMatch();
}

bool isResultToken(const QString &token)
{
    return token == QStringLiteral("1-0")
        || token == QStringLiteral("0-1")
        || token == QStringLiteral("1/2-1/2")
        || token == QStringLiteral("*");
}

} // namespace

QStringList pgnMoveList(const QString &pgnText)
{
    if (pgnText.trimmed().isEmpty()) {
        return {};
    }

    QString sanitized = pgnText;
    sanitized.remove(QRegularExpression(QStringLiteral("(?m)^\\[[^\\n]*\\]\\s*")));
    sanitized = stripCommentsAndVariations(sanitized);
    sanitized.replace(QRegularExpression(QStringLiteral("\\$\\d+")), QStringLiteral(" "));

    const QStringList rawTokens = sanitized.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QStringList moves;
    moves.reserve(rawTokens.size());
    for (const QString &token : rawTokens) {
        const QString trimmed = token.trimmed();
        if (trimmed.isEmpty() || isMoveNumberToken(trimmed) || isResultToken(trimmed)) {
            continue;
        }
        moves.append(trimmed);
    }
    return moves;
}

QString pgnHeaderValue(const QString &pgnText, const QString &headerName)
{
    const QRegularExpression headerRegex(
        QStringLiteral("^\\[%1\\s+\"([^\"]*)\"\\]$")
            .arg(QRegularExpression::escape(headerName)),
        QRegularExpression::MultilineOption);
    const auto match = headerRegex.match(pgnText);
    if (!match.hasMatch()) {
        return QString();
    }
    return match.captured(1).trimmed();
}

QString pgnOpeningName(const QString &pgnText)
{
    return pgnHeaderValue(pgnText, QStringLiteral("Opening"));
}

QString pgnEcoCode(const QString &pgnText)
{
    return pgnHeaderValue(pgnText, QStringLiteral("ECO"));
}

QString pgnDisplayOpening(const QString &pgnText)
{
    const QString opening = pgnOpeningName(pgnText);
    const QString eco = pgnEcoCode(pgnText);
    if (!opening.isEmpty()) {
        return eco.isEmpty() ? opening : QStringLiteral("%1 (%2)").arg(opening, eco);
    }
    return eco;
}

} // namespace parlawl::puzzle_runner
