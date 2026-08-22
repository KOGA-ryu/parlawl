#include "market_workspace_window.h"

#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include "market_attempt_repository.h"
#include "market_chart_widget.h"
#include "market_hud.h"
#include "market_hud_widget.h"
#include "market_rush_panel.h"
#include "market_study_panel.h"

using namespace parlawl::market;

namespace {

//! `<name>.visible.jsonl` and `<name>.sealed.jsonl` are produced together and
//! shipped together, so the operator picks one and gets both or neither.
QString sealedPartnerPath(const QString &visiblePath)
{
    const QString suffix = QStringLiteral(".visible.jsonl");
    if (!visiblePath.endsWith(suffix)) {
        return {};
    }
    return visiblePath.left(visiblePath.size() - suffix.size()) + QStringLiteral(".sealed.jsonl");
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

} // namespace

MarketWorkspaceWindow::MarketWorkspaceWindow(
    MarketAttemptRepository *repository,
    QString solverId,
    QString sessionId,
    QWidget *parent)
    : QMainWindow(parent)
    , m_repository(repository)
    , m_solverId(std::move(solverId))
    , m_sessionId(std::move(sessionId))
    , m_chart(new MarketChartWidget(this))
    , m_hud(new MarketHudWidget(this))
    , m_rushPanel(new MarketRushPanel(this))
    , m_studyPanel(new MarketStudyPanel(this))
    , m_surfaces(new QStackedWidget(this))
    , m_statusLabel(new QLabel(this))
    , m_evidenceGradeLabel(new QLabel(this))
{
    setObjectName(QStringLiteral("marketWorkspaceWindow"));
    setWindowTitle(QStringLiteral("ParlAWL — market workspace"));
    m_statusLabel->setObjectName(QStringLiteral("marketWorkspaceStatus"));
    m_evidenceGradeLabel->setObjectName(QStringLiteral("marketWorkspaceEvidenceGrade"));
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_evidenceGradeLabel->setTextFormat(Qt::PlainText);
    m_evidenceGradeLabel->setWordWrap(true);
    m_evidenceGradeLabel->setText(marketPackEvidenceGrade());

    m_surfaces->setObjectName(QStringLiteral("marketWorkspaceSurfaces"));
    m_surfaces->addWidget(m_rushPanel);
    m_surfaces->addWidget(m_studyPanel);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(m_chart, 3);
    layout->addWidget(m_evidenceGradeLabel);
    setCentralWidget(central);

    auto *hudDock = new QDockWidget(QStringLiteral("HUD"), this);
    hudDock->setObjectName(QStringLiteral("marketWorkspaceHudDock"));
    hudDock->setWidget(m_hud);
    hudDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::RightDockWidgetArea, hudDock);

    auto *surfaceDock = new QDockWidget(QStringLiteral("Decision"), this);
    surfaceDock->setObjectName(QStringLiteral("marketWorkspaceSurfaceDock"));
    surfaceDock->setWidget(m_surfaces);
    surfaceDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::BottomDockWidgetArea, surfaceDock);

    auto *toolBar = addToolBar(QStringLiteral("market"));
    toolBar->setObjectName(QStringLiteral("marketWorkspaceToolBar"));
    auto *importAction = toolBar->addAction(QStringLiteral("Import Market Pack"));
    importAction->setObjectName(QStringLiteral("marketWorkspaceImportAction"));
    auto *studyAction = toolBar->addAction(QStringLiteral("Study mode"));
    studyAction->setObjectName(QStringLiteral("marketWorkspaceStudyAction"));
    studyAction->setCheckable(true);
    auto *revealAction = toolBar->addAction(QStringLiteral("Reveal"));
    revealAction->setObjectName(QStringLiteral("marketWorkspaceRevealAction"));
    auto *nextAction = toolBar->addAction(QStringLiteral("Next rep"));
    nextAction->setObjectName(QStringLiteral("marketWorkspaceNextAction"));
    auto *exportAction = toolBar->addAction(QStringLiteral("Export Market Solve History"));
    exportAction->setObjectName(QStringLiteral("marketWorkspaceExportAction"));

    statusBar()->addWidget(m_statusLabel);

    connect(importAction, &QAction::triggered, this, &MarketWorkspaceWindow::onImportPackRequested);
    connect(studyAction, &QAction::toggled, this, &MarketWorkspaceWindow::setStudyMode);
    connect(revealAction, &QAction::triggered, this, &MarketWorkspaceWindow::onRevealRequested);
    connect(nextAction, &QAction::triggered, this, &MarketWorkspaceWindow::onNextRepRequested);
    connect(exportAction, &QAction::triggered, this, &MarketWorkspaceWindow::onExportResultsRequested);
    connect(m_rushPanel, &MarketRushPanel::revealRequested, this, &MarketWorkspaceWindow::onRevealRequested);
    connect(m_rushPanel, &MarketRushPanel::nextRepRequested, this, &MarketWorkspaceWindow::onNextRepRequested);
    connect(m_studyPanel, &MarketStudyPanel::revealChanged, this, &MarketWorkspaceWindow::syncSurfaces);
    connect(
        &m_controller,
        &MarketSessionController::sessionChanged,
        this,
        &MarketWorkspaceWindow::syncSurfaces);
    connect(&m_controller, &MarketSessionController::errorRaised, this, [this](const QString &message) {
        m_statusLabel->setText(message);
    });

    if (m_repository != nullptr) {
        m_controller.configureJournal(
            m_repository, m_repository, m_repository, m_solverId, m_sessionId);
    }
    m_rushPanel->setController(&m_controller);
    m_studyPanel->setController(&m_controller);
    syncSurfaces();
}

