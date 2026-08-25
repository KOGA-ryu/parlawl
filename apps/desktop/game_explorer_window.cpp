#include "game_explorer_window.h"

#include <algorithm>

#include <QGuiApplication>
#include <QScreen>

#include "review_ui_style.h"

GameExplorerWindow::GameExplorerWindow(QWidget *parent)
    : QMainWindow(parent, Qt::Window)
    , m_panel(new PlayerStatisticsPanel(this))
{
    setObjectName(QStringLiteral("gameExplorerWindow"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(QStringLiteral("Player Explorer"));
    setStyleSheet(parlawl::review_ui::studyWindowStyleSheet());
    m_panel->setObjectName(QStringLiteral("gameStudyExplorerPanel"));
    m_panel->setDedicatedExplorerMode(true);
    setCentralWidget(m_panel);

    resize(980, 820);
    if (QScreen *screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        const QRect available = screen->availableGeometry();
        resize(
            std::min(980, std::max(1, available.width() - 48)),
            std::min(820, std::max(1, available.height() - 96)));
        move(
            available.left() + std::max(24, (available.width() - width()) / 2),
            available.top() + std::max(24, (available.height() - height()) / 2));
    }

    connect(
        m_panel,
        &PlayerStatisticsPanel::gameBreakdownRequested,
        this,
        &GameExplorerWindow::gameBreakdownRequested);
}

bool GameExplorerWindow::loadExplorerDatabase(
    const QString &path,
    QString *errorMessage)
{
    return m_panel->loadExplorerDatabase(path, errorMessage);
}

bool GameExplorerWindow::selectPlayer(const QString &playerId)
{
    return m_panel->selectPlayer(playerId);
}

void GameExplorerWindow::clearExplorer()
{
    m_panel->clearSnapshot();
}

std::optional<PlayerStatisticsGameBreakdown> GameExplorerWindow::gameBreakdown(
    const QString &sourceGameId,
    QString *errorMessage) const
{
    return m_panel->gameBreakdown(sourceGameId, errorMessage);
}

void GameExplorerWindow::surface()
{
    showNormal();
    raise();
    activateWindow();
}
