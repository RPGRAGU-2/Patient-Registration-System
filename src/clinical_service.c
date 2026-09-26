#include "prs/clinical_service.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "prs/auth.h"
#include "prs/logging.h"

static void set_error(char *error, size_t size, const char *message) {
    if (error != NULL && size > 0U) {
        (void)snprintf(error, size, "%s", message);
    }
}

static bool can_edit_history(const PrsUserSession *session) {
    return prs_session_has_role(session, PRS_ROLE_CLINICIAN) ||
           prs_session_has_role(session, PRS_ROLE_ADMINISTRATOR);
}

static bool can_manage_appointments(const PrsUserSession *session) {
    return prs_session_has_role(session, PRS_ROLE_RECEPTIONIST) ||
           prs_session_has_role(session, PRS_ROLE_ADMINISTRATOR);
}

static bool execute(sqlite3 *db, const char *sql) {
    return sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK;
}

static void copy_text(char *target, size_t target_size,
                      const unsigned char *value) {
    if (target != NULL && target_size > 0U) {
        (void)snprintf(target, target_size, "%s",
                       value != NULL ? (const char *)value : "");
    }
}

static bool find_patient_key(sqlite3 *db, const char *patient_id, bool active_only,
                             long long *patient_key) {
    sqlite3_stmt *statement = NULL;
    int status = SQLITE_ERROR;

    if (db == NULL || patient_id == NULL || patient_id[0] == '\0' ||
        patient_key == NULL) {
        return false;
    }
    status = sqlite3_prepare_v2(
        db, active_only
                ? "SELECT patient_key FROM patients WHERE patient_id = ? AND status = 'ACTIVE';"
                : "SELECT patient_key FROM patients WHERE patient_id = ?;",
        -1, &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_text(statement, 1, patient_id, -1, SQLITE_TRANSIENT);
        status = sqlite3_step(statement);
        if (status == SQLITE_ROW) {
            *patient_key = sqlite3_column_int64(statement, 0);
        }
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    return status == SQLITE_ROW;
}

static bool provider_is_active(sqlite3 *db, long long provider_id) {
    sqlite3_stmt *statement = NULL;
    int status = sqlite3_prepare_v2(
        db, "SELECT 1 FROM providers WHERE provider_id = ? AND active = 1;", -1,
        &statement, NULL);

    if (status == SQLITE_OK) {
        (void)sqlite3_bind_int64(statement, 1, provider_id);
        status = sqlite3_step(statement);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    return status == SQLITE_ROW;
}

static bool write_audit(sqlite3 *db, long long user_id, const char *action,
                        const char *entity_type, const char *entity_id,
                        const char *old_summary, const char *new_summary) {
    sqlite3_stmt *statement = NULL;
    int status = sqlite3_prepare_v2(
        db, "INSERT INTO audit_logs(actor_user_id, action, entity_type, entity_id, "
            "old_value_summary, new_value_summary) VALUES (?, ?, ?, ?, ?, ?);",
        -1, &statement, NULL);

    if (status == SQLITE_OK) {
        (void)sqlite3_bind_int64(statement, 1, user_id);
        (void)sqlite3_bind_text(statement, 2, action, -1, SQLITE_STATIC);
        (void)sqlite3_bind_text(statement, 3, entity_type, -1, SQLITE_STATIC);
        (void)sqlite3_bind_text(statement, 4, entity_id, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 5, old_summary, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 6, new_summary, -1, SQLITE_TRANSIENT);
        status = sqlite3_step(statement);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    return status == SQLITE_DONE;
}

static bool valid_blood_group(const char *value) {
    static const char *const blood_groups[] = {
        "", "A+", "A-", "B+", "B-", "AB+", "AB-", "O+", "O-", "UNKNOWN"};
    size_t index = 0U;

    if (value == NULL) {
        return false;
    }
    for (index = 0U; index < sizeof(blood_groups) / sizeof(blood_groups[0]); ++index) {
        if (strcmp(value, blood_groups[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static bool parse_datetime(const char *value, struct tm *result) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    size_t index = 0U;

    if (value == NULL || result == NULL || strlen(value) != 16U || value[4] != '-' ||
        value[7] != '-' || value[10] != ' ' || value[13] != ':') {
        return false;
    }
    for (index = 0U; index < 16U; ++index) {
        if (index != 4U && index != 7U && index != 10U && index != 13U &&
            !isdigit((unsigned char)value[index])) {
            return false;
        }
    }
    if (sscanf(value, "%4d-%2d-%2d %2d:%2d", &year, &month, &day, &hour,
               &minute) != 5 || year < 1900 || month < 1 || month > 12 ||
        hour > 23 || minute > 59) {
        return false;
    }
    if (month == 2 && is_leap_year(year)) {
        days[1] = 29;
    }
    if (day < 1 || day > days[month - 1]) {
        return false;
    }
    (void)memset(result, 0, sizeof(*result));
    result->tm_year = year - 1900;
    result->tm_mon = month - 1;
    result->tm_mday = day;
    result->tm_hour = hour;
    result->tm_min = minute;
    result->tm_isdst = -1;
    return true;
}

static bool valid_future_interval(const char *start_at, const char *end_at) {
    struct tm start_tm;
    struct tm end_tm;
    time_t start_time;
    time_t end_time;

    if (!parse_datetime(start_at, &start_tm) || !parse_datetime(end_at, &end_tm)) {
        return false;
    }
    start_time = mktime(&start_tm);
    end_time = mktime(&end_tm);
    return start_time != (time_t)-1 && end_time != (time_t)-1 &&
           start_time > time(NULL) && end_time > start_time;
}

static bool has_provider_conflict(sqlite3 *db, long long provider_id,
                                  long long excluded_id, const char *start_at,
                                  const char *end_at, bool *conflict) {
    sqlite3_stmt *statement = NULL;
    int status = sqlite3_prepare_v2(
        db, "SELECT EXISTS(SELECT 1 FROM appointments WHERE provider_id = ? "
            "AND appointment_id <> ? AND status IN ('SCHEDULED', 'CHECKED_IN') "
            "AND start_at < ? AND end_at > ?);",
        -1, &statement, NULL);

    *conflict = false;
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_int64(statement, 1, provider_id);
        (void)sqlite3_bind_int64(statement, 2, excluded_id);
        (void)sqlite3_bind_text(statement, 3, end_at, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 4, start_at, -1, SQLITE_TRANSIENT);
        status = sqlite3_step(statement);
        if (status == SQLITE_ROW) {
            *conflict = sqlite3_column_int(statement, 0) != 0;
        }
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    return status == SQLITE_ROW;
}

bool prs_medical_history_add(PrsDatabase *database, const PrsUserSession *session,
                             const char *patient_id,
                             const PrsMedicalHistoryEntry *entry, char *error,
                             size_t error_size) {
    sqlite3_stmt *statement = NULL;
    long long patient_key = 0;
    int status = SQLITE_ERROR;

    if (!can_edit_history(session)) {
        set_error(error, error_size,
                  "Only clinicians and administrators may change medical history.");
        return false;
    }
    if (database == NULL || database->connection == NULL || entry == NULL ||
        !valid_blood_group(entry->blood_group) ||
        strlen(entry->allergies) >= PRS_MEDICAL_TEXT_CAPACITY ||
        strlen(entry->conditions) >= PRS_MEDICAL_TEXT_CAPACITY ||
        strlen(entry->medications) >= PRS_MEDICAL_TEXT_CAPACITY ||
        strlen(entry->surgeries) >= PRS_MEDICAL_TEXT_CAPACITY ||
        strlen(entry->clinical_notes) >= PRS_MEDICAL_TEXT_CAPACITY ||
        !find_patient_key(database->connection, patient_id, false, &patient_key)) {
        set_error(error, error_size, "Medical history details are invalid.");
        return false;
    }
    if (!execute(database->connection, "BEGIN IMMEDIATE;")) {
        set_error(error, error_size, "Unable to save medical history.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "INSERT INTO medical_history_entries(patient_key, blood_group, allergies, "
        "conditions, medications, surgeries, clinical_notes, author_user_id) "
        "VALUES (?, NULLIF(?, ''), ?, ?, ?, ?, ?, ?);",
        -1, &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_int64(statement, 1, patient_key);
        (void)sqlite3_bind_text(statement, 2, entry->blood_group, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 3, entry->allergies, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 4, entry->conditions, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 5, entry->medications, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 6, entry->surgeries, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 7, entry->clinical_notes, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_int64(statement, 8, session->user_id);
        status = sqlite3_step(statement);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    if (status != SQLITE_DONE ||
        !write_audit(database->connection, session->user_id, "MEDICAL_HISTORY_ADDED",
                     "MEDICAL_HISTORY", patient_id, NULL,
                     "Medical history entry added.") ||
        !execute(database->connection, "COMMIT;")) {
        (void)execute(database->connection, "ROLLBACK;");
        set_error(error, error_size, "Unable to save medical history.");
        return false;
    }
    prs_log(PRS_LOG_INFO, "clinical", "Medical-history entry added.");
    return true;
}

bool prs_medical_history_update(PrsDatabase *database, const PrsUserSession *session,
                                const char *patient_id,
                                const PrsMedicalHistoryEntry *entry,
                                char *error, size_t error_size) {
    sqlite3_stmt *statement = NULL;
    long long patient_key = 0;
    int status = SQLITE_ERROR;
    long long history_id = 0;

    if (!can_edit_history(session)) {
        set_error(error, error_size,
                  "Only clinicians and administrators may change medical history.");
        return false;
    }
    if (database == NULL || database->connection == NULL || entry == NULL ||
        patient_id == NULL || patient_id[0] == '\0' ||
        !valid_blood_group(entry->blood_group) ||
        strlen(entry->allergies) >= PRS_MEDICAL_TEXT_CAPACITY ||
        strlen(entry->conditions) >= PRS_MEDICAL_TEXT_CAPACITY ||
        strlen(entry->medications) >= PRS_MEDICAL_TEXT_CAPACITY ||
        strlen(entry->surgeries) >= PRS_MEDICAL_TEXT_CAPACITY ||
        strlen(entry->clinical_notes) >= PRS_MEDICAL_TEXT_CAPACITY ||
        !find_patient_key(database->connection, patient_id, false, &patient_key)) {
        set_error(error, error_size, "Medical history details are invalid.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "SELECT history_id FROM medical_history_entries WHERE patient_key = ? ORDER BY history_id DESC LIMIT 1;",
        -1, &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_int64(statement, 1, patient_key);
        status = sqlite3_step(statement);
        if (status == SQLITE_ROW) {
            history_id = sqlite3_column_int64(statement, 0);
        }
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    if (status != SQLITE_ROW) {
        set_error(error, error_size, "No medical history has been recorded.");
        return false;
    }
    if (!execute(database->connection, "BEGIN IMMEDIATE;")) {
        set_error(error, error_size, "Unable to save medical history.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "UPDATE medical_history_entries SET blood_group = NULLIF(?, ''), allergies = ?, "
        "conditions = ?, medications = ?, surgeries = ?, clinical_notes = ?, "
        "author_user_id = ?, updated_at = CURRENT_TIMESTAMP WHERE history_id = ?;",
        -1, &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_text(statement, 1, entry->blood_group, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 2, entry->allergies, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 3, entry->conditions, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 4, entry->medications, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 5, entry->surgeries, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 6, entry->clinical_notes, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_int64(statement, 7, session->user_id);
        (void)sqlite3_bind_int64(statement, 8, history_id);
        status = sqlite3_step(statement);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    if (status != SQLITE_DONE ||
        !write_audit(database->connection, session->user_id, "MEDICAL_HISTORY_UPDATED",
                     "MEDICAL_HISTORY", patient_id, NULL,
                     "Medical history entry updated.") ||
        !execute(database->connection, "COMMIT;")) {
        (void)execute(database->connection, "ROLLBACK;");
        set_error(error, error_size, "Unable to save medical history.");
        return false;
    }
    prs_log(PRS_LOG_INFO, "clinical", "Medical-history entry updated.");
    return true;
}

bool prs_medical_history_delete(PrsDatabase *database, const PrsUserSession *session,
                                const char *patient_id,
                                char *error, size_t error_size) {
    sqlite3_stmt *statement = NULL;
    long long patient_key = 0;
    int status = SQLITE_ERROR;

    if (!can_edit_history(session)) {
        set_error(error, error_size,
                  "Only clinicians and administrators may change medical history.");
        return false;
    }
    if (database == NULL || database->connection == NULL || patient_id == NULL ||
        patient_id[0] == '\0' || !find_patient_key(database->connection, patient_id, false, &patient_key)) {
        set_error(error, error_size, "Patient record is invalid.");
        return false;
    }
    if (!execute(database->connection, "BEGIN IMMEDIATE;")) {
        set_error(error, error_size, "Unable to delete medical history.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "DELETE FROM medical_history_entries WHERE patient_key = ?;",
        -1, &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_int64(statement, 1, patient_key);
        status = sqlite3_step(statement);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    if (status != SQLITE_DONE ||
        !write_audit(database->connection, session->user_id, "MEDICAL_HISTORY_DELETED",
                     "MEDICAL_HISTORY", patient_id, NULL,
                     "Medical history entry deleted.") ||
        !execute(database->connection, "COMMIT;")) {
        (void)execute(database->connection, "ROLLBACK;");
        set_error(error, error_size, "Unable to delete medical history.");
        return false;
    }
    prs_log(PRS_LOG_INFO, "clinical", "Medical-history entry deleted.");
    return true;
}

bool prs_medical_history_latest(PrsDatabase *database, const PrsUserSession *session,
                                const char *patient_id,
                                PrsMedicalHistoryEntry *entry, char *error,
                                size_t error_size) {
    sqlite3_stmt *statement = NULL;
    int status = SQLITE_ERROR;

    if (entry != NULL) {
        (void)memset(entry, 0, sizeof(*entry));
    }
    if (!can_edit_history(session) || database == NULL || database->connection == NULL ||
        patient_id == NULL || entry == NULL) {
        set_error(error, error_size, "Medical history is restricted.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "SELECT h.history_id, h.blood_group, h.allergies, h.conditions, h.medications, "
        "h.surgeries, h.clinical_notes, h.author_user_id, h.created_at "
        "FROM medical_history_entries h JOIN patients p ON p.patient_key = h.patient_key "
        "WHERE p.patient_id = ? ORDER BY h.history_id DESC LIMIT 1;",
        -1, &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_text(statement, 1, patient_id, -1, SQLITE_TRANSIENT);
        status = sqlite3_step(statement);
    }
    if (status != SQLITE_ROW) {
        if (statement != NULL) {
            (void)sqlite3_finalize(statement);
        }
        set_error(error, error_size, "No medical history has been recorded.");
        return false;
    }
    entry->history_id = sqlite3_column_int64(statement, 0);
    entry->author_user_id = sqlite3_column_int64(statement, 7);
    copy_text(entry->blood_group, sizeof(entry->blood_group), sqlite3_column_text(statement, 1));
    copy_text(entry->allergies, sizeof(entry->allergies), sqlite3_column_text(statement, 2));
    copy_text(entry->conditions, sizeof(entry->conditions), sqlite3_column_text(statement, 3));
    copy_text(entry->medications, sizeof(entry->medications), sqlite3_column_text(statement, 4));
    copy_text(entry->surgeries, sizeof(entry->surgeries), sqlite3_column_text(statement, 5));
    copy_text(entry->clinical_notes, sizeof(entry->clinical_notes), sqlite3_column_text(statement, 6));
    copy_text(entry->created_at, sizeof(entry->created_at), sqlite3_column_text(statement, 8));
    (void)sqlite3_finalize(statement);
    return true;
}

bool prs_provider_create(PrsDatabase *database, const PrsUserSession *session,
                         const char *display_name, const char *specialty,
                         long long *provider_id, char *error, size_t error_size) {
    sqlite3_stmt *statement = NULL;
    int status = SQLITE_ERROR;

    if (provider_id != NULL) {
        *provider_id = 0;
    }
    if (!prs_session_has_role(session, PRS_ROLE_ADMINISTRATOR) ||
        database == NULL || database->connection == NULL || display_name == NULL ||
        display_name[0] == '\0' || strlen(display_name) >= PRS_NAME_CAPACITY ||
        (specialty != NULL && strlen(specialty) >= PRS_NAME_CAPACITY)) {
        set_error(error, error_size, "Provider details are invalid or not permitted.");
        return false;
    }
    status = sqlite3_prepare_v2(database->connection,
                                "INSERT INTO providers(display_name, specialty) VALUES (?, ?);",
                                -1, &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_text(statement, 1, display_name, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 2, specialty != NULL ? specialty : "", -1,
                                SQLITE_TRANSIENT);
        status = sqlite3_step(statement);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    if (status != SQLITE_DONE) {
        set_error(error, error_size, "Unable to create provider.");
        return false;
    }
    if (provider_id != NULL) {
        *provider_id = sqlite3_last_insert_rowid(database->connection);
    }
    return true;
}

bool prs_appointment_create(PrsDatabase *database, const PrsUserSession *session,
                            const char *patient_id, long long provider_id,
                            const char *start_at, const char *end_at,
                            const char *reason, long long *appointment_id,
                            char *error, size_t error_size) {
    sqlite3_stmt *statement = NULL;
    long long patient_key = 0;
    long long created_id = 0;
    bool conflict = false;
    char audit_id[32];
    int status = SQLITE_ERROR;

    if (appointment_id != NULL) {
        *appointment_id = 0;
    }
    if (!can_manage_appointments(session) || database == NULL ||
        database->connection == NULL || provider_id <= 0 || reason == NULL ||
        reason[0] == '\0' || strlen(reason) >= PRS_APPOINTMENT_REASON_CAPACITY ||
        !valid_future_interval(start_at, end_at) ||
        !find_patient_key(database->connection, patient_id, true, &patient_key) ||
        !provider_is_active(database->connection, provider_id)) {
        set_error(error, error_size, "Appointment details are invalid or not permitted.");
        return false;
    }
    if (!execute(database->connection, "BEGIN IMMEDIATE;") ||
        !has_provider_conflict(database->connection, provider_id, 0, start_at, end_at,
                               &conflict) || conflict) {
        (void)execute(database->connection, "ROLLBACK;");
        set_error(error, error_size, conflict ? "The provider has an overlapping appointment."
                                              : "Unable to schedule appointment.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "INSERT INTO appointments(patient_key, provider_id, start_at, end_at, reason, "
        "status, created_by) VALUES (?, ?, ?, ?, ?, 'SCHEDULED', ?);",
        -1, &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_int64(statement, 1, patient_key);
        (void)sqlite3_bind_int64(statement, 2, provider_id);
        (void)sqlite3_bind_text(statement, 3, start_at, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 4, end_at, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(statement, 5, reason, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_int64(statement, 6, session->user_id);
        status = sqlite3_step(statement);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    created_id = sqlite3_last_insert_rowid(database->connection);
    (void)snprintf(audit_id, sizeof(audit_id), "%lld", created_id);
    if (status != SQLITE_DONE ||
        !write_audit(database->connection, session->user_id, "APPOINTMENT_CREATED",
                     "APPOINTMENT", audit_id, NULL, "Appointment scheduled.") ||
        !execute(database->connection, "COMMIT;")) {
        (void)execute(database->connection, "ROLLBACK;");
        set_error(error, error_size, "Unable to schedule appointment.");
        return false;
    }
    if (appointment_id != NULL) {
        *appointment_id = created_id;
    }
    prs_log(PRS_LOG_INFO, "clinical", "Appointment scheduled.");
    return true;
}

bool prs_appointments_for_patient(PrsDatabase *database, const PrsUserSession *session,
                                  const char *patient_id,
                                  PrsAppointment *appointments, size_t capacity,
                                  size_t *count, char *error, size_t error_size) {
    sqlite3_stmt *statement = NULL;
    size_t total = 0U;
    int status = SQLITE_ERROR;

    if (count != NULL) {
        *count = 0U;
    }
    if (appointments != NULL && capacity > 0U) {
        (void)memset(appointments, 0, capacity * sizeof(*appointments));
    }
    if (session == NULL || !session->authenticated || database == NULL ||
        database->connection == NULL || patient_id == NULL || patient_id[0] == '\0') {
        set_error(error, error_size, "Appointment access is restricted.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "SELECT a.appointment_id, a.patient_key, a.provider_id, v.display_name, "
        "a.start_at, a.end_at, a.reason, a.status, COALESCE(a.cancellation_reason, '') "
        "FROM appointments a JOIN patients p ON p.patient_key = a.patient_key "
        "JOIN providers v ON v.provider_id = a.provider_id WHERE p.patient_id = ? "
        "ORDER BY a.start_at DESC;",
        -1, &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_text(statement, 1, patient_id, -1, SQLITE_TRANSIENT);
        while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
            if (appointments != NULL && total < capacity) {
                PrsAppointment *item = &appointments[total];
                item->appointment_id = sqlite3_column_int64(statement, 0);
                item->patient_key = sqlite3_column_int64(statement, 1);
                item->provider_id = sqlite3_column_int64(statement, 2);
                copy_text(item->provider_name, sizeof(item->provider_name), sqlite3_column_text(statement, 3));
                copy_text(item->start_at, sizeof(item->start_at), sqlite3_column_text(statement, 4));
                copy_text(item->end_at, sizeof(item->end_at), sqlite3_column_text(statement, 5));
                copy_text(item->reason, sizeof(item->reason), sqlite3_column_text(statement, 6));
                copy_text(item->status, sizeof(item->status), sqlite3_column_text(statement, 7));
                copy_text(item->cancellation_reason, sizeof(item->cancellation_reason), sqlite3_column_text(statement, 8));
            }
            ++total;
        }
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    if (status != SQLITE_DONE) {
        set_error(error, error_size, "Unable to retrieve appointments.");
        return false;
    }
    if (count != NULL) {
        *count = total;
    }
    return true;
}

bool prs_appointment_reschedule(PrsDatabase *database,
                                const PrsUserSession *session,
                                long long appointment_id, const char *start_at,
                                const char *end_at, const char *reason,
                                char *error, size_t error_size) {
    sqlite3_stmt *lookup = NULL;
    sqlite3_stmt *update = NULL;
    long long provider_id = 0;
    bool conflict = false;
    char audit_id[32];
    int status = SQLITE_ERROR;

    if (!can_manage_appointments(session) || database == NULL ||
        database->connection == NULL || appointment_id <= 0 || reason == NULL ||
        reason[0] == '\0' || strlen(reason) >= PRS_APPOINTMENT_REASON_CAPACITY ||
        !valid_future_interval(start_at, end_at) ||
        !execute(database->connection, "BEGIN IMMEDIATE;")) {
        set_error(error, error_size, "Rescheduling details are invalid or not permitted.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "SELECT provider_id FROM appointments WHERE appointment_id = ? "
        "AND status IN ('SCHEDULED', 'CHECKED_IN');",
        -1, &lookup, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_int64(lookup, 1, appointment_id);
        status = sqlite3_step(lookup);
        if (status == SQLITE_ROW) {
            provider_id = sqlite3_column_int64(lookup, 0);
        }
    }
    if (lookup != NULL) {
        (void)sqlite3_finalize(lookup);
    }
    if (status != SQLITE_ROW ||
        !has_provider_conflict(database->connection, provider_id, appointment_id,
                               start_at, end_at, &conflict) || conflict) {
        (void)execute(database->connection, "ROLLBACK;");
        set_error(error, error_size, conflict ? "The provider has an overlapping appointment."
                                              : "Appointment cannot be rescheduled.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "UPDATE appointments SET start_at = ?, end_at = ?, reason = ?, "
        "status = 'SCHEDULED', updated_at = CURRENT_TIMESTAMP WHERE appointment_id = ?;",
        -1, &update, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_text(update, 1, start_at, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(update, 2, end_at, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(update, 3, reason, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_int64(update, 4, appointment_id);
        status = sqlite3_step(update);
    }
    if (update != NULL) {
        (void)sqlite3_finalize(update);
    }
    (void)snprintf(audit_id, sizeof(audit_id), "%lld", appointment_id);
    if (status != SQLITE_DONE ||
        !write_audit(database->connection, session->user_id, "APPOINTMENT_RESCHEDULED",
                     "APPOINTMENT", audit_id, "Previous schedule", "Rescheduled") ||
        !execute(database->connection, "COMMIT;")) {
        (void)execute(database->connection, "ROLLBACK;");
        set_error(error, error_size, "Unable to reschedule appointment.");
        return false;
    }
    prs_log(PRS_LOG_INFO, "clinical", "Appointment rescheduled.");
    return true;
}

bool prs_appointment_set_status(PrsDatabase *database,
                                const PrsUserSession *session,
                                long long appointment_id, const char *new_status,
                                const char *cancellation_reason, char *error,
                                size_t error_size) {
    sqlite3_stmt *statement = NULL;
    const bool cancelled = new_status != NULL && strcmp(new_status, "CANCELLED") == 0;
    const bool valid_status = new_status != NULL &&
        (strcmp(new_status, "SCHEDULED") == 0 || strcmp(new_status, "CHECKED_IN") == 0 ||
         strcmp(new_status, "COMPLETED") == 0 || strcmp(new_status, "CANCELLED") == 0 ||
         strcmp(new_status, "NO_SHOW") == 0);
    char audit_id[32];
    int status = SQLITE_ERROR;

    if (!can_manage_appointments(session) || database == NULL ||
        database->connection == NULL || appointment_id <= 0 || !valid_status ||
        (cancelled && (cancellation_reason == NULL || cancellation_reason[0] == '\0' ||
                       strlen(cancellation_reason) >= PRS_APPOINTMENT_REASON_CAPACITY)) ||
        !execute(database->connection, "BEGIN IMMEDIATE;")) {
        set_error(error, error_size, "Appointment status details are invalid or not permitted.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "UPDATE appointments SET status = ?, cancellation_reason = ?, "
        "updated_at = CURRENT_TIMESTAMP WHERE appointment_id = ? "
        "AND status IN ('SCHEDULED', 'CHECKED_IN');",
        -1, &statement, NULL);
    if (status == SQLITE_OK) {
        (void)sqlite3_bind_text(statement, 1, new_status, -1, SQLITE_STATIC);
        if (cancelled) {
            (void)sqlite3_bind_text(statement, 2, cancellation_reason, -1, SQLITE_TRANSIENT);
        } else {
            (void)sqlite3_bind_null(statement, 2);
        }
        (void)sqlite3_bind_int64(statement, 3, appointment_id);
        status = sqlite3_step(statement);
    }
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    (void)snprintf(audit_id, sizeof(audit_id), "%lld", appointment_id);
    if (status != SQLITE_DONE || sqlite3_changes(database->connection) != 1 ||
        !write_audit(database->connection, session->user_id,
                     cancelled ? "APPOINTMENT_CANCELLED" : "APPOINTMENT_STATUS_UPDATED",
                     "APPOINTMENT", audit_id, "Previous status", new_status) ||
        !execute(database->connection, "COMMIT;")) {
        (void)execute(database->connection, "ROLLBACK;");
        set_error(error, error_size, "Appointment was not found or cannot change status.");
        return false;
    }
    prs_log(PRS_LOG_INFO, "clinical", cancelled ? "Appointment cancelled."
                                                  : "Appointment status updated.");
    return true;
}

bool prs_appointment_cancel(PrsDatabase *database, const PrsUserSession *session,
                            long long appointment_id, const char *reason,
                            char *error, size_t error_size) {
    return prs_appointment_set_status(database, session, appointment_id, "CANCELLED",
                                      reason, error, error_size);
}
