#include "board_widget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

using parlawl::puzzle_runner::ChessPosition;

BoardWidget::BoardWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void BoardWidget::setPosition(
    const ChessPosition &position,
    parlawl::puzzle_runner::PieceColor viewColor,
    int selectedSquare,
    const QSet<int> &legalTargets,
    const QPair<int, int> &lastMoveSquares,
    parlawl::puzzle_runner::SessionStatus status,
    bool reviewMode,
    bool inputEnabled)
{
    m_position = position;
    m_viewColor = viewColor;
    m_selectedSquare = selectedSquare;
    m_legalTargets = legalTargets;
    m_lastMoveSquares = lastMoveSquares;
    m_status = status;
    m_reviewMode = reviewMode;
    m_inputEnabled = inputEnabled;
    update();
}

void BoardWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const int boardSize = qMin(width(), height());
    const int squareSize = boardSize / 8;
    const int xOffset = (width() - boardSize) / 2;
    const int yOffset = (height() - boardSize) / 2;

    const QColor lightSquare(240, 217, 181);
    const QColor darkSquare(181, 136, 99);
    const QColor selectedColor(246, 246, 105, 220);
    const QColor targetColor(64, 126, 201, 110);
    const QColor targetRing(32, 82, 157, 210);
    const QColor lastMoveColor(186, 202, 68, 160);

    // Review mode takes precedence over solved/failed coloring because the user is
    // explicitly browsing an older position rather than acting on the latest state.
    QColor frameColor(58, 95, 11);
    if (m_reviewMode) {
        frameColor = QColor(172, 122, 58);
    } else if (m_status == parlawl::puzzle_runner::SessionStatus::Failed) {
        frameColor = QColor(170, 54, 54);
    } else if (m_status == parlawl::puzzle_runner::SessionStatus::Solved) {
        frameColor = QColor(39, 132, 104);
    }

    QFont pieceFont = font();
    pieceFont.setPointSizeF(squareSize * 0.6);
    painter.setFont(pieceFont);

    for (int boardRank = 7; boardRank >= 0; --boardRank) {
        for (int boardFile = 0; boardFile < 8; ++boardFile) {
            const int displayFile = displayFileForBoardFile(boardFile);
            const int displayRank = displayRankForBoardRank(boardRank);
            const int square = ChessPosition::squareIndex(boardFile, boardRank);
            QRect rect(xOffset + (displayFile * squareSize), yOffset + (displayRank * squareSize), squareSize, squareSize);
            const bool isLight = ((boardFile + boardRank) % 2) == 0;
            painter.fillRect(rect, isLight ? lightSquare : darkSquare);
            if (square == m_selectedSquare) {
                painter.fillRect(rect, selectedColor);
            } else if (square == m_lastMoveSquares.first || square == m_lastMoveSquares.second) {
                painter.fillRect(rect, lastMoveColor);
            }
            if (m_legalTargets.contains(square)) {
                painter.fillRect(rect, targetColor);
                painter.setPen(QPen(targetRing, 2));
                painter.drawEllipse(rect.center(), squareSize / 8, squareSize / 8);
            }

            const auto piece = m_position.pieceAt(square);
            const QChar glyph = ChessPosition::pieceGlyph(piece);
            if (!glyph.isNull()) {
                painter.setPen(piece.color == parlawl::puzzle_runner::PieceColor::White ? Qt::white : Qt::black);
                painter.drawText(rect, Qt::AlignCenter, QString(glyph));
            }
        }
    }

    painter.setPen(QPen(frameColor, 4));
    painter.drawRect(xOffset, yOffset, boardSize, boardSize);

    QFont coordFont = font();
    coordFont.setPointSizeF(std::max(9.0, squareSize * 0.12));
    painter.setFont(coordFont);
    painter.setPen(QColor(70, 70, 70));

    for (int displayFile = 0; displayFile < 8; ++displayFile) {
        const int boardFile = boardFileForDisplayFile(displayFile);
        const QString fileLabel(QChar(QLatin1Char('a' + boardFile)));
        const QRect fileRect(xOffset + (displayFile * squareSize), yOffset + boardSize - std::max(18, squareSize / 4), squareSize, std::max(18, squareSize / 4));
        painter.drawText(fileRect.adjusted(4, 0, -4, -2), Qt::AlignRight | Qt::AlignBottom, fileLabel);
    }

    for (int displayRank = 0; displayRank < 8; ++displayRank) {
        const int boardRank = boardRankForDisplayRank(displayRank);
        const QString rankLabel = QString::number(boardRank + 1);
        const QRect rankRect(xOffset + 2, yOffset + (displayRank * squareSize), std::max(18, squareSize / 4), squareSize);
        painter.drawText(rankRect.adjusted(0, 4, 0, -4), Qt::AlignLeft | Qt::AlignTop, rankLabel);
    }
}

void BoardWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_inputEnabled) {
        event->ignore();
        return;
    }

    const int square = squareAtPoint(event->position().toPoint());
    if (square >= 0) {
        emit squareClicked(square);
    }
}

void BoardWidget::wheelEvent(QWheelEvent *event)
{
    if (m_selectedSquare >= 0) {
        event->ignore();
        return;
    }

    const QPoint angleDelta = event->angleDelta();
    const QPoint pixelDelta = event->pixelDelta();
    constexpr int pixelStep = 18;
    int emittedSteps = 0;

    if (angleDelta.y() != 0) {
        emittedSteps = angleDelta.y() > 0 ? -1 : 1;
        m_wheelAccumulatorY = 0;
    } else if (!pixelDelta.isNull()) {
        m_wheelAccumulatorY += pixelDelta.y();
        while (m_wheelAccumulatorY >= pixelStep) {
            --emittedSteps;
            m_wheelAccumulatorY -= pixelStep;
        }
        while (m_wheelAccumulatorY <= -pixelStep) {
            ++emittedSteps;
            m_wheelAccumulatorY += pixelStep;
        }
    } else {
        event->ignore();
        return;
    }

    if (emittedSteps != 0) {
        emit scrubRequested(emittedSteps);
        event->accept();
        return;
    }

    event->accept();
}

QSize BoardWidget::minimumSizeHint() const
{
    return {360, 360};
}

QSize BoardWidget::sizeHint() const
{
    return {520, 520};
}

int BoardWidget::squareAtPoint(const QPoint &point) const
{
    const int boardSize = qMin(width(), height());
    const int squareSize = boardSize / 8;
    const int xOffset = (width() - boardSize) / 2;
    const int yOffset = (height() - boardSize) / 2;
    if (point.x() < xOffset || point.y() < yOffset || point.x() >= xOffset + boardSize || point.y() >= yOffset + boardSize) {
        return -1;
    }
    const int displayFile = (point.x() - xOffset) / squareSize;
    const int displayRank = (point.y() - yOffset) / squareSize;
    return ChessPosition::squareIndex(
        boardFileForDisplayFile(displayFile),
        boardRankForDisplayRank(displayRank));
}

int BoardWidget::displayFileForBoardFile(int boardFile) const
{
    return m_viewColor == parlawl::puzzle_runner::PieceColor::Black ? 7 - boardFile : boardFile;
}

int BoardWidget::displayRankForBoardRank(int boardRank) const
{
    return m_viewColor == parlawl::puzzle_runner::PieceColor::Black ? boardRank : 7 - boardRank;
}

int BoardWidget::boardFileForDisplayFile(int displayFile) const
{
    return m_viewColor == parlawl::puzzle_runner::PieceColor::Black ? 7 - displayFile : displayFile;
}

int BoardWidget::boardRankForDisplayRank(int displayRank) const
{
    return m_viewColor == parlawl::puzzle_runner::PieceColor::Black ? displayRank : 7 - displayRank;
}
