#pragma once

#include <QObject>
#include <QVector>

#include "puzzle_api_requests.h"
#include "puzzle_round.h"
#include "puzzle_types.h"
#include "source_game.h"

class QNetworkAccessManager;
class QNetworkReply;

struct LichessFetchResult
{
    bool ok = false;
    QString failureStage;
    QString errorMessage;
    PuzzleRound puzzleRound;
    SourceGame sourceGame;
};

struct LichessBatchResult
{
    bool ok = false;
    QString failureStage;
    QString errorMessage;
    int statusCode = 0;
    QVector<parlawl::puzzle_runner::PuzzleDefinition> puzzles;
};

struct SourceGamePgnResult
{
    bool ok = false;
    QString errorMessage;
    int statusCode = 0;
    QString pgnText;
    QString openingName;
};

class LichessClient : public QObject
{
    Q_OBJECT

public:
    explicit LichessClient(QObject *parent = nullptr);

    LichessFetchResult fetchLatestSolvedPuzzle(const QString &apiToken);
    LichessBatchResult fetchTrainingBatch(const QString &apiToken, int count, const QString &difficulty);
    SourceGamePgnResult fetchSourceGamePgnText(const QString &sourceGameId, const QString &apiToken);

private:
    struct HttpResult
    {
        bool ok = false;
        int statusCode = 0;
        QByteArray body;
        QString errorMessage;
    };

    HttpResult performGet(
        const QString &url,
        const QString &apiToken,
        const QList<QPair<QByteArray, QByteArray>> &headers = {}
    );
    HttpResult performGet(
        const puzzle_api::request_spec &request,
        const QString &apiToken,
        const QList<QPair<QByteArray, QByteArray>> &headers = {}
    );
    HttpResult fetchLatestPuzzleActivity(const QString &apiToken);
    HttpResult fetchPuzzleBatch(int count, const QString &difficulty, const QString &apiToken);
    HttpResult fetchPuzzleDetail(const QString &puzzleId, const QString &apiToken);
    HttpResult fetchSourceGamePgn(const QString &sourceGameId, const QString &apiToken);

    LichessFetchResult normalizeFetch(
        const QByteArray &activityBody,
        const QByteArray &detailBody,
        const QByteArray &pgnBody
    ) const;
    LichessBatchResult normalizeBatchFetch(
        const QByteArray &batchBody,
        const QString &requestedDifficulty
    );

    QNetworkAccessManager *m_networkAccessManager;
};
