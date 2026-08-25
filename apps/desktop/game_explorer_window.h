#pragma once

#include <optional>

#include <QMainWindow>

#include "player_statistics_panel.h"

class GameExplorerWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit GameExplorerWindow(QWidget *parent = nullptr);

    bool loadExplorerDatabase(const QString &path, QString *errorMessage = nullptr);
    bool selectPlayer(const QString &playerId);
    void clearExplorer();
    [[nodiscard]] std::optional<PlayerStatisticsGameBreakdown> gameBreakdown(
        const QString &sourceGameId,
        QString *errorMessage = nullptr) const;
    [[nodiscard]] PlayerStatisticsPanel *panel() const { return m_panel; }
    void surface();

signals:
    void gameBreakdownRequested(const QString &sourceGameId);

private:
    PlayerStatisticsPanel *m_panel;
};
