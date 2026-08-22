#pragma once

// The market workspace: chart, fixed stat HUD, and one of the two decision
// surfaces. It owns the session controller, which owns the vault.
//
// Note what this header does NOT include, and could not: `market_continuation.h`
// is not reachable from here, so nothing in this window can name a bar after T
// except through the `MarketReveal` a committed terminal produced.

#include <memory>

#include <QMainWindow>

#include "market_session_controller.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QStackedWidget;
class QToolBar;
QT_END_NAMESPACE

class MarketAttemptRepository;
class MarketChartWidget;
class MarketHudWidget;
class MarketRushPanel;
class MarketStudyPanel;

class MarketWorkspaceWindow : public QMainWindow
{
    Q_OBJECT

public:
    MarketWorkspaceWindow(
        MarketAttemptRepository *repository,
        QString solverId,
        QString sessionId,
        QWidget *parent = nullptr);

    bool loadPackFromFiles(const QString &visiblePath, QString *errorMessage);
    bool loadPackFromBytes(
        const QByteArray &visibleJsonLines,
        const QByteArray &sealedJsonLines,
        QString *errorMessage);

    [[nodiscard]] parlawl::market::MarketSessionController *controller() { return &m_controller; }
    [[nodiscard]] MarketChartWidget *chart() { return m_chart; }
    [[nodiscard]] MarketHudWidget *hud() { return m_hud; }
    [[nodiscard]] MarketRushPanel *rushPanel() { return m_rushPanel; }
    [[nodiscard]] MarketStudyPanel *studyPanel() { return m_studyPanel; }
    [[nodiscard]] QString statusText() const;

public slots:
    void setStudyMode(bool study);
    void onImportPackRequested();
    void onExportResultsRequested();
    void onRevealRequested();
    void onNextRepRequested();

private:
    void syncSurfaces();

    parlawl::market::MarketSessionController m_controller;
    MarketAttemptRepository *m_repository;
    QString m_solverId;
    QString m_sessionId;

    MarketChartWidget *m_chart;
    MarketHudWidget *m_hud;
    MarketRushPanel *m_rushPanel;
    MarketStudyPanel *m_studyPanel;
    QStackedWidget *m_surfaces;
    QLabel *m_statusLabel;
    QLabel *m_evidenceGradeLabel;
};
