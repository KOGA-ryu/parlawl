#include "analysis_repository.h"
#include "analysis_insert_specs.h"
#include "analysis_row_mappers.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

QString isoOrEmpty(const QDateTime &value)
{
    return value.isValid() ? value.toUTC().toString(Qt::ISODateWithMs) : QString();
}

bool setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

} // namespace

AnalysisRepository::AnalysisRepository(const QSqlDatabase &database)
    : m_database(database)
{
}

bool AnalysisRepository::upsertPuzzleRound(const PuzzleRound &puzzleRound, QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO puzzle_rounds ("
        "puzzle_id, puzzle_rating, time_control, white_player, white_rating, black_player, black_rating, "
        "side_to_move, source_game_id, fetched_at_utc, initial_fen, last_move, solved, raw_puzzle_json, "
        "raw_activity_json, solution_moves_json, themes_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(puzzle_id) DO UPDATE SET "
        "puzzle_rating = excluded.puzzle_rating, "
        "time_control = excluded.time_control, "
        "white_player = excluded.white_player, "
        "white_rating = excluded.white_rating, "
        "black_player = excluded.black_player, "
        "black_rating = excluded.black_rating, "
        "side_to_move = excluded.side_to_move, "
        "source_game_id = excluded.source_game_id, "
        "fetched_at_utc = excluded.fetched_at_utc, "
        "initial_fen = excluded.initial_fen, "
        "last_move = excluded.last_move, "
        "solved = excluded.solved, "
        "raw_puzzle_json = excluded.raw_puzzle_json, "
        "raw_activity_json = excluded.raw_activity_json, "
        "solution_moves_json = excluded.solution_moves_json, "
        "themes_json = excluded.themes_json"
    ));
    query.addBindValue(puzzleRound.puzzleId);
    query.addBindValue(puzzleRound.puzzleRating);
    query.addBindValue(puzzleRound.timeControl);
    query.addBindValue(puzzleRound.whitePlayer);
    query.addBindValue(puzzleRound.whiteRating);
    query.addBindValue(puzzleRound.blackPlayer);
    query.addBindValue(puzzleRound.blackRating);
    query.addBindValue(puzzleRound.sideToMove);
    query.addBindValue(puzzleRound.sourceGameId);
    query.addBindValue(isoOrEmpty(puzzleRound.fetchedAtUtc));
    query.addBindValue(puzzleRound.initialFen);
    query.addBindValue(puzzleRound.lastMove);
    query.addBindValue(puzzleRound.solved ? 1 : 0);
    query.addBindValue(puzzleRound.rawPuzzleJson);
    query.addBindValue(puzzleRound.rawActivityJson);
    query.addBindValue(puzzleRound.solutionMovesJson);
    query.addBindValue(puzzleRound.themesJson);
    if (!query.exec()) {
        return setError(errorMessage, QStringLiteral("failed to upsert puzzle_round: %1").arg(query.lastError().text()));
    }
    return true;
}

bool AnalysisRepository::upsertSourceGame(const SourceGame &sourceGame, QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO source_games (source_game_id, pgn_text, opening_name, fetched_at_utc) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(source_game_id) DO UPDATE SET "
        "pgn_text = excluded.pgn_text, "
        "opening_name = excluded.opening_name, "
        "fetched_at_utc = excluded.fetched_at_utc"
    ));
    query.addBindValue(sourceGame.sourceGameId);
    query.addBindValue(sourceGame.pgnText);
    query.addBindValue(sourceGame.openingName);
    query.addBindValue(isoOrEmpty(sourceGame.fetchedAtUtc));
    if (!query.exec()) {
        return setError(errorMessage, QStringLiteral("failed to upsert source_game: %1").arg(query.lastError().text()));
    }
    return true;
}

AnalysisRun AnalysisRepository::createAnalysisRun(
    const QString &puzzleId,
    const QString &engineMode,
    const QString &engineName,
    int engineDepth,
    QString *errorMessage
) const
{
    AnalysisRun run;
    run.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    run.puzzleId = puzzleId;
    run.status = QStringLiteral("running");
    run.engineMode = engineMode;
    run.engineName = engineName;
    run.engineDepth = engineDepth;
    run.createdAtUtc = QDateTime::currentDateTimeUtc();

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO analysis_runs (run_id, puzzle_id, status, engine_mode, engine_name, engine_depth, created_at_utc) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"
    ));
    query.addBindValue(run.runId);
    query.addBindValue(run.puzzleId);
    query.addBindValue(run.status);
    query.addBindValue(run.engineMode);
    query.addBindValue(run.engineName);
    query.addBindValue(run.engineDepth);
    query.addBindValue(isoOrEmpty(run.createdAtUtc));
    if (!query.exec()) {
        setError(errorMessage, QStringLiteral("failed to create analysis_run: %1").arg(query.lastError().text()));
        run.runId.clear();
    }
    return run;
}

