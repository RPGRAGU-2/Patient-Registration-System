#include "prs/patient_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "prs/auth.h"
#include "prs/logging.h"

static void set_error(char *error_message, size_t error_message_size,
                      const char *message) {
    if (error_message != NULL && error_message_size > 0U) {
        (void)snprintf(error_message, error_message_size, "%s", message);
    }
}

static void copy_text(char *destination, size_t destination_size,
                      const unsigned char *source) {
    if (destination != NULL && destination_size > 0U) {
        (void)snprintf(destination, destination_size, "%s",
                       source != NULL ? (const char *)source : "");
    }
}

static bool can_view_patients(const PrsUserSession *session) {
    return session != NULL && session->authenticated;
}

static bool can_write_patients(const PrsUserSession *session) {
    return prs_session_has_role(session, PRS_ROLE_RECEPTIONIST) ||
           prs_session_has_role(session, PRS_ROLE_ADMINISTRATOR);
}

static bool execute(sqlite3 *connection, const char *sql, char *error_message,
                    size_t error_message_size) {
    char *sqlite_error = NULL;
    const int status = sqlite3_exec(connection, sql, NULL, NULL, &sqlite_error);

    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size,
                  sqlite_error != NULL ? sqlite_error : "Database operation failed.");
        sqlite3_free(sqlite_error);
        return false;
    }
    return true;
}

static void clear_summaries(PrsPatientSummary *summaries, size_t capacity,
                            size_t *count) {
    if (summaries != NULL && capacity > 0U) {
        (void)memset(summaries, 0, capacity * sizeof(*summaries));
    }
    if (count != NULL) {
        *count = 0U;
    }
}

static void populate_summary(sqlite3_stmt *statement, PrsPatientSummary *summary) {
    summary->patient_key = sqlite3_column_int64(statement, 0);
    copy_text(summary->patient_id, sizeof(summary->patient_id),
              sqlite3_column_text(statement, 1));
    copy_text(summary->first_name, sizeof(summary->first_name),
              sqlite3_column_text(statement, 2));
    copy_text(summary->last_name, sizeof(summary->last_name),
              sqlite3_column_text(statement, 3));
    copy_text(summary->date_of_birth, sizeof(summary->date_of_birth),
              sqlite3_column_text(statement, 4));
    copy_text(summary->phone, sizeof(summary->phone), sqlite3_column_text(statement, 5));
    copy_text(summary->status, sizeof(summary->status), sqlite3_column_text(statement, 6));
}

static bool find_duplicates(sqlite3 *connection, const PrsPatientDraft *draft,
                            PrsPatientSummary *candidates, size_t capacity,
                            size_t *candidate_count, char *error_message,
                            size_t error_message_size) {
    static const char SQL[] =
        "SELECT patient_key, patient_id, first_name, last_name, date_of_birth, phone, status "
        "FROM patients WHERE status = 'ACTIVE' AND "
        "((NULLIF(?, '') IS NOT NULL AND clinic_identifier = ? COLLATE NOCASE) "
        "OR (first_name = ? COLLATE NOCASE AND last_name = ? COLLATE NOCASE "
        "AND date_of_birth = ?) OR (phone = ? AND date_of_birth = ?)) "
        "ORDER BY patient_key DESC;";
    sqlite3_stmt *statement = NULL;
    int status = SQLITE_ERROR;
    size_t total = 0U;

    clear_summaries(candidates, capacity, candidate_count);
    status = sqlite3_prepare_v2(connection, SQL, -1, &statement, NULL);
    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size, "Unable to check duplicate patients.");
        return false;
    }
    (void)sqlite3_bind_text(statement, 1, draft->clinic_identifier, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 2, draft->clinic_identifier, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 3, draft->first_name, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 4, draft->last_name, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 5, draft->date_of_birth, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 6, draft->phone, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 7, draft->date_of_birth, -1, SQLITE_TRANSIENT);
    while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
        if (candidates != NULL && total < capacity) {
            populate_summary(statement, &candidates[total]);
        }
        ++total;
    }
    (void)sqlite3_finalize(statement);
    if (status != SQLITE_DONE) {
        set_error(error_message, error_message_size, "Unable to check duplicate patients.");
        return false;
    }
    if (candidate_count != NULL) {
        *candidate_count = total;
    }
    return true;
}

