#include "game_study_window.h"

#include <algorithm>
#include <cstdlib>

#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCryptographicHash>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "board_widget.h"
#include "chess_position.h"
#include "puzzle_panels.h"
#include "review_ui_style.h"

using namespace parlawl::puzzle_runner;

namespace {

QString compactDate(const QString &timestamp)
{
    return timestamp.size() >= 10 ? timestamp.left(10) : timestamp;
}

QString gameTitle(const AnnotatedReplayPack &pack)
{
    return QStringLiteral("%1 %2 · %3 · %4 %5 · %6")
        .arg(pack.whiteUsername())
        .arg(pack.whiteRating())
        .arg(pack.result())
        .arg(pack.blackUsername())
        .arg(pack.blackRating())
        .arg(compactDate(pack.eventStartUtc()));
}

QString tabTitle(const AnnotatedReplayPack &pack)
{
    return QStringLiteral("%1 – %2")
        .arg(pack.whiteUsername(), pack.blackUsername());
}

QColor themeAccent(int themeIndex)
{
    switch (themeIndex % 4) {
    case 1:
        return QColor(QStringLiteral("#79a9d1"));
    case 2:
        return QColor(QStringLiteral("#99ad83"));
    case 3:
        return QColor(QStringLiteral("#8cbf68"));
    default:
        return QColor(QStringLiteral("#c28b5c"));
    }
}

QString safeSettingsId(const QString &sourceGameId)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(sourceGameId.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QPair<int, int> lastMoveSquares(
    const AnnotatedReplayPack &pack,
    const ReplaySession &session,
    int variationAnchorPly)
{
    QString uci;
    if (session.inVariation()) {
        const ReplayPreferredVariation *variation = pack.preferredVariation(variationAnchorPly);
        const int localPly = session.currentVariationPly();
        if (variation != nullptr && localPly > 0
            && localPly <= variation->displayedSteps.size()) {
            uci = variation->displayedSteps.at(localPly - 1).uci;
        } else if (variationAnchorPly > 1) {
            uci = pack.moves().at(variationAnchorPly - 2).notation.uci;
        }
    } else if (session.currentMainlinePly() > 0) {
        uci = pack.moves().at(session.currentMainlinePly() - 1).notation.uci;
    }
    const auto move = Move::fromUci(uci);
    return move.has_value() ? QPair<int, int> {move->from, move->to}
                            : QPair<int, int> {-1, -1};
}

} // namespace

GameBoardWindow::GameBoardWindow(
    const AnnotatedReplayPack &pack,
    int identityIndex,
    QWidget *parent)
    : QMainWindow(parent, Qt::Window)
    , m_pack(pack)
    , m_sourceGameId(pack.sourceGameId())
    , m_identityLabel(QString(QChar(QLatin1Char('A' + (identityIndex % 26)))))
    , m_boardWidget(nullptr)
    , m_tabs(new QTabWidget(this))
    , m_boardPaletteCombo(nullptr)
    , m_pieceStyleCombo(nullptr)
    , m_positionLabel(nullptr)
    , m_transportControls(nullptr)
    , m_notesEdit(nullptr)
    , m_pinMoveButton(nullptr)
    , m_copyContextButton(nullptr)
    , m_notesSaveTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("floatingGameBoardWindow"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(QStringLiteral("%1 · %2").arg(m_identityLabel, gameTitle(pack)));
    setStyleSheet(parlawl::review_ui::studyWindowStyleSheet());
    resize(700, 790);

    m_tabs->setObjectName(QStringLiteral("boardWorkspaceTabs"));
    auto *boardPage = new QWidget(m_tabs);
    auto *boardLayout = new QVBoxLayout(boardPage);
    boardLayout->setContentsMargins(10, 10, 10, 10);
    boardLayout->setSpacing(8);

    auto *identityRow = new QHBoxLayout();
    auto *identity = new QLabel(
        QStringLiteral("Game %1").arg(m_identityLabel), boardPage);
    identity->setObjectName(QStringLiteral("boardGameIdentity"));
    identity->setToolTip(gameTitle(pack));
    identity->setAccessibleDescription(gameTitle(pack));
    QFont identityFont = identity->font();
    identityFont.setBold(true);
    identity->setFont(identityFont);
    m_boardPaletteCombo = new QComboBox(boardPage);
    m_boardPaletteCombo->setObjectName(QStringLiteral("boardPaletteCombo"));
    m_boardPaletteCombo->addItems({QStringLiteral("Walnut"), QStringLiteral("Graphite"),
                                   QStringLiteral("Sage"), QStringLiteral("Tournament")});
    m_pieceStyleCombo = new QComboBox(boardPage);
    m_pieceStyleCombo->setObjectName(QStringLiteral("pieceStyleCombo"));
    m_pieceStyleCombo->addItems({QStringLiteral("Classic"), QStringLiteral("Outlined"),
                                 QStringLiteral("Monochrome")});
    identityRow->addWidget(identity, 1);
    identityRow->addWidget(new QLabel(QStringLiteral("Board"), boardPage));
    identityRow->addWidget(m_boardPaletteCombo);
    identityRow->addWidget(new QLabel(QStringLiteral("Pieces"), boardPage));
    identityRow->addWidget(m_pieceStyleCombo);
    boardLayout->addLayout(identityRow);

    m_boardWidget = new BoardWidget(boardPage);
    m_boardWidget->setObjectName(QStringLiteral("floatingBoardWidget"));
    m_boardWidget->setMinimumSize(520, 520);
    boardLayout->addWidget(m_boardWidget, 1);

    auto *transportRow = new QHBoxLayout();
    m_positionLabel = new QLabel(QStringLiteral("Start position"), boardPage);
    m_positionLabel->setObjectName(QStringLiteral("floatingBoardPositionLabel"));
    m_transportControls = new TransportControls(boardPage);
    m_transportControls->setReplayMode(true);
    auto *showReview = new QPushButton(QStringLiteral("Review"), boardPage);
    showReview->setObjectName(QStringLiteral("showReviewHubButton"));
    showReview->setToolTip(QStringLiteral("Show this game's notation tab"));
    transportRow->addWidget(m_positionLabel, 1);
    transportRow->addWidget(m_transportControls);
    transportRow->addWidget(showReview);
    boardLayout->addLayout(transportRow);

    auto *notesPage = new QWidget(m_tabs);
    auto *notesLayout = new QVBoxLayout(notesPage);
    notesLayout->setContentsMargins(10, 10, 10, 10);
    notesLayout->setSpacing(8);
    auto *notesBoundary = new QLabel(
        QStringLiteral("Local notes · separate from retained game evidence"),
        notesPage);
    notesBoundary->setObjectName(QStringLiteral("notesBoundaryLabel"));
    notesLayout->addWidget(notesBoundary);
    auto *notesActions = new QHBoxLayout();
    m_pinMoveButton = new QPushButton(QStringLiteral("Pin current move"), notesPage);
    m_pinMoveButton->setObjectName(QStringLiteral("pinCurrentMoveButton"));
    m_copyContextButton = new QPushButton(QStringLiteral("Copy study context"), notesPage);
    m_copyContextButton->setObjectName(QStringLiteral("copyStudyContextButton"));
    notesActions->addWidget(m_pinMoveButton);
    notesActions->addWidget(m_copyContextButton);
    notesActions->addStretch(1);
    notesLayout->addLayout(notesActions);
    m_notesEdit = new QPlainTextEdit(notesPage);
    m_notesEdit->setObjectName(QStringLiteral("gameNotesEditor"));
    m_notesEdit->setPlaceholderText(
        QStringLiteral("Write observations, comparison questions, or discussion prompts here…"));
    notesLayout->addWidget(m_notesEdit, 1);

    m_tabs->addTab(boardPage, QStringLiteral("Board"));
    m_tabs->addTab(notesPage, QStringLiteral("Notes"));
    setCentralWidget(m_tabs);

    QSettings settings;
    const int savedPalette = settings.value(
        settingsRoot() + QStringLiteral("/board_palette"), identityIndex % 4).toInt();
    const int savedPieces = settings.value(
        settingsRoot() + QStringLiteral("/piece_style"), identityIndex % 3).toInt();
    m_boardPaletteCombo->setCurrentIndex(std::clamp(savedPalette, 0, 3));
    m_pieceStyleCombo->setCurrentIndex(std::clamp(savedPieces, 0, 2));
    m_notesEdit->setPlainText(
        settings.value(settingsRoot() + QStringLiteral("/notes")).toString());
    const QByteArray savedGeometry = settings.value(
        settingsRoot() + QStringLiteral("/window_geometry")).toByteArray();
    if (!savedGeometry.isEmpty()) {
        restoreGeometry(savedGeometry);
    } else if (QScreen *screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        const QRect available = screen->availableGeometry();
        const int stagger = identityIndex * 38;
        const int x = std::clamp(
            available.left() + 700 + stagger,
            available.left(),
            std::max(available.left(), available.right() - width()));
        const int y = std::clamp(
            available.top() + 70 + stagger,
            available.top(),
            std::max(available.top(), available.bottom() - height()));
        move(x, y);
    }
    applyAppearance();

    m_notesSaveTimer->setSingleShot(true);
    m_notesSaveTimer->setInterval(450);
    connect(m_notesSaveTimer, &QTimer::timeout, this, &GameBoardWindow::saveNotesNow);
    connect(m_notesEdit, &QPlainTextEdit::textChanged, m_notesSaveTimer,
            qOverload<>(&QTimer::start));
    connect(m_boardPaletteCombo, &QComboBox::currentIndexChanged, this, [this]() {
        applyAppearance();
        saveNotesNow();
    });
    connect(m_pieceStyleCombo, &QComboBox::currentIndexChanged, this, [this]() {
        applyAppearance();
        saveNotesNow();
    });
    connect(m_boardWidget, &BoardWidget::scrubRequested, this, [this](int delta) {
        emit scrubRequested(m_sourceGameId, delta);
    });
    connect(m_transportControls, &TransportControls::previousRequested, this, [this]() {
        emit previousRequested(m_sourceGameId);
    });
    connect(m_transportControls, &TransportControls::nextRequested, this, [this]() {
        emit nextRequested(m_sourceGameId);
    });
    connect(m_transportControls, &TransportControls::retryRequested, this, [this]() {
        emit playPauseRequested(m_sourceGameId);
    });
    connect(showReview, &QPushButton::clicked, this, [this]() {
        emit reviewTabRequested(m_sourceGameId);
    });
    connect(m_pinMoveButton, &QPushButton::clicked, this, [this]() {
        const QString marker = QStringLiteral("[%1]\n").arg(selectedMoveLabel());
        if (!m_notesEdit->toPlainText().isEmpty()
            && !m_notesEdit->toPlainText().endsWith(QLatin1Char('\n'))) {
            m_notesEdit->appendPlainText(QString());
        }
        m_notesEdit->insertPlainText(marker);
        m_notesEdit->setFocus();
    });
    connect(m_copyContextButton, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(studyContextText());
    });
}

