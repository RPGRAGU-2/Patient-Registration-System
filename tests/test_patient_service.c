#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "prs/auth.h"
#include "prs/backup_service.h"
#include "prs/clinical_service.h"
#include "prs/database.h"
#include "prs/models.h"
#include "prs/patient_service.h"

static unsigned int failures = 0U;

#define EXPECT(condition, description)                                           \
    do {                                                                         \
        if (!(condition)) {                                                      \
            (void)fprintf(stderr, "FAIL: %s (line %d)\n", description, __LINE__); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static PrsPatientDraft patient_draft(const char *first_name, const char *phone) {
    PrsPatientDraft draft = {0};

    (void)snprintf(draft.first_name, sizeof(draft.first_name), "%s", first_name);
    (void)snprintf(draft.last_name, sizeof(draft.last_name), "%s", "Example");
    (void)snprintf(draft.date_of_birth, sizeof(draft.date_of_birth), "%s", "1985-04-18");
    (void)snprintf(draft.sex_at_registration, sizeof(draft.sex_at_registration), "%s", "Female");
    (void)snprintf(draft.phone, sizeof(draft.phone), "%s", phone);
    (void)snprintf(draft.email, sizeof(draft.email), "%s", "patient@example.com");
    (void)snprintf(draft.address, sizeof(draft.address), "%s", "42 Example Avenue");
    (void)snprintf(draft.emergency_contact_name, sizeof(draft.emergency_contact_name), "%s", "Alex Example");
    (void)snprintf(draft.emergency_contact_phone, sizeof(draft.emergency_contact_phone), "%s", "+1 555 010 9876");
    return draft;
}

static void cleanup_database_files(const char *database_path) {
    char sidecar_path[300];

    (void)remove(database_path);
    (void)snprintf(sidecar_path, sizeof(sidecar_path), "%s-wal", database_path);
    (void)remove(sidecar_path);
    (void)snprintf(sidecar_path, sizeof(sidecar_path), "%s-shm", database_path);
    (void)remove(sidecar_path);
}

static void future_time(char *start_at, size_t start_at_size, char *end_at,
                        size_t end_at_size) {
    time_t now = time(NULL) + 86400;
    struct tm *local_time = localtime(&now);
    struct tm end_time;
    time_t end;

    if (local_time == NULL) {
        start_at[0] = '\0';
        end_at[0] = '\0';
        return;
    }
    (void)strftime(start_at, start_at_size, "%Y-%m-%d %H:%M", local_time);
    end = mktime(local_time) + 1800;
    local_time = localtime(&end);
    if (local_time != NULL) {
        end_time = *local_time;
        (void)strftime(end_at, end_at_size, "%Y-%m-%d %H:%M", &end_time);
    } else {
        end_at[0] = '\0';
    }
}

int main(void) {
    PrsDatabase database = {0};
    PrsUserSession session = {0};
    PrsPatientDraft first = patient_draft("Jane", "+1 555 010 1234");
    PrsPatientDraft second = patient_draft("Mary", "+1 555 010 3333");
    PrsPatientSummary duplicate_candidates[5] = {0};
    PrsPatientSummary search_results[10] = {0};
    PrsPatientRecord record = {0};
    PrsValidationError validation_error = {0};
    char database_path[256];
    char error_message[256] = {0};
    char first_id[PRS_PATIENT_ID_CAPACITY] = {0};
    char second_id[PRS_PATIENT_ID_CAPACITY] = {0};
    char backup_path[512] = {0};
    char start_at[PRS_DATETIME_CAPACITY] = {0};
    char end_at[PRS_DATETIME_CAPACITY] = {0};
    char backup_directory[256] = {0};
    long long provider_id = 0;
    long long appointment_id = 0;
    size_t duplicate_count = 0U;
    size_t search_count = 0U;

    (void)snprintf(database_path, sizeof(database_path), "/tmp/prs_phase3_%ld.db",
                   (long)getpid());
    cleanup_database_files(database_path);
    EXPECT(prs_database_open(&database, database_path, error_message,
                             sizeof(error_message)),
           "database should open and apply migrations");
    if (failures != 0U) {
        cleanup_database_files(database_path);
        return 1;
    }
    EXPECT(prs_auth_bootstrap_administrator(&database, "admin", "A-strong-test-password", error_message,
                                            sizeof(error_message)),
           "initial administrator should be created");
    EXPECT(prs_authenticate(&database, "admin", "A-strong-test-password", &session,
                            error_message, sizeof(error_message)),
           "administrator should authenticate");
    EXPECT(session.authenticated && session.role == PRS_ROLE_ADMINISTRATOR,
           "authenticated user should carry the administrator role");
    EXPECT(!prs_auth_create_user(&database, &session, "weakuser", "aaaaaaaaaaaa",
                                  PRS_ROLE_RECEPTIONIST, error_message,
                                  sizeof(error_message)),
           "long repeated-character password should be rejected");
    EXPECT(prs_auth_create_user(&database, &session, "reception", "A-strong-user-password",
                                PRS_ROLE_RECEPTIONIST, error_message,
                                sizeof(error_message)),
           "administrator should be able to create a receptionist account");
    EXPECT(!prs_auth_create_user(&database, &session, "reception", "Another-strong-password",
                                 PRS_ROLE_CLINICIAN, error_message,
                                 sizeof(error_message)) &&
               strstr(error_message, "already exists") != NULL,
           "duplicate username should be rejected with a clear account-creation error");
    {
        PrsUserSession receptionist = {0};
        EXPECT(prs_authenticate(&database, "reception", "A-strong-user-password",
                                &receptionist, error_message, sizeof(error_message)) &&
                   receptionist.role == PRS_ROLE_RECEPTIONIST,
               "created receptionist should be able to authenticate");
        prs_session_clear(&receptionist);
    }
    EXPECT(prs_patient_create(&database, &session, &first, false, first_id,
                              sizeof(first_id), &validation_error,
                              duplicate_candidates,
                              sizeof(duplicate_candidates) / sizeof(duplicate_candidates[0]),
                              &duplicate_count, error_message,
                              sizeof(error_message)) == PRS_PATIENT_CREATE_SAVED,
           "validated patient should be saved");
    EXPECT(strncmp(first_id, "PAT-", 4U) == 0,
           "saved patient should receive a generated patient ID");
    EXPECT(prs_patient_create(&database, &session, &first, false, second_id,
                              sizeof(second_id), &validation_error,
                              duplicate_candidates,
                              sizeof(duplicate_candidates) / sizeof(duplicate_candidates[0]),
                              &duplicate_count, error_message,
                              sizeof(error_message)) ==
               PRS_PATIENT_CREATE_DUPLICATE_REVIEW_REQUIRED,
           "possible duplicate should require acknowledgement");
    EXPECT(duplicate_count == 1U, "duplicate review should return the existing record");
    EXPECT(prs_patient_create(&database, &session, &second, false, second_id,
                              sizeof(second_id), &validation_error,
                              duplicate_candidates,
                              sizeof(duplicate_candidates) / sizeof(duplicate_candidates[0]),
                              &duplicate_count, error_message,
                              sizeof(error_message)) == PRS_PATIENT_CREATE_SAVED,
           "distinct patient should be saved");
    EXPECT(strcmp(first_id, second_id) != 0,
           "two saved patients should have different patient IDs");
    EXPECT(prs_patient_get_by_id(&database, &session, first_id, &record, error_message,
                                 sizeof(error_message)),
           "saved patient should be retrievable by patient ID");
    EXPECT(strcmp(record.details.first_name, "Jane") == 0,
           "retrieved patient should preserve the first name");
    EXPECT(prs_patient_search(&database, &session, "Jane", search_results,
                              sizeof(search_results) / sizeof(search_results[0]),
                              &search_count, error_message, sizeof(error_message)),
           "authenticated user should be able to search patients");
    EXPECT(search_count == 1U, "search should return the matching patient");
    EXPECT(prs_patient_search(&database, &session, "", search_results,
                              sizeof(search_results) / sizeof(search_results[0]),
                              &search_count, error_message, sizeof(error_message)),
           "empty search should return the active patient list");
    EXPECT(search_count >= 1U, "empty search should show registered active patients");
    (void)snprintf(first.email, sizeof(first.email), "%s", "updated@example.com");
    EXPECT(prs_patient_update(&database, &session, first_id, &first, &validation_error,
                              error_message, sizeof(error_message)),
           "authorized user should be able to update patient demographics");
    EXPECT(prs_patient_get_by_id(&database, &session, first_id, &record, error_message,
                                 sizeof(error_message)) &&
               strcmp(record.details.email, "updated@example.com") == 0,
           "updated patient details should be stored");
    EXPECT(prs_provider_create(&database, &session, "Dr Example", "Family medicine",
                               &provider_id, error_message, sizeof(error_message)),
           "administrator should be able to create a provider");
    EXPECT(provider_id > 0, "created provider should receive an identifier");
    {
        PrsMedicalHistoryEntry entry = {0};
        PrsMedicalHistoryEntry latest = {0};
        (void)snprintf(entry.blood_group, sizeof(entry.blood_group), "%s", "O+");
        (void)snprintf(entry.allergies, sizeof(entry.allergies), "%s", "None recorded");
        EXPECT(prs_medical_history_add(&database, &session, first_id, &entry,
                                       error_message, sizeof(error_message)),
               "administrator should be able to add medical history");
        EXPECT(prs_medical_history_latest(&database, &session, first_id, &latest,
                                          error_message, sizeof(error_message)) &&
                   strcmp(latest.blood_group, "O+") == 0,
               "latest medical history should be retrievable");
        (void)snprintf(entry.allergies, sizeof(entry.allergies), "%s", "Penicillin");
        (void)snprintf(entry.conditions, sizeof(entry.conditions), "%s", "Asthma");
        EXPECT(prs_medical_history_update(&database, &session, first_id, &entry,
                                          error_message, sizeof(error_message)),
               "administrator should be able to update medical history");
        EXPECT(prs_medical_history_latest(&database, &session, first_id, &latest,
                                          error_message, sizeof(error_message)) &&
                   strcmp(latest.allergies, "Penicillin") == 0 &&
                   strcmp(latest.conditions, "Asthma") == 0,
               "updated medical history should overwrite the latest record");
        EXPECT(prs_medical_history_delete(&database, &session, first_id,
                                          error_message, sizeof(error_message)),
               "administrator should be able to delete medical history");
        EXPECT(!prs_medical_history_latest(&database, &session, first_id, &latest,
                                           error_message, sizeof(error_message)),
               "deleted medical history should no longer be retrievable");
    }
    future_time(start_at, sizeof(start_at), end_at, sizeof(end_at));
    EXPECT(prs_appointment_create(&database, &session, first_id, provider_id, start_at,
                                  end_at, "Initial consultation", &appointment_id,
                                  error_message, sizeof(error_message)),
           "administrator should be able to schedule an appointment");
    EXPECT(appointment_id > 0, "scheduled appointment should receive an identifier");
    EXPECT(!prs_appointment_create(&database, &session, second_id, provider_id, start_at,
                                   end_at, "Conflicting consultation", NULL, error_message,
                                   sizeof(error_message)),
           "overlapping provider appointments should be rejected");
    {
        PrsAppointment appointments[5] = {0};
        size_t appointment_count = 0U;
        EXPECT(prs_appointments_for_patient(&database, &session, first_id, appointments,
                                            sizeof(appointments) / sizeof(appointments[0]),
                                            &appointment_count, error_message,
                                            sizeof(error_message)) &&
                   appointment_count == 1U,
               "scheduled appointment should be visible on the patient profile");
    }
    EXPECT(prs_appointment_cancel(&database, &session, appointment_id,
                                  "Patient requested another date", error_message,
                                  sizeof(error_message)),
           "appointment cancellation should retain a cancelled record");
    (void)snprintf(backup_directory, sizeof(backup_directory), "/tmp/prs_backups_%ld",
                   (long)getpid());
    EXPECT(prs_backup_create_timestamped(&database, &session, backup_directory, backup_path,
                                         sizeof(backup_path), error_message,
                                         sizeof(error_message)),
           "administrator should be able to create a validated backup");
    EXPECT(prs_backup_restore(&database, &session, backup_path, error_message,
                              sizeof(error_message)),
           "administrator should be able to restore a validated backup");
    EXPECT(prs_patient_set_active(&database, &session, second_id, false, error_message,
                                  sizeof(error_message)),
           "administrator should be able to deactivate a patient record");
    EXPECT(!prs_patient_search(&database, &session, "Mary", search_results,
                               sizeof(search_results) / sizeof(search_results[0]),
                               &search_count, error_message, sizeof(error_message)) ||
               search_count == 0U,
           "deactivated records should not appear in active-patient search results");
    EXPECT(prs_auth_set_user_active(&database, &session, "reception", false,
                                    error_message, sizeof(error_message)),
           "administrator should be able to deactivate another user account");
    prs_session_clear(&session);
    EXPECT(prs_patient_create(&database, &session, &second, false, second_id,
                              sizeof(second_id), &validation_error,
                              duplicate_candidates,
                              sizeof(duplicate_candidates) / sizeof(duplicate_candidates[0]),
                              &duplicate_count, error_message,
                              sizeof(error_message)) == PRS_PATIENT_CREATE_DENIED,
           "unauthenticated access should not create patient records");

    prs_database_close(&database);
    cleanup_database_files(database_path);
    (void)remove(backup_path);
    (void)rmdir(backup_directory);
    if (failures == 0U) {
        (void)printf("Patient service integration tests passed.\n");
        return 0;
    }
    (void)fprintf(stderr, "%u patient-service assertion(s) failed.\n", failures);
    return 1;
}