static bool generate_patient_id(sqlite3 *connection, char *patient_id,
                                size_t patient_id_size, char *error_message,
                                size_t error_message_size) {
    sqlite3_stmt *select_statement = NULL;
    sqlite3_stmt *write_statement = NULL;
    char date_part[9];
    time_t now;
    struct tm *local_time = NULL;
    long long sequence = 0;
    int status = SQLITE_ERROR;
    bool exists = false;

    now = time(NULL);
    local_time = localtime(&now);
    if (local_time == NULL || strftime(date_part, sizeof(date_part), "%Y%m%d", local_time) == 0U) {
        set_error(error_message, error_message_size, "Unable to generate the patient ID date.");
        return false;
    }
    status = sqlite3_prepare_v2(
        connection,
        "SELECT next_value FROM patient_id_sequences WHERE sequence_date = ?;", -1,
        &select_statement, NULL);
    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size, "Unable to reserve a patient ID.");
        return false;
    }
    (void)sqlite3_bind_text(select_statement, 1, date_part, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(select_statement) == SQLITE_ROW) {
        exists = true;
        sequence = sqlite3_column_int64(select_statement, 0);
    }
    (void)sqlite3_finalize(select_statement);
    if (sequence <= 0 || sequence > 9999) {
        if (exists) {
            set_error(error_message, error_message_size,
                      "Daily patient ID capacity has been reached.");
            return false;
        }
        sequence = 1;
    }
    status = sqlite3_prepare_v2(
        connection,
        exists
            ? "UPDATE patient_id_sequences SET next_value = ? WHERE sequence_date = ?;"
            : "INSERT INTO patient_id_sequences(sequence_date, next_value) VALUES (?, ?);",
        -1, &write_statement, NULL);
    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size, "Unable to reserve a patient ID.");
        return false;
    }
    if (exists) {
        (void)sqlite3_bind_int64(write_statement, 1, sequence + 1);
        (void)sqlite3_bind_text(write_statement, 2, date_part, -1, SQLITE_TRANSIENT);
    } else {
        (void)sqlite3_bind_text(write_statement, 1, date_part, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_int64(write_statement, 2, sequence + 1);
    }
    if (sqlite3_step(write_statement) != SQLITE_DONE) {
        (void)sqlite3_finalize(write_statement);
        set_error(error_message, error_message_size, "Unable to reserve a patient ID.");
        return false;
    }
    (void)sqlite3_finalize(write_statement);
    if ((size_t)snprintf(patient_id, patient_id_size, "PAT-%s-%04lld", date_part,
                         sequence) >= patient_id_size) {
        set_error(error_message, error_message_size, "Patient ID buffer is too small.");
        return false;
    }
    return true;
}

static bool insert_audit_log(sqlite3 *connection, long long actor_user_id,
                             const char *action, const char *patient_id,
                             const char *old_summary, const char *new_summary,
                             char *error_message,
                             size_t error_message_size) {
    sqlite3_stmt *statement = NULL;
    int status = sqlite3_prepare_v2(
        connection,
        "INSERT INTO audit_logs(actor_user_id, action, entity_type, entity_id, "
        "old_value_summary, new_value_summary) VALUES (?, ?, 'PATIENT', ?, ?, ?);",
        -1, &statement, NULL);

    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size, "Unable to write the audit log.");
        return false;
    }
    (void)sqlite3_bind_int64(statement, 1, actor_user_id);
    (void)sqlite3_bind_text(statement, 2, action, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(statement, 3, patient_id, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 4, old_summary, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 5, new_summary, -1, SQLITE_TRANSIENT);
    status = sqlite3_step(statement);
    (void)sqlite3_finalize(statement);
    if (status != SQLITE_DONE) {
        set_error(error_message, error_message_size, "Unable to write the audit log.");
        return false;
    }
    return true;
}