GameBoardWindow::~GameBoardWindow()
{
    saveNotesNow();
}

QString GameBoardWindow::notesText() const
{
    return m_notesEdit->toPlainText();
}

void GameBoardWindow::setReplayState(
    const ReplaySession &session,
    int variationAnchorPly)
{
    if (!session.hasReplay()) {
        return;
    }
    m_currentPly = session.currentMainlinePly();
    const PieceColor viewColor = m_pack.viewedPlayerColor() == QStringLiteral("black")
        ? PieceColor::Black : PieceColor::White;
    m_boardWidget->setPosition(
        session.currentPosition(),
        viewColor,
        -1,
        {},
        lastMoveSquares(m_pack, session, variationAnchorPly),
        SessionStatus::Ready,
        true,
        false);
    m_positionLabel->setText(selectedMoveLabel());
}

bool GameBoardWindow::showCoachPreview(
    const QString &beforeFen,
    const QString &rootMoveUci,
    const QString &positionLabel)
{
    QString errorMessage;
    const auto position = ChessPosition::fromFen(beforeFen, &errorMessage);
    const auto move = Move::fromUci(rootMoveUci);
    if (!position.has_value() || !move.has_value()) {
        return false;
    }
    const PieceColor viewColor = m_pack.viewedPlayerColor() == QStringLiteral("black")
        ? PieceColor::Black : PieceColor::White;
    m_boardWidget->setPosition(
        *position,
        viewColor,
        -1,
        {},
        {move->from, move->to},
        SessionStatus::Ready,
        true,
        false);
    m_positionLabel->setText(positionLabel);
    return true;
}

