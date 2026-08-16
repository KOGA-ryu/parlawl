#include <QtTest>

#include <QFile>

#include "engine_validated_puzzle_pack.h"
#include "session_controller.h"

using namespace parlawl::puzzle_runner;

namespace {

QByteArray goldenRecord()
{
    QFile file(QStringLiteral(PARLAWL_TEST_SOURCE_DIR "/tests/fixtures/engine_validated_puzzle_v1.jsonl"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QByteArray tacticalProfileRecord()
{
    QFile file(QStringLiteral(PARLAWL_TEST_SOURCE_DIR "/tests/fixtures/engine_validated_tactical_profile_v1.jsonl"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QByteArray oneLine(QByteArray payload)
{
    while (payload.endsWith('\n') || payload.endsWith('\r')) {
        payload.chop(1);
    }
    return payload;
}

} // namespace

class TestUnitEngineValidatedPuzzlePack : public QObject
{
    Q_OBJECT

private slots:
    void acceptsPythonCanonicalGolden();
    void sessionControllerAcceptsOnlyCoherentImportedDto();
    void exactDuplicatesAreIdempotent();
    void conflictingValidatedVersionsAreRejected();
    void rejectsUnvalidatedStatus();
    void rejectsRecursiveDuplicateKeys();
    void rejectsUnknownNestedFields();
    void rejectsIntegerWhereContractCanonicalizesFloat();
    void rejectsFloatingPointSourcePly();
    void rejectsNonCanonicalUtcSpellingsWithRawIdentity();
    void acceptsCanonicalSixDigitUtcFraction();
    void rejectsTamperedSemanticIdentity();
    void rejectsIllegalSolutionReplay();
    void rejectsCanonicalRecordWithImpossibleEnPassant();
    void validatesKnownTacticalProfileCrosslinks();
    void invalidLaterLineRejectsWholePack();
    void rejectsOversizedLine();
    void optionalRealExportAcceptance();
};

void TestUnitEngineValidatedPuzzlePack::acceptsPythonCanonicalGolden()
{
    const QByteArray payload = goldenRecord();
    QVERIFY(!payload.isEmpty());

    QString error;
    const auto pack = EngineValidatedPuzzlePack::fromJsonLines(payload, &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    QCOMPARE(pack->puzzles().size(), 1);

    const PuzzleDefinition &puzzle = pack->puzzles().first();
    QCOMPARE(
        puzzle.id,
        QStringLiteral("puzzle-v1:04926a186c84f8e1014e8fb2ccd635b24ced4f8251aadeb8ebbfc03386c67161"));
    QCOMPARE(
        puzzle.analysisSeed.sourceRecordId,
        QStringLiteral("puzzle-record-v1:4ad64240c6e947259d7ca856631c0f61e53558605ab1bddfc61af1d5d95ef71b"));
    QCOMPARE(puzzle.analysisSeed.sourceProvider, QStringLiteral("local-fixture"));
    QCOMPARE(
        puzzle.analysisSeed.sourceRecordSchema,
        QStringLiteral("esports-probability-lab/puzzle-candidate/v1"));
    QCOMPARE(puzzle.analysisSeed.rawSourceRecordJson.toUtf8(), oneLine(payload));
    QCOMPARE(puzzle.analysisSeed.allowLichessPgnHydration, false);
    QCOMPARE(puzzle.metadata.difficulty, QStringLiteral("all"));
    QCOMPARE(puzzle.metadata.rating, 0);
    QCOMPARE(puzzle.metadata.ratingHidden, true);
    QCOMPARE(puzzle.metadata.source, importedEngineRecordSource());
    QCOMPARE(puzzle.metadata.title, importedEngineRecordTitle());
    QVERIFY(puzzle.metadata.sourceLabel.contains(QStringLiteral("declares engine_validated")));
    QVERIFY(puzzle.metadata.sourceLabel.contains(QStringLiteral("not independently verified by ParlAWL")));
    QVERIFY(puzzle.metadata.sourceLabel.contains(QStringLiteral("did not authenticate the producer")));
    QCOMPARE(puzzle.solutionMoves, QStringList({QStringLiteral("e2e4"), QStringLiteral("e7e5"), QStringLiteral("g1f3")}));
}

void TestUnitEngineValidatedPuzzlePack::sessionControllerAcceptsOnlyCoherentImportedDto()
{
    QString error;
    const auto pack = EngineValidatedPuzzlePack::fromJsonLines(goldenRecord(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));

    SessionController controller;
    QVERIFY2(controller.replacePuzzles(pack->puzzles(), &error), qPrintable(error));
    QCOMPARE(controller.gameStateStore()->currentPuzzle().id, pack->puzzles().first().id);
    QVERIFY(!controller.setCurrentPuzzleSourceGamePgn(QStringLiteral("1. e4 e5"), QString(), &error));
    QVERIFY2(error.contains(QStringLiteral("disallow source-game hydration")), qPrintable(error));

    PuzzleDefinition forged = pack->puzzles().first();
    forged.metadata.sourceLabel = QStringLiteral("independently verified by ParlAWL");
    QVERIFY(!controller.replacePuzzles({forged}, &error));
    QVERIFY2(error.contains(QStringLiteral("disagree")), qPrintable(error));
    QCOMPARE(controller.gameStateStore()->currentPuzzle().id, pack->puzzles().first().id);
}

void TestUnitEngineValidatedPuzzlePack::exactDuplicatesAreIdempotent()
{
    const QByteArray payload = goldenRecord();
    QString error;
    const auto pack = EngineValidatedPuzzlePack::fromJsonLines(payload + payload, &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    QCOMPARE(pack->puzzles().size(), 1);
}

void TestUnitEngineValidatedPuzzlePack::conflictingValidatedVersionsAreRejected()
{
    const QByteArray first = goldenRecord();
    QByteArray second = goldenRecord();
    second.replace(" w KQkq - 0 1\"", " w KQkq - 0 2\"");
    second.replace(
        "puzzle-record-v1:4ad64240c6e947259d7ca856631c0f61e53558605ab1bddfc61af1d5d95ef71b",
        "puzzle-record-v1:21f67160a3fd6abea500163ed9abe6f5862f2a5e15c86718a2d9195a924a802b");
    QString error;
    QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(first + second, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("multiple source record versions")), qPrintable(error));
}

void TestUnitEngineValidatedPuzzlePack::rejectsUnvalidatedStatus()
{
    QByteArray payload = goldenRecord();
    payload.replace("\"status\":\"engine_validated\"", "\"status\":\"candidate\"");
    QString error;
    QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(payload, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("not engine_validated")), qPrintable(error));
}

void TestUnitEngineValidatedPuzzlePack::rejectsRecursiveDuplicateKeys()
{
    QByteArray payload = goldenRecord();
    payload.replace(
        "\"metadata\":{\"purpose\":\"cross-language-golden\"}",
        "\"metadata\":{\"purpose\":\"first\",\"purpose\":\"cross-language-golden\"}");
    QString error;
    QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(payload, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("duplicate key 'purpose'")), qPrintable(error));
}

void TestUnitEngineValidatedPuzzlePack::rejectsUnknownNestedFields()
{
    QByteArray payload = goldenRecord();
    payload.replace(
        "\"engine_name\":\"Fixture Engine\"",
        "\"engine_name\":\"Fixture Engine\",\"future_field\":\"nope\"");
    QString error;
    QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(payload, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("future_field")), qPrintable(error));
}

void TestUnitEngineValidatedPuzzlePack::rejectsIntegerWhereContractCanonicalizesFloat()
{
    QByteArray payload = goldenRecord();
    payload.replace("\"solution_plies\":3.0", "\"solution_plies\":3");
    QString error;
    QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(payload, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("non-float value")), qPrintable(error));
}

void TestUnitEngineValidatedPuzzlePack::rejectsFloatingPointSourcePly()
{
    QByteArray payload = goldenRecord();
    payload.replace("\"source_ply\":0", "\"source_ply\":0.0");
    QString error;
    QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(payload, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("source_ply must be an integer")), qPrintable(error));
}

void TestUnitEngineValidatedPuzzlePack::rejectsNonCanonicalUtcSpellingsWithRawIdentity()
{
    struct ForgedTimestampCase {
        QByteArray originalField;
        QByteArray forgedField;
        QByteArray forgedRecordId;
    };
    const QVector<ForgedTimestampCase> cases{
        {
            "\"acquired_at_utc\":\"2026-08-16T00:00:00Z\"",
            "\"acquired_at_utc\":\"2026-08-16T00:00:00.1Z\"",
            "puzzle-record-v1:932d543f9b046d9030952597a31b83e0a080d9b2093f5fe89df6a0e0392a7689",
        },
        {
            "\"acquired_at_utc\":\"2026-08-16T00:00:00Z\"",
            "\"acquired_at_utc\":\"2026-08-16T00:00:00.000000Z\"",
            "puzzle-record-v1:f36b5031337b0c1565b2c91f9b6b30aaf7742fe1dbc698102b697a183510321c",
        },
        {
            "\"game_played_at_utc\":null",
            "\"game_played_at_utc\":\"2026-08-16T00:00:00.1Z\"",
            "puzzle-record-v1:7aae58e9638551fbde5468acb734f038e7a1f5aa0c94f9a29ccb7998cc33d5c3",
        },
        {
            "\"game_played_at_utc\":null",
            "\"game_played_at_utc\":\"2026-08-16T00:00:00.000000Z\"",
            "puzzle-record-v1:c0ac797ea25843418ffcb12d507b9ba57f76628064d15c03cd82da022a685696",
        },
    };
    for (const ForgedTimestampCase &testCase : cases) {
        QByteArray payload = goldenRecord();
        payload.replace(testCase.originalField, testCase.forgedField);
        payload.replace(
            "puzzle-record-v1:4ad64240c6e947259d7ca856631c0f61e53558605ab1bddfc61af1d5d95ef71b",
            testCase.forgedRecordId);
        QString error;
        QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(payload, &error).has_value());
        QVERIFY2(
            error.contains(QStringLiteral("canonical ISO-8601"))
                || error.contains(QStringLiteral("omit a zero microsecond")),
            qPrintable(error));
    }
}

void TestUnitEngineValidatedPuzzlePack::acceptsCanonicalSixDigitUtcFraction()
{
    QByteArray payload = goldenRecord();
    payload.replace(
        "\"acquired_at_utc\":\"2026-08-16T00:00:00Z\"",
        "\"acquired_at_utc\":\"2026-08-16T00:00:00.100000Z\"");
    payload.replace(
        "puzzle-record-v1:4ad64240c6e947259d7ca856631c0f61e53558605ab1bddfc61af1d5d95ef71b",
        "puzzle-record-v1:6fceb43c1defa9a33526ae3c9de638f77cc88f025d04380a589393a1dd43cdc8");
    QString error;
    const auto pack = EngineValidatedPuzzlePack::fromJsonLines(payload, &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    QCOMPARE(pack->puzzles().size(), 1);
}

void TestUnitEngineValidatedPuzzlePack::rejectsTamperedSemanticIdentity()
{
    QByteArray payload = goldenRecord();
    payload.replace(
        "puzzle-v1:04926a186c84f8e1014e8fb2ccd635b24ced4f8251aadeb8ebbfc03386c67161",
        "puzzle-v1:14926a186c84f8e1014e8fb2ccd635b24ced4f8251aadeb8ebbfc03386c67161");
    QString error;
    QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(payload, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("puzzle_id does not match")), qPrintable(error));
}

void TestUnitEngineValidatedPuzzlePack::rejectsIllegalSolutionReplay()
{
    QByteArray payload = goldenRecord();
    payload.replace("\"e2e4\"", "\"e2e5\"");
    QString error;
    QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(payload, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("is not legal")), qPrintable(error));
}