static void append_changed_field(char *summary, size_t summary_size,
                                 const char *field, bool changed) {
    const size_t current_length = summary != NULL ? strlen(summary) : 0U;

    if (!changed || summary == NULL || summary_size == 0U ||
        current_length >= summary_size - 1U) {
        return;
    }
    (void)snprintf(summary + current_length, summary_size - current_length,
                   "%s%s", current_length == 0U ? "" : ", ", field);
}

static bool patient_drafts_differ(const PrsPatientDraft *left,
                                  const PrsPatientDraft *right,
                                  char *changed_fields,
                                  size_t changed_fields_size) {
    bool changed = false;

    if (changed_fields != NULL && changed_fields_size > 0U) {
        changed_fields[0] = '\0';
    }
#define ADD_CHANGED_FIELD(member, label)                                         \
    do {                                                                          \
        const bool field_changed = strcmp(left->member, right->member) != 0;    \
        append_changed_field(changed_fields, changed_fields_size, label,         \
                             field_changed);                                      \
        changed = changed || field_changed;                                       \
    } while (0)
    ADD_CHANGED_FIELD(first_name, "first name");
    ADD_CHANGED_FIELD(last_name, "last name");
    ADD_CHANGED_FIELD(date_of_birth, "date of birth");
    ADD_CHANGED_FIELD(sex_at_registration, "sex at registration");
    ADD_CHANGED_FIELD(phone, "phone");
    ADD_CHANGED_FIELD(email, "email");
    ADD_CHANGED_FIELD(clinic_identifier, "clinic identifier");
    ADD_CHANGED_FIELD(insurance_reference, "insurance reference");
    ADD_CHANGED_FIELD(address, "address");
    ADD_CHANGED_FIELD(emergency_contact_name, "emergency contact name");
    ADD_CHANGED_FIELD(emergency_contact_phone, "emergency contact phone");
    ADD_CHANGED_FIELD(guardian_name, "guardian name");
    ADD_CHANGED_FIELD(guardian_phone, "guardian phone");
    ADD_CHANGED_FIELD(patient_notes, "patient notes");
#undef ADD_CHANGED_FIELD
    return changed;
}