void GameBoardWindow::setTransportState(
    bool canStepBackward,
    bool canStepForward,
    bool playing)
{
    m_transportControls->setEnabledState(
        true, canStepBackward, canStepForward, false, false);
    m_transportControls->setPlaying(playing);
}

void GameBoardWindow::saveNotesNow()
{
    if (m_notesSaveTimer->isActive()) {
        m_notesSaveTimer->stop();
    }
    QSettings settings;
    settings.setValue(settingsRoot() + QStringLiteral("/notes"), notesText());
    settings.setValue(
        settingsRoot() + QStringLiteral("/board_palette"),
        m_boardPaletteCombo->currentIndex());
    settings.setValue(
        settingsRoot() + QStringLiteral("/piece_style"),
        m_pieceStyleCombo->currentIndex());
    settings.setValue(
        settingsRoot() + QStringLiteral("/window_geometry"), saveGeometry());
    settings.sync();
}

bool GameBoardWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WindowActivate) {
        emit reviewTabRequested(m_sourceGameId);
    }
    return QMainWindow::event(event);
}

void GameBoardWindow::closeEvent(QCloseEvent *event)
{
    saveNotesNow();
    event->accept();
}

void GameBoardWindow::applyAppearance()
{
    m_boardWidget->setAppearance(
        static_cast<BoardWidget::BoardPalette>(m_boardPaletteCombo->currentIndex()),
        static_cast<BoardWidget::PieceStyle>(m_pieceStyleCombo->currentIndex()));
}

