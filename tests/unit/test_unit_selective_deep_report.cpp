#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "selective_deep_report.h"

using namespace parlawl::puzzle_runner;

namespace {

QString fixtureId(const QString &prefix, QChar digit)
{
    return prefix + QLatin1Char(':') + QString(64, digit);
}

QJsonObject engineLine(const QString &role, const QString &rootMove, QChar digit)
{
    return {
        {QStringLiteral("role"), role},
        {QStringLiteral("engine_observation"), QJsonObject {
             {QStringLiteral("observation_id"),
                 fixtureId(QStringLiteral("chess-engine-deep-line-observation-v1"), digit)},
             {QStringLiteral("engine_contract_id"), fixtureId(
                  QStringLiteral("chess-engine-deep-engine-contract-v1"), QLatin1Char('8'))},
             {QStringLiteral("transition_id"), fixtureId(
                  QStringLiteral("chess-engine-transition-v1"), QLatin1Char('2'))},
             {QStringLiteral("fen"), QStringLiteral(
                  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")},
             {QStringLiteral("side_to_move"), QStringLiteral("white")},
             {QStringLiteral("score_kind"), QStringLiteral("cp")},
             {QStringLiteral("centipawns_white"), 20},
             {QStringLiteral("mate_for_white"), QJsonValue::Null},
             {QStringLiteral("root_move_uci"), rootMove},
             {QStringLiteral("line_rank"), 1},
             {QStringLiteral("depth"), 10},
             {QStringLiteral("selective_depth"), 12},
             {QStringLiteral("nodes"), 50'000},
             {QStringLiteral("wdl_white"), QJsonArray {500, 500, 0}},
             {QStringLiteral("pv_uci"), QJsonArray {rootMove}},
         }},
    };
}

QByteArray reportJson(QChar reportDigit = QLatin1Char('a'))
{
    const QString sourceGameId = fixtureId(QStringLiteral("chesscom-game-v1"), QLatin1Char('1'));
    const QString transitionId = fixtureId(QStringLiteral("chess-engine-transition-v1"), QLatin1Char('2'));
    const QString assessmentId = fixtureId(
        QStringLiteral("chess-engine-deep-decision-assessment-v1"), QLatin1Char('3'));
    const QString initialFen = QStringLiteral(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const QString afterE4 = QStringLiteral(
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    const QString afterE5 = QStringLiteral(
        "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
    const QJsonArray moves {
        QJsonObject {
            {QStringLiteral("ply"), 1}, {QStringLiteral("mover"), QStringLiteral("white")},
            {QStringLiteral("phase"), QStringLiteral("opening")},
            {QStringLiteral("san"), QStringLiteral("e4")},
            {QStringLiteral("uci"), QStringLiteral("e2e4")},
            {QStringLiteral("before_fen"), initialFen}, {QStringLiteral("after_fen"), afterE4},
            {QStringLiteral("deep_selection_status"),
                QStringLiteral("selected_for_deep_assessment")},
        },
        QJsonObject {
            {QStringLiteral("ply"), 2}, {QStringLiteral("mover"), QStringLiteral("black")},
            {QStringLiteral("phase"), QStringLiteral("opening")},
            {QStringLiteral("san"), QStringLiteral("e5")},
            {QStringLiteral("uci"), QStringLiteral("e7e5")},
            {QStringLiteral("before_fen"), afterE4}, {QStringLiteral("after_fen"), afterE5},
            {QStringLiteral("deep_selection_status"),
                QStringLiteral("not_selected_for_deep_assessment")},
        },
    };
    const QJsonObject assessment {
        {QStringLiteral("assessment_id"), assessmentId},
        {QStringLiteral("transition_id"), transitionId},
        {QStringLiteral("priority_rank"), 1}, {QStringLiteral("ply"), 1},
        {QStringLiteral("mover"), QStringLiteral("white")},
        {QStringLiteral("phase"), QStringLiteral("opening")},
        {QStringLiteral("played_move_uci"), QStringLiteral("e2e4")},
        {QStringLiteral("status"), QStringLiteral("below_confirmation_threshold")},
        {QStringLiteral("severity"), QStringLiteral("none")},
        {QStringLiteral("best_move_uci"), QStringLiteral("d2d4")},
        {QStringLiteral("best_expectation_millionths"), 600'000},
        {QStringLiteral("played_expectation_millionths"), 590'000},
        {QStringLiteral("signed_expectation_delta_millionths"), 10'000},
        {QStringLiteral("wdl_loss_millionths"), 10'000},
        {QStringLiteral("centipawn_loss"), 5},
        {QStringLiteral("mate_comparison"), QStringLiteral("none")},
        {QStringLiteral("pair_stability"), QStringLiteral("played_move_absent_from_multipv")},
    };
    const QJsonObject occurrence {
        {QStringLiteral("occurrence_id"),
            fixtureId(QStringLiteral("chess-engine-deep-episode-occurrence-v1"), QLatin1Char('4'))},
        {QStringLiteral("assessment_id"), assessmentId},
        {QStringLiteral("episode_id"),
            fixtureId(QStringLiteral("chess-engine-critical-episode-v1"), QLatin1Char('5'))},
        {QStringLiteral("transition_id"), transitionId},
        {QStringLiteral("ply"), 1}, {QStringLiteral("mover"), QStringLiteral("white")},
        {QStringLiteral("phase"), QStringLiteral("opening")},
        {QStringLiteral("played_move_uci"), QStringLiteral("e2e4")},
    };
    const QJsonObject sourceMove {
        {QStringLiteral("ply"), 1}, {QStringLiteral("mover"), QStringLiteral("white")},
        {QStringLiteral("phase"), QStringLiteral("opening")},
        {QStringLiteral("san"), QStringLiteral("e4")},
        {QStringLiteral("uci"), QStringLiteral("e2e4")},
        {QStringLiteral("before_fen"), initialFen},
    };
    const QJsonObject root {
        {QStringLiteral("contract_version"),
            QStringLiteral("chess-selective-game-analysis-report-v2")},
        {QStringLiteral("report_id"), fixtureId(
             QStringLiteral("chess-selective-game-analysis-report-v2"), reportDigit)},
        {QStringLiteral("selection_receipt_id"), fixtureId(
             QStringLiteral("chess-selective-game-selection-receipt-v1"), QLatin1Char('6'))},
        {QStringLiteral("claim_boundary"), QJsonObject {
             {QStringLiteral("source_and_interpretation_replayed"), true},
             {QStringLiteral("outcomes_used_for_selection_or_order"), false},
             {QStringLiteral("engine_processes_started"), 0},
             {QStringLiteral("network_requests"), 0},
             {QStringLiteral("writes_performed"), 0},
         }},
        {QStringLiteral("presentation_context"), QJsonObject {
             {QStringLiteral("game"), QJsonObject {
                  {QStringLiteral("source_game_id"), sourceGameId},
                  {QStringLiteral("canonical_game_url"), QStringLiteral("https://www.chess.com/game/live/1")},
                  {QStringLiteral("start_time_utc"), QStringLiteral("2026-08-01T12:00:00Z")},
                  {QStringLiteral("result"), QStringLiteral("1-0")},
                  {QStringLiteral("white"), QJsonObject {
                       {QStringLiteral("username"), QStringLiteral("Alpha")},
                       {QStringLiteral("postgame_rating_observed"), 2100},
                   }},
                  {QStringLiteral("black"), QJsonObject {
                       {QStringLiteral("username"), QStringLiteral("Beta")},
                       {QStringLiteral("postgame_rating_observed"), 2050},
                   }},
              }},
             {QStringLiteral("moves"), moves},
         }},
        {QStringLiteral("engine_interpretation"), QJsonObject {
             {QStringLiteral("interpretation_id"), fixtureId(
                  QStringLiteral("chess-engine-deep-interpretation-v1"), QLatin1Char('7'))},
             {QStringLiteral("engine_contract"), QJsonObject {
                  {QStringLiteral("contract_id"), fixtureId(
                       QStringLiteral("chess-engine-deep-engine-contract-v1"), QLatin1Char('8'))},
                  {QStringLiteral("engine_name"), QStringLiteral("Stockfish 18")},
                  {QStringLiteral("engine_author"), QStringLiteral("Stockfish developers")},
                  {QStringLiteral("engine_binary_sha256"), QString(64, QLatin1Char('e'))},
                  {QStringLiteral("node_limit"), 50'000},
                  {QStringLiteral("alternative_line_count"), 3},
                  {QStringLiteral("threads"), 1},
              }},
         }},
        {QStringLiteral("selected_moments"), QJsonArray {QJsonObject {
             {QStringLiteral("presentation_order"), 1},
             {QStringLiteral("assessment"), assessment},
             {QStringLiteral("occurrence"), occurrence},
             {QStringLiteral("source_move"), sourceMove},
             {QStringLiteral("engine_lines"), QJsonObject {
                  {QStringLiteral("alternatives"), QJsonArray {
                       engineLine(QStringLiteral("root_multipv_alternative"),
                           QStringLiteral("d2d4"), QLatin1Char('9')),
                   }},
                  {QStringLiteral("played_move"),
                      engineLine(QStringLiteral("played_move_constrained"),
                          QStringLiteral("e2e4"), QLatin1Char('a'))},
              }},
         }}},
        {QStringLiteral("audit"), QJsonObject {
             {QStringLiteral("selected_occurrence_count"), 1},
         }},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString writeReport(QTemporaryDir *directory, QChar reportDigit, const QByteArray &raw)
{
    const QString path = directory->filePath(
        QStringLiteral("selective-game-analysis-report-v2-%1.json").arg(QString(64, reportDigit)));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(raw) != raw.size()) {
        return {};
    }
    file.close();
    return path;
}

} // namespace

class SelectiveDeepReportTest : public QObject
{
    Q_OBJECT

private slots:
    void loadsExactReportAndPreservesExplicitNoSeverity();
    void rejectsMainlineMismatchAndDuplicateSourceGame();
};

void SelectiveDeepReportTest::loadsExactReportAndPreservesExplicitNoSeverity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeReport(&directory, QLatin1Char('a'), reportJson()).isEmpty());
    QString error;
    const auto catalog = SelectiveDeepReportCatalog::fromDirectory(
        QFileInfo(directory.path()).canonicalFilePath(), &error);
    QVERIFY2(catalog.has_value(), qPrintable(error));
    QCOMPARE(catalog->reportCount(), 1);
    const auto *review = catalog->reviewForGame(
        fixtureId(QStringLiteral("chesscom-game-v1"), QLatin1Char('1')));
    QVERIFY(review != nullptr);
    QCOMPARE(review->nodeLimit, 50'000);
    QCOMPARE(review->moments.size(), 1);
    QVERIFY(!review->moments.first().severity.has_value());
    QCOMPARE(review->moments.first().wdlLossMillionths.value_or(-1), 10'000);
    QCOMPARE(review->moments.first().alternativeLines.first().rootMoveUci,
        QStringLiteral("d2d4"));
}

void SelectiveDeepReportTest::rejectsMainlineMismatchAndDuplicateSourceGame()
{
    QString error;
    QTemporaryDir tamperedDirectory;
    QJsonDocument tamperedDocument = QJsonDocument::fromJson(reportJson());
    QJsonObject tamperedRoot = tamperedDocument.object();
    QJsonObject context = tamperedRoot.value(QStringLiteral("presentation_context")).toObject();
    QJsonArray moves = context.value(QStringLiteral("moves")).toArray();
    QJsonObject firstMove = moves.first().toObject();
    firstMove.insert(QStringLiteral("san"), QStringLiteral("d4"));
    moves.replace(0, firstMove);
    context.insert(QStringLiteral("moves"), moves);
    tamperedRoot.insert(QStringLiteral("presentation_context"), context);
    const QByteArray tampered = QJsonDocument(tamperedRoot).toJson(QJsonDocument::Compact);
    QVERIFY(!writeReport(&tamperedDirectory, QLatin1Char('a'), tampered).isEmpty());
    QVERIFY(!SelectiveDeepReportCatalog::fromDirectory(
        QFileInfo(tamperedDirectory.path()).canonicalFilePath(), &error).has_value());
    QVERIFY(error.contains(QStringLiteral("mainline")));

    QTemporaryDir duplicateDirectory;
    QVERIFY(!writeReport(&duplicateDirectory, QLatin1Char('a'), reportJson()).isEmpty());
    QVERIFY(!writeReport(&duplicateDirectory, QLatin1Char('b'),
        reportJson(QLatin1Char('b'))).isEmpty());
    QVERIFY(!SelectiveDeepReportCatalog::fromDirectory(
        QFileInfo(duplicateDirectory.path()).canonicalFilePath(), &error).has_value());
    QVERIFY(error.contains(QStringLiteral("repeats one source game")));
}

QTEST_MAIN(SelectiveDeepReportTest)

#include "test_unit_selective_deep_report.moc"