PrsPatientCreateResult prs_patient_create(
    PrsDatabase *database, const PrsUserSession *session,
    const PrsPatientDraft *draft, bool duplicate_review_acknowledged,
    char *patient_id, size_t patient_id_size, PrsValidationError *validation_error,
    PrsPatientSummary *duplicate_candidates, size_t duplicate_capacity,
    size_t *duplicate_count, char *error_message, size_t error_message_size) {
    static const char INSERT_SQL[] =
        "INSERT INTO patients(patient_id, first_name, last_name, date_of_birth, "
        "sex_at_registration, phone, email, clinic_identifier, insurance_reference, "
        "address, emergency_contact_name, emergency_contact_phone, guardian_name, "
        "guardian_phone, patient_notes, created_by, updated_by) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *statement = NULL;
    bool transaction_started = false;
    int status = SQLITE_ERROR;
    PrsPatientCreateResult result = PRS_PATIENT_CREATE_FAILED;

    clear_summaries(duplicate_candidates, duplicate_capacity, duplicate_count);
    if (patient_id != NULL && patient_id_size > 0U) {
        patient_id[0] = '\0';
    }
    if (error_message != NULL && error_message_size > 0U) {
        error_message[0] = '\0';
    }
    if (!can_write_patients(session)) {
        set_error(error_message, error_message_size,
                  "You do not have permission to register patients.");
        return PRS_PATIENT_CREATE_DENIED;
    }
    if (!prs_validate_patient_draft(draft, validation_error)) {
        return PRS_PATIENT_CREATE_INVALID;
    }
    if (database == NULL || database->connection == NULL || patient_id == NULL ||
        patient_id_size < PRS_PATIENT_ID_CAPACITY) {
        set_error(error_message, error_message_size,
                  "Patient registration service is unavailable.");
        return PRS_PATIENT_CREATE_FAILED;
    }
    if (!execute(database->connection, "BEGIN IMMEDIATE;", error_message,
                 error_message_size)) {
        return PRS_PATIENT_CREATE_FAILED;
    }
    transaction_started = true;
    if (!find_duplicates(database->connection, draft, duplicate_candidates,
                         duplicate_capacity, duplicate_count, error_message,
                         error_message_size)) {
        goto cleanup;
    }
    if (duplicate_count != NULL && *duplicate_count > 0U &&
        !duplicate_review_acknowledged) {
        result = PRS_PATIENT_CREATE_DUPLICATE_REVIEW_REQUIRED;
        goto cleanup;
    }
    if (!generate_patient_id(database->connection, patient_id, patient_id_size,
                             error_message, error_message_size)) {
        goto cleanup;
    }
    status = sqlite3_prepare_v2(database->connection, INSERT_SQL, -1, &statement, NULL);
    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size, "Unable to create the patient record.");
        goto cleanup;
    }
    (void)sqlite3_bind_text(statement, 1, patient_id, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 2, draft->first_name, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 3, draft->last_name, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 4, draft->date_of_birth, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 5, draft->sex_at_registration, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 6, draft->phone, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 7, draft->email, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 8, draft->clinic_identifier, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 9, draft->insurance_reference, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 10, draft->address, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 11, draft->emergency_contact_name, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 12, draft->emergency_contact_phone, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 13, draft->guardian_name, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 14, draft->guardian_phone, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 15, draft->patient_notes, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_int64(statement, 16, session->user_id);
    (void)sqlite3_bind_int64(statement, 17, session->user_id);
    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_error(error_message, error_message_size, "Unable to create the patient record.");
        goto cleanup;
    }
    (void)sqlite3_finalize(statement);
    statement = NULL;
    if (!insert_audit_log(database->connection, session->user_id, "PATIENT_CREATED",
                          patient_id, NULL, "Patient record created.", error_message,
                          error_message_size) ||
        !execute(database->connection, "COMMIT;", error_message, error_message_size)) {
        goto cleanup;
    }
    transaction_started = false;
    prs_log(PRS_LOG_INFO, "patient", "Patient record created.");
    return PRS_PATIENT_CREATE_SAVED;

cleanup:
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    if (transaction_started) {
        (void)sqlite3_exec(database->connection, "ROLLBACK;", NULL, NULL, NULL);
    }
    if (result != PRS_PATIENT_CREATE_DUPLICATE_REVIEW_REQUIRED) {
        patient_id[0] = '\0';
    }
    return result;
}

