#include <QtTest>
#include <QFile>

#include "replay_session.h"

#include "annotated_replay_pack.h"

using namespace parlawl::puzzle_runner;

namespace parlawl::test_support {

QByteArray fixtureId(const QByteArray &prefix = QByteArray("fixture-v1"), char digit = '0')
{
    return prefix + ':' + QByteArray(64, digit);
}

QByteArray syntheticAnnotatedReplayJson()
{
    QByteArray json = R"JSON({
  "black_username":"Beta",
  "classification_evidence_id":"@ID@",
  "contract_version":"annotated-game-replay-v1",
  "json_acquisition_id":"@ID@",
  "move_evidence_id":"@ID@",
  "move_vocabulary_id":"@ID@",
  "moves":[
    {
      "alternative_status":"available",
      "alternative_unavailable_reason":null,
      "annotation_id":"@AID@",
      "centipawn_loss":20,
      "engine_preferred_alternative":{
        "anchor_ply":1,
        "checkpoint_fen":"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "checkpoint_position_id":"@PID0@",
        "checkpoint_replay_state_id":"@SID0@",
        "displayed_steps":[
          {
            "after_fen":"rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1",
            "after_position_id":"@VPID1@",
            "after_replay_state_id":"@VSID1@",
            "before_fen":"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "before_position_id":"@PID0@",
            "before_replay_state_id":"@SID0@",
            "local_ply":1,
            "mechanical_fact_ids":[],
            "notation_id":"@VNID1@",
            "san":"d4",
            "step_id":"@STEP1@",
            "uci":"d2d4",
            "variation_context_id":"@VCID@"
          },
          {
            "after_fen":"rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2",
            "after_position_id":"@VPID2@",
            "after_replay_state_id":"@VSID2@",
            "before_fen":"rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1",
            "before_position_id":"@VPID1@",
            "before_replay_state_id":"@VSID1@",
            "local_ply":2,
            "mechanical_fact_ids":[],
            "notation_id":"@VNID2@",
            "san":"d5",
            "step_id":"@STEP2@",
            "uci":"d7d5",
            "variation_context_id":"@VCID@"
          }
        ],
        "engine_config_id":"@ECID@",
        "presentation_label":"engine line, not played",
        "reported_best_move_uci":"d2d4",
        "reported_pv_move_count":2,
        "reported_pv_sha256":"@SHA@",
        "restore_after_fen":"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
        "restore_after_position_id":"@PID1@",
        "restore_after_replay_state_id":"@SID1@",
        "restore_played_uci":"e2e4",
        "root_centipawns_white":12,
        "root_depth":8,
        "root_mate_for_white":null,
        "root_nodes":1000,
        "root_position_fact_id":"@RPFID@",
        "root_score_kind":"cp",
        "root_selective_depth":10,
        "root_wdl_white":[500,0,500],
        "source_game_id":"@SGID@",
        "source_run_id":"@SRID@",
        "variation_context_id":"@VCID@",
        "variation_id":"@VID@"
      },
      "expected_after_millionths":400000,
      "expected_before_millionths":500000,
      "facts":[
        {"authority":"mechanical_board","details":{},"fact_id":"@FACTA@","kind":"literal_move","status":"observed","summary":"1.e4 moves the pawn from e2 to e4.","supporting_ids":["@NID1@"]},
        {"authority":"threshold_derived","details":{},"fact_id":"@FACTB@","kind":"move_loss","status":"observed","summary":"The recorded mover expectation fell by 10%.","supporting_ids":["@MFID1@"]}
      ],
      "label_id":"@LID1@",
      "missed_forced_mate":false,
      "missed_winning_advantage":false,
      "move_fact_id":"@MFID1@",
      "narration":[{"narration_id":"@NARR1@","supporting_fact_ids":["@FACTA@"],"template_id":"literal-move-v1","text":"1.e4 moves the pawn from e2 to e4."}],
      "notation":{
        "after_fen":"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
        "after_position_id":"@PID1@","after_replay_state_id":"@SID1@",
        "before_fen":"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "before_position_id":"@PID0@","before_replay_state_id":"@SID0@",
        "capture":false,"captured_piece":null,"castling":null,"check":false,"checkmate":false,"en_passant":false,
        "match_id":"@SGID@","motifs":[],"move_number":1,"mover":"white","notation_id":"@NID1@","notation_version":"strict-uci-to-san-v1",
        "origin":"e2","piece":"pawn","ply":1,"promotion_piece":null,"san":"e4","target":"e4","uci":"e2e4"
      },
      "ply":1,"severity":"severe","wdl_loss_millionths":100000
    },
    {
      "alternative_status":"not_eligible","alternative_unavailable_reason":null,"annotation_id":"@BID@","centipawn_loss":0,"engine_preferred_alternative":null,
      "expected_after_millionths":600000,"expected_before_millionths":600000,
      "facts":[
        {"authority":"mechanical_board","details":{},"fact_id":"@FACTC@","kind":"literal_move","status":"observed","summary":"1...e5 moves the pawn from e7 to e5.","supporting_ids":["@NID2@"]},
        {"authority":"threshold_derived","details":{},"fact_id":"@FACTD@","kind":"move_loss","status":"not_applicable","summary":"No threshold loss was recorded.","supporting_ids":["@MFID2@"]}
      ],
      "label_id":"@LID2@","missed_forced_mate":false,"missed_winning_advantage":false,"move_fact_id":"@MFID2@",
      "narration":[{"narration_id":"@NARR2@","supporting_fact_ids":["@FACTC@"],"template_id":"literal-move-v1","text":"1...e5 moves the pawn from e7 to e5."}],
      "notation":{
        "after_fen":"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
        "after_position_id":"@PID2@","after_replay_state_id":"@SID2@",
        "before_fen":"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
        "before_position_id":"@PID1@","before_replay_state_id":"@SID1@",
        "capture":false,"captured_piece":null,"castling":null,"check":false,"checkmate":false,"en_passant":false,
        "match_id":"@SGID@","motifs":[],"move_number":1,"mover":"black","notation_id":"@NID2@","notation_version":"strict-uci-to-san-v1",
        "origin":"e7","piece":"pawn","ply":2,"promotion_piece":null,"san":"e5","target":"e5","uci":"e7e5"
      },
      "ply":2,"severity":"none","wdl_loss_millionths":0
    }
  ],
  "opening":{"classification_id":"@OCID@","corpus_id":"@OCORP@","eco":null,"name":null,"status":"unknown"},
  "pgn_acquisition_id":"@ID@",
  "policy":{"contract_version":"annotated-game-replay-v1","displayed_variation_plies":2,"maximum_variations":1,"minimum_variation_severity":"mistake","policy_id":"@POLID@","template_version":"deterministic-chess-explanations-v1"},
  "replay_id":"@RID@","result":"1/2-1/2","source_bundle_id":"@ID@",
  "source_engine":{"author":"Fixture","binary_sha256":"@SHA@","config_id":"@ECID@","name":"Synthetic Engine","node_limit":1000},
  "source_game_fingerprint":"@SHA@","source_game_id":"@SGID@","source_run_id":"@SRID@","white_username":"Alpha"
})JSON";

    const QList<QPair<QByteArray, QByteArray>> replacements {
        {"@ID@", fixtureId()}, {"@AID@", fixtureId("annotated-move-v1", '1')},
        {"@BID@", fixtureId("annotated-move-v1", '2')}, {"@RID@", fixtureId("annotated-game-replay-v1", '3')},
        {"@SGID@", fixtureId("chesscom-game-v1", '4')}, {"@SRID@", fixtureId("performance-analysis-run-v1", '5')},
        {"@ECID@", fixtureId("performance-engine-config-v1", '6')}, {"@SHA@", QByteArray(64, 'a')},
        {"@PID0@", fixtureId("standard-position-epd-v1", '0')}, {"@PID1@", fixtureId("standard-position-epd-v1", '1')},
        {"@PID2@", fixtureId("standard-position-epd-v1", '2')}, {"@SID0@", fixtureId("standard-replay-state-v1", '0')},
        {"@SID1@", fixtureId("standard-replay-state-v1", '1')}, {"@SID2@", fixtureId("standard-replay-state-v1", '2')},
        {"@VPID1@", fixtureId("standard-position-epd-v1", '7')}, {"@VPID2@", fixtureId("standard-position-epd-v1", '8')},
        {"@VSID1@", fixtureId("standard-replay-state-v1", '7')}, {"@VSID2@", fixtureId("standard-replay-state-v1", '8')},
        {"@NID1@", fixtureId("notation-move-v1", '1')}, {"@NID2@", fixtureId("notation-move-v1", '2')},
        {"@VNID1@", fixtureId("notation-move-v1", '7')}, {"@VNID2@", fixtureId("notation-move-v1", '8')},
        {"@STEP1@", fixtureId("annotated-replay-variation-step-v1", '1')}, {"@STEP2@", fixtureId("annotated-replay-variation-step-v1", '2')},
        {"@VCID@", fixtureId("engine-preferred-variation-context-v1", '7')}, {"@VID@", fixtureId("engine-preferred-variation-v1", '8')},
        {"@RPFID@", fixtureId("move-position-fact-v1", '9')}, {"@MFID1@", fixtureId("move-raw-fact-v1", '1')},
        {"@MFID2@", fixtureId("move-raw-fact-v1", '2')}, {"@LID1@", fixtureId("move-label-v1", '1')},
        {"@LID2@", fixtureId("move-label-v1", '2')}, {"@FACTA@", fixtureId("annotated-replay-fact-v1", '1')},
        {"@FACTB@", fixtureId("annotated-replay-fact-v1", '2')}, {"@FACTC@", fixtureId("annotated-replay-fact-v1", '3')},
        {"@FACTD@", fixtureId("annotated-replay-fact-v1", '4')}, {"@NARR1@", fixtureId("annotated-replay-narration-v1", '1')},
        {"@NARR2@", fixtureId("annotated-replay-narration-v1", '2')}, {"@OCID@", fixtureId("opening-classification-v1", '1')},
        {"@OCORP@", fixtureId("opening-corpus-v1", '2')}, {"@POLID@", fixtureId("annotated-replay-policy-v1", '3')}
    };
    for (const auto &replacement : replacements) {
        json.replace(replacement.first, replacement.second);
    }
    return json;
}

} // namespace parlawl::test_support

