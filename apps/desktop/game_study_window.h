#pragma once

#include <optional>

#include <QHash>
#include <QMainWindow>

#include "annotated_replay_pack.h"
#include "game_review_display.h"
#include "replay_session.h"

class BoardWidget;
class GameReviewPanel;
class QCloseEvent;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;
class TransportControls;

class GameBoardWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit GameBoardWindow(
        const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
        int identityIndex,
        QWidget *parent = nullptr);
    ~GameBoardWindow() override;

    [[nodiscard]] const QString &sourceGameId() const { return m_sourceGameId; }
    [[nodiscard]] QString notesText() const;
    void setReplayState(
        const parlawl::puzzle_runner::ReplaySession &session,
        int variationAnchorPly);
    bool showCoachPreview(
        const QString &beforeFen,
        const QString &rootMoveUci,
        const QString &positionLabel);
    void setTransportState(bool canStepBackward, bool canStepForward, bool playing);
    void saveNotesNow();

signals:
    void reviewTabRequested(const QString &sourceGameId);
    void previousRequested(const QString &sourceGameId);
    void nextRequested(const QString &sourceGameId);
    void playPauseRequested(const QString &sourceGameId);
    void scrubRequested(const QString &sourceGameId, int stepDelta);

protected:
    bool event(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void applyAppearance();
    QString settingsRoot() const;
    QString selectedMoveLabel() const;
    QString studyContextText() const;

    parlawl::puzzle_runner::AnnotatedReplayPack m_pack;
    QString m_sourceGameId;
    QString m_identityLabel;
    int m_currentPly = 0;
    BoardWidget *m_boardWidget;
    QTabWidget *m_tabs;
    QComboBox *m_boardPaletteCombo;
    QComboBox *m_pieceStyleCombo;
    QLabel *m_positionLabel;
    TransportControls *m_transportControls;
    QPlainTextEdit *m_notesEdit;
    QPushButton *m_pinMoveButton;
    QPushButton *m_copyContextButton;
    QTimer *m_notesSaveTimer;
};

class GameReviewHubWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit GameReviewHubWindow(QWidget *parent = nullptr);
    ~GameReviewHubWindow() override;

    bool openGame(
        const parlawl::puzzle_runner::AnnotatedReplayPack &pack,
        QString *errorMessage = nullptr,
        const parlawl::puzzle_runner::GameReviewDisplay *display = nullptr);
    [[nodiscard]] int openGameCount() const;
    [[nodiscard]] QString activeGameId() const;
    [[nodiscard]] GameBoardWindow *boardWindowForGame(const QString &sourceGameId) const;
    [[nodiscard]] GameReviewPanel *reviewPanelForGame(const QString &sourceGameId) const;
    bool activateGame(const QString &sourceGameId);
    bool seekGame(const QString &sourceGameId, int ply);
    void surfaceActiveGame();
    void hideWorkspace();

signals:
    void playerExplorerRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct OpenGame {
        parlawl::puzzle_runner::AnnotatedReplayPack pack;
        std::optional<parlawl::puzzle_runner::GameReviewDisplay> display;
        parlawl::puzzle_runner::ReplaySession session;
        int variationAnchorPly = 0;
        GameReviewPanel *reviewPanel = nullptr;
        GameBoardWindow *boardWindow = nullptr;
        QTimer *playbackTimer = nullptr;
        bool playing = false;
    };

    OpenGame *game(const QString &sourceGameId) const;
    OpenGame *currentGame() const;
    void refreshGame(OpenGame *openGame);
    void stepGame(const QString &sourceGameId, int delta);
    void togglePlayback(const QString &sourceGameId);
    void closeGameAt(int tabIndex);
    void activateBoardForCurrentTab();
    void updateWindowTitleForCurrentTab();
    void updateGameTabBarVisibility();

    QTabWidget *m_gameTabs;
    QHash<QString, OpenGame *> m_games;
};