QString GameBoardWindow::settingsRoot() const
{
    return QStringLiteral("game_study/%1").arg(safeSettingsId(m_sourceGameId));
}

QString GameBoardWindow::selectedMoveLabel() const
{
    if (m_currentPly <= 0 || m_currentPly > m_pack.moves().size()) {
        return QStringLiteral("Start position");
    }
    const ReplayMove &move = m_pack.moves().at(m_currentPly - 1);
    const int moveNumber = (move.ply + 1) / 2;
    return move.ply % 2 == 1
        ? QStringLiteral("%1. %2").arg(moveNumber).arg(move.notation.san)
        : QStringLiteral("%1… %2").arg(moveNumber).arg(move.notation.san);
}

QString GameBoardWindow::studyContextText() const
{
    QStringList lines {
        QStringLiteral("ParlAWL study context"),
        gameTitle(m_pack),
        QStringLiteral("Source game: %1").arg(m_sourceGameId),
        QStringLiteral("Selected: %1").arg(selectedMoveLabel()),
    };
    if (!notesText().trimmed().isEmpty()) {
        lines << QString() << QStringLiteral("User notes") << notesText();
    }
    lines << QString()
          << QStringLiteral("User notes are separate from retained engine evidence. No data was uploaded by this copy action.");
    return lines.join(QLatin1Char('\n'));
}

