#pragma once

#include <QString>

struct CriticalMove
{
    QString criticalMoveId;
    QString eventId;
    QString role;
    int ply = 0;
    QString side;
    QString playedMove;
    QString bestMove;
    QString whyCritical;
    bool hasEvalBeforeCp = false;
    int evalBeforeCp = 0;
    bool hasEvalAfterPlayedCp = false;
    int evalAfterPlayedCp = 0;
    bool hasEvalAfterBestCp = false;
    int evalAfterBestCp = 0;
    bool hasEvalDeltaCp = false;
    int evalDeltaCp = 0;
    QString decisiveSwing;
    int analysisDepth = 0;
    QString mateFlag;
    QString criticalReasonType;
    QString criticalReasonSeverity;
    QString criticalReasonCompactSummary;
    QString continuationFormatVersion;
    QString bestContinuationCompact;
    QString playedContinuationCompact;
    QString pvUci;
    QString pvSan;
    QString bestContinuationSummary;
    QString playedContinuationSummary;
    QString onlyMoveStatus;
    bool hasOnlyMoveMarginCp = false;
    int onlyMoveMarginCp = 0;
    QString onlyMoveReasoning;
    QString candidateMovesJson;
    QString candidateRankingType;
    QString candidateRankingSeverity;
    QString candidateRankingCompactSummary;
    QString candidateRankingSummary;
    QString evidenceNoteType;
    QString evidenceNoteSeverity;
    QString evidenceNoteCompact;
    QString structuralLinkFormatVersion;
    QString linkedKingZoneTarget;
    QString linkedVulnerablePieceTarget;
    QString linkedPressureLaneTarget;
    QString linkedDecisiveImbalanceTarget;
    QString linkedPinnedCriticalPiece;
    QString linkedDefenderRemovalExposure;
    QString linkedKingColorComplex;
    QString linkedTargetZoneImbalance;
    QString linkedAttackerCoordination;
    QString linkedDefensiveNetworkFragility;
    QString structuralLinkSummary;
    int strongerAlternativeCount = 0;
};
