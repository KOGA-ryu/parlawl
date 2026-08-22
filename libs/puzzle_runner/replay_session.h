#pragma once

#include <optional>

#include <QString>

#include "annotated_replay_pack.h"

namespace parlawl::puzzle_runner {

class ReplaySession
{
public:
    void load(const AnnotatedReplayPack &pack);

    [[nodiscard]] bool hasReplay() const { return m_pack.has_value(); }
    bool seekMainlinePly(int ply);
    bool stepBackward();
    bool stepForward();
    bool enterPreferredVariation(int anchorPly, QString *errorMessage = nullptr);
    bool exitVariation(QString *errorMessage = nullptr);

    [[nodiscard]] const ChessPosition &currentPosition() const;
    [[nodiscard]] int currentMainlinePly() const { return m_mainlinePly; }
    [[nodiscard]] int currentVariationPly() const;
    [[nodiscard]] bool inVariation() const { return m_variation.has_value(); }

private:
    struct ActiveVariation {
        int anchorPly = 0;
        int localPly = 0;
        const ReplayPreferredVariation *definition = nullptr;
        QVector<ChessPosition> positions;
    };

    std::optional<AnnotatedReplayPack> m_pack;
    int m_mainlinePly = 0;
    std::optional<ActiveVariation> m_variation;
};

} // namespace parlawl::puzzle_runner