GameReviewHubWindow::GameReviewHubWindow(QWidget *parent)
    : QMainWindow(parent, Qt::Window)
    , m_gameTabs(new QTabWidget(this))
{
    setObjectName(QStringLiteral("gameReviewHubWindow"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(QStringLiteral("Game Review"));
    setStyleSheet(parlawl::review_ui::studyWindowStyleSheet());
    resize(660, 860);
    if (QScreen *screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        const QRect available = screen->availableGeometry();
        move(available.left() + 24, available.top() + 64);
    }
    m_gameTabs->setObjectName(QStringLiteral("reviewHubGameTabs"));
    m_gameTabs->setTabsClosable(true);
    m_gameTabs->setMovable(true);
    m_gameTabs->setDocumentMode(true);
    setCentralWidget(m_gameTabs);

    connect(m_gameTabs, &QTabWidget::currentChanged, this, [this](int) {
        updateWindowTitleForCurrentTab();
        activateBoardForCurrentTab();
    });
    connect(m_gameTabs->tabBar(), &QTabBar::tabBarClicked, this, [this](int tabIndex) {
        if (tabIndex == m_gameTabs->currentIndex()) {
            activateBoardForCurrentTab();
        }
    });
    connect(m_gameTabs, &QTabWidget::tabCloseRequested,
            this, &GameReviewHubWindow::closeGameAt);
}

GameReviewHubWindow::~GameReviewHubWindow()
{
    const auto games = m_games.values();
    for (OpenGame *openGame : games) {
        if (openGame->boardWindow != nullptr) {
            openGame->boardWindow->saveNotesNow();
            openGame->boardWindow->close();
            delete openGame->boardWindow;
        }
        delete openGame;
    }
    m_games.clear();
}

bool GameReviewHubWindow::openGame(
    const AnnotatedReplayPack &pack,
    QString *errorMessage,
    const GameReviewDisplay *display)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (!pack.isMechanicalGameBreakdown() || pack.sourceGameId().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Review Hub requires a mechanical game breakdown with an exact game ID.");
        }
        return false;
    }
    if (display != nullptr) {
        if (display->sourceGameId != pack.sourceGameId()
            || display->moves.size() != pack.moves().size()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral(
                    "Coach Review sidecar does not match the exact replay game.");
            }
            return false;
        }
        for (int index = 0; index < display->moves.size(); ++index) {
            if (display->moves.at(index).san != pack.moves().at(index).notation.san
                || display->moves.at(index).uci != pack.moves().at(index).notation.uci) {
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral(
                        "Coach Review sidecar move sequence differs from the exact replay.");
                }
                return false;
            }
        }
    }
    if (m_games.contains(pack.sourceGameId())) {
        activateGame(pack.sourceGameId());
        return true;
    }

    auto *openGame = new OpenGame;
    openGame->pack = pack;
    if (display != nullptr) {
        openGame->display = *display;
    }
    openGame->session.load(openGame->pack);
    openGame->reviewPanel = new GameReviewPanel(m_gameTabs);
    openGame->reviewPanel->setObjectName(QStringLiteral("reviewHubGamePage"));
    openGame->boardWindow = new GameBoardWindow(
        openGame->pack, m_games.size(), this);
    openGame->playbackTimer = new QTimer(this);
    openGame->playbackTimer->setInterval(650);
    const QString sourceGameId = pack.sourceGameId();
    m_games.insert(sourceGameId, openGame);

    const int tabIndex = m_gameTabs->addTab(openGame->reviewPanel, tabTitle(pack));
    m_gameTabs->setTabToolTip(tabIndex, gameTitle(pack));
    m_gameTabs->tabBar()->setTabTextColor(tabIndex, themeAccent(tabIndex));

    connect(openGame->reviewPanel, &GameReviewPanel::replayPlyRequested,
            this, [this, sourceGameId](int ply) { seekGame(sourceGameId, ply); });
    connect(openGame->reviewPanel, &GameReviewPanel::backToPlayerStatisticsRequested,
            this, &GameReviewHubWindow::playerExplorerRequested);
    connect(openGame->reviewPanel, &GameReviewPanel::criticalMomentRequested,
            this, [this, sourceGameId](int ply, const QString &beforeFen,
                    const QString &playedUci, const QString &positionLabel) {
                if (seekGame(sourceGameId, ply)) {
                    if (OpenGame *selected = game(sourceGameId); selected != nullptr) {
                        selected->boardWindow->showCoachPreview(
                            beforeFen, playedUci, positionLabel);
                    }
                }
            });
    connect(openGame->reviewPanel, &GameReviewPanel::coachLinePreviewRequested,
            this, [this, sourceGameId](const QString &beforeFen,
                    const QString &rootUci, const QString &positionLabel) {
                if (OpenGame *selected = game(sourceGameId); selected != nullptr) {
                    selected->boardWindow->showCoachPreview(
                        beforeFen, rootUci, positionLabel);
                }
            });
    connect(openGame->boardWindow, &GameBoardWindow::reviewTabRequested,
            this, [this](const QString &id) { activateGame(id); });
    connect(openGame->boardWindow, &GameBoardWindow::previousRequested,
            this, [this](const QString &id) { stepGame(id, -1); });
    connect(openGame->boardWindow, &GameBoardWindow::nextRequested,
            this, [this](const QString &id) { stepGame(id, 1); });
    connect(openGame->boardWindow, &GameBoardWindow::playPauseRequested,
            this, &GameReviewHubWindow::togglePlayback);
    connect(openGame->boardWindow, &GameBoardWindow::scrubRequested,
            this, [this](const QString &id, int delta) { stepGame(id, delta); });
    connect(openGame->playbackTimer, &QTimer::timeout,
            this, [this, sourceGameId]() { stepGame(sourceGameId, 1); });

    refreshGame(openGame);
    showNormal();
    raise();
    activateWindow();
    m_gameTabs->setCurrentIndex(tabIndex);
    openGame->boardWindow->showNormal();
    openGame->boardWindow->raise();
    openGame->boardWindow->activateWindow();
    return true;
}

