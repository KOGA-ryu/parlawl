#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>

struct StockfishReviewSnapshot {
    QString fen;
    QString statusText;
    QString evaluationText;
    QString bestMove;
    QString pvLine;
    double whiteExpectation = 0.5;
    bool available = false;
    bool inProgress = false;
};

class StockfishReviewController : public QObject
{
    Q_OBJECT

public:
    explicit StockfishReviewController(QObject *parent = nullptr);

    void setEnginePath(const QString &enginePath);
    void setAutoRefreshEnabled(bool enabled);
    void clearCache();
    void resetCurrentReview(const QString &message);
    [[nodiscard]] bool autoRefreshEnabled() const { return m_autoRefreshEnabled; }
    [[nodiscard]] const StockfishReviewSnapshot &snapshot() const { return m_snapshot; }

public slots:
    void requestReview(const QString &fen, bool forceRefresh = false);
    void refreshCurrent();

signals:
    void reviewUpdated();

private slots:
    void onProcessStarted();
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    void resetProcess();
    void finalizeUnavailable(const QString &message);
    void finalizeFromCacheOrUnavailable(const QString &fen);
    void finalizeSuccessfulReview();
    void parseOutputChunk(const QByteArray &chunk);
    void handleOutputLine(const QString &line);
    void startEngineRequest(const QString &fen);
    double expectationFromScore() const;

    struct ParsedInfo {
        bool hasCp = false;
        int cp = 0;
        bool hasMate = false;
        int mate = 0;
        bool hasWdl = false;
        int wins = 0;
        int draws = 0;
        int losses = 0;
        QString pvLine;
        QString bestMove;
    };

    enum class ProcessState {
        Idle,
        WaitingForUciOk,
        WaitingForReadyOk,
        WaitingForBestMove,
    };

    QString cacheKeyForFen(const QString &fen) const;
    bool sideToMoveIsWhite(const QString &fen) const;

    QProcess *m_process;
    QHash<QString, StockfishReviewSnapshot> m_cache;
    QString m_enginePath;
    QString m_requestedFen;
    QString m_currentFen;
    QByteArray m_stdoutBuffer;
    ParsedInfo m_lastInfo;
    StockfishReviewSnapshot m_snapshot;
    ProcessState m_state = ProcessState::Idle;
    bool m_autoRefreshEnabled = true;
};
