#pragma once

#include <optional>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "chess_position.h"

namespace parlawl::puzzle_runner {

inline constexpr qsizetype kMaximumAnnotatedReplayBytes = 4 * 1024 * 1024;

struct ReplayFact {
    QString factId;
    QString authority;
    QString kind;
    QString status;
    QString summary;
    QVector<QPair<QString, QString>> details;
    QStringList supportingIds;
};

struct ReplayNarration {
    QString narrationId;
    QString templateId;
    QString text;
    QStringList supportingFactIds;
};

struct ReplayNotation {
    QString notationId;
    QString notationVersion;
    QString matchId;
    int ply = 0;
    int moveNumber = 0;
    QString mover;
    QString uci;
    QString san;
    QString piece;
    QString origin;
    QString target;
    std::optional<QString> capturedPiece;
    bool capture = false;
    bool enPassant = false;
    bool check = false;
    bool checkmate = false;
    std::optional<QString> castling;
    std::optional<QString> promotionPiece;
    QString beforeFen;
    QString afterFen;
    QString beforePositionId;
    QString afterPositionId;
    QString beforeReplayStateId;
    QString afterReplayStateId;
};

struct ReplayVariationStep {
    QString stepId;
    QString variationContextId;
    int localPly = 0;
    QString uci;
    QString san;
    QString notationId;
    QString beforeFen;
    QString afterFen;
    QString beforePositionId;
    QString afterPositionId;
    QString beforeReplayStateId;
    QString afterReplayStateId;
    QStringList mechanicalFactIds;
};

struct ReplayPreferredVariation {
    QString variationId;
    QString variationContextId;
    QString sourceGameId;
    QString sourceRunId;
    QString engineConfigId;
    int anchorPly = 0;
    QString checkpointFen;
    QString checkpointPositionId;
    QString checkpointReplayStateId;
    QString reportedBestMoveUci;
    QString reportedPvSha256;
    int reportedPvMoveCount = 0;
    QString rootPositionFactId;
    QVector<ReplayVariationStep> displayedSteps;
    QString rootScoreKind;
    std::optional<qint64> rootCentipawnsWhite;
    std::optional<qint64> rootMateForWhite;
    QVector<int> rootWdlWhite;
    qint64 rootDepth = 0;
    qint64 rootSelectiveDepth = 0;
    qint64 rootNodes = 0;
    QString restorePlayedUci;
    QString restoreAfterFen;
    QString restoreAfterPositionId;
    QString restoreAfterReplayStateId;
    QString presentationLabel;
};

struct PersistedEngineMoveEvidence {
    int expectedBeforeMillionths = 0;
    int expectedAfterMillionths = 0;
    int wdlLossMillionths = 0;
    std::optional<qint64> centipawnLoss;
    bool missedWinningAdvantage = false;
    bool missedForcedMate = false;
    QString severity;
    QString beforeScoreKind;
    std::optional<qint64> beforeCentipawnsWhite;
    std::optional<qint64> beforeMateForWhite;
    QVector<int> beforeWdlWhite;
    std::optional<QString> beforeBestMoveUci;
    int beforeDepth = 0;
    int beforeSelectiveDepth = 0;
    int beforeNodes = 0;
    QString beforePvUci;
    QString afterScoreKind;
    std::optional<qint64> afterCentipawnsWhite;
    std::optional<qint64> afterMateForWhite;
    QVector<int> afterWdlWhite;
};

struct PersistedEngineGameEvidence {
    QString evidenceId;
    QString representativeRunId;
    QString analysisRecordedAtUtc;
    int lineageCount = 0;
    QString engineConfigId;
    QString engineName;
    QString engineAuthor;
    QString engineBinarySha256;
    QString engineAdapterVersion;
    int nodeLimit = 0;
    int hashMebibytes = 0;
    int threads = 0;
    QVector<int> wdlLossThresholds;
    int winningExpectationMillionths = 0;
};

struct ReplayMove {
    int ply = 0;
    QString annotationId;
    QString moveFactId;
    QString labelId;
    QString severity;
    int expectedBeforeMillionths = 0;
    int expectedAfterMillionths = 0;
    int wdlLossMillionths = 0;
    std::optional<qint64> centipawnLoss;
    bool missedWinningAdvantage = false;
    bool missedForcedMate = false;
    QVector<ReplayFact> facts;
    QVector<ReplayNarration> narration;
    QString alternativeStatus;
    std::optional<QString> alternativeUnavailableReason;
    ReplayNotation notation;
    std::optional<ReplayPreferredVariation> preferredVariation;
    std::optional<PersistedEngineMoveEvidence> persistedEngineEvidence;
    QString positionPhase;
    QString forcednessStatus;
    int legalMoveCount = 0;
    std::optional<qint64> decisionStartClockMs;
    std::optional<qint64> clockRemainingAfterMoveMs;
    std::optional<qint64> elapsedMoveMs;
    QString elapsedStatus;
};

struct MechanicalReplayMove {
    int ply = 0;
    QString san;
    QString uci;
    QString positionPhase;
    QString forcednessStatus;
    int legalMoveCount = 0;
    std::optional<qint64> decisionStartClockMs;
    std::optional<qint64> clockRemainingAfterMoveMs;
    std::optional<qint64> elapsedMoveMs;
    QString elapsedStatus;
    std::optional<PersistedEngineMoveEvidence> engineEvidence;
};

struct MechanicalReplayGame {
    QString sourceGameId;
    QString canonicalGameUrl;
    QString eventStartUtc;
    QString whiteUsername;
    QString blackUsername;
    int whiteRating = 0;
    int blackRating = 0;
    QString result;
    QString openingStatus;
    std::optional<QString> openingEco;
    std::optional<QString> openingName;
    std::optional<int> openingLastBookPly;
    QString viewedPlayerColor;
    std::optional<PersistedEngineGameEvidence> engineEvidence;
    QVector<MechanicalReplayMove> moves;
};

class AnnotatedReplayPack
{
public:
    static std::optional<AnnotatedReplayPack> fromJson(
        const QByteArray &rawJson,
        QString *errorMessage = nullptr);
    static std::optional<AnnotatedReplayPack> fromMechanicalGame(
        const MechanicalReplayGame &game,
        QString *errorMessage = nullptr);