bool prs_patient_get_by_id(PrsDatabase *database, const PrsUserSession *session,
                           const char *patient_id, PrsPatientRecord *patient,
                           char *error_message, size_t error_message_size) {
    sqlite3_stmt *statement = NULL;
    int status = SQLITE_ERROR;

    if (patient != NULL) {
        (void)memset(patient, 0, sizeof(*patient));
    }
    if (!can_view_patients(session) || database == NULL || database->connection == NULL ||
        patient == NULL || patient_id == NULL || patient_id[0] == '\0') {
        set_error(error_message, error_message_size, "Patient record access is unavailable.");
        return false;
    }
    status = sqlite3_prepare_v2(
        database->connection,
        "SELECT patient_key, patient_id, first_name, last_name, date_of_birth, "
        "sex_at_registration, phone, email, clinic_identifier, insurance_reference, "
        "address, emergency_contact_name, emergency_contact_phone, guardian_name, "
        "guardian_phone, patient_notes, status, created_at, updated_at "
        "FROM patients WHERE patient_id = ?;",
        -1, &statement, NULL);
    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size, "Unable to retrieve the patient record.");
        return false;
    }
    (void)sqlite3_bind_text(statement, 1, patient_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(statement) != SQLITE_ROW) {
        (void)sqlite3_finalize(statement);
        set_error(error_message, error_message_size, "Patient record was not found.");
        return false;
    }
    patient->patient_key = sqlite3_column_int64(statement, 0);
    copy_text(patient->patient_id, sizeof(patient->patient_id), sqlite3_column_text(statement, 1));
    copy_text(patient->details.first_name, sizeof(patient->details.first_name), sqlite3_column_text(statement, 2));
    copy_text(patient->details.last_name, sizeof(patient->details.last_name), sqlite3_column_text(statement, 3));
    copy_text(patient->details.date_of_birth, sizeof(patient->details.date_of_birth), sqlite3_column_text(statement, 4));
    copy_text(patient->details.sex_at_registration, sizeof(patient->details.sex_at_registration), sqlite3_column_text(statement, 5));
    copy_text(patient->details.phone, sizeof(patient->details.phone), sqlite3_column_text(statement, 6));
    copy_text(patient->details.email, sizeof(patient->details.email), sqlite3_column_text(statement, 7));
    copy_text(patient->details.clinic_identifier, sizeof(patient->details.clinic_identifier), sqlite3_column_text(statement, 8));
    copy_text(patient->details.insurance_reference, sizeof(patient->details.insurance_reference), sqlite3_column_text(statement, 9));
    copy_text(patient->details.address, sizeof(patient->details.address), sqlite3_column_text(statement, 10));
    copy_text(patient->details.emergency_contact_name, sizeof(patient->details.emergency_contact_name), sqlite3_column_text(statement, 11));
    copy_text(patient->details.emergency_contact_phone, sizeof(patient->details.emergency_contact_phone), sqlite3_column_text(statement, 12));
    copy_text(patient->details.guardian_name, sizeof(patient->details.guardian_name), sqlite3_column_text(statement, 13));
    copy_text(patient->details.guardian_phone, sizeof(patient->details.guardian_phone), sqlite3_column_text(statement, 14));
    copy_text(patient->details.patient_notes, sizeof(patient->details.patient_notes), sqlite3_column_text(statement, 15));
    copy_text(patient->status, sizeof(patient->status), sqlite3_column_text(statement, 16));
    copy_text(patient->created_at, sizeof(patient->created_at), sqlite3_column_text(statement, 17));
    copy_text(patient->updated_at, sizeof(patient->updated_at), sqlite3_column_text(statement, 18));
    (void)sqlite3_finalize(statement);
    return true;
}

bool prs_patient_search(PrsDatabase *database, const PrsUserSession *session,
                        const char *search_term, PrsPatientSummary *results,
                        size_t result_capacity, size_t *result_count,
                        char *error_message, size_t error_message_size) {
    static const char SQL[] =
        "SELECT patient_key, patient_id, first_name, last_name, date_of_birth, phone, status "
        "FROM patients WHERE status = 'ACTIVE' AND "
        "(patient_id = ? OR phone = ? OR date_of_birth = ? "
        "OR first_name || ' ' || last_name LIKE ? COLLATE NOCASE "
        "OR last_name || ' ' || first_name LIKE ? COLLATE NOCASE) "
        "ORDER BY last_name, first_name LIMIT 50;";
    sqlite3_stmt *statement = NULL;
    char pattern[PRS_NAME_CAPACITY * 2U + 4U];
    int status = SQLITE_ERROR;
    size_t total = 0U;

    clear_summaries(results, result_capacity, result_count);
    if (!can_view_patients(session) || database == NULL || database->connection == NULL ||
        search_term == NULL) {
        set_error(error_message, error_message_size, "Search is unavailable.");
        return false;
    }
    if (search_term[0] == '\0') {
        static const char ALL_ACTIVE_SQL[] =
            "SELECT patient_key, patient_id, first_name, last_name, date_of_birth, phone, status "
            "FROM patients WHERE status = 'ACTIVE' ORDER BY last_name, first_name LIMIT 50;";
        sqlite3_stmt *all_statement = NULL;
        status = sqlite3_prepare_v2(database->connection, ALL_ACTIVE_SQL, -1, &all_statement, NULL);
        if (status != SQLITE_OK) {
            set_error(error_message, error_message_size, "Unable to list patient records.");
            return false;
        }
        while ((status = sqlite3_step(all_statement)) == SQLITE_ROW) {
            if (results != NULL && total < result_capacity) {
                populate_summary(all_statement, &results[total]);
            }
            ++total;
        }
        (void)sqlite3_finalize(all_statement);
        if (status != SQLITE_DONE) {
            set_error(error_message, error_message_size, "Unable to list patient records.");
            return false;
        }
        if (result_count != NULL) {
            *result_count = total;
        }
        return true;
    }
    if ((size_t)snprintf(pattern, sizeof(pattern), "%%%s%%", search_term) >=
        sizeof(pattern)) {
        set_error(error_message, error_message_size, "Search term is too long.");
        return false;
    }
    status = sqlite3_prepare_v2(database->connection, SQL, -1, &statement, NULL);
    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size, "Unable to search patient records.");
        return false;
    }
    (void)sqlite3_bind_text(statement, 1, search_term, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 2, search_term, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 3, search_term, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 4, pattern, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 5, pattern, -1, SQLITE_TRANSIENT);
    while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
        if (results != NULL && total < result_capacity) {
            populate_summary(statement, &results[total]);
        }
        ++total;
    }
    (void)sqlite3_finalize(statement);
    if (status != SQLITE_DONE) {
        set_error(error_message, error_message_size, "Unable to search patient records.");
        return false;
    }
    if (result_count != NULL) {
        *result_count = total;
    }
    return true;
}

