#pragma once

#include <QGroupBox>
#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include <optional>
#include <memory>

class QByteArray;
class QComboBox;
class QDateEdit;
class QJsonArray;
class QLabel;
class QLineEdit;
class QPushButton;
class QSqlQuery;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QWidget;
class PlayerAnalysisCatalog;

struct PlayerStatisticsEngineMoveEvidence {
    int expectedBeforeMillionths = 0;
    int expectedAfterMillionths = 0;
    int wdlLossMillionths = 0;
    std::optional<qint64> centipawnLoss;
    bool missedWinningAdvantage = false;
    bool missedForcedMate = false;
    QString severity;
    QString beforeScoreKind;
    std::optional<qint64> beforeCentipawnsWhite;
    std::optional<qint64> beforeMateForWhite;
    QVector<int> beforeWdlWhite;
    std::optional<QString> beforeBestMoveUci;
    int beforeDepth = 0;
    int beforeSelectiveDepth = 0;
    int beforeNodes = 0;
    QString beforePvUci;
    QString afterScoreKind;
    std::optional<qint64> afterCentipawnsWhite;
    std::optional<qint64> afterMateForWhite;
    QVector<int> afterWdlWhite;
};

struct PlayerStatisticsEngineGameEvidence {
    QString evidenceId;
    QString representativeRunId;
    QString analysisRecordedAtUtc;
    int lineageCount = 0;
    QString engineConfigId;
    QString engineName;
    QString engineAuthor;
    QString engineBinarySha256;
    QString engineAdapterVersion;
    int nodeLimit = 0;
    int hashMebibytes = 0;
    int threads = 0;
    QVector<int> wdlLossThresholds;
    int winningExpectationMillionths = 0;
};

struct PlayerStatisticsGameMove {
    int ply = 0;
    QString playerId;
    QString opponentId;
    QString playerColor;
    QString san;
    QString uci;
    QString phase;
    QString forcedness;
    int legalMoveCount = 0;
    std::optional<qint64> decisionStartClockMs;
    std::optional<qint64> clockAfterMs;
    std::optional<qint64> elapsedMs;
    QString elapsedStatus;
    std::optional<PlayerStatisticsEngineMoveEvidence> engineEvidence;
};

struct PlayerStatisticsGameBreakdown {
    QString sourceGameId;
    QString canonicalGameUrl;
    QString eventStartUtc;
    QString whitePlayerId;
    QString blackPlayerId;
    int whiteRating = 0;
    int blackRating = 0;
    QString result;
    QString openingStatus;
    std::optional<QString> openingEco;
    std::optional<QString> openingName;
    std::optional<int> openingLastBookPly;
    QString viewedPlayerId;
    QString viewedPlayerColor;
    std::optional<PlayerStatisticsEngineGameEvidence> engineEvidence;
    QVector<PlayerStatisticsGameMove> moves;
};

class PlayerStatisticsPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit PlayerStatisticsPanel(QWidget *parent = nullptr);
    ~PlayerStatisticsPanel() override;

    bool loadSnapshot(const QByteArray &raw, QString *errorMessage = nullptr);
    bool loadBoardStructureSnapshot(
        const QByteArray &raw,
        QString *errorMessage = nullptr);
    bool loadExplorerDatabase(const QString &path, QString *errorMessage = nullptr);
    void clearSnapshot();
    bool selectPlayer(const QString &playerId);

    [[nodiscard]] bool hasSnapshot() const;
    [[nodiscard]] bool hasBoardStructureSnapshot() const;
    [[nodiscard]] QString selectedPlayerId() const;
    [[nodiscard]] QString summaryText() const;
    [[nodiscard]] QStringList playerIds() const;
    [[nodiscard]] int phaseRowCount() const;
    [[nodiscard]] int decisionContextRowCount() const;
    [[nodiscard]] int opponentRowCount() const;
    [[nodiscard]] int longestMoveRowCount() const;
    [[nodiscard]] int gameRowCount() const;
    [[nodiscard]] int openingRowCount() const;
    [[nodiscard]] int boardStructureMetricRowCount() const;
    [[nodiscard]] int boardStructureCastlingRowCount() const;
    [[nodiscard]] std::optional<PlayerStatisticsGameBreakdown> gameBreakdown(
        const QString &sourceGameId,
        QString *errorMessage = nullptr) const;

