#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QTimer>

#include <cstdio>

#include "puzzle_runner_window.h"

namespace {

struct StartupPreflight {
    bool helpRequested = false;
    bool playerExplorerPresent = false;
    bool playerIdPresent = false;
    bool sourceGameIdPresent = false;
    bool selectiveReportDirectoryPresent = false;
    bool gameReviewDirectoryPresent = false;
    QString error;
};

StartupPreflight preflightStartupArguments(int argc, char *argv[])
{
    StartupPreflight result;
    const auto markValueOption = [&result, argc, argv](
                                     int *index,
                                     const QString &argument,
                                     const QString &name,
                                     bool *present) {
        const QString prefix = name + QLatin1Char('=');
        if (argument == name) {
            if (*present) {
                result.error = QStringLiteral("%1 may be supplied only once").arg(name);
                return true;
            }
            if (*index + 1 >= argc
                || QString::fromLocal8Bit(argv[*index + 1]).startsWith(QLatin1Char('-'))) {
                result.error = QStringLiteral("%1 requires a value").arg(name);
                return true;
            }
            *present = true;
            ++*index;
            return true;
        }
        if (argument.startsWith(prefix)) {
            if (*present) {
                result.error = QStringLiteral("%1 may be supplied only once").arg(name);
                return true;
            }
            if (argument.size() == prefix.size()) {
                result.error = QStringLiteral("%1 requires a value").arg(name);
                return true;
            }
            *present = true;
            return true;
        }
        return false;
    };

    for (int index = 1; index < argc && result.error.isEmpty(); ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("-h")
            || argument == QStringLiteral("--help")
            || argument == QStringLiteral("--help-all")) {
            result.helpRequested = true;
            continue;
        }
        if (markValueOption(
                &index,
                argument,
                QStringLiteral("--player-explorer"),
                &result.playerExplorerPresent)
            || markValueOption(
                &index,
                argument,
                QStringLiteral("--player-id"),
                &result.playerIdPresent)
            || markValueOption(
                &index,
                argument,
                QStringLiteral("--source-game-id"),
                &result.sourceGameIdPresent)
            || markValueOption(
                &index,
                argument,
                QStringLiteral("--selective-report-directory"),
                &result.selectiveReportDirectoryPresent)
            || markValueOption(
                &index,
                argument,
                QStringLiteral("--game-review-directory"),
                &result.gameReviewDirectoryPresent)) {
            continue;
        }
    }

    if (result.error.isEmpty()
        && !result.playerExplorerPresent
        && (result.playerIdPresent || result.sourceGameIdPresent
            || result.selectiveReportDirectoryPresent
            || result.gameReviewDirectoryPresent)) {
        result.error = QStringLiteral(
            "--player-id, --source-game-id, --selective-report-directory, and --game-review-directory require --player-explorer");
    }
    if (result.error.isEmpty()
        && result.sourceGameIdPresent
        && !result.playerIdPresent) {
        result.error = QStringLiteral("--source-game-id requires --player-id");
    }
    return result;
}

void printStartupHelp(const char *program)
{
    std::fprintf(
        stdout,
        "Usage: %s [options]\n"
        "ParlAWL desktop chess review\n\n"
        "Options:\n"
        "  -h, --help                                Displays this help.\n"
        "  --player-explorer <absolute-sqlite-path>  Open an exact local player-game explorer read-only.\n"
        "  --player-id <player-id>                   Select an exact player from --player-explorer.\n"
        "  --source-game-id <source-game-id>         Open an exact game for --player-id at its report start position.\n"
        "  --selective-report-directory <directory>  Join bounded selective deep Report-v2 files read-only.\n"
        "  --game-review-directory <directory>       Join display-v1 Coach Review sidecars read-only.\n",
        program != nullptr ? program : "parlawl");
}

} // namespace

int main(int argc, char *argv[])
{
    const StartupPreflight preflight = preflightStartupArguments(argc, argv);
    if (preflight.helpRequested) {
        printStartupHelp(argc > 0 ? argv[0] : nullptr);
        return 0;
    }
    if (!preflight.error.isEmpty()) {
        std::fprintf(stderr, "%s\n", preflight.error.toLocal8Bit().constData());
        return 2;
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName("parlawl");
    QApplication::setOrganizationDomain("local.parlawl");
    QApplication::setApplicationName("parlawl");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("ParlAWL desktop chess review"));
    parser.addHelpOption();
    const QCommandLineOption playerExplorerOption(
        QStringLiteral("player-explorer"),
        QStringLiteral("Open an exact local player-game explorer read-only."),
        QStringLiteral("absolute-sqlite-path"));
    const QCommandLineOption playerIdOption(
        QStringLiteral("player-id"),
        QStringLiteral("Select an exact player from --player-explorer."),
        QStringLiteral("player-id"));
    const QCommandLineOption sourceGameIdOption(
        QStringLiteral("source-game-id"),
        QStringLiteral("Open an exact game for --player-id at its report start position."),
        QStringLiteral("source-game-id"));
    const QCommandLineOption selectiveReportDirectoryOption(
        QStringLiteral("selective-report-directory"),
        QStringLiteral("Join bounded selective deep Report-v2 files read-only."),
        QStringLiteral("absolute-directory"));
    const QCommandLineOption gameReviewDirectoryOption(
        QStringLiteral("game-review-directory"),
        QStringLiteral("Join display-v1 Coach Review sidecars read-only."),
        QStringLiteral("absolute-directory"));
    parser.addOptions({
        playerExplorerOption,
        playerIdOption,
        sourceGameIdOption,
        selectiveReportDirectoryOption,
        gameReviewDirectoryOption,
    });
    parser.process(app);

    const QString explorerPath = parser.value(playerExplorerOption);
    const QString playerId = parser.value(playerIdOption);
    const QString sourceGameId = parser.value(sourceGameIdOption);
    const QString selectiveReportDirectory = parser.value(selectiveReportDirectoryOption);
    const QString gameReviewDirectory = parser.value(gameReviewDirectoryOption);
    if (explorerPath.isEmpty() && (!playerId.isEmpty() || !sourceGameId.isEmpty()
            || !selectiveReportDirectory.isEmpty() || !gameReviewDirectory.isEmpty())) {
        qCritical().noquote()
            << QStringLiteral("--player-id, --source-game-id, --selective-report-directory, and --game-review-directory require --player-explorer");
        return 2;
    }
    if (!sourceGameId.isEmpty() && playerId.isEmpty()) {
        qCritical().noquote()
            << QStringLiteral("--source-game-id requires --player-id");
        return 2;
    }

    PuzzleRunnerWindow window;
    if (!explorerPath.isEmpty()) {
        QString errorMessage;
        if (!window.openPlayerGameExplorer(
                explorerPath,
                playerId,
                sourceGameId,
                selectiveReportDirectory,
                gameReviewDirectory,
                &errorMessage)) {
            qCritical().noquote()
                << QStringLiteral("player explorer startup failed: %1").arg(errorMessage);
            return 2;
        }
    }
    window.show();
    QTimer::singleShot(0, &window, &PuzzleRunnerWindow::surfaceGameStudyWorkspace);
    return app.exec();
}
