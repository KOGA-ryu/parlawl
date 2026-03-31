#include "chess_position.h"

#include <algorithm>

#include <QRegularExpression>
#include <QStringList>

namespace parlawl::puzzle_runner {

namespace {

PieceColor pieceColorFromFen(QChar token)
{
    if (!token.isLetter()) {
        return PieceColor::None;
    }
    return token.isUpper() ? PieceColor::White : PieceColor::Black;
}

PieceType pieceTypeFromFen(QChar token)
{
    switch (token.toLower().unicode()) {
    case 'p':
        return PieceType::Pawn;
    case 'n':
        return PieceType::Knight;
    case 'b':
        return PieceType::Bishop;
    case 'r':
        return PieceType::Rook;
    case 'q':
        return PieceType::Queen;
    case 'k':
        return PieceType::King;
    default:
        return PieceType::None;
    }
}

QChar fenCharForPiece(const Piece &piece)
{
    if (piece.isEmpty()) {
        return QChar();
    }

    QChar token;
    switch (piece.type) {
    case PieceType::Pawn:
        token = QLatin1Char('p');
        break;
    case PieceType::Knight:
        token = QLatin1Char('n');
        break;
    case PieceType::Bishop:
        token = QLatin1Char('b');
        break;
    case PieceType::Rook:
        token = QLatin1Char('r');
        break;
    case PieceType::Queen:
        token = QLatin1Char('q');
        break;
    case PieceType::King:
        token = QLatin1Char('k');
        break;
    case PieceType::None:
        return QChar();
    }

    return piece.color == PieceColor::White ? token.toUpper() : token;
}

PieceType promotionTypeFromUci(QChar token)
{
    switch (token.toLower().unicode()) {
    case 'q':
        return PieceType::Queen;
    case 'r':
        return PieceType::Rook;
    case 'b':
        return PieceType::Bishop;
    case 'n':
        return PieceType::Knight;
    default:
        return PieceType::None;
    }
}

QChar promotionChar(PieceType type)
{
    switch (type) {
    case PieceType::Queen:
        return QLatin1Char('q');
    case PieceType::Rook:
        return QLatin1Char('r');
    case PieceType::Bishop:
        return QLatin1Char('b');
    case PieceType::Knight:
        return QLatin1Char('n');
    default:
        return QChar();
    }
}

bool isOnBoard(int file, int rank)
{
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

} // namespace

bool Move::isValid() const
{
    return from >= 0 && from < 64 && to >= 0 && to < 64;
}

QString Move::uci() const
{
    if (!isValid()) {
        return QString();
    }
    QString value = ChessPosition::squareName(from) + ChessPosition::squareName(to);
    if (promotion != PieceType::None) {
        value.append(promotionChar(promotion));
    }
    return value;
}

std::optional<Move> Move::fromUci(const QString &uci)
{
    const QString trimmed = uci.trimmed().toLower();
    static const QRegularExpression pattern(QStringLiteral("^[a-h][1-8][a-h][1-8][qrbn]?$"));
    if (!pattern.match(trimmed).hasMatch()) {
        return std::nullopt;
    }

    Move move;
    move.from = ChessPosition::squareFromName(trimmed.left(2));
    move.to = ChessPosition::squareFromName(trimmed.mid(2, 2));
    if (trimmed.size() == 5) {
        move.promotion = promotionTypeFromUci(trimmed.at(4));
    }
    if (!move.isValid()) {
        return std::nullopt;
    }
    return move;
}

std::optional<ChessPosition> ChessPosition::fromFen(const QString &fen, QString *errorMessage)
{
    const QStringList parts = fen.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() != 6) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("fen must contain 6 space-separated fields");
        }
        return std::nullopt;
    }

    ChessPosition position;
    position.m_board.fill({});

    const QStringList ranks = parts.at(0).split(QLatin1Char('/'));
    if (ranks.size() != 8) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("fen board field must contain 8 ranks");
        }
        return std::nullopt;
    }

    for (int rank = 7; rank >= 0; --rank) {
        int file = 0;
        const QString rankField = ranks.at(7 - rank);
        for (const QChar token : rankField) {
            if (token.isDigit()) {
                file += token.digitValue();
                continue;
            }
            const Piece piece {pieceTypeFromFen(token), pieceColorFromFen(token)};
            if (piece.isEmpty() || file >= 8) {
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral("invalid fen board layout");
                }
                return std::nullopt;
            }
            position.m_board[static_cast<size_t>(squareIndex(file, rank))] = piece;
            ++file;
        }
        if (file != 8) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("fen rank does not sum to 8 squares");
            }
            return std::nullopt;
        }
    }

    if (parts.at(1) == QStringLiteral("w")) {
        position.m_sideToMove = PieceColor::White;
    } else if (parts.at(1) == QStringLiteral("b")) {
        position.m_sideToMove = PieceColor::Black;
    } else {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("invalid side-to-move field");
        }
        return std::nullopt;
    }

    const QString castling = parts.at(2);
    position.m_whiteCastleKingSide = castling.contains(QLatin1Char('K'));
    position.m_whiteCastleQueenSide = castling.contains(QLatin1Char('Q'));
    position.m_blackCastleKingSide = castling.contains(QLatin1Char('k'));
    position.m_blackCastleQueenSide = castling.contains(QLatin1Char('q'));

    position.m_enPassantSquare = parts.at(3) == QStringLiteral("-") ? -1 : squareFromName(parts.at(3));
    if (parts.at(3) != QStringLiteral("-") && position.m_enPassantSquare < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("invalid en passant square");
        }
        return std::nullopt;
    }

    bool ok = false;
    position.m_halfmoveClock = parts.at(4).toInt(&ok);
    if (!ok) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("invalid halfmove clock");
        }
        return std::nullopt;
    }
    position.m_fullmoveNumber = parts.at(5).toInt(&ok);
    if (!ok || position.m_fullmoveNumber <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("invalid fullmove number");
        }
        return std::nullopt;
    }

    return position;
}