int GameReviewHubWindow::openGameCount() const
{
    return m_games.size();
}

QString GameReviewHubWindow::activeGameId() const
{
    OpenGame *openGame = currentGame();
    return openGame == nullptr ? QString() : openGame->pack.sourceGameId();
}

GameBoardWindow *GameReviewHubWindow::boardWindowForGame(
    const QString &sourceGameId) const
{
    OpenGame *openGame = game(sourceGameId);
    return openGame == nullptr ? nullptr : openGame->boardWindow;
}

GameReviewPanel *GameReviewHubWindow::reviewPanelForGame(
    const QString &sourceGameId) const
{
    OpenGame *openGame = game(sourceGameId);
    return openGame == nullptr ? nullptr : openGame->reviewPanel;
}

bool GameReviewHubWindow::activateGame(const QString &sourceGameId)
{
    OpenGame *openGame = game(sourceGameId);
    if (openGame == nullptr) {
        return false;
    }
    const int index = m_gameTabs->indexOf(openGame->reviewPanel);
    if (index < 0) {
        return false;
    }
    showNormal();
    raise();
    if (m_gameTabs->currentIndex() != index) {
        m_gameTabs->setCurrentIndex(index);
    }
    updateWindowTitleForCurrentTab();
    return true;
}

bool GameReviewHubWindow::seekGame(const QString &sourceGameId, int ply)
{
    OpenGame *openGame = game(sourceGameId);
    if (openGame == nullptr) {
        return false;
    }
    openGame->playing = false;
    openGame->playbackTimer->stop();
    if (openGame->session.inVariation()) {
        QString error;
        if (!openGame->session.exitVariation(&error)) {
            return false;
        }
        openGame->variationAnchorPly = 0;
    }
    if (!openGame->session.seekMainlinePly(ply)) {
        return false;
    }
    refreshGame(openGame);
    return true;
}

void GameReviewHubWindow::surfaceActiveGame()
{
    showNormal();
    raise();
    activateWindow();
    if (OpenGame *openGame = currentGame(); openGame != nullptr) {
        openGame->boardWindow->showNormal();
        openGame->boardWindow->raise();
        openGame->boardWindow->activateWindow();
    }
}

void GameReviewHubWindow::closeEvent(QCloseEvent *event)
{
    for (OpenGame *openGame : m_games) {
        openGame->playing = false;
        openGame->playbackTimer->stop();
        openGame->boardWindow->saveNotesNow();
        openGame->boardWindow->hide();
    }
    QMainWindow::closeEvent(event);
}