    [[nodiscard]] const QString &replayId() const { return m_replayId; }
    [[nodiscard]] const QString &sourceGameId() const { return m_sourceGameId; }
    [[nodiscard]] const QString &sourceRunId() const { return m_sourceRunId; }
    [[nodiscard]] const QString &sourceEngineConfigId() const { return m_sourceEngineConfigId; }
    [[nodiscard]] const QString &sourceEngineName() const { return m_sourceEngineName; }
    [[nodiscard]] const QString &sourceEngineAuthor() const { return m_sourceEngineAuthor; }
    [[nodiscard]] const QString &sourceEngineBinarySha256() const { return m_sourceEngineBinarySha256; }
    [[nodiscard]] qint64 sourceEngineNodeLimit() const { return m_sourceEngineNodeLimit; }
    [[nodiscard]] const QString &whiteUsername() const { return m_whiteUsername; }
    [[nodiscard]] const QString &blackUsername() const { return m_blackUsername; }
    [[nodiscard]] const QString &result() const { return m_result; }
    [[nodiscard]] const QString &openingStatus() const { return m_openingStatus; }
    [[nodiscard]] const std::optional<QString> &openingEco() const { return m_openingEco; }
    [[nodiscard]] const std::optional<QString> &openingName() const { return m_openingName; }
    [[nodiscard]] const std::optional<int> &openingLastBookPly() const { return m_openingLastBookPly; }
    [[nodiscard]] const QString &canonicalGameUrl() const { return m_canonicalGameUrl; }
    [[nodiscard]] const QString &eventStartUtc() const { return m_eventStartUtc; }
    [[nodiscard]] int whiteRating() const { return m_whiteRating; }
    [[nodiscard]] int blackRating() const { return m_blackRating; }
    [[nodiscard]] const QString &viewedPlayerColor() const { return m_viewedPlayerColor; }
    [[nodiscard]] bool isMechanicalGameBreakdown() const { return m_mechanicalGameBreakdown; }
    [[nodiscard]] const std::optional<PersistedEngineGameEvidence> &persistedEngineEvidence() const { return m_persistedEngineEvidence; }
    [[nodiscard]] const QVector<ReplayMove> &moves() const { return m_moves; }
    [[nodiscard]] const QVector<ChessPosition> &mainlinePositions() const { return m_mainlinePositions; }
    [[nodiscard]] constexpr bool legalMechanicsAreVerified() const { return true; }
    [[nodiscard]] constexpr bool suppliedAnnotationsAreVerified() const { return false; }
    [[nodiscard]] const ReplayPreferredVariation *preferredVariation(int anchorPly) const;

private:
    QString m_replayId;
    QString m_sourceGameId;
    QString m_sourceRunId;
    QString m_sourceEngineConfigId;
    QString m_sourceEngineName;
    QString m_sourceEngineAuthor;
    QString m_sourceEngineBinarySha256;
    qint64 m_sourceEngineNodeLimit = 0;
    QString m_whiteUsername;
    QString m_blackUsername;
    QString m_result;
    QString m_openingStatus;
    std::optional<QString> m_openingEco;
    std::optional<QString> m_openingName;
    std::optional<int> m_openingLastBookPly;
    QString m_canonicalGameUrl;
    QString m_eventStartUtc;
    int m_whiteRating = 0;
    int m_blackRating = 0;
    QString m_viewedPlayerColor = QStringLiteral("white");
    bool m_mechanicalGameBreakdown = false;
    std::optional<PersistedEngineGameEvidence> m_persistedEngineEvidence;
    QVector<ReplayMove> m_moves;
    QVector<ChessPosition> m_mainlinePositions;
};

} // namespace parlawl::puzzle_runner