QString ChessPosition::toFen() const
{
    QStringList fenRanks;
    for (int rank = 7; rank >= 0; --rank) {
        QString rankField;
        int emptyCount = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece piece = pieceAt(squareIndex(file, rank));
            if (piece.isEmpty()) {
                ++emptyCount;
                continue;
            }
            if (emptyCount > 0) {
                rankField.append(QString::number(emptyCount));
                emptyCount = 0;
            }
            rankField.append(fenCharForPiece(piece));
        }
        if (emptyCount > 0) {
            rankField.append(QString::number(emptyCount));
        }
        fenRanks.append(rankField);
    }

    QString castling;
    if (m_whiteCastleKingSide) {
        castling.append(QLatin1Char('K'));
    }
    if (m_whiteCastleQueenSide) {
        castling.append(QLatin1Char('Q'));
    }
    if (m_blackCastleKingSide) {
        castling.append(QLatin1Char('k'));
    }
    if (m_blackCastleQueenSide) {
        castling.append(QLatin1Char('q'));
    }
    if (castling.isEmpty()) {
        castling = QStringLiteral("-");
    }

    return QStringLiteral("%1 %2 %3 %4 %5 %6")
        .arg(fenRanks.join(QLatin1Char('/')),
             m_sideToMove == PieceColor::White ? QStringLiteral("w") : QStringLiteral("b"),
             castling,
             m_enPassantSquare >= 0 ? squareName(m_enPassantSquare) : QStringLiteral("-"),
             QString::number(m_halfmoveClock),
             QString::number(m_fullmoveNumber));
}

Piece ChessPosition::pieceAt(int square) const
{
    if (square < 0 || square >= 64) {
        return {};
    }
    return m_board[static_cast<size_t>(square)];
}

QVector<Move> ChessPosition::legalMoves() const
{
    QVector<Move> result;
    const QVector<Move> candidates = pseudoLegalMoves();
    result.reserve(candidates.size());

    for (const Move &move : candidates) {
        ChessPosition next = *this;
        if (!next.applyUnchecked(move)) {
            continue;
        }
        if (!next.isInCheck(m_sideToMove)) {
            result.append(move);
        }
    }
    return result;
}

bool ChessPosition::isLegalMove(const Move &move) const
{
    const QVector<Move> moves = legalMoves();
    return std::any_of(moves.begin(), moves.end(), [&](const Move &candidate) { return candidate == move; });
}

bool ChessPosition::applyMove(const Move &move)
{
    if (!isLegalMove(move)) {
        return false;
    }
    return applyUnchecked(move);
}

