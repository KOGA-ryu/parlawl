#pragma once

// Shared scaffolding for the market tests: a real on-disk SQLite journal, a
// real `MarketAttemptRepository`, and the local task-kinds fixture pair.
// Nothing here stubs the journal, because the guard under test is precisely
// that the reveal asks the journal.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "market_attempt_repository.h"
#include "market_session_controller.h"
#include "migration_runner.h"

namespace parlawl::market_test {

inline QByteArray readMarketFixture(const char *name)
{
    QFile file(QStringLiteral(PARLAWL_TEST_SOURCE_DIR "/tests/fixtures/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// The ParlAWL-local task-kinds pack, NOT the golden pair. These callers need
// one record per task kind, which no Arc-compiled pack can supply: a pack spec
// carries a single task kind, and Arc's compiler refuses `trade_line` outright.
// The Arc-generated golden pair — the cross-language artifact of contract §8 —
// sits beside it as `market_puzzle_pack_v1.*` and is exercised by
// `tests/unit/test_unit_market_puzzle_pack.cpp`.
inline QByteArray visibleMarketFixture()
{
    return readMarketFixture("market_task_kinds_v1.visible.jsonl");
}

inline QByteArray sealedMarketFixture()
{
    return readMarketFixture("market_task_kinds_v1.sealed.jsonl");
}

inline QString opaqueSolverId()
{
    return QStringLiteral("parlawl-solver-v1:")
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

inline QString opaqueSessionId()
{
    return QStringLiteral("parlawl-session-v1:")
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

//! One temporary database with the full migration set applied, including `024`
//! on top of `023`.
class JournalHarness
{
public:
    JournalHarness()
        : m_connectionName(QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        m_database.setDatabaseName(m_directory.filePath(QStringLiteral("market.sqlite")));
        m_open = m_database.open();
        if (m_open) {
            QSqlQuery pragma(m_database);
            pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
            const DatabaseInitializationResult result = MigrationRunner().run(m_database);
            m_open = result.ok;
            m_message = result.message;
        }
    }

    ~JournalHarness()
    {
        m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    JournalHarness(const JournalHarness &) = delete;
    JournalHarness &operator=(const JournalHarness &) = delete;

    [[nodiscard]] bool isOpen() const { return m_open; }
    [[nodiscard]] QString message() const { return m_message; }
    [[nodiscard]] QSqlDatabase database() const { return m_database; }
    //! Canonicalised: on macOS a temporary directory lives under a symlinked
    //! `/var`, and the export deliberately refuses a symlinked path component.
    [[nodiscard]] QString path(const QString &name) const
    {
        return QFileInfo(m_directory.path()).canonicalFilePath() + QLatin1Char('/') + name;
    }
    [[nodiscard]] QString directoryPath() const
    {
        return QFileInfo(m_directory.path()).canonicalFilePath();
    }

private:
    QTemporaryDir m_directory;
    QString m_connectionName;
    QSqlDatabase m_database;
    bool m_open = false;
    QString m_message;
};

//! Answer every ply of the exposed rep with its first legal choice, then close
//! the calibration interval. Returns false with `errorMessage` set on the first
//! refusal, so a test failure names the step that refused.
inline bool completeRepWithFirstLegalAnswers(
    parlawl::market::MarketSessionController *controller,
    const parlawl::market::MarketTaskSpec &spec,
    QString *errorMessage)
{
    while (const parlawl::market::MarketPlySpec *ply = controller->currentPly()) {
        bool ok = false;
        switch (ply->kind) {
        case parlawl::market::PlyKind::Entry:
            ok = controller->answerCategorical(spec.entries.value(0), errorMessage);
            break;
        case parlawl::market::PlyKind::SizeBand:
            ok = controller->answerCategorical(spec.sizeBands.value(1), errorMessage);
            break;
        case parlawl::market::PlyKind::Bracket:
            ok = controller->answerBracket(
                {spec.stopAtrMultiples.value(2), spec.targetAtrMultiples.value(2)}, errorMessage);
            break;
        case parlawl::market::PlyKind::FollowUp:
            ok = controller->answerCategorical(spec.followUpActions.value(2), errorMessage);
            break;
        case parlawl::market::PlyKind::Label:
            ok = controller->answerCategorical(spec.patternLabels.value(2), errorMessage);
            break;
        case parlawl::market::PlyKind::Verdict:
            ok = controller->answerCategorical(QStringLiteral("planted"), errorMessage);
            break;
        case parlawl::market::PlyKind::ArtifactClass:
            ok = controller->answerCategorical(spec.artifactClasses.value(2), errorMessage);
            break;
        case parlawl::market::PlyKind::Confidence:
            ok = controller->answerConfidence(0.8, errorMessage);
            break;
        }
        if (!ok) {
            return false;
        }
    }
    if (!controller->awaitingCalibration()) {
        return true;
    }
    return controller->answerCalibration(-4.0, 6.5, errorMessage);
}

} // namespace parlawl::market_test
