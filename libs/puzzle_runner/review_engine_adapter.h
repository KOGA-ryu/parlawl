#pragma once

#include <QString>

namespace parlawl::puzzle_runner {

class ReviewEngineAdapter
{
public:
    virtual ~ReviewEngineAdapter() = default;

    virtual QString adapterName() const = 0;
    virtual bool isAvailable() const = 0;
    virtual QString statusSummary() const = 0;
};

class NullReviewEngineAdapter final : public ReviewEngineAdapter
{
public:
    QString adapterName() const override { return QStringLiteral("stockfish review adapter"); }
    bool isAvailable() const override { return false; }
    QString statusSummary() const override { return QStringLiteral("review mode is planned; no engine is attached to the strict runner yet"); }
};

} // namespace parlawl::puzzle_runner