int ChessPosition::selectedKingSquare(PieceColor color) const
{
    for (int square = 0; square < 64; ++square) {
        const Piece piece = pieceAt(square);
        if (piece.type == PieceType::King && piece.color == color) {
            return square;
        }
    }
    return -1;
}

bool ChessPosition::isInCheck(PieceColor color) const
{
    const int kingSquare = selectedKingSquare(color);
    if (kingSquare < 0) {
        return false;
    }
    return isSquareAttacked(kingSquare, opposite(color));
}

int ChessPosition::fileOf(int square)
{
    return square % 8;
}

int ChessPosition::rankOf(int square)
{
    return square / 8;
}

int ChessPosition::squareIndex(int file, int rank)
{
    return rank * 8 + file;
}

QString ChessPosition::squareName(int square)
{
    if (square < 0 || square >= 64) {
        return QString();
    }
    return QStringLiteral("%1%2")
        .arg(QChar(QLatin1Char('a' + fileOf(square))))
        .arg(rankOf(square) + 1);
}

int ChessPosition::squareFromName(const QString &name)
{
    if (name.size() != 2) {
        return -1;
    }
    const int file = name.at(0).toLatin1() - 'a';
    const int rank = name.at(1).digitValue() - 1;
    return isOnBoard(file, rank) ? squareIndex(file, rank) : -1;
}

PieceColor ChessPosition::opposite(PieceColor color)
{
    switch (color) {
    case PieceColor::White:
        return PieceColor::Black;
    case PieceColor::Black:
        return PieceColor::White;
    case PieceColor::None:
        return PieceColor::None;
    }
    return PieceColor::None;
}

QChar ChessPosition::pieceGlyph(const Piece &piece)
{
    if (piece.isEmpty()) {
        return QChar();
    }
    if (piece.color == PieceColor::White) {
        switch (piece.type) {
        case PieceType::Pawn:
            return QChar(0x2659);
        case PieceType::Knight:
            return QChar(0x2658);
        case PieceType::Bishop:
            return QChar(0x2657);
        case PieceType::Rook:
            return QChar(0x2656);
        case PieceType::Queen:
            return QChar(0x2655);
        case PieceType::King:
            return QChar(0x2654);
        case PieceType::None:
            break;
        }
    } else {
        switch (piece.type) {
        case PieceType::Pawn:
            return QChar(0x265F);
        case PieceType::Knight:
            return QChar(0x265E);
        case PieceType::Bishop:
            return QChar(0x265D);
        case PieceType::Rook:
            return QChar(0x265C);
        case PieceType::Queen:
            return QChar(0x265B);
        case PieceType::King:
            return QChar(0x265A);
        case PieceType::None:
            break;
        }
    }
    return QChar();
}

QVector<Move> ChessPosition::pseudoLegalMoves() const
{
    QVector<Move> moves;
    for (int square = 0; square < 64; ++square) {
        const Piece piece = pieceAt(square);
        if (piece.isEmpty() || piece.color != m_sideToMove) {
            continue;
        }
        switch (piece.type) {
        case PieceType::Pawn:
            addPawnMoves(&moves, square, piece);
            break;
        case PieceType::Knight:
            addKnightMoves(&moves, square, piece);
            break;
        case PieceType::Bishop:
            addSlidingMoves(&moves, square, piece, QVector<int>{9, 7, -7, -9});
            break;
        case PieceType::Rook:
            addSlidingMoves(&moves, square, piece, QVector<int>{8, -8, 1, -1});
            break;
        case PieceType::Queen:
            addSlidingMoves(&moves, square, piece, QVector<int>{9, 7, -7, -9, 8, -8, 1, -1});
            break;
        case PieceType::King:
            addKingMoves(&moves, square, piece);
            break;
        case PieceType::None:
            break;
        }
    }
    return moves;
}