QString MarketWorkspaceWindow::statusText() const
{
    return m_statusLabel->text();
}

bool MarketWorkspaceWindow::loadPackFromBytes(
    const QByteArray &visibleJsonLines,
    const QByteArray &sealedJsonLines,
    QString *errorMessage)
{
    if (!m_controller.loadPack(visibleJsonLines, sealedJsonLines, errorMessage)) {
        return false;
    }
    syncSurfaces();
    return true;
}

bool MarketWorkspaceWindow::loadPackFromFiles(const QString &visiblePath, QString *errorMessage)
{
    const QString sealedPath = sealedPartnerPath(visiblePath);
    if (sealedPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "choose the '<name>.visible.jsonl' half; its '.sealed.jsonl' partner is loaded with it");
        }
        return false;
    }
    if (!QFileInfo::exists(sealedPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral(
                "the sealed partner '%1' is missing; a rep whose answer cannot be scored is not a rep")
                                .arg(QFileInfo(sealedPath).fileName());
        }
        return false;
    }
    return loadPackFromBytes(readAll(visiblePath), readAll(sealedPath), errorMessage);
}

void MarketWorkspaceWindow::setStudyMode(bool study)
{
    m_controller.setMode(study ? MarketMode::Study : MarketMode::Rush);
    m_surfaces->setCurrentWidget(study ? static_cast<QWidget *>(m_studyPanel) : m_rushPanel);
    syncSurfaces();
}

void MarketWorkspaceWindow::onImportPackRequested()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Market Pack"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("Visible pack (*.visible.jsonl)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!loadPackFromFiles(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("market pack"), error);
        m_statusLabel->setText(error);
        return;
    }
    m_statusLabel->setText(
        QStringLiteral("Loaded %1 reps from %2")
            .arg(m_controller.puzzleCount())
            .arg(m_controller.header().packId));
}

void MarketWorkspaceWindow::onExportResultsRequested()
{
    if (m_repository == nullptr) {
        QMessageBox::warning(
            this,
            QStringLiteral("market solve history"),
            QStringLiteral("The local market solve journal is not available."));
        return;
    }
    QString exportDirectory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (exportDirectory.isEmpty()) {
        exportDirectory = QDir::homePath();
    }
    const QString suggested = exportDirectory
        + QStringLiteral("/parlawl-market-results-%1.jsonl")
              .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Market Solve History"), suggested, QStringLiteral("JSONL (*.jsonl)"));
    if (path.isEmpty()) {
        return;
    }
    int exported = 0;
    QString error;
    if (!m_repository->exportMarketSolveResults(path, &exported, &error)) {
        QMessageBox::warning(this, QStringLiteral("market solve history"), error);
        m_statusLabel->setText(error);
        return;
    }
    m_statusLabel->setText(QStringLiteral("Exported %1 completed reps.").arg(exported));
}

void MarketWorkspaceWindow::onRevealRequested()
{
    QString error;
    if (!m_controller.openReveal(&error)) {
        m_statusLabel->setText(error);
        return;
    }
    syncSurfaces();
}

void MarketWorkspaceWindow::onNextRepRequested()
{
    QString error;
    if (!m_controller.nextPuzzle(&error)) {
        m_statusLabel->setText(error);
        return;
    }
    syncSurfaces();
}

void MarketWorkspaceWindow::syncSurfaces()
{
    const MarketPuzzleVisible *puzzle = m_controller.currentPuzzle();
    if (puzzle == nullptr) {
        m_chart->clear();
        m_hud->clear();
        return;
    }
    m_chart->setWindow(
        puzzle->displaySymbol,
        puzzle->window.grain,
        puzzle->window.bars,
        puzzle->window.sessionBreakAfter);
    m_hud->setPuzzle(*puzzle, m_controller.header().verifiedHudStats);

    const MarketReveal *reveal = m_controller.reveal();
    if (reveal == nullptr) {
        // Before the reveal there is nothing after T to draw, and no code path
        // that could supply it.
        m_chart->clearRevealedContinuation();
    } else {
        m_chart->setRevealedContinuation(
            reveal->continuationBars,
            m_controller.mode() == MarketMode::Study
                ? m_controller.revealedContinuationBars()
                : static_cast<int>(reveal->continuationBars.size()));
    }
    m_statusLabel->setText(
        QStringLiteral("rep %1 of %2 · %3 · prior exposures %4")
            .arg(m_controller.currentPuzzleIndex() + 1)
            .arg(m_controller.puzzleCount())
            .arg(marketModeText(m_controller.mode()))
            .arg(m_controller.priorExposureCount()));
}
