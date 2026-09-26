#include "prs/migrations.h"

#include <stdio.h>

#include "prs/logging.h"

typedef struct {
    int version;
    const char *description;
    const char *sql;
} PrsMigration;

static const PrsMigration MIGRATIONS[] = {
    {
        .version = 1,
        .description = "Initial clinical registration schema",
        .sql =
            "CREATE TABLE users ("
            " user_id INTEGER PRIMARY KEY,"
            " username TEXT NOT NULL COLLATE NOCASE UNIQUE,"
            " password_hash TEXT NOT NULL,"
            " role TEXT NOT NULL CHECK (role IN ('RECEPTIONIST', 'CLINICIAN', 'ADMINISTRATOR')),"
            " active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0, 1)),"
            " created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE patients ("
            " patient_key INTEGER PRIMARY KEY,"
            " patient_id TEXT NOT NULL UNIQUE,"
            " first_name TEXT NOT NULL,"
            " last_name TEXT NOT NULL,"
            " date_of_birth TEXT NOT NULL,"
            " sex_at_registration TEXT NOT NULL,"
            " phone TEXT NOT NULL,"
            " email TEXT,"
            " address TEXT NOT NULL,"
            " emergency_contact_name TEXT NOT NULL,"
            " emergency_contact_phone TEXT NOT NULL,"
            " status TEXT NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE', 'INACTIVE')),"
            " created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " created_by INTEGER NOT NULL REFERENCES users(user_id),"
            " updated_by INTEGER NOT NULL REFERENCES users(user_id)"
            ");"
            "CREATE INDEX patient_name_dob_idx ON patients(last_name, first_name, date_of_birth);"
            "CREATE INDEX patient_phone_idx ON patients(phone);"
            "CREATE TABLE medical_history_entries ("
            " history_id INTEGER PRIMARY KEY,"
            " patient_key INTEGER NOT NULL REFERENCES patients(patient_key),"
            " blood_group TEXT CHECK (blood_group IN ('A+', 'A-', 'B+', 'B-', 'AB+', 'AB-', 'O+', 'O-', 'UNKNOWN')),"
            " allergies TEXT,"
            " conditions TEXT,"
            " medications TEXT,"
            " surgeries TEXT,"
            " clinical_notes TEXT,"
            " author_user_id INTEGER NOT NULL REFERENCES users(user_id),"
            " created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE INDEX medical_history_patient_idx ON medical_history_entries(patient_key, updated_at DESC);"
            "CREATE TABLE providers ("
            " provider_id INTEGER PRIMARY KEY,"
            " display_name TEXT NOT NULL,"
            " specialty TEXT,"
            " active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0, 1)),"
            " created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE appointments ("
            " appointment_id INTEGER PRIMARY KEY,"
            " patient_key INTEGER NOT NULL REFERENCES patients(patient_key),"
            " provider_id INTEGER NOT NULL REFERENCES providers(provider_id),"
            " start_at TEXT NOT NULL,"
            " end_at TEXT NOT NULL,"
            " reason TEXT NOT NULL,"
            " status TEXT NOT NULL CHECK (status IN ('SCHEDULED', 'CHECKED_IN', 'COMPLETED', 'CANCELLED', 'NO_SHOW')),"
            " cancellation_reason TEXT,"
            " created_by INTEGER NOT NULL REFERENCES users(user_id),"
            " created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " CHECK (end_at > start_at)"
            ");"
            "CREATE INDEX appointment_provider_time_idx ON appointments(provider_id, start_at);"
            "CREATE INDEX appointment_patient_time_idx ON appointments(patient_key, start_at DESC);"
            "CREATE TABLE audit_logs ("
            " audit_id INTEGER PRIMARY KEY,"
            " actor_user_id INTEGER REFERENCES users(user_id),"
            " action TEXT NOT NULL,"
            " entity_type TEXT NOT NULL,"
            " entity_id TEXT NOT NULL,"
            " old_value_summary TEXT,"
            " new_value_summary TEXT,"
            " created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE INDEX audit_entity_idx ON audit_logs(entity_type, entity_id, created_at DESC);"
            "CREATE INDEX audit_actor_idx ON audit_logs(actor_user_id, created_at DESC);",
    },
    {
        .version = 2,
        .description = "Patient identifier sequences",
        .sql =
            "CREATE TABLE patient_id_sequences ("
            " sequence_date TEXT PRIMARY KEY,"
            " next_value INTEGER NOT NULL CHECK (next_value > 0)"
            ");",
    },
    {
        .version = 3,
        .description = "Extended registration and duplicate-detection fields",
        .sql =
            "ALTER TABLE patients ADD COLUMN clinic_identifier TEXT;"
            "ALTER TABLE patients ADD COLUMN insurance_reference TEXT;"
            "ALTER TABLE patients ADD COLUMN guardian_name TEXT;"
            "ALTER TABLE patients ADD COLUMN guardian_phone TEXT;"
            "ALTER TABLE patients ADD COLUMN patient_notes TEXT;"
            "CREATE UNIQUE INDEX patient_clinic_identifier_unique "
            "ON patients(clinic_identifier COLLATE NOCASE) "
            "WHERE clinic_identifier IS NOT NULL AND clinic_identifier <> '';",
    },
};