bool ChessPosition::isSquareAttacked(int square, PieceColor attacker) const
{
    if (square < 0 || attacker == PieceColor::None) {
        return false;
    }

    const int targetFile = fileOf(square);
    const int targetRank = rankOf(square);
    const int pawnRankOffset = attacker == PieceColor::White ? -1 : 1;
    for (const int fileOffset : {-1, 1}) {
        const int pawnFile = targetFile + fileOffset;
        const int pawnRank = targetRank + pawnRankOffset;
        if (!isOnBoard(pawnFile, pawnRank)) {
            continue;
        }
        const Piece piece = pieceAt(squareIndex(pawnFile, pawnRank));
        if (piece.color == attacker && piece.type == PieceType::Pawn) {
            return true;
        }
    }

    static const std::array<std::pair<int, int>, 8> knightOffsets = {{
        {1, 2}, {2, 1}, {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2},
    }};
    for (const auto &[df, dr] : knightOffsets) {
        const int file = targetFile + df;
        const int rank = targetRank + dr;
        if (!isOnBoard(file, rank)) {
            continue;
        }
        const Piece piece = pieceAt(squareIndex(file, rank));
        if (piece.color == attacker && piece.type == PieceType::Knight) {
            return true;
        }
    }

    const auto attackedBySlider = [&](const QVector<int> &directions, PieceType first, PieceType second) {
        for (const int delta : directions) {
            int current = square + delta;
            while (current >= 0 && current < 64) {
                const int prevFile = fileOf(current - delta);
                const int currentFile = fileOf(current);
                if (std::abs(currentFile - prevFile) > 1 && (delta == 1 || delta == -1 || delta == 9 || delta == -9 || delta == 7 || delta == -7)) {
                    break;
                }
                const Piece piece = pieceAt(current);
                if (!piece.isEmpty()) {
                    if (piece.color == attacker && (piece.type == first || piece.type == second)) {
                        return true;
                    }
                    break;
                }
                current += delta;
            }
        }
        return false;
    };

    if (attackedBySlider(QVector<int>{9, 7, -7, -9}, PieceType::Bishop, PieceType::Queen)) {
        return true;
    }
    if (attackedBySlider(QVector<int>{8, -8, 1, -1}, PieceType::Rook, PieceType::Queen)) {
        return true;
    }

    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) {
                continue;
            }
            const int file = targetFile + df;
            const int rank = targetRank + dr;
            if (!isOnBoard(file, rank)) {
                continue;
            }
            const Piece piece = pieceAt(squareIndex(file, rank));
            if (piece.color == attacker && piece.type == PieceType::King) {
                return true;
            }
        }
    }

    return false;
}

void ChessPosition::addPawnMoves(QVector<Move> *moves, int square, const Piece &piece) const
{
    const int direction = piece.color == PieceColor::White ? 1 : -1;
    const int startRank = piece.color == PieceColor::White ? 1 : 6;
    const int promotionRank = piece.color == PieceColor::White ? 7 : 0;
    const int file = fileOf(square);
    const int rank = rankOf(square);

    const int oneStepRank = rank + direction;
    if (isOnBoard(file, oneStepRank) && pieceAt(squareIndex(file, oneStepRank)).isEmpty()) {
        const int oneStep = squareIndex(file, oneStepRank);
        if (oneStepRank == promotionRank) {
            addPromotionMoves(moves, square, oneStep);
        } else {
            moves->append(Move{square, oneStep, PieceType::None});
        }

        const int twoStepRank = rank + (2 * direction);
        if (rank == startRank && isOnBoard(file, twoStepRank) && pieceAt(squareIndex(file, twoStepRank)).isEmpty()) {
            moves->append(Move{square, squareIndex(file, twoStepRank), PieceType::None});
        }
    }

    for (const int fileOffset : {-1, 1}) {
        const int targetFile = file + fileOffset;
        const int targetRank = rank + direction;
        if (!isOnBoard(targetFile, targetRank)) {
            continue;
        }
        const int targetSquare = squareIndex(targetFile, targetRank);
        const Piece targetPiece = pieceAt(targetSquare);
        if (!targetPiece.isEmpty() && targetPiece.color == opposite(piece.color)) {
            if (targetRank == promotionRank) {
                addPromotionMoves(moves, square, targetSquare);
            } else {
                moves->append(Move{square, targetSquare, PieceType::None});
            }
        } else if (targetSquare == m_enPassantSquare) {
            moves->append(Move{square, targetSquare, PieceType::None});
        }
    }
}

