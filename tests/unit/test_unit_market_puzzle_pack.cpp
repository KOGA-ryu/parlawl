#include <QtTest>

#include <QFile>

#include <cmath>

#include "market_hud.h"
#include "market_puzzle_pack.h"
#include "strict_json.h"

using namespace parlawl::market;
using namespace parlawl::strictjson;

namespace {

QByteArray readFixture(const char *name)
{
    QFile file(QStringLiteral(PARLAWL_TEST_SOURCE_DIR "/tests/fixtures/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// Two fixture pairs, and the difference between them is load-bearing.
//
// `market_puzzle_pack_v1.*` is THE golden pair of contract §8: the exact bytes
// Arc's `python/dojo` compiler emits, copied here unchanged, so that the
// digests below are a real Python-encoder / C++-loader agreement and not this
// repo agreeing with itself. Nothing in this file may edit those bytes; if the
// compiler's output changes, Arc regenerates the pair and it is re-copied.
//
// `market_task_kinds_v1.*` is a ParlAWL-local pack, hand-built by
// `tools/generate_market_task_kinds_fixture.py`, carrying one record per task
// kind. It exists because Arc's compiler cannot emit `trade_line` at all (its
// `compile.py` refuses: "trade_line needs a declared rule to replay and this
// compiler has none") and a pack spec carries a single task kind, so no
// Arc-generated pair can exercise the loader's three shapes at once. It is a
// LOADER fixture, not a cross-language one, and it is named so that nobody
// mistakes it for the golden pair again.

QByteArray localVisibleFixture()
{
    return readFixture("market_task_kinds_v1.visible.jsonl");
}

QByteArray localSealedFixture()
{
    return readFixture("market_task_kinds_v1.sealed.jsonl");
}

QByteArray goldenVisibleFixture()
{
    return readFixture("market_puzzle_pack_v1.visible.jsonl");
}

QByteArray goldenSealedFixture()
{
    return readFixture("market_puzzle_pack_v1.sealed.jsonl");
}

QList<QByteArray> splitLines(const QByteArray &raw)
{
    QList<QByteArray> lines;
    for (const QByteArray &line : raw.split('\n')) {
        if (!line.trimmed().isEmpty()) {
            lines.append(line);
        }
    }
    return lines;
}

QByteArray joinLines(const QList<QByteArray> &lines)
{
    QByteArray out;
    for (const QByteArray &line : lines) {
        out.append(line);
        out.append('\n');
    }
    return out;
}

JsonValue parseLine(const QByteArray &line)
{
    JsonValue value;
    StrictJsonParser parser(line);
    QString error;
    if (!parser.parse(&value, &error)) {
        qWarning("fixture line did not parse: %s", qPrintable(error));
    }
    return value;
}

//! Re-emit a mutated record with its `record_id` recomputed, so a test targets
//! the validator it means to target instead of always tripping the identity
//! check first.
QByteArray reseal(JsonValue record, const QString &recordIdPrefix)
{
    JsonValue content = record;
    removeMember(&content, QStringLiteral("record_id"));
    removeMember(&content, QStringLiteral("record_type"));
    removeMember(&content, QStringLiteral("schema"));
    JsonValue *recordId = member(record, QStringLiteral("record_id"));
    recordId->string = semanticId(recordIdPrefix, content);
    return canonicalJson(record);
}

QByteArray resealHeader(JsonValue header)
{
    JsonValue content = header;
    removeMember(&content, QStringLiteral("pack_id"));
    JsonValue *packId = member(header, QStringLiteral("pack_id"));
    packId->string = semanticId(QStringLiteral("market-puzzle-pack-v1"), content);
    return canonicalJson(header);
}

//! Replace visible record `index` (0-based over the body) with a mutated copy.
QByteArray mutatedVisible(int index, const std::function<void(JsonValue *)> &mutate)
{
    QList<QByteArray> lines = splitLines(localVisibleFixture());
    JsonValue record = parseLine(lines.at(index + 1));
    mutate(&record);
    lines[index + 1] = reseal(record, QStringLiteral("market-puzzle-record-v1"));
    return joinLines(lines);
}

QByteArray mutatedHeader(const std::function<void(JsonValue *)> &mutate)
{
    QList<QByteArray> lines = splitLines(localVisibleFixture());
    JsonValue header = parseLine(lines.at(0));
    mutate(&header);
    lines[0] = resealHeader(header);
    return joinLines(lines);
}

int indexOfTaskKind(const QString &taskKind)
{
    const QList<QByteArray> lines = splitLines(localVisibleFixture());
    for (int index = 1; index < lines.size(); ++index) {
        const JsonValue record = parseLine(lines.at(index));
        if (member(record, QStringLiteral("task_kind"))->string == taskKind) {
            return index - 1;
        }
    }
    return -1;
}

} // namespace

class TestUnitMarketPuzzlePack : public QObject
{
    Q_OBJECT

private slots:
    void acceptsTheArcGeneratedGoldenPair();
    void goldenPairIsTheBytesArcCommitted();
    void acceptsTheLocalTaskKindsPair();
    void preservesAbsentBarFieldsAsAbsentRatherThanZero();
    void recomputesVerifiedHudStatsFromTheVisibleBars();
    void refusesSealedFileShortOneRecord();
    void refusesMissingSealedPartnerEntirely();
    void refusesTamperedVerifiedHudValue();
    void refusesUnknownHudStatId();
    void refusesUnknownTheme();
    void refusesThemeThatIsNotKnowableAtT();
    void refusesRatingSeedOutsideTheDeclaredBand();
    void refusesRatingBasisOutsideThePermittedSet();
    void refusesVaryingResponseHorizon();
    void refusesPlyShapeThatDisagreesWithTaskKind();
    void refusesRewrittenBarsThroughTheWindowDigest();
    void refusesNonCanonicalLine();
    void refusesRecursiveDuplicateKeys();
    void refusesRecordsNotSortedByPuzzleId();
    void refusesHeaderCountsThatDisagreeWithWhatWasParsed();
    void refusesTamperedContinuationCommitment();
    void refusesDisclosedCalendar();
    void refusesTamperedRecordIdentity();
    void retainedLineVerificationStandsAlone();
};

// Contract §8, "the one thing that must be shared with Arc", and Arc's
// acceptance item 19. These bytes were produced by Arc's `python/dojo`
// compiler (`build_golden_pack` in `python/tests/test_dojo.py`, which pins the
// compiler identity, build instant, sampling seed and symbol permutation) and
// copied here without a byte changed. This test is therefore the only place
// where the Python encoder and the C++ loader are checked against each other
// on the same bytes, rather than each against itself.
void TestUnitMarketPuzzlePack::acceptsTheArcGeneratedGoldenPair()
{
    QString error;
    auto pack =
        MarketPuzzlePack::fromJsonLines(goldenVisibleFixture(), goldenSealedFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));

    const MarketPackHeader &header = pack->header();
    // Recomputed from the bytes read, per D2: if the Python encoder and this
    // loader disagreed on key ordering or on one float spelling, this is where
    // it would show.
    QCOMPARE(
        header.packId,
        QStringLiteral("market-puzzle-pack-v1:"
                       "8687e641b991cd1a591f7bf380b220e3d5607052702df9c2e591439cb32d7c58"));
    QCOMPARE(
        header.continuationPackId,
        QStringLiteral("market-continuation-pack-v1:"
                       "230cc4933c80fd014a4e0bf917e4ce70e05998c35d3c06211fb8d2d367b97f13"));
    QCOMPARE(header.recordCount, 6);
    QCOMPARE(header.grain, QStringLiteral("1d"));
    QCOMPARE(header.visibleBarCount, 25);
    QCOMPARE(header.continuationBarCount, 6);
    QCOMPARE(header.responseHorizonBars, 3);
    QCOMPARE(header.evidenceGrade, marketPackEvidenceGrade());
    QCOMPARE(header.calendarDisclosed, false);

    QCOMPARE(pack->puzzles().size(), 6);
    for (const MarketPuzzleVisible &puzzle : pack->puzzles()) {
        QCOMPARE(puzzle.taskKind, TaskKind::PatternCall);
        QCOMPARE(puzzle.window.bars.size(), 25);
        QCOMPARE(puzzle.responseHorizonBars, 3);
        QCOMPARE(*puzzle.window.bars.last().close, 100.0);
        QCOMPARE(marketBarsDigest(puzzle.window.bars), puzzle.window.windowDigest);
    }

    // The float spellings that break encoders, carried by the real compiler and
    // read back by this loader as the same doubles.
    QVERIFY(goldenVisibleFixture().contains("1e+20"));
    QVERIFY(goldenVisibleFixture().contains("-0.0"));
    QVERIFY(goldenVisibleFixture().contains("381.0"));
    bool sawLargeExponent = false;
    bool sawIntegralSpelling = false;
    bool sawNegativeZero = false;
    for (const MarketPuzzleVisible &puzzle : pack->puzzles()) {
        for (const MarketHudStat &stat : puzzle.hud) {
            if (stat.statId == QStringLiteral("extreme_return_z")) {
                sawLargeExponent = sawLargeExponent || stat.value == 1e20;
                sawIntegralSpelling = sawIntegralSpelling || stat.value == 381.0;
            }
            // Negative zero compares equal to zero, so the sign bit is the only
            // witness that the loader did not flatten it.
            if (stat.value == 0.0 && std::signbit(stat.value)) {
                sawNegativeZero = true;
            }
        }
    }
    QVERIFY2(sawLargeExponent, "the loader lost the 1e+20 HUD value");
    QVERIFY2(sawIntegralSpelling, "the loader lost the 381.0 HUD value");
    QVERIFY2(sawNegativeZero, "the loader flattened -0.0 to 0.0");

    auto vault = pack->takeVault();
    QVERIFY(vault != nullptr);
    QCOMPARE(vault->size(), 6);
    QCOMPARE(vault->continuationPackId(), header.continuationPackId);
}

// The byte-identity half of §8, which loading alone cannot check: a copy that
// drifted from Arc's would still load. These two digests are pinned in BOTH
// repos — here, and in Arc's `test_the_golden_fixture_matches_the_digest_
// parlawl_pins`. Regenerating the pair in Arc must fail this test until the
// bytes are re-copied and both constants are updated together, which is what
// makes "committed in both repos and byte-identical" enforceable rather than
// aspirational.
void TestUnitMarketPuzzlePack::goldenPairIsTheBytesArcCommitted()
{
    QVERIFY2(!goldenVisibleFixture().isEmpty(), "golden visible fixture is missing");
    QVERIFY2(!goldenSealedFixture().isEmpty(), "golden sealed fixture is missing");
    QCOMPARE(
        sha256Hex(goldenVisibleFixture()),
        QStringLiteral("b7a47c33a4d54a23f6fc467f01b43bc9e8ec94aa5fed25137e03360264ee2bda"));
    QCOMPARE(
        sha256Hex(goldenSealedFixture()),
        QStringLiteral("d2ec576f338ad185f5d1568597ff489a288d0976bc1373d0718579b9eb090b06"));
}

void TestUnitMarketPuzzlePack::acceptsTheLocalTaskKindsPair()
{
    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(localVisibleFixture(), localSealedFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    QCOMPARE(pack->puzzles().size(), 3);

    const MarketPackHeader &header = pack->header();
    QCOMPARE(
        header.packId,
        QStringLiteral("market-puzzle-pack-v1:"
                       "95c7cd6c0efd3f2e790e6d3b3b560a63182c76f77ebaa963eaeb9b07cb9dd51a"));
    QCOMPARE(
        header.continuationPackId,
        QStringLiteral("market-continuation-pack-v1:"
                       "d18108016f2350ba86549493fb0a99754dc5c83fc38318953e84b92bec169340"));
    QCOMPARE(header.recordCount, 3);
    QCOMPARE(header.grain, QStringLiteral("1d"));
    QCOMPARE(header.visibleBarCount, 24);
    QCOMPARE(header.continuationBarCount, 8);
    QCOMPARE(header.responseHorizonBars, 5);
    QCOMPARE(header.evidenceGrade, marketPackEvidenceGrade());
    QCOMPARE(header.calendarDisclosed, false);
    QCOMPARE(header.sessionBreaksDisclosed, true);
    QCOMPARE(header.source.scanManifestId, QStringLiteral("20260821T041500Z-attention_v1-42e15520"));

    // Sorted by puzzle_id ascending: a hash order carries no information about
    // ticker, date, outcome or planted-ness.
    QCOMPARE(
        pack->puzzles().at(0).puzzleId,
        QStringLiteral("market-puzzle-v1:"
                       "60171bf5345645398711d5278bb087b6c3abd2e736e1fe971dfba4ccca19cba4"));
    QCOMPARE(pack->puzzles().at(0).taskKind, TaskKind::AnomalyFlag);
    QCOMPARE(pack->puzzles().at(2).taskKind, TaskKind::TradeLine);
    QVERIFY(pack->puzzles().at(0).puzzleId < pack->puzzles().at(1).puzzleId);
    QVERIFY(pack->puzzles().at(1).puzzleId < pack->puzzles().at(2).puzzleId);

    // The sealed half went to the vault and nowhere else.
    auto vault = pack->takeVault();
    QVERIFY(vault != nullptr);
    QCOMPARE(vault->size(), 3);
    QVERIFY(vault->holds(pack->puzzles().at(0).puzzleId));
    QCOMPARE(vault->continuationPackId(), header.continuationPackId);

    for (const MarketPuzzleVisible &puzzle : pack->puzzles()) {
        QCOMPARE(puzzle.window.bars.size(), 24);
        QCOMPARE(puzzle.window.sessionBreakAfter.size(), 24);
        QCOMPARE(puzzle.responseHorizonBars, 5);
        QVERIFY(puzzle.displaySymbol.startsWith(QStringLiteral("SYM-")));
        QCOMPARE(*puzzle.window.bars.last().close, 100.0);
        QVERIFY(!puzzle.canonicalLine.isEmpty());
        QCOMPARE(marketBarsDigest(puzzle.window.bars), puzzle.window.windowDigest);
    }
}

void TestUnitMarketPuzzlePack::preservesAbsentBarFieldsAsAbsentRatherThanZero()
{
    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(localVisibleFixture(), localSealedFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    const MarketBar &holed = pack->puzzles().first().window.bars.at(3);
    QVERIFY(holed.hasOhlc());
    QVERIFY(!holed.volume.has_value());
    QVERIFY(!holed.tradeCount.has_value());
}

void TestUnitMarketPuzzlePack::recomputesVerifiedHudStatsFromTheVisibleBars()
{
    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(localVisibleFixture(), localSealedFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    const MarketPuzzleVisible &puzzle = pack->puzzles().first();
    for (const QString &statId : verifiedHudStatIds()) {
        const auto recomputed = computeVerifiedHudStat(statId, puzzle.window.bars);
        QVERIFY2(recomputed.has_value(), qPrintable(statId));
        bool found = false;
        for (const MarketHudStat &stat : puzzle.hud) {
            if (stat.statId == statId) {
                found = true;
                QVERIFY(std::abs(stat.value - *recomputed) <= kHudVerificationTolerance);
            }
        }
        QVERIFY2(found, qPrintable(statId));
    }
}

void TestUnitMarketPuzzlePack::refusesSealedFileShortOneRecord()
{
    QList<QByteArray> sealed = splitLines(localSealedFixture());
    sealed.removeLast();
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(localVisibleFixture(), joinLines(sealed), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("atomically")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesMissingSealedPartnerEntirely()
{
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(localVisibleFixture(), QByteArray(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY(!error.isEmpty());
}

void TestUnitMarketPuzzlePack::refusesTamperedVerifiedHudValue()
{
    const QByteArray mutated = mutatedVisible(0, [](JsonValue *record) {
        JsonValue *hud = member(*record, QStringLiteral("hud"));
        for (JsonValue &entry : hud->array) {
            if (member(entry, QStringLiteral("stat_id"))->string == QStringLiteral("atr_pct_20")) {
                member(entry, QStringLiteral("value"))->number += 0.25;
            }
        }
    });
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(mutated, localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("own derivation")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesUnknownHudStatId()
{
    const QByteArray mutated = mutatedVisible(0, [](JsonValue *record) {
        JsonValue *hud = member(*record, QStringLiteral("hud"));
        member(hud->array[4], QStringLiteral("stat_id"))->string = QStringLiteral("insider_score");
    });
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(mutated, localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("unknown or repeated")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesUnknownTheme()
{
    const QByteArray mutated = mutatedVisible(0, [](JsonValue *record) {
        member(*record, QStringLiteral("theme"))->string = QStringLiteral("false_breakout");
    });
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(mutated, localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("header does not list")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesThemeThatIsNotKnowableAtT()
{
    const QByteArray mutated = mutatedHeader([](JsonValue *header) {
        JsonValue *themes = member(*header, QStringLiteral("themes"));
        member(themes->array[0], QStringLiteral("knowable_at_t"))->boolean = false;
    });
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(mutated, localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("knowable_at_t false")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesRatingSeedOutsideTheDeclaredBand()
{
    const QByteArray mutated = mutatedVisible(0, [](JsonValue *record) {
        member(*record, QStringLiteral("rating_seed"))->integer = 9000;
    });
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(mutated, localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("rating_seed")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesRatingBasisOutsideThePermittedSet()
{
    const QByteArray mutated = mutatedHeader([](JsonValue *header) {
        JsonValue *rating = member(*header, QStringLiteral("rating"));
        member(*rating, QStringLiteral("basis_id"))->string = QStringLiteral("outcome_dispersion_v1");
    });
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(mutated, localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("not permitted")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesVaryingResponseHorizon()
{
    const int tradeLineIndex = indexOfTaskKind(QStringLiteral("trade_line"));
    QVERIFY(tradeLineIndex >= 0);
    const QByteArray mutated = mutatedVisible(tradeLineIndex, [](JsonValue *record) {
        JsonValue *spec = member(*record, QStringLiteral("response_spec"));
        member(*spec, QStringLiteral("horizon_bars"))->integer = 3;
    });
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(mutated, localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("pack constant")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesPlyShapeThatDisagreesWithTaskKind()
{
    const int patternIndex = indexOfTaskKind(QStringLiteral("pattern_call"));
    QVERIFY(patternIndex >= 0);
    const QByteArray mutated = mutatedVisible(patternIndex, [](JsonValue *record) {
        JsonValue *spec = member(*record, QStringLiteral("response_spec"));
        JsonValue *plies = member(*spec, QStringLiteral("plies"));
        member(plies->array[0], QStringLiteral("ply_kind"))->string = QStringLiteral("entry");
    });
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(mutated, localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("does not match its task kind")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesRewrittenBarsThroughTheWindowDigest()
{
    // Shifting the last close breaks the window digest first, which is exactly
    // the check that should catch a rewritten bar.
    QList<QByteArray> lines = splitLines(localVisibleFixture());
    QByteArray record = lines.at(1);
    QVERIFY(record.contains("100.0,"));
    record.replace("100.0,", "100.5,");
    lines[1] = record;
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(joinLines(lines), localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY(!error.isEmpty());
}

void TestUnitMarketPuzzlePack::refusesNonCanonicalLine()
{
    QList<QByteArray> lines = splitLines(localVisibleFixture());
    QByteArray record = lines.at(1);
    record.replace("{\"calibration_question\":", "{ \"calibration_question\":");
    lines[1] = record;
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(joinLines(lines), localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("canonical")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesRecursiveDuplicateKeys()
{
    QList<QByteArray> lines = splitLines(localVisibleFixture());
    QByteArray record = lines.at(1);
    record.replace("\"grain\":\"1d\",\"session_break_after\"", "\"grain\":\"1d\",\"grain\":\"1m\",\"session_break_after\"");
    lines[1] = record;
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(joinLines(lines), localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("duplicate key")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesRecordsNotSortedByPuzzleId()
{
    QList<QByteArray> visible = splitLines(localVisibleFixture());
    QList<QByteArray> sealed = splitLines(localSealedFixture());
    visible.swapItemsAt(1, 3);
    sealed.swapItemsAt(1, 3);
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(joinLines(visible), joinLines(sealed), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("sorted by puzzle_id")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesHeaderCountsThatDisagreeWithWhatWasParsed()
{
    const QByteArray mutated = mutatedHeader([](JsonValue *header) {
        JsonValue *counts = member(*header, QStringLiteral("counts"));
        JsonValue *byTaskKind = member(*counts, QStringLiteral("by_task_kind"));
        member(*byTaskKind, QStringLiteral("anomaly_flag"))->integer = 2;
        member(*byTaskKind, QStringLiteral("pattern_call"))->integer = 0;
    });
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(mutated, localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("by_task_kind")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesTamperedContinuationCommitment()
{
    // Rewrite the sealed answer and reseal its own record_id. The commitment on
    // the visible side no longer covers it, and a solver could otherwise grade
    // themselves generously.
    QList<QByteArray> sealed = splitLines(localSealedFixture());
    JsonValue record = parseLine(sealed.at(1));
    JsonValue *content = member(record, QStringLiteral("content"));
    member(*content, QStringLiteral("outcome_theme"))->string = QStringLiteral("clean_trend");
    sealed[1] = reseal(record, QStringLiteral("market-continuation-record-v1"));
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(localVisibleFixture(), joinLines(sealed), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("recomputed commitment")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesDisclosedCalendar()
{
    const QByteArray mutated = mutatedHeader([](JsonValue *header) {
        JsonValue *anonymization = member(*header, QStringLiteral("anonymization"));
        member(*anonymization, QStringLiteral("calendar_disclosed"))->boolean = true;
    });
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(mutated, localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("absolute dates")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::refusesTamperedRecordIdentity()
{
    QList<QByteArray> lines = splitLines(localVisibleFixture());
    JsonValue record = parseLine(lines.at(1));
    // Change presentation only, and do NOT reseal: the record_id must no longer
    // match its own canonical content.
    member(record, QStringLiteral("display_symbol"))->string = QStringLiteral("SYM-9999");
    lines[1] = canonicalJson(record);
    QString error;
    const auto pack = MarketPuzzlePack::fromJsonLines(joinLines(lines), localSealedFixture(), &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("record_id")), qPrintable(error));
}

void TestUnitMarketPuzzlePack::retainedLineVerificationStandsAlone()
{
    QString error;
    auto pack = MarketPuzzlePack::fromJsonLines(localVisibleFixture(), localSealedFixture(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    const MarketPuzzleVisible &puzzle = pack->puzzles().first();
    QVERIFY2(
        verifyRetainedMarketPuzzleLine(
            puzzle.canonicalLine, puzzle.recordId, puzzle.puzzleId, &error),
        qPrintable(error));

    QByteArray tampered = puzzle.canonicalLine;
    tampered.replace("\"SYM-", "\"XYM-");
    QVERIFY(!verifyRetainedMarketPuzzleLine(tampered, puzzle.recordId, puzzle.puzzleId, &error));
    QVERIFY(!verifyRetainedMarketPuzzleLine(
        puzzle.canonicalLine, puzzle.recordId, QStringLiteral("market-puzzle-v1:") + QString(64, QLatin1Char('a')), &error));
}

QTEST_MAIN(TestUnitMarketPuzzlePack)
#include "test_unit_market_puzzle_pack.moc"