void TestUnitEngineValidatedPuzzlePack::rejectsCanonicalRecordWithImpossibleEnPassant()
{
    QByteArray payload = goldenRecord();
    payload.replace(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e3 0 1");
    payload.replace(
        "puzzle-v1:04926a186c84f8e1014e8fb2ccd635b24ced4f8251aadeb8ebbfc03386c67161",
        "puzzle-v1:b1756ed1e452845dcd51fe616fe74a96713eaaedd5c433b0a9a32ff7736d5b34");
    payload.replace(
        "puzzle-record-v1:4ad64240c6e947259d7ca856631c0f61e53558605ab1bddfc61af1d5d95ef71b",
        "puzzle-record-v1:1b37f89136f264a02a49f0eccdd47abf3b68ebf0a0e25a87c187f98a09d153e6");

    QString error;
    QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(payload, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("en passant")), qPrintable(error));
}

void TestUnitEngineValidatedPuzzlePack::validatesKnownTacticalProfileCrosslinks()
{
    const QByteArray valid = tacticalProfileRecord();
    QVERIFY(!valid.isEmpty());
    QString error;
    const auto pack = EngineValidatedPuzzlePack::fromJsonLines(valid, &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    QCOMPARE(pack->puzzles().size(), 1);

    struct CrosslinkMutation {
        QByteArray original;
        QByteArray replacement;
        QByteArray recordId;
        QString expectedError;
    };
    const QVector<CrosslinkMutation> mutations{
        {
            QByteArrayLiteral("\"source_position_index\":0"),
            QByteArrayLiteral("\"source_position_index\":1"),
            QByteArrayLiteral("puzzle-record-v1:80ddc58576b3091325c615c33452299642031e56181db7fed8b12fd990f1a323"),
            QStringLiteral("position cross-link"),
        },
        {
            QByteArrayLiteral("\"analysis_parameters\":{\"deep_best_move_uci\":\"e2e4\""),
            QByteArrayLiteral("\"analysis_parameters\":{\"deep_best_move_uci\":\"d2d4\""),
            QByteArrayLiteral("puzzle-record-v1:aa863ae268568ae48f5c0ea4080ca357ffb7ea3f414365aee6070a0f8c5ff589"),
            QStringLiteral("solution cross-link"),
        },
        {
            QByteArrayLiteral("\"deep_pv_uci\":\"e2e4 e7e5 g1f3\""),
            QByteArrayLiteral("\"deep_pv_uci\":\"e2e4 e7e5\""),
            QByteArrayLiteral("puzzle-record-v1:4015ee5da1a548a64415bae7c38f5005e120db6449d147c068cba5ff296e8471"),
            QStringLiteral("solution cross-link"),
        },
        {
            QByteArrayLiteral("\"tactical_candidate_id\":\"stockfish-tactical-candidate-v1:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\""),
            QByteArrayLiteral("\"tactical_candidate_id\":\"stockfish-tactical-candidate-v1:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\""),
            QByteArrayLiteral("puzzle-record-v1:c3a9872f915d9188179cad48adf530ddf22edec2cb9f7603c822247f2312f787"),
            QStringLiteral("candidate ID"),
        },
        {
            QByteArrayLiteral("\"run_id\":\"golden-run-v1\""),
            QByteArrayLiteral("\"run_id\":\"other-run-v1\""),
            QByteArrayLiteral("puzzle-record-v1:97bb4ef75721356366ee80609cf03a6f3aafef2b743eb3e9380db015117aae96"),
            QStringLiteral("engine run ID"),
        },
        {
            QByteArrayLiteral("\"played_move_uci\":\"d2d4\""),
            QByteArrayLiteral("\"played_move_uci\":\"a1a8\""),
            QByteArrayLiteral("puzzle-record-v1:f85ed0bda6d9eb984e26d5d04d66ead42f6b0fcef6f7671d65b84c1f781938e7"),
            QStringLiteral("played_move_uci"),
        },
        {
            QByteArrayLiteral("\"played_move_uci\":\"d2d4\""),
            QByteArrayLiteral("\"played_move_uci\":\"e2e4\""),
            QByteArrayLiteral("puzzle-record-v1:334789afd8b5a833bcd2fcd27c424a4c870c1a37ee67dc418c5871e9dcb0d048"),
            QStringLiteral("played_move_uci"),
        },
    };
    for (const CrosslinkMutation &mutation : mutations) {
        QByteArray payload = valid;
        QVERIFY(payload.contains(mutation.original));
        payload.replace(mutation.original, mutation.replacement);
        payload.replace(
            "puzzle-record-v1:64331c0f575c53373869089fbfa4c985e7c4a17c42d7c4efddf8ea0ced6a8e55",
            mutation.recordId);
        error.clear();
        QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(payload, &error).has_value());
        QVERIFY2(error.contains(mutation.expectedError), qPrintable(error));
    }
}