void ChessPosition::addKnightMoves(QVector<Move> *moves, int square, const Piece &piece) const
{
    static const std::array<std::pair<int, int>, 8> offsets = {{
        {1, 2}, {2, 1}, {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2},
    }};
    const int file = fileOf(square);
    const int rank = rankOf(square);
    for (const auto &[df, dr] : offsets) {
        const int targetFile = file + df;
        const int targetRank = rank + dr;
        if (!isOnBoard(targetFile, targetRank)) {
            continue;
        }
        const int targetSquare = squareIndex(targetFile, targetRank);
        const Piece targetPiece = pieceAt(targetSquare);
        if (targetPiece.isEmpty() || targetPiece.color != piece.color) {
            moves->append(Move{square, targetSquare, PieceType::None});
        }
    }
}

void ChessPosition::addSlidingMoves(QVector<Move> *moves, int square, const Piece &piece, const QVector<int> &directions) const
{
    for (const int delta : directions) {
        int current = square + delta;
        while (current >= 0 && current < 64) {
            const int prevFile = fileOf(current - delta);
            const int currentFile = fileOf(current);
            if (std::abs(currentFile - prevFile) > 1 && (delta == 1 || delta == -1 || delta == 9 || delta == -9 || delta == 7 || delta == -7)) {
                break;
            }

            const Piece targetPiece = pieceAt(current);
            if (targetPiece.isEmpty()) {
                moves->append(Move{square, current, PieceType::None});
            } else {
                if (targetPiece.color != piece.color) {
                    moves->append(Move{square, current, PieceType::None});
                }
                break;
            }

            current += delta;
        }
    }
}

void ChessPosition::addKingMoves(QVector<Move> *moves, int square, const Piece &piece) const
{
    const int file = fileOf(square);
    const int rank = rankOf(square);
    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) {
                continue;
            }
            const int targetFile = file + df;
            const int targetRank = rank + dr;
            if (!isOnBoard(targetFile, targetRank)) {
                continue;
            }
            const int targetSquare = squareIndex(targetFile, targetRank);
            const Piece targetPiece = pieceAt(targetSquare);
            if (targetPiece.isEmpty() || targetPiece.color != piece.color) {
                moves->append(Move{square, targetSquare, PieceType::None});
            }
        }
    }

    if (piece.color == PieceColor::White) {
        if (canCastleKingSide(piece.color)) {
            moves->append(Move{square, squareIndex(6, 0), PieceType::None});
        }
        if (canCastleQueenSide(piece.color)) {
            moves->append(Move{square, squareIndex(2, 0), PieceType::None});
        }
    } else if (piece.color == PieceColor::Black) {
        if (canCastleKingSide(piece.color)) {
            moves->append(Move{square, squareIndex(6, 7), PieceType::None});
        }
        if (canCastleQueenSide(piece.color)) {
            moves->append(Move{square, squareIndex(2, 7), PieceType::None});
        }
    }
}

void ChessPosition::addPromotionMoves(QVector<Move> *moves, int from, int to) const
{
    for (const PieceType type : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
        moves->append(Move{from, to, type});
    }
}