bool AnalysisRepository::completeAnalysisRun(
    const QString &runId,
    const QString &status,
    const QString &errorMessageText,
    QString *errorMessage
) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE analysis_runs SET status = ?, completed_at_utc = ?, error_message = ? WHERE run_id = ?"
    ));
    query.addBindValue(status);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(errorMessageText);
    query.addBindValue(runId);
    if (!query.exec()) {
        return setError(errorMessage, QStringLiteral("failed to update analysis_run: %1").arg(query.lastError().text()));
    }
    return true;
}

bool AnalysisRepository::saveAnalysisResult(
    const TacticalEvent &tacticalEvent,
    const QList<CriticalMove> &criticalMoves,
    QString *errorMessage
) const
{
    if (!m_database.transaction()) {
        return setError(errorMessage, QStringLiteral("failed to start transaction: %1").arg(m_database.lastError().text()));
    }

    QSqlQuery eventQuery(m_database);
    eventQuery.prepare(storage::insert_specs::tacticalEventInsertSql());
    storage::insert_specs::bindTacticalEvent(eventQuery, tacticalEvent);
    if (!eventQuery.exec()) {
        m_database.rollback();
        return setError(errorMessage, QStringLiteral("failed to insert tactical_event: %1").arg(eventQuery.lastError().text()));
    }

    QSqlQuery deleteMovesQuery(m_database);
    deleteMovesQuery.prepare(QStringLiteral("DELETE FROM critical_moves WHERE event_id = ?"));
    deleteMovesQuery.addBindValue(tacticalEvent.eventId);
    if (!deleteMovesQuery.exec()) {
        m_database.rollback();
        return setError(errorMessage, QStringLiteral("failed to clear critical_moves: %1").arg(deleteMovesQuery.lastError().text()));
    }

    for (const CriticalMove &move : criticalMoves) {
        QSqlQuery moveQuery(m_database);
        moveQuery.prepare(storage::insert_specs::criticalMoveInsertSql());
        storage::insert_specs::bindCriticalMove(moveQuery, move);
        if (!moveQuery.exec()) {
            m_database.rollback();
            return setError(errorMessage, QStringLiteral("failed to insert critical_move: %1").arg(moveQuery.lastError().text()));
        }
    }

    if (!m_database.commit()) {
        m_database.rollback();
        return setError(errorMessage, QStringLiteral("failed to commit analysis result: %1").arg(m_database.lastError().text()));
    }
    return true;
}

bool AnalysisRepository::saveAssistantInference(
    const QString &runId,
    const QString &status,
    const QString &labelsJson,
    const QString &summaryMarkdown,
    QString *errorMessage
) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE tactical_events "
        "SET assistant_inference_status = ?, assistant_labels_json = ?, assistant_summary_markdown = ? "
        "WHERE run_id = ?"
    ));
    query.addBindValue(status);
    query.addBindValue(labelsJson);
    query.addBindValue(summaryMarkdown);
    query.addBindValue(runId);
    if (!query.exec()) {
        return setError(errorMessage, QStringLiteral("failed to update assistant inference: %1").arg(query.lastError().text()));
    }
    if (query.numRowsAffected() <= 0) {
        return setError(errorMessage, QStringLiteral("no tactical_event found for run %1").arg(runId));
    }
    return true;
}

QList<AnalysisRun> AnalysisRepository::listRecentRuns(int limit, QString *errorMessage) const
{
    QList<AnalysisRun> runs;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT run_id, puzzle_id, status, engine_mode, engine_name, engine_depth, created_at_utc, completed_at_utc, error_message "
        "FROM analysis_runs ORDER BY created_at_utc DESC LIMIT ?"
    ));
    query.addBindValue(limit);
    if (!query.exec()) {
        setError(errorMessage, QStringLiteral("failed to list recent analysis_runs: %1").arg(query.lastError().text()));
        return runs;
    }

    while (query.next()) {
        AnalysisRun run;
        run.runId = query.value(0).toString();
        run.puzzleId = query.value(1).toString();
        run.status = query.value(2).toString();
        run.engineMode = query.value(3).toString();
        run.engineName = query.value(4).toString();
        run.engineDepth = query.value(5).toInt();
        run.createdAtUtc = QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
        run.completedAtUtc = QDateTime::fromString(query.value(7).toString(), Qt::ISODate);
        run.errorMessage = query.value(8).toString();
        runs.append(run);
    }

    return runs;
}