void TestUnitEngineValidatedPuzzlePack::invalidLaterLineRejectsWholePack()
{
    const QByteArray valid = goldenRecord();
    QByteArray invalid = goldenRecord();
    invalid.replace("\"status\":\"engine_validated\"", "\"status\":\"candidate\"");
    QString error;
    const auto pack = EngineValidatedPuzzlePack::fromJsonLines(valid + invalid, &error);
    QVERIFY(!pack.has_value());
    QVERIFY2(error.contains(QStringLiteral("line 2")) || error.contains(QStringLiteral("not engine_validated")), qPrintable(error));
}

void TestUnitEngineValidatedPuzzlePack::rejectsOversizedLine()
{
    QByteArray payload(kMaximumValidatedPuzzleLineBytes + 1, 'x');
    QString error;
    QVERIFY(!EngineValidatedPuzzlePack::fromJsonLines(payload, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("exceeds 1 MiB")), qPrintable(error));
}

void TestUnitEngineValidatedPuzzlePack::optionalRealExportAcceptance()
{
    const QString path = qEnvironmentVariable("PARLAWL_ACCEPTANCE_PUZZLES").trimmed();
    if (path.isEmpty()) {
        QSKIP("set PARLAWL_ACCEPTANCE_PUZZLES for a local exported pack");
    }
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const QByteArray payload = file.read(kMaximumValidatedPuzzlePackBytes + 1);
    QVERIFY2(payload.size() <= kMaximumValidatedPuzzlePackBytes, "acceptance pack exceeds importer bound");
    QString error;
    const auto pack = EngineValidatedPuzzlePack::fromJsonLines(payload, &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    QVERIFY(!pack->puzzles().isEmpty());
}

QTEST_MAIN(TestUnitEngineValidatedPuzzlePack)

#include "test_unit_engine_validated_puzzle_pack.moc"