bool ChessPosition::applyUnchecked(const Move &move)
{
    if (!move.isValid()) {
        return false;
    }

    Piece movingPiece = pieceAt(move.from);
    if (movingPiece.isEmpty()) {
        return false;
    }

    const Piece capturedPiece = pieceAt(move.to);
    const int fromFile = fileOf(move.from);
    const int toFile = fileOf(move.to);

    m_board[static_cast<size_t>(move.from)] = {};

    if (movingPiece.type == PieceType::Pawn && move.to == m_enPassantSquare && capturedPiece.isEmpty() && fromFile != toFile) {
        const int capturedPawnRank = rankOf(move.to) + (movingPiece.color == PieceColor::White ? -1 : 1);
        m_board[static_cast<size_t>(squareIndex(toFile, capturedPawnRank))] = {};
    }

    if (movingPiece.type == PieceType::King) {
        if (movingPiece.color == PieceColor::White) {
            m_whiteCastleKingSide = false;
            m_whiteCastleQueenSide = false;
        } else {
            m_blackCastleKingSide = false;
            m_blackCastleQueenSide = false;
        }

        if (std::abs(toFile - fromFile) == 2) {
            const bool kingSide = toFile > fromFile;
            const int rookFrom = kingSide ? squareIndex(7, rankOf(move.from)) : squareIndex(0, rankOf(move.from));
            const int rookTo = kingSide ? squareIndex(5, rankOf(move.from)) : squareIndex(3, rankOf(move.from));
            m_board[static_cast<size_t>(rookTo)] = pieceAt(rookFrom);
            m_board[static_cast<size_t>(rookFrom)] = {};
        }
    }

    if (movingPiece.type == PieceType::Rook) {
        if (move.from == squareIndex(0, 0)) {
            m_whiteCastleQueenSide = false;
        } else if (move.from == squareIndex(7, 0)) {
            m_whiteCastleKingSide = false;
        } else if (move.from == squareIndex(0, 7)) {
            m_blackCastleQueenSide = false;
        } else if (move.from == squareIndex(7, 7)) {
            m_blackCastleKingSide = false;
        }
    }

    if (capturedPiece.type == PieceType::Rook) {
        if (move.to == squareIndex(0, 0)) {
            m_whiteCastleQueenSide = false;
        } else if (move.to == squareIndex(7, 0)) {
            m_whiteCastleKingSide = false;
        } else if (move.to == squareIndex(0, 7)) {
            m_blackCastleQueenSide = false;
        } else if (move.to == squareIndex(7, 7)) {
            m_blackCastleKingSide = false;
        }
    }

    if (movingPiece.type == PieceType::Pawn && move.promotion != PieceType::None) {
        movingPiece.type = move.promotion;
    }
    m_board[static_cast<size_t>(move.to)] = movingPiece;

    if (movingPiece.type == PieceType::Pawn && std::abs(rankOf(move.to) - rankOf(move.from)) == 2) {
        m_enPassantSquare = squareIndex(fromFile, (rankOf(move.to) + rankOf(move.from)) / 2);
    } else {
        m_enPassantSquare = -1;
    }

    const bool pawnMove = movingPiece.type == PieceType::Pawn;
    m_halfmoveClock = (pawnMove || !capturedPiece.isEmpty()) ? 0 : (m_halfmoveClock + 1);
    if (m_sideToMove == PieceColor::Black) {
        ++m_fullmoveNumber;
    }
    m_sideToMove = opposite(m_sideToMove);
    return true;
}

bool ChessPosition::canCastleKingSide(PieceColor color) const
{
    if (color == PieceColor::White) {
        if (!m_whiteCastleKingSide || isInCheck(color)) {
            return false;
        }
        if (!pieceAt(squareIndex(5, 0)).isEmpty() || !pieceAt(squareIndex(6, 0)).isEmpty()) {
            return false;
        }
        return !isSquareAttacked(squareIndex(5, 0), PieceColor::Black)
            && !isSquareAttacked(squareIndex(6, 0), PieceColor::Black);
    }
    if (color == PieceColor::Black) {
        if (!m_blackCastleKingSide || isInCheck(color)) {
            return false;
        }
        if (!pieceAt(squareIndex(5, 7)).isEmpty() || !pieceAt(squareIndex(6, 7)).isEmpty()) {
            return false;
        }
        return !isSquareAttacked(squareIndex(5, 7), PieceColor::White)
            && !isSquareAttacked(squareIndex(6, 7), PieceColor::White);
    }
    return false;
}

bool ChessPosition::canCastleQueenSide(PieceColor color) const
{
    if (color == PieceColor::White) {
        if (!m_whiteCastleQueenSide || isInCheck(color)) {
            return false;
        }
        if (!pieceAt(squareIndex(1, 0)).isEmpty()
            || !pieceAt(squareIndex(2, 0)).isEmpty()
            || !pieceAt(squareIndex(3, 0)).isEmpty()) {
            return false;
        }
        return !isSquareAttacked(squareIndex(3, 0), PieceColor::Black)
            && !isSquareAttacked(squareIndex(2, 0), PieceColor::Black);
    }
    if (color == PieceColor::Black) {
        if (!m_blackCastleQueenSide || isInCheck(color)) {
            return false;
        }
        if (!pieceAt(squareIndex(1, 7)).isEmpty()
            || !pieceAt(squareIndex(2, 7)).isEmpty()
            || !pieceAt(squareIndex(3, 7)).isEmpty()) {
            return false;
        }
        return !isSquareAttacked(squareIndex(3, 7), PieceColor::White)
            && !isSquareAttacked(squareIndex(2, 7), PieceColor::White);
    }
    return false;
}

} // namespace parlawl::puzzle_runner