bool AnalysisRepository::pruneAnalysisHistory(
    int keepRecentRuns,
    bool preserveCompletedRuns,
    int *deletedRunCount,
    QString *errorMessage
) const
{
    if (deletedRunCount != nullptr) {
        *deletedRunCount = 0;
    }

    QList<AnalysisRun> runs = listRecentRuns(100000, errorMessage);
    if (runs.isEmpty()) {
        return errorMessage == nullptr || errorMessage->isEmpty();
    }

    QStringList runIdsToDelete;
    int keptEligible = 0;
    for (const AnalysisRun &run : runs) {
        const bool protectedCompleted = preserveCompletedRuns && run.status == QStringLiteral("completed");
        if (protectedCompleted) {
            continue;
        }
        if (keptEligible < keepRecentRuns) {
            ++keptEligible;
            continue;
        }
        runIdsToDelete.append(run.runId);
    }

    if (runIdsToDelete.isEmpty()) {
        return true;
    }

    QStringList placeholders;
    placeholders.reserve(runIdsToDelete.size());
    for (int i = 0; i < runIdsToDelete.size(); ++i) {
        placeholders.append(QStringLiteral("?"));
    }
    const QString inClause = placeholders.join(QStringLiteral(","));

    if (!m_database.transaction()) {
        return setError(errorMessage, QStringLiteral("failed to start prune transaction: %1").arg(m_database.lastError().text()));
    }

    auto execDelete = [&](const QString &sql) {
        QSqlQuery query(m_database);
        query.prepare(sql);
        for (const QString &runId : runIdsToDelete) {
            query.addBindValue(runId);
        }
        if (!query.exec()) {
            m_database.rollback();
            return setError(errorMessage, query.lastError().text());
        }
        return true;
    };

    if (!execDelete(QStringLiteral(
            "DELETE FROM critical_moves "
            "WHERE event_id IN (SELECT event_id FROM tactical_events WHERE run_id IN (%1))").arg(inClause))) {
        return false;
    }
    if (!execDelete(QStringLiteral(
            "DELETE FROM tactical_events WHERE run_id IN (%1)").arg(inClause))) {
        return false;
    }
    if (!execDelete(QStringLiteral(
            "DELETE FROM analysis_runs WHERE run_id IN (%1)").arg(inClause))) {
        return false;
    }

    {
        QSqlQuery query(m_database);
        if (!query.exec(QStringLiteral(
                "DELETE FROM puzzle_rounds "
                "WHERE puzzle_id NOT IN (SELECT DISTINCT puzzle_id FROM analysis_runs)"))) {
            m_database.rollback();
            return setError(errorMessage, QStringLiteral("failed to prune orphan puzzle_rounds: %1").arg(query.lastError().text()));
        }
    }
    {
        QSqlQuery query(m_database);
        if (!query.exec(QStringLiteral(
                "DELETE FROM source_games "
                "WHERE source_game_id NOT IN (SELECT DISTINCT source_game_id FROM puzzle_rounds WHERE source_game_id IS NOT NULL AND source_game_id != '')"))) {
            m_database.rollback();
            return setError(errorMessage, QStringLiteral("failed to prune orphan source_games: %1").arg(query.lastError().text()));
        }
    }

    if (!m_database.commit()) {
        m_database.rollback();
        return setError(errorMessage, QStringLiteral("failed to commit prune transaction: %1").arg(m_database.lastError().text()));
    }

    {
        QSqlQuery query(m_database);
        query.exec(QStringLiteral("VACUUM"));
    }

    if (deletedRunCount != nullptr) {
        *deletedRunCount = runIdsToDelete.size();
    }
    return true;
}

