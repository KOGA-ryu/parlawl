#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>

#include "puzzle_runner_window.h"

int main(int argc, char *argv[])
{
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
    parser.addOptions({
        playerExplorerOption,
        playerIdOption,
        sourceGameIdOption,
    });
    parser.process(app);

    const QString explorerPath = parser.value(playerExplorerOption);
    const QString playerId = parser.value(playerIdOption);
    const QString sourceGameId = parser.value(sourceGameIdOption);
    if (explorerPath.isEmpty() && (!playerId.isEmpty() || !sourceGameId.isEmpty())) {
        qCritical().noquote()
            << QStringLiteral("--player-id and --source-game-id require --player-explorer");
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
                &errorMessage)) {
            qCritical().noquote()
                << QStringLiteral("player explorer startup failed: %1").arg(errorMessage);
            return 2;
        }
    }
    window.show();
    return app.exec();
}