static void set_error(char *error_message, size_t error_message_size,
                      const char *message) {
    if (error_message != NULL && error_message_size > 0U) {
        (void)snprintf(error_message, error_message_size, "%s", message);
    }
}

static bool execute(sqlite3 *connection, const char *sql, char *error_message,
                    size_t error_message_size) {
    char *sqlite_error = NULL;
    const int status = sqlite3_exec(connection, sql, NULL, NULL, &sqlite_error);

    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size,
                  sqlite_error != NULL ? sqlite_error : sqlite3_errmsg(connection));
        sqlite3_free(sqlite_error);
        return false;
    }
    return true;
}

static bool migration_is_applied(sqlite3 *connection, int version,
                                 bool *is_applied, char *error_message,
                                 size_t error_message_size) {
    sqlite3_stmt *statement = NULL;
    int status = SQLITE_ERROR;

    *is_applied = false;
    status = sqlite3_prepare_v2(connection,
                                "SELECT 1 FROM schema_migrations WHERE version = ?;",
                                -1, &statement, NULL);
    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size, sqlite3_errmsg(connection));
        return false;
    }
    (void)sqlite3_bind_int(statement, 1, version);
    status = sqlite3_step(statement);
    if (status == SQLITE_ROW) {
        *is_applied = true;
    } else if (status != SQLITE_DONE) {
        set_error(error_message, error_message_size, sqlite3_errmsg(connection));
        (void)sqlite3_finalize(statement);
        return false;
    }
    (void)sqlite3_finalize(statement);
    return true;
}

static bool record_migration(sqlite3 *connection, const PrsMigration *migration,
                             char *error_message, size_t error_message_size) {
    sqlite3_stmt *statement = NULL;
    int status = sqlite3_prepare_v2(
        connection,
        "INSERT INTO schema_migrations(version, description) VALUES (?, ?);", -1,
        &statement, NULL);

    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size, sqlite3_errmsg(connection));
        return false;
    }
    (void)sqlite3_bind_int(statement, 1, migration->version);
    (void)sqlite3_bind_text(statement, 2, migration->description, -1,
                            SQLITE_STATIC);
    status = sqlite3_step(statement);
    if (status != SQLITE_DONE) {
        set_error(error_message, error_message_size, sqlite3_errmsg(connection));
        (void)sqlite3_finalize(statement);
        return false;
    }
    (void)sqlite3_finalize(statement);
    return true;
}

bool prs_migrations_apply(sqlite3 *connection, char *error_message,
                          size_t error_message_size) {
    size_t index = 0U;

    if (connection == NULL) {
        set_error(error_message, error_message_size, "Database connection is required.");
        return false;
    }
    if (!execute(connection,
                 "CREATE TABLE IF NOT EXISTS schema_migrations ("
                 "version INTEGER PRIMARY KEY, description TEXT NOT NULL,"
                 "applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);",
                 error_message, error_message_size)) {
        return false;
    }
    for (index = 0U; index < sizeof(MIGRATIONS) / sizeof(MIGRATIONS[0]); ++index) {
        bool is_applied = false;
        const PrsMigration *migration = &MIGRATIONS[index];

        if (!migration_is_applied(connection, migration->version, &is_applied,
                                  error_message, error_message_size)) {
            return false;
        }
        if (is_applied) {
            continue;
        }
        if (!execute(connection, "BEGIN IMMEDIATE;", error_message,
                     error_message_size) ||
            !execute(connection, migration->sql, error_message, error_message_size) ||
            !record_migration(connection, migration, error_message,
                              error_message_size) ||
            !execute(connection, "COMMIT;", error_message, error_message_size)) {
            (void)sqlite3_exec(connection, "ROLLBACK;", NULL, NULL, NULL);
            return false;
        }
        prs_log(PRS_LOG_INFO, "migrations", "Applied database migration.");
    }
    return true;
}
