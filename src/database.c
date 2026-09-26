#include "prs/database.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "prs/app_config.h"
#include "prs/logging.h"
#include "prs/migrations.h"

static void set_error(char *error_message, size_t error_message_size,
                      const char *message) {
    if (error_message != NULL && error_message_size > 0U) {
        (void)snprintf(error_message, error_message_size, "%s", message);
    }
}

static bool execute_pragma(sqlite3 *connection, const char *sql,
                           char *error_message, size_t error_message_size) {
    char *sqlite_error = NULL;
    const int status = sqlite3_exec(connection, sql, NULL, NULL, &sqlite_error);

    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size,
                  sqlite_error != NULL ? sqlite_error : "Database configuration failed.");
        sqlite3_free(sqlite_error);
        return false;
    }
    return true;
}

bool prs_database_open(PrsDatabase *database, const char *path,
                       char *error_message, size_t error_message_size) {
    int status = SQLITE_ERROR;

    if (database == NULL || path == NULL) {
        set_error(error_message, error_message_size,
                  "Database configuration is incomplete.");
        return false;
    }
    database->connection = NULL;
    set_error(error_message, error_message_size, "");
    if (!prs_ensure_parent_directory(path, error_message, error_message_size)) {
        return false;
    }

    status = sqlite3_open_v2(path, &database->connection,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                 SQLITE_OPEN_FULLMUTEX,
                             NULL);
    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size,
                  database->connection != NULL ? sqlite3_errmsg(database->connection)
                                               : "Unable to open database.");
        prs_database_close(database);
        return false;
    }
    if (chmod(path, S_IRUSR | S_IWUSR) != 0) {
        set_error(error_message, error_message_size,
                  "Unable to protect the database file.");
        prs_database_close(database);
        return false;
    }
    if (sqlite3_busy_timeout(database->connection, 5000) != SQLITE_OK ||
        !execute_pragma(database->connection, "PRAGMA foreign_keys = ON;",
                        error_message, error_message_size) ||
        !execute_pragma(database->connection, "PRAGMA journal_mode = WAL;",
                        error_message, error_message_size) ||
        !execute_pragma(database->connection, "PRAGMA synchronous = FULL;",
                        error_message, error_message_size)) {
        if (error_message != NULL && error_message[0] == '\0') {
            set_error(error_message, error_message_size,
                      "Unable to configure the database connection.");
        }
        prs_database_close(database);
        return false;
    }
    if (!prs_migrations_apply(database->connection, error_message,
                              error_message_size)) {
        prs_database_close(database);
        return false;
    }
    prs_log(PRS_LOG_INFO, "database", "Database connection opened and migrated.");
    return true;
}

void prs_database_close(PrsDatabase *database) {
    if (database != NULL && database->connection != NULL) {
        (void)sqlite3_close_v2(database->connection);
        database->connection = NULL;
    }
}
