#include <QtTest/QtTest>

#include "puzzle_api_requests.h"

class PuzzleApiRequestsTest : public QObject
{
    Q_OBJECT

private slots:
    void buildNextRequestOmitsEmptyParams();
    void buildBatchFetchRequestValidatesColorAndNb();
    void buildBatchFetchRequestOmitsColorForTrainerBatch();
    void buildBatchSolveRequestSerializesBody();
    void buildActivityRequestSerializesBoundedParams();
    void buildReplayAndDashboardRequestsUsePathParams();
};

void PuzzleApiRequestsTest::buildNextRequestOmitsEmptyParams()
{
    puzzle_api::request_spec request;
    QString errorMessage;
    const bool ok = puzzle_api::build_puzzle_next_request(
        puzzle_api::puzzle_next_options{
            QStringLiteral("mix"),
            puzzle_api::puzzle_difficulty::harder,
            std::nullopt,
        },
        &request,
        &errorMessage);

    QVERIFY2(ok, qPrintable(errorMessage));
    QCOMPARE(request.path, QStringLiteral("/api/puzzle/next"));
    QCOMPARE(request.query_items.size(), 2);
    QCOMPARE(request.query_items.at(0).first, QStringLiteral("angle"));
    QCOMPARE(request.query_items.at(0).second, QStringLiteral("mix"));
    QCOMPARE(request.query_items.at(1).first, QStringLiteral("difficulty"));
    QCOMPARE(request.query_items.at(1).second, QStringLiteral("harder"));
    QVERIFY(request.json_body.isEmpty());
}

void PuzzleApiRequestsTest::buildBatchFetchRequestValidatesColorAndNb()
{
    puzzle_api::request_spec request;
    QString errorMessage;

    const bool invalid = puzzle_api::build_puzzle_batch_fetch_request(
        puzzle_api::puzzle_batch_fetch_options{
            QStringLiteral("mix"),
            puzzle_api::puzzle_difficulty::normal,
            10,
            puzzle_api::puzzle_color::white,
        },
        &request,
        &errorMessage);
    QVERIFY(!invalid);
    QVERIFY(errorMessage.contains(QStringLiteral("nb == 1")));

    errorMessage.clear();
    const bool valid = puzzle_api::build_puzzle_batch_fetch_request(
        puzzle_api::puzzle_batch_fetch_options{
            QStringLiteral("mix"),
            puzzle_api::puzzle_difficulty::normal,
            1,
            puzzle_api::puzzle_color::white,
        },
        &request,
        &errorMessage);
    QVERIFY2(valid, qPrintable(errorMessage));
    QCOMPARE(request.path, QStringLiteral("/api/puzzle/batch/mix"));
    QCOMPARE(request.query_items.size(), 3);
    QCOMPARE(puzzle_api::serialize_url(QStringLiteral("https://lichess.org"), request),
             QStringLiteral("https://lichess.org/api/puzzle/batch/mix?difficulty=normal&nb=1&color=white"));
}

void PuzzleApiRequestsTest::buildBatchFetchRequestOmitsColorForTrainerBatch()
{
    puzzle_api::request_spec request;
    QString errorMessage;
    const bool ok = puzzle_api::build_puzzle_batch_fetch_request(
        puzzle_api::puzzle_batch_fetch_options{
            QStringLiteral("mix"),
            puzzle_api::puzzle_difficulty::harder,
            25,
            std::nullopt,
        },
        &request,
        &errorMessage);

    QVERIFY2(ok, qPrintable(errorMessage));
    QCOMPARE(request.path, QStringLiteral("/api/puzzle/batch/mix"));
    QCOMPARE(puzzle_api::serialize_url(QStringLiteral("https://lichess.org"), request),
             QStringLiteral("https://lichess.org/api/puzzle/batch/mix?difficulty=harder&nb=25"));
}

void PuzzleApiRequestsTest::buildBatchSolveRequestSerializesBody()
{
    puzzle_api::request_spec request;
    QString errorMessage;
    const bool ok = puzzle_api::build_puzzle_batch_solve_request(
        puzzle_api::puzzle_batch_solve_options{QStringLiteral("mix"), 1},
        puzzle_api::puzzle_batch_solve_request{
            {puzzle_api::puzzle_batch_solution{QStringLiteral("abc12"), true, true}}
        },
        &request,
        &errorMessage);

    QVERIFY2(ok, qPrintable(errorMessage));
    QCOMPARE(request.path, QStringLiteral("/api/puzzle/batch/mix"));
    QCOMPARE(request.content_type, QStringLiteral("application/json"));
    QCOMPARE(QString::fromUtf8(request.json_body),
             QStringLiteral("{\"solutions\":[{\"id\":\"abc12\",\"rated\":true,\"win\":true}]}"));
}

void PuzzleApiRequestsTest::buildActivityRequestSerializesBoundedParams()
{
    puzzle_api::request_spec request;
    QString errorMessage;
    const bool ok = puzzle_api::build_puzzle_activity_request(
        puzzle_api::puzzle_activity_options{10, 1710000000, 1700000000},
        &request,
        &errorMessage);

    QVERIFY2(ok, qPrintable(errorMessage));
    QCOMPARE(request.path, QStringLiteral("/api/puzzle/activity"));
    QCOMPARE(request.query_items.size(), 3);
    QCOMPARE(puzzle_api::serialize_url(QStringLiteral("https://lichess.org"), request),
             QStringLiteral("https://lichess.org/api/puzzle/activity?max=10&before=1710000000&since=1700000000"));
}

void PuzzleApiRequestsTest::buildReplayAndDashboardRequestsUsePathParams()
{
    puzzle_api::request_spec replayRequest;
    puzzle_api::request_spec dashboardRequest;
    QString errorMessage;

    QVERIFY(puzzle_api::build_puzzle_replay_request(
        puzzle_api::puzzle_replay_request{30, QStringLiteral("fork")},
        &replayRequest,
        &errorMessage));
    QCOMPARE(replayRequest.path, QStringLiteral("/api/puzzle/replay/30/fork"));

    QVERIFY(puzzle_api::build_puzzle_dashboard_request(
        puzzle_api::puzzle_dashboard_request{30},
        &dashboardRequest,
        &errorMessage));
    QCOMPARE(dashboardRequest.path, QStringLiteral("/api/puzzle/dashboard/30"));
}

QTEST_MAIN(PuzzleApiRequestsTest)

#include "test_unit_puzzle_api_requests.moc"