#ifndef PARLAWL_REPLAY_FIXTURE_PROVIDER_ONLY

using parlawl::test_support::syntheticAnnotatedReplayJson;

class AnnotatedReplayPackTest : public QObject
{
    Q_OBJECT

private slots:
    void acceptsStrictLegalReplayAndPreservesEvidence();
    void buildsMechanicalGameBreakdownFromLegalExplorerMoves();
    void rejectsTamperedMainlineAndPlyOrder();
    void rejectsBrokenVariationAndRestore();
    void rejectsDuplicateKeysAndOversizeInput();
    void preservesButDoesNotAuthenticateSuppliedAnnotations();
    void acceptsExternalReplayWhenExplicitlyRequested();
};

void AnnotatedReplayPackTest::acceptsStrictLegalReplayAndPreservesEvidence()
{
    QString error;
    const auto pack = AnnotatedReplayPack::fromJson(syntheticAnnotatedReplayJson(), &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    QCOMPARE(pack->moves().size(), 2);
    QCOMPARE(pack->mainlinePositions().size(), 3);
    QCOMPARE(pack->moves().at(0).notation.san, QStringLiteral("e4"));
    QCOMPARE(pack->moves().at(0).facts.at(0).summary, QStringLiteral("1.e4 moves the pawn from e2 to e4."));
    QVERIFY(pack->preferredVariation(1) != nullptr);
    QCOMPARE(pack->preferredVariation(1)->displayedSteps.at(1).san, QStringLiteral("d5"));
    QCOMPARE(pack->sourceEngineNodeLimit(), 1000);
    QVERIFY(pack->legalMechanicsAreVerified());
    QVERIFY(!pack->suppliedAnnotationsAreVerified());
}

void AnnotatedReplayPackTest::buildsMechanicalGameBreakdownFromLegalExplorerMoves()
{
    MechanicalReplayGame game;
    game.sourceGameId = QStringLiteral(
        "chesscom-game-v1:1111111111111111111111111111111111111111111111111111111111111111");
    game.canonicalGameUrl = QStringLiteral("https://www.chess.com/game/live/1");
    game.eventStartUtc = QStringLiteral("2026-08-01T12:00:00Z");
    game.whiteUsername = QStringLiteral("Alpha");
    game.blackUsername = QStringLiteral("Beta");
    game.whiteRating = 2100;
    game.blackRating = 2050;
    game.result = QStringLiteral("1-0");
    game.openingStatus = QStringLiteral("classified");
    game.openingEco = QStringLiteral("C20");
    game.openingName = QStringLiteral("King's Pawn Game");
    game.openingLastBookPly = 2;
    game.viewedPlayerColor = QStringLiteral("black");
    const auto addMove = [&game](
                             int ply,
                             const QString &san,
                             const QString &uci,
                             int legalMoves,
                             qint64 before,
                             qint64 after,
                             qint64 elapsed) {
        MechanicalReplayMove move;
        move.ply = ply;
        move.san = san;
        move.uci = uci;
        move.positionPhase = QStringLiteral("opening");
        move.forcednessStatus = QStringLiteral("nonforced");
        move.legalMoveCount = legalMoves;
        move.decisionStartClockMs = before;
        move.clockRemainingAfterMoveMs = after;
        move.elapsedMoveMs = elapsed;
        move.elapsedStatus = QStringLiteral("derived_clock_difference");
        game.moves.append(move);
    };
    addMove(1, QStringLiteral("e4"), QStringLiteral("e2e4"), 20, 180'000, 179'000, 1'000);
    addMove(2, QStringLiteral("e5"), QStringLiteral("e7e5"), 20, 180'000, 178'000, 2'000);
    addMove(3, QStringLiteral("Nf3"), QStringLiteral("g1f3"), 29, 179'000, 176'000, 3'000);
    addMove(4, QStringLiteral("Nc6"), QStringLiteral("b8c6"), 29, 178'000, 174'000, 4'000);

    QString error;
    const auto pack = AnnotatedReplayPack::fromMechanicalGame(game, &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    QVERIFY(pack->isMechanicalGameBreakdown());
    QCOMPARE(pack->viewedPlayerColor(), QStringLiteral("black"));
    QCOMPARE(pack->mainlinePositions().size(), 5);
    QVERIFY(pack->openingLastBookPly().has_value());
    QCOMPARE(*pack->openingLastBookPly(), 2);
    QCOMPARE(pack->moves().at(3).elapsedMoveMs.value_or(-1), 4'000);
    QVERIFY(pack->preferredVariation(4) == nullptr);

    MechanicalReplayGame tampered = game;
    tampered.moves[2].san = QStringLiteral("Nc3");
    QVERIFY(!AnnotatedReplayPack::fromMechanicalGame(tampered, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("SAN")));

    PersistedEngineGameEvidence engineGame;
    engineGame.evidenceId = QStringLiteral("player-game-engine-view-v1:")
        + QString(64, QLatin1Char('a'));
    engineGame.representativeRunId = QStringLiteral("performance-analysis-run-v2:")
        + QString(64, QLatin1Char('b'));
    engineGame.analysisRecordedAtUtc = QStringLiteral("2026-08-03T12:00:00Z");
    engineGame.lineageCount = 1;
    engineGame.engineConfigId = QStringLiteral("performance-engine-config-v1:")
        + QString(64, QLatin1Char('c'));
    engineGame.engineName = QStringLiteral("Stockfish 18");
    engineGame.engineAuthor = QStringLiteral("Stockfish developers");
    engineGame.engineBinarySha256 = QString(64, QLatin1Char('d'));
    engineGame.engineAdapterVersion = QStringLiteral("stockfish-complete-position-v1");
    engineGame.nodeLimit = 1'000;
    engineGame.hashMebibytes = 16;
    engineGame.threads = 1;
    engineGame.wdlLossThresholds = {25'000, 50'000, 100'000};
    engineGame.winningExpectationMillionths = 750'000;
    game.engineEvidence = engineGame;
    for (int index = 0; index < game.moves.size(); ++index) {
        PersistedEngineMoveEvidence engineMove;
        const bool severe = index == 2;
        engineMove.expectedBeforeMillionths = 750'000;
        engineMove.expectedAfterMillionths = severe ? 584'000 : 740'000;
        engineMove.wdlLossMillionths = severe ? 166'000 : 10'000;
        engineMove.centipawnLoss = severe ? 40 : 5;
        engineMove.missedWinningAdvantage = severe;
        engineMove.severity = severe ? QStringLiteral("severe") : QStringLiteral("none");
        engineMove.beforeScoreKind = QStringLiteral("cp");
        engineMove.beforeCentipawnsWhite = 100;
        engineMove.beforeWdlWhite = {500, 500, 0};
        engineMove.beforeBestMoveUci = QStringLiteral("d2d4");
        engineMove.beforeDepth = 7;
        engineMove.beforeSelectiveDepth = 9;
        engineMove.beforeNodes = 1'000;
        engineMove.beforePvUci = QStringLiteral("d2d4 d7d5");
        engineMove.afterScoreKind = QStringLiteral("cp");
        engineMove.afterCentipawnsWhite = severe ? 60 : 90;
        engineMove.afterWdlWhite = {450, 550, 0};
        game.moves[index].engineEvidence = engineMove;
    }
    const auto enginePack = AnnotatedReplayPack::fromMechanicalGame(game, &error);
    QVERIFY2(enginePack.has_value(), qPrintable(error));
    QVERIFY(enginePack->persistedEngineEvidence().has_value());
    QCOMPARE(enginePack->sourceEngineName(), QStringLiteral("Stockfish 18"));
    QCOMPARE(enginePack->moves().at(2).severity, QStringLiteral("severe"));
    QVERIFY(enginePack->moves().at(2).persistedEngineEvidence.has_value());
    QCOMPARE(
        enginePack->moves().at(2).persistedEngineEvidence->beforePvUci,
        QStringLiteral("d2d4 d7d5"));
    QVERIFY(enginePack->replayId() != pack->replayId());

    game.moves[1].engineEvidence.reset();
    QVERIFY(!AnnotatedReplayPack::fromMechanicalGame(game, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("complete per game")));
}

void AnnotatedReplayPackTest::rejectsTamperedMainlineAndPlyOrder()
{
    QByteArray tampered = syntheticAnnotatedReplayJson();
    tampered.replace("\"uci\":\"e2e4\"", "\"uci\":\"e2e5\"");
    QString error;
    QVERIFY(!AnnotatedReplayPack::fromJson(tampered, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("legal")) || error.contains(QStringLiteral("notation")));

    tampered = syntheticAnnotatedReplayJson();
    tampered.replace("\"ply\":2,\"severity\"", "\"ply\":3,\"severity\"");
    QVERIFY(!AnnotatedReplayPack::fromJson(tampered, &error).has_value());
}

void AnnotatedReplayPackTest::rejectsBrokenVariationAndRestore()
{
    QByteArray tampered = syntheticAnnotatedReplayJson();
    tampered.replace("\"restore_played_uci\":\"e2e4\"", "\"restore_played_uci\":\"g1g3\"");
    QString error;
    QVERIFY(!AnnotatedReplayPack::fromJson(tampered, &error).has_value());

    tampered = syntheticAnnotatedReplayJson();
    tampered.replace("\"uci\":\"d7d5\"", "\"uci\":\"d7d6\"");
    QVERIFY(!AnnotatedReplayPack::fromJson(tampered, &error).has_value());
}

void AnnotatedReplayPackTest::rejectsDuplicateKeysAndOversizeInput()
{
    QByteArray duplicate = syntheticAnnotatedReplayJson();
    duplicate.replace("{\n  \"black_username\"", "{\n  \"black_username\":\"Other\",\n  \"black_username\"");
    QString error;
    QVERIFY(!AnnotatedReplayPack::fromJson(duplicate, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("duplicate")));

    const QByteArray oversized(kMaximumAnnotatedReplayBytes + 1, ' ');
    QVERIFY(!AnnotatedReplayPack::fromJson(oversized, &error).has_value());

    QByteArray oversizedDetailKey = syntheticAnnotatedReplayJson();
    const QByteArray replacement = QByteArrayLiteral("\"details\":{\"")
        + QByteArray(97, 'k') + QByteArrayLiteral("\":\"\"}");
    oversizedDetailKey.replace(QByteArrayLiteral("\"details\":{}"), replacement);
    QVERIFY(!AnnotatedReplayPack::fromJson(oversizedDetailKey, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("fact-detail key")));
}

void AnnotatedReplayPackTest::preservesButDoesNotAuthenticateSuppliedAnnotations()
{
    QByteArray supplied = syntheticAnnotatedReplayJson();
    supplied.replace(
        QByteArrayLiteral("1.e4 moves the pawn from e2 to e4."),
        QByteArrayLiteral("A supplied claim whose semantic identity was not recomputed."));
    QString error;
    const auto pack = AnnotatedReplayPack::fromJson(supplied, &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    QCOMPARE(
        pack->moves().at(0).facts.at(0).summary,
        QStringLiteral("A supplied claim whose semantic identity was not recomputed."));
    QVERIFY(pack->legalMechanicsAreVerified());
    QVERIFY(!pack->suppliedAnnotationsAreVerified());
}

void AnnotatedReplayPackTest::acceptsExternalReplayWhenExplicitlyRequested()
{
    const QString path = qEnvironmentVariable("PARLAWL_ACCEPTANCE_REPLAY").trimmed();
    if (path.isEmpty()) {
        QSKIP("PARLAWL_ACCEPTANCE_REPLAY was not supplied");
    }
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const QByteArray payload = file.read(kMaximumAnnotatedReplayBytes + 1);
    QString error;
    const auto pack = AnnotatedReplayPack::fromJson(payload, &error);
    QVERIFY2(pack.has_value(), qPrintable(error));
    QVERIFY(pack->moves().size() >= 2);
    QCOMPARE(pack->mainlinePositions().size(), pack->moves().size() + 1);
    int checkedVariations = 0;
    for (const auto &move : pack->moves()) {
        if (!move.preferredVariation.has_value()) {
            continue;
        }
        ReplaySession session;
        session.load(*pack);
        QVERIFY(session.seekMainlinePly(move.ply));
        QVERIFY2(session.enterPreferredVariation(move.ply, &error), qPrintable(error));
        while (session.stepForward()) {
        }
        QVERIFY2(session.exitVariation(&error), qPrintable(error));
        QCOMPARE(session.currentMainlinePly(), move.ply);
        QCOMPARE(session.currentPosition().toFen(), pack->mainlinePositions().at(move.ply).toFen());
        ++checkedVariations;
    }
    QVERIFY(checkedVariations > 0);
}

QTEST_MAIN(AnnotatedReplayPackTest)
#include "test_unit_annotated_replay_pack.moc"

#endif
