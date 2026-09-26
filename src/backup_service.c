#include "prs/backup_service.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "prs/app_config.h"
#include "prs/auth.h"
#include "prs/logging.h"

static void set_error(char *error, size_t size, const char *message) {
    if (error != NULL && size > 0U) {
        (void)snprintf(error, size, "%s", message);
    }
}

static bool copy_database(sqlite3 *destination, sqlite3 *source) {
    sqlite3_backup *backup = sqlite3_backup_init(destination, "main", source, "main");
    int status = SQLITE_ERROR;

    if (backup == NULL) {
        return false;
    }
    status = sqlite3_backup_step(backup, -1);
    (void)sqlite3_backup_finish(backup);
    return status == SQLITE_DONE;
}

static bool has_valid_integrity(sqlite3 *connection) {
    sqlite3_stmt *statement = NULL;
    const unsigned char *result = NULL;
    bool valid = false;

    if (sqlite3_prepare_v2(connection, "PRAGMA integrity_check;", -1, &statement,
                           NULL) != SQLITE_OK) {
        return false;
    }
    if (sqlite3_step(statement) == SQLITE_ROW) {
        result = sqlite3_column_text(statement, 0);
        valid = result != NULL && strcmp((const char *)result, "ok") == 0;
    }
    (void)sqlite3_finalize(statement);
    return valid;
}

static bool has_prs_schema(sqlite3 *connection) {
    sqlite3_stmt *statement = NULL;
    int status = sqlite3_prepare_v2(
        connection,
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
        "AND name IN ('schema_migrations', 'users', 'patients');",
        -1, &statement, NULL);
    bool valid = false;

    if (status == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW) {
        valid = sqlite3_column_int(statement, 0) == 3;
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    return valid;
}

bool prs_backup_create_timestamped(PrsDatabase *database,
                                   const PrsUserSession *session,
                                   const char *directory, char *backup_path,
                                   size_t backup_path_size, char *error,
                                   size_t error_size) {
    sqlite3 *backup_database = NULL;
    time_t now;
    struct tm *local_time = NULL;
    char timestamp[32];

    if (backup_path != NULL && backup_path_size > 0U) {
        backup_path[0] = '\0';
    }
    if (!prs_session_has_role(session, PRS_ROLE_ADMINISTRATOR)) {
        set_error(error, error_size, "Only administrators may create backups.");
        return false;
    }
    if (database == NULL || database->connection == NULL || directory == NULL ||
        directory[0] == '\0' || backup_path == NULL || backup_path_size == 0U) {
        set_error(error, error_size, "Backup configuration is incomplete.");
        return false;
    }
    now = time(NULL);
    local_time = localtime(&now);
    if (local_time == NULL || strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S",
                                       local_time) == 0U ||
        (size_t)snprintf(backup_path, backup_path_size, "%s/prs-%s.db", directory,
                         timestamp) >= backup_path_size ||
        !prs_ensure_parent_directory(backup_path, error, error_size)) {
        set_error(error, error_size, "Unable to prepare the backup location.");
        return false;
    }
    if (sqlite3_open_v2(backup_path, &backup_database,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        NULL) != SQLITE_OK ||
        !copy_database(backup_database, database->connection) ||
        !has_valid_integrity(backup_database) ||
        chmod(backup_path, S_IRUSR | S_IWUSR) != 0) {
        if (backup_database != NULL) {
            (void)sqlite3_close_v2(backup_database);
        }
        set_error(error, error_size, "Unable to create a validated protected backup.");
        return false;
    }
    (void)sqlite3_close_v2(backup_database);
    prs_log(PRS_LOG_INFO, "backup", "Database backup created.");
    return true;
}

bool prs_backup_restore(PrsDatabase *database, const PrsUserSession *session,
                        const char *backup_path, char *error, size_t error_size) {
    sqlite3 *source = NULL;
    int status = SQLITE_ERROR;

    if (!prs_session_has_role(session, PRS_ROLE_ADMINISTRATOR)) {
        set_error(error, error_size, "Only administrators may restore backups.");
        return false;
    }
    if (database == NULL || database->connection == NULL || backup_path == NULL ||
        backup_path[0] == '\0') {
        set_error(error, error_size, "Select a backup to restore.");
        return false;
    }
    status = sqlite3_open_v2(backup_path, &source,
                             SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, NULL);
    if (status != SQLITE_OK || source == NULL || !has_valid_integrity(source) ||
        !has_prs_schema(source)) {
        if (source != NULL) {
            (void)sqlite3_close_v2(source);
        }
        set_error(error, error_size, "The selected backup is not a valid PRS database.");
        return false;
    }
    if (!copy_database(database->connection, source)) {
        (void)sqlite3_close_v2(source);
        set_error(error, error_size, "Unable to restore the selected backup.");
        return false;
    }
    (void)sqlite3_close_v2(source);
    prs_log(PRS_LOG_WARNING, "backup", "Database restored from validated backup.");
    return true;
}