bool prs_patient_update(PrsDatabase *database, const PrsUserSession *session,
                        const char *patient_id, const PrsPatientDraft *draft,
                        PrsValidationError *validation_error,
                        char *error_message, size_t error_message_size) {
    static const char SQL[] =
        "UPDATE patients SET first_name = ?, last_name = ?, date_of_birth = ?, "
        "sex_at_registration = ?, phone = ?, email = ?, clinic_identifier = ?, "
        "insurance_reference = ?, address = ?, emergency_contact_name = ?, "
        "emergency_contact_phone = ?, guardian_name = ?, guardian_phone = ?, "
        "patient_notes = ?, "
        "updated_by = ?, updated_at = CURRENT_TIMESTAMP WHERE patient_id = ?;";
    sqlite3_stmt *statement = NULL;
    PrsPatientRecord existing = {0};
    char changed_fields[512] = {0};
    char old_summary[600] = {0};
    char new_summary[600] = {0};
    bool transaction_started = false;
    int status = SQLITE_ERROR;

    if (!can_write_patients(session)) {
        set_error(error_message, error_message_size,
                  "You do not have permission to update patient records.");
        return false;
    }
    if (!prs_validate_patient_draft(draft, validation_error)) {
        return false;
    }
    if (database == NULL || database->connection == NULL || patient_id == NULL ||
        patient_id[0] == '\0') {
        set_error(error_message, error_message_size, "Patient update service is unavailable.");
        return false;
    }
    if (!execute(database->connection, "BEGIN IMMEDIATE;", error_message,
                 error_message_size)) {
        return false;
    }
    transaction_started = true;
    if (!prs_patient_get_by_id(database, session, patient_id, &existing,
                               error_message, error_message_size)) {
        goto cleanup;
    }
    if (!patient_drafts_differ(&existing.details, draft, changed_fields,
                               sizeof(changed_fields))) {
        (void)execute(database->connection, "COMMIT;", error_message,
                      error_message_size);
        transaction_started = false;
        return true;
    }
    status = sqlite3_prepare_v2(database->connection, SQL, -1, &statement, NULL);
    if (status != SQLITE_OK) {
        set_error(error_message, error_message_size, "Unable to update the patient record.");
        goto cleanup;
    }
    (void)sqlite3_bind_text(statement, 1, draft->first_name, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 2, draft->last_name, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 3, draft->date_of_birth, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 4, draft->sex_at_registration, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 5, draft->phone, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 6, draft->email, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 7, draft->clinic_identifier, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 8, draft->insurance_reference, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 9, draft->address, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 10, draft->emergency_contact_name, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 11, draft->emergency_contact_phone, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 12, draft->guardian_name, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 13, draft->guardian_phone, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 14, draft->patient_notes, -1,
                            SQLITE_TRANSIENT);
    (void)sqlite3_bind_int64(statement, 15, session->user_id);
    (void)sqlite3_bind_text(statement, 16, patient_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(database->connection) != 1) {
        set_error(error_message, error_message_size, "Patient record was not found.");
        goto cleanup;
    }
    (void)sqlite3_finalize(statement);
    statement = NULL;
    (void)snprintf(old_summary, sizeof(old_summary), "Changed: %s", changed_fields);
    (void)snprintf(new_summary, sizeof(new_summary), "Updated: %s", changed_fields);
    if (!insert_audit_log(database->connection, session->user_id, "PATIENT_UPDATED",
                          patient_id, old_summary, new_summary, error_message,
                          error_message_size) ||
        !execute(database->connection, "COMMIT;", error_message, error_message_size)) {
        goto cleanup;
    }
    transaction_started = false;
    prs_log(PRS_LOG_INFO, "patient", "Patient record updated.");
    return true;

cleanup:
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    if (transaction_started) {
        (void)sqlite3_exec(database->connection, "ROLLBACK;", NULL, NULL, NULL);
    }
    return false;
}