bool AnalysisRepository::loadReport(const QString &runId, PersistedAnalysisReport *report, QString *errorMessage) const
{
    if (report == nullptr) {
        return setError(errorMessage, QStringLiteral("report output pointer is null"));
    }

    QSqlQuery runQuery(m_database);
    runQuery.prepare(QStringLiteral(
        "SELECT run_id, puzzle_id, status, engine_mode, engine_name, engine_depth, created_at_utc, completed_at_utc, error_message "
        "FROM analysis_runs WHERE run_id = ?"
    ));
    runQuery.addBindValue(runId);
    if (!runQuery.exec() || !runQuery.next()) {
        return setError(errorMessage, QStringLiteral("failed to load analysis_run for %1").arg(runId));
    }
    report->analysisRun.runId = runQuery.value(0).toString();
    report->analysisRun.puzzleId = runQuery.value(1).toString();
    report->analysisRun.status = runQuery.value(2).toString();
    report->analysisRun.engineMode = runQuery.value(3).toString();
    report->analysisRun.engineName = runQuery.value(4).toString();
    report->analysisRun.engineDepth = runQuery.value(5).toInt();
    report->analysisRun.createdAtUtc = QDateTime::fromString(runQuery.value(6).toString(), Qt::ISODate);
    report->analysisRun.completedAtUtc = QDateTime::fromString(runQuery.value(7).toString(), Qt::ISODate);
    report->analysisRun.errorMessage = runQuery.value(8).toString();

    QSqlQuery puzzleQuery(m_database);
    puzzleQuery.prepare(QStringLiteral(
        "SELECT puzzle_id, puzzle_rating, time_control, white_player, white_rating, black_player, black_rating, "
        "side_to_move, fetched_at_utc, source_game_id, initial_fen, last_move, solved, raw_puzzle_json, "
        "raw_activity_json, solution_moves_json, themes_json "
        "FROM puzzle_rounds WHERE puzzle_id = ?"
    ));
    puzzleQuery.addBindValue(report->analysisRun.puzzleId);
    if (!puzzleQuery.exec() || !puzzleQuery.next()) {
        return setError(errorMessage, QStringLiteral("failed to load puzzle_round for %1").arg(report->analysisRun.puzzleId));
    }
    report->puzzleRound.puzzleId = puzzleQuery.value(0).toString();
    report->puzzleRound.puzzleRating = puzzleQuery.value(1).toInt();
    report->puzzleRound.timeControl = puzzleQuery.value(2).toString();
    report->puzzleRound.whitePlayer = puzzleQuery.value(3).toString();
    report->puzzleRound.whiteRating = puzzleQuery.value(4).toInt();
    report->puzzleRound.blackPlayer = puzzleQuery.value(5).toString();
    report->puzzleRound.blackRating = puzzleQuery.value(6).toInt();
    report->puzzleRound.sideToMove = puzzleQuery.value(7).toString();
    report->puzzleRound.fetchedAtUtc = QDateTime::fromString(puzzleQuery.value(8).toString(), Qt::ISODate);
    report->puzzleRound.sourceGameId = puzzleQuery.value(9).toString();
    report->puzzleRound.initialFen = puzzleQuery.value(10).toString();
    report->puzzleRound.lastMove = puzzleQuery.value(11).toString();
    report->puzzleRound.solved = puzzleQuery.value(12).toInt() != 0;
    report->puzzleRound.rawPuzzleJson = puzzleQuery.value(13).toString();
    report->puzzleRound.rawActivityJson = puzzleQuery.value(14).toString();
    report->puzzleRound.solutionMovesJson = puzzleQuery.value(15).toString();
    report->puzzleRound.themesJson = puzzleQuery.value(16).toString();

    QSqlQuery sourceGameQuery(m_database);
    sourceGameQuery.prepare(QStringLiteral(
        "SELECT source_game_id, pgn_text, opening_name, fetched_at_utc FROM source_games WHERE source_game_id = ?"
    ));
    sourceGameQuery.addBindValue(report->puzzleRound.sourceGameId);
    if (!sourceGameQuery.exec() || !sourceGameQuery.next()) {
        return setError(errorMessage, QStringLiteral("failed to load source_game for %1").arg(report->puzzleRound.sourceGameId));
    }
    report->sourceGame.sourceGameId = sourceGameQuery.value(0).toString();
    report->sourceGame.pgnText = sourceGameQuery.value(1).toString();
    report->sourceGame.openingName = sourceGameQuery.value(2).toString();
    report->sourceGame.fetchedAtUtc = QDateTime::fromString(sourceGameQuery.value(3).toString(), Qt::ISODate);

    QSqlQuery eventQuery(m_database);
    eventQuery.prepare(QStringLiteral(
        "SELECT event_id, run_id, puzzle_id, source_game_id, opening_family, game_phase, solution_summary, "
        "side_to_move, material_balance, king_safety_state, piece_activity, key_weakness, "
        "king_exposure_type, king_exposure_severity, loose_piece_count, loose_piece_summary, "
        "overloaded_defender_count, overloaded_defender_summary, back_rank_state, luft_state, "
        "king_line_pressure_type, king_square_pressure_type, critical_piece_imbalance_summary, "
        "structural_feature_summary, structural_feature_confidence, king_zone_target_type, "
        "king_zone_target_summary, vulnerable_piece_target_type, vulnerable_piece_target_summary, "
        "pressure_lane_target_type, pressure_lane_target_summary, decisive_imbalance_target, "
        "pinned_critical_piece_type, pinned_critical_piece_summary, defender_removal_exposure_type, defender_removal_exposure_summary, "
        "king_color_complex_state, king_color_complex_summary, target_zone_imbalance_type, target_zone_imbalance_summary, "
        "structural_v2_summary, structural_v2_confidence, "
        "escape_geometry_state, escape_geometry_summary, flight_control_type, flight_control_summary, "
        "defensive_escape_fragility_type, defensive_escape_fragility_summary, structural_v3_summary, structural_v3_confidence, "
        "attacker_coordination_type, attacker_coordination_summary, defensive_network_fragility_type, defensive_network_fragility_summary, "
        "structural_v4_summary, structural_v4_confidence, "
        "local_target_summary, local_target_confidence, trigger_event, "
        "player_rating_range, mapping_status, mapped_start_ply, mapping_method, mapping_confidence, mapping_notes, "
        "analysis_window_start_ply, analysis_window_end_ply, primary_break_ply, primary_break_reason, retained_break_summary, "
        "retained_break_format_version, retained_break_role, retained_break_played_move, retained_break_best_move, retained_break_compact_sequence, "
        "best_vs_played_divergence_summary, divergence_type, divergence_severity, divergence_compact_summary, divergence_evidence_notes, "
        "local_sequence_confidence, collapse_sequence_format_version, collapse_sequence_type, collapse_sequence_summary, retained_role_summary, "
        "omitted_adjacent_candidate_count, omission_reason_summary, omission_reason_counts_json, engine_limit_summary, "
        "tactical_candidates_json, structural_candidates_json, evidence_payload_json, "
        "assistant_inference_status, assistant_labels_json, assistant_summary_markdown "
        "FROM tactical_events WHERE run_id = ? ORDER BY rowid ASC LIMIT 1"
    ));
    eventQuery.addBindValue(runId);
    if (!eventQuery.exec() || !eventQuery.next()) {
        return setError(errorMessage, QStringLiteral("failed to load tactical_event for run %1").arg(runId));
    }
    report->tacticalEvent = storage::mapTacticalEventRow(eventQuery);

    report->criticalMoves.clear();
    QSqlQuery moveQuery(m_database);
    moveQuery.prepare(QStringLiteral(
        "SELECT critical_move_id, event_id, role, ply, side, played_move, best_move, why_critical, "
        "eval_before_cp, eval_after_played_cp, eval_after_best_cp, eval_delta_cp, decisive_swing, analysis_depth, mate_flag, "
        "critical_reason_type, critical_reason_severity, critical_reason_compact_summary, continuation_format_version, "
        "best_continuation_compact, played_continuation_compact, "
        "pv_uci, pv_san, best_continuation_summary, played_continuation_summary, only_move_status, only_move_margin_cp, "
        "only_move_reasoning, candidate_moves_json, candidate_ranking_type, candidate_ranking_severity, "
        "candidate_ranking_compact_summary, candidate_ranking_summary, evidence_note_type, evidence_note_severity, "
        "evidence_note_compact, structural_link_format_version, linked_king_zone_target, linked_vulnerable_piece_target, "
        "linked_pressure_lane_target, linked_decisive_imbalance_target, linked_pinned_critical_piece, "
        "linked_defender_removal_exposure, linked_king_color_complex, linked_target_zone_imbalance, "
        "linked_attacker_coordination, linked_defensive_network_fragility, "
        "structural_link_summary, stronger_alternative_count "
        "FROM critical_moves WHERE event_id = ? ORDER BY ply ASC"
    ));
    moveQuery.addBindValue(report->tacticalEvent.eventId);
    if (!moveQuery.exec()) {
        return setError(errorMessage, QStringLiteral("failed to load critical_moves: %1").arg(moveQuery.lastError().text()));
    }
    while (moveQuery.next()) {
        report->criticalMoves.append(storage::mapCriticalMoveRow(moveQuery));
    }

    return true;
}