signals:
    void openSnapshotRequested();
    void openBoardStructureSnapshotRequested();
    void gameBreakdownRequested(const QString &sourceGameId);

private:
    void closeExplorerDatabase();
    void populateExplorerPlayers(const QString &preferredPlayerId);
    void populateExplorerDependentFilters();
    void resetExplorerFilters();
    void rebuildExplorerView();
    void bindExplorerFilters(QSqlQuery *query) const;
    void selectTypedPlayer();
    void rebuildView();
    void rebuildBoardStructureView();
    void refreshBoardStructureComparisonPlayers();
    QJsonObject catalogBoardStructureView(
        const QString &playerId,
        QString *errorMessage);
    void clearBoardStructureDrilldown();
    void showBoardStructureDrilldown(
        const QString &playerId,
        const QString &metricCode);
    void populatePhaseTable(const QJsonArray &groups);
    void populateDecisionContextTable(
        const QJsonArray &colors,
        const QJsonArray &forcedness);
    void populateOpponentTable(const QJsonArray &opponents);
    void populateLongestTable(const QJsonArray &moves);

    QPushButton *m_openButton;
    QPushButton *m_openStructureButton;
    QComboBox *m_playerCombo;
    QWidget *m_explorerFilterPanel;
    QDateEdit *m_fromDateEdit;
    QDateEdit *m_toDateEdit;
    QComboBox *m_colorFilter;
    QComboBox *m_resultFilter;
    QComboBox *m_opponentFilter;
    QComboBox *m_openingFilter;
    QPushButton *m_resetFiltersButton;
    QPushButton *m_replayGameButton;
    QLabel *m_statusLabel;
    QLabel *m_summaryLabel;
    QLabel *m_gameMetricLabel;
    QLabel *m_moveMetricLabel;
    QLabel *m_populationMetricTitleLabel;
    QLabel *m_populationMetricLabel;
    QLabel *m_coverageMetricLabel;
    QLabel *m_medianMetricLabel;
    QLabel *m_p90MetricLabel;
    QLabel *m_pressureLabel;
    QLabel *m_opponentHintLabel;
    QLabel *m_structureStatusLabel;
    QLabel *m_structureSummaryLabel;
    QLabel *m_structureConcentrationLabel;
    QWidget *m_structureComparisonPanel;
    QComboBox *m_structureComparisonPlayerCombo;
    QComboBox *m_structureComparisonCategoryCombo;
    QLabel *m_structureComparisonSummaryLabel;
    QTableWidget *m_structureComparisonTable;
    QLabel *m_structureDrilldownLabel;
    QTableWidget *m_structureDrilldownTable;
    QLabel *m_structureHeadToHeadLabel;
    QWidget *m_structureHeadToHeadFilterPanel;
    QLineEdit *m_structureOpponentSearch;
    QSpinBox *m_structureMinimumGamesSpin;
    QTabWidget *m_detailTabs;
    QTableWidget *m_phaseTable;
    QTableWidget *m_decisionContextTable;
    QTableWidget *m_gameTable;
    QTableWidget *m_openingTable;
    QTableWidget *m_opponentTable;
    QTableWidget *m_longestTable;
    QTableWidget *m_structureMetricTable;
    QTableWidget *m_structureCastlingTable;
    QTableWidget *m_structureHeadToHeadTable;
    QJsonObject m_snapshot;
    QHash<QString, QJsonObject> m_playersById;
    QJsonObject m_structureSnapshot;
    QHash<QString, QJsonObject> m_structurePlayersById;
    QString m_structureComparisonSourcePlayerId;
    std::unique_ptr<PlayerAnalysisCatalog> m_analysisCatalog;
    QStringList m_catalogStructurePlayerIds;
    QJsonObject m_catalogGlobalStructureView;
    QHash<QString, QJsonObject> m_catalogStructureViewsByPlayer;
    QHash<QString, QString> m_explorerMetadata;
    QString m_explorerConnectionName;
    bool m_updatingExplorerFilters;
};