GameReviewHubWindow::OpenGame *GameReviewHubWindow::game(
    const QString &sourceGameId) const
{
    return m_games.value(sourceGameId, nullptr);
}

GameReviewHubWindow::OpenGame *GameReviewHubWindow::currentGame() const
{
    QWidget *page = m_gameTabs->currentWidget();
    for (OpenGame *openGame : m_games) {
        if (openGame->reviewPanel == page) {
            return openGame;
        }
    }
    return nullptr;
}

void GameReviewHubWindow::refreshGame(OpenGame *openGame)
{
    if (openGame == nullptr) {
        return;
    }
    openGame->reviewPanel->setReplayState(
        openGame->pack, openGame->session, openGame->variationAnchorPly);
    openGame->reviewPanel->setGameReviewDisplay(
        openGame->display.has_value() ? &*openGame->display : nullptr);
    openGame->boardWindow->setReplayState(
        openGame->session, openGame->variationAnchorPly);
    const bool canBack = openGame->session.inVariation()
        ? openGame->session.currentVariationPly() > 0
        : openGame->session.currentMainlinePly() > 0;
    const bool canForward = openGame->session.inVariation()
        ? false
        : openGame->session.currentMainlinePly() < openGame->pack.moves().size();
    if (!canForward && openGame->playing) {
        openGame->playing = false;
        openGame->playbackTimer->stop();
    }
    openGame->boardWindow->setTransportState(
        canBack, canForward, openGame->playing);
}

void GameReviewHubWindow::stepGame(const QString &sourceGameId, int delta)
{
    OpenGame *openGame = game(sourceGameId);
    if (openGame == nullptr || delta == 0) {
        return;
    }
    bool changed = false;
    const int steps = std::abs(delta);
    for (int index = 0; index < steps; ++index) {
        changed = (delta < 0 ? openGame->session.stepBackward()
                             : openGame->session.stepForward()) || changed;
    }
    if (changed) {
        refreshGame(openGame);
    } else if (openGame->playing) {
        openGame->playing = false;
        openGame->playbackTimer->stop();
        refreshGame(openGame);
    }
}

void GameReviewHubWindow::togglePlayback(const QString &sourceGameId)
{
    OpenGame *openGame = game(sourceGameId);
    if (openGame == nullptr) {
        return;
    }
    if (openGame->playing) {
        openGame->playing = false;
        openGame->playbackTimer->stop();
        refreshGame(openGame);
        return;
    }
    if (!openGame->session.inVariation()
        && openGame->session.currentMainlinePly() >= openGame->pack.moves().size()) {
        openGame->session.seekMainlinePly(0);
    }
    openGame->playing = true;
    openGame->playbackTimer->start();
    refreshGame(openGame);
}

void GameReviewHubWindow::closeGameAt(int tabIndex)
{
    QWidget *page = m_gameTabs->widget(tabIndex);
    OpenGame *target = nullptr;
    QString targetId;
    for (auto iterator = m_games.begin(); iterator != m_games.end(); ++iterator) {
        if (iterator.value()->reviewPanel == page) {
            target = iterator.value();
            targetId = iterator.key();
            break;
        }
    }
    if (target == nullptr) {
        return;
    }
    target->playbackTimer->stop();
    target->boardWindow->saveNotesNow();
    target->boardWindow->close();
    m_gameTabs->removeTab(tabIndex);
    delete target->boardWindow;
    delete target->playbackTimer;
    delete target->reviewPanel;
    m_games.remove(targetId);
    delete target;
}

void GameReviewHubWindow::activateBoardForCurrentTab()
{
    OpenGame *openGame = currentGame();
    if (openGame == nullptr) {
        return;
    }
    openGame->boardWindow->showNormal();
    openGame->boardWindow->raise();
    openGame->boardWindow->activateWindow();
}

void GameReviewHubWindow::updateWindowTitleForCurrentTab()
{
    const OpenGame *openGame = currentGame();
    setWindowTitle(openGame == nullptr
        ? QStringLiteral("Game Review")
        : gameTitle(openGame->pack));
}