bool prs_patient_set_active(PrsDatabase *database, const PrsUserSession *session,
                            const char *patient_id, bool active,
                            char *error_message, size_t error_message_size) {
    sqlite3_stmt *statement = NULL;
    const char *status = active ? "ACTIVE" : "INACTIVE";
    int sqlite_status = SQLITE_ERROR;
    bool transaction_started = false;

    if (!prs_session_has_role(session, PRS_ROLE_ADMINISTRATOR) || database == NULL ||
        database->connection == NULL || patient_id == NULL || patient_id[0] == '\0') {
        set_error(error_message, error_message_size,
                  "Only administrators may change a record's active status.");
        return false;
    }
    if (!execute(database->connection, "BEGIN IMMEDIATE;", error_message,
                 error_message_size)) {
        return false;
    }
    transaction_started = true;
    sqlite_status = sqlite3_prepare_v2(
        database->connection,
        "UPDATE patients SET status = ?, updated_by = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE patient_id = ? AND status <> ?;",
        -1, &statement, NULL);
    if (sqlite_status != SQLITE_OK) {
        set_error(error_message, error_message_size,
                  "Unable to change the patient record status.");
        goto cleanup;
    }
    (void)sqlite3_bind_text(statement, 1, status, -1, SQLITE_STATIC);
    (void)sqlite3_bind_int64(statement, 2, session->user_id);
    (void)sqlite3_bind_text(statement, 3, patient_id, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(statement, 4, status, -1, SQLITE_STATIC);
    if (sqlite3_step(statement) != SQLITE_DONE ||
        sqlite3_changes(database->connection) != 1) {
        set_error(error_message, error_message_size,
                  "Patient record was not found or already has that status.");
        goto cleanup;
    }
    (void)sqlite3_finalize(statement);
    statement = NULL;
    if (!insert_audit_log(database->connection, session->user_id,
                          active ? "PATIENT_REACTIVATED" : "PATIENT_DEACTIVATED",
                          patient_id, active ? "Status: INACTIVE" : "Status: ACTIVE",
                          active ? "Status: ACTIVE" : "Status: INACTIVE", error_message,
                          error_message_size) ||
        !execute(database->connection, "COMMIT;", error_message,
                 error_message_size)) {
        goto cleanup;
    }
    prs_log(PRS_LOG_INFO, "patient", active ? "Patient record reactivated."
                                             : "Patient record deactivated.");
    return true;

cleanup:
    if (statement != NULL) {
        (void)sqlite3_finalize(statement);
    }
    if (transaction_started) {
        (void)sqlite3_exec(database->connection, "ROLLBACK;", NULL, NULL, NULL);
    }
    return false;
}
