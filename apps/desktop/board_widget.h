#pragma once

#include <QSet>
#include <QWidget>

#include "chess_position.h"
#include "puzzle_types.h"

class BoardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BoardWidget(QWidget *parent = nullptr);
    QSize sizeHint() const override;

    void setPosition(
        const parlawl::puzzle_runner::ChessPosition &position,
        parlawl::puzzle_runner::PieceColor viewColor,
        int selectedSquare,
        const QSet<int> &legalTargets,
        const QPair<int, int> &lastMoveSquares,
        parlawl::puzzle_runner::SessionStatus status,
        bool reviewMode,
        bool inputEnabled);

signals:
    void squareClicked(int square);
    void scrubRequested(int stepDelta);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    QSize minimumSizeHint() const override;

private:
    int squareAtPoint(const QPoint &point) const;
    int displayFileForBoardFile(int boardFile) const;
    int displayRankForBoardRank(int boardRank) const;
    int boardFileForDisplayFile(int displayFile) const;
    int boardRankForDisplayRank(int displayRank) const;

    parlawl::puzzle_runner::ChessPosition m_position;
    parlawl::puzzle_runner::PieceColor m_viewColor = parlawl::puzzle_runner::PieceColor::White;
    int m_selectedSquare = -1;
    QSet<int> m_legalTargets;
    QPair<int, int> m_lastMoveSquares {-1, -1};
    parlawl::puzzle_runner::SessionStatus m_status = parlawl::puzzle_runner::SessionStatus::Ready;
    bool m_reviewMode = false;
    bool m_inputEnabled = false;
    int m_wheelAccumulatorY = 0;
};
